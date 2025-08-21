/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file nms_post_process.hpp
 * @brief NMS op
 *
 * https://learnopencv.com/object-detection-using-yolov5-and-opencv-dnn-in-c-and-python :
 * The headline '4.3.5 POST-PROCESSING YOLOv5 Prediction Output' contains explanations on the YOLOv5 post-process.
 **/

#ifndef _HAILO_NET_FLOW_NMS_POST_PROCESS_HPP_
#define _HAILO_NET_FLOW_NMS_POST_PROCESS_HPP_

#include "hailo/hailort.h"
#include "hailo/quantization.hpp"
#include "hailo/buffer.hpp"
#include "hailo/transform.hpp"

#include "common/utils.hpp"
#include "common/logger_macros.hpp"

#include "net_flow/ops/op.hpp"
#include "net_flow/ops_metadata/nms_op_metadata.hpp"


namespace hailort
{
namespace net_flow
{

#define INVALID_BBOX_DIM (std::numeric_limits<float32_t>::max())
#define INVALID_NMS_DETECTION (std::numeric_limits<uint32_t>::max())
#define INVALID_NMS_SCORE (std::numeric_limits<float32_t>::max())
#define INVALID_NMS_CONFIG (-1)
#define MAX_NMS_CLASSES (std::numeric_limits<uint16_t>::max())
#define LOGIT_CLIP_MIN (1e-7f)

inline bool operator==(const hailo_bbox_float32_t &first, const hailo_bbox_float32_t &second) {
    return first.y_min == second.y_min && first.x_min == second.x_min && first.y_max == second.y_max && first.x_max == second.x_max && first.score == second.score;
}

inline bool operator==(const hailo_bbox_t &first, const hailo_bbox_t &second) {
    return first.y_min == second.y_min && first.x_min == second.x_min && first.y_max == second.y_max && first.x_max == second.x_max && first.score == second.score;
}

struct DetectionBbox
{
    DetectionBbox(float32_t x_min, float32_t y_min, float32_t width, float32_t height, float32_t score, uint32_t class_id)
        : m_class_id(class_id), m_bbox{y_min, x_min, (y_min + height), (x_min + width), score}, m_bbox_with_mask{} {}

    DetectionBbox(const hailo_bbox_float32_t &bbox, uint32_t class_id)
        : m_class_id(class_id), m_bbox(bbox), m_bbox_with_mask{} {}

    DetectionBbox(const hailo_bbox_float32_t &bbox, uint16_t class_id, std::vector<float32_t> &&mask,
        float32_t image_height, float32_t image_width, bool is_crop_optimization_on)
        : m_class_id(class_id), m_coefficients(std::move(mask)), m_bbox(bbox),
            m_bbox_with_mask{{bbox.y_min, bbox.x_min, bbox.y_max, bbox.x_max}, bbox.score, class_id,
                get_mask_size_in_bytes(static_cast<uint32_t>(image_height), static_cast<uint32_t>(image_width), is_crop_optimization_on), nullptr}
        {}

    DetectionBbox() : DetectionBbox(hailo_bbox_float32_t{
        INVALID_BBOX_DIM,
        INVALID_BBOX_DIM,
        INVALID_BBOX_DIM,
        INVALID_BBOX_DIM,
        INVALID_BBOX_DIM
    }, INVALID_NMS_DETECTION) {}

    inline uint32_t get_bbox_y_min(uint32_t image_height) const
    {
        return static_cast<uint32_t>(std::max(std::floor(m_bbox.y_min * static_cast<float32_t>(image_height)), 0.0f));
    }

    inline uint32_t get_bbox_y_max(uint32_t image_height) const
    {
        return std::min(static_cast<uint32_t>(std::ceil(m_bbox.y_max * static_cast<float32_t>(image_height))), image_height);
    }

    inline uint32_t get_bbox_height(uint32_t image_height, bool is_crop_optimization_on) const
    {
        if (is_crop_optimization_on) {
            return get_bbox_y_max(image_height) - get_bbox_y_min(image_height);
        } else {
            return static_cast<uint32_t>(std::ceil((m_bbox.y_max - m_bbox.y_min) * static_cast<float32_t>(image_height)));
        }
    }

    inline uint32_t get_bbox_x_min(uint32_t image_width) const
    {
        return static_cast<uint32_t>(std::max(std::floor(m_bbox.x_min * static_cast<float32_t>(image_width)), 0.0f));
    }

    inline uint32_t get_bbox_x_max(uint32_t image_width) const
    {
        return std::min(static_cast<uint32_t>(std::ceil(m_bbox.x_max * static_cast<float32_t>(image_width))), image_width);
    }

    inline uint32_t get_bbox_width(uint32_t image_width, bool is_crop_optimization_on) const
    {
        if (is_crop_optimization_on) {
            return get_bbox_x_max(image_width) - get_bbox_x_min(image_width);
        } else {
            return static_cast<uint32_t>(std::ceil((m_bbox.x_max - m_bbox.x_min) * static_cast<float32_t>(image_width)));
        }
    }

    inline size_t get_mask_size_in_bytes(uint32_t image_height, uint32_t image_width, bool is_crop_optimization_on) const
    {
        auto box_height = get_bbox_height(image_height, is_crop_optimization_on);
        auto box_width = get_bbox_width(image_width, is_crop_optimization_on);
        auto mask_size = box_width * box_height;

        return mask_size;
    }

    uint32_t m_class_id;
    std::vector<float32_t> m_coefficients; // Used in segmentation networks
    // TODO: HRT-12093 - Unite usage and remove `hailo_bbox_float32_t`.
    hailo_bbox_float32_t m_bbox;
    hailo_detection_with_byte_mask_t m_bbox_with_mask;
};

inline bool operator==(const DetectionBbox &first, const DetectionBbox &second) {
    return first.m_class_id == second.m_class_id && first.m_bbox == second.m_bbox;
}


class NmsPostProcessOp : public Op
{
public:
    virtual ~NmsPostProcessOp() = default;

    /**
     * Computes the IOU ratio of @a box_1 and @a box_2
    */
    static float compute_iou(const hailo_bbox_float32_t &box_1, const hailo_bbox_float32_t &box_2);

    std::shared_ptr<NmsOpMetadata> metadata() { return m_nms_metadata;}
    virtual bool should_sigmoid()
    {
        return false;
    };

    virtual bool should_add_mask()
    {
        return false;
    };

    static float32_t sigmoid(float32_t number)
    {
        return (1.0f / (1.0f + std::exp(-number)));
    }

    /**
     * Compute the logit function (inverse of sigmoid) of @a number.
     * This is used for threshold optimization when sigmoid is applied after dequantization.
     *
     * @param[in] number               The value to compute logit for (should be in range [0,1]).
     *
     * @return Returns the logit of @a number.
     */
     static inline float32_t logit(float32_t number)
     {
         // Prevent numerical overflow: log(<number too close to 0>) = -infinity and log(1 / <number too close to 0>) will get us to infinity
         // Clip number to [LOGIT_CLIP_MIN, 1-LOGIT_CLIP_MIN] to ensure log(number/(1-number)) is finite
         number = Quantization::clip(number, LOGIT_CLIP_MIN, 1.0f - LOGIT_CLIP_MIN);
         return std::log(number / (1.0f - number));
     }

    /**
     * Removes overlapping boxes in @a detections by setting the class confidence to zero.
     *
     * @param[in] detections            A vector of @a DetectionBbox containing the detections boxes after ::extract_detections() function.
     *
    */
    static void remove_overlapping_boxes(std::vector<DetectionBbox> &detections,
        std::vector<uint32_t> &classes_detections_count, double nms_iou_th);

    template<typename DstType = float32_t, typename SrcType>
    DstType dequantize_and_sigmoid(SrcType number, hailo_quant_info_t quant_info)
    {
        auto dequantized_val = Quantization::dequantize_output<DstType, SrcType>(number, quant_info);
        if (should_sigmoid()) {
            return sigmoid(dequantized_val);
        } else {
            return dequantized_val;
        }
    }

    static inline void transform__parse_and_copy_bbox(hailo_bbox_t *dst, uint64_t* proposal)
    {
        dst->y_min = (uint16_t)((*((uint64_t*)proposal) & 0xfff000000000) >> 36);
        dst->x_min = (uint16_t)((*((uint64_t*)proposal) & 0xfff000000) >> 24);
        dst->y_max = (uint16_t)((*((uint64_t*)proposal) & 0xfff000) >> 12);
        dst->x_max = (uint16_t)((*((uint64_t*)proposal) & 0xfff));
        dst->score = (uint16_t)((*((uint64_t*)proposal) & 0xffff000000000000) >> 48);
    }

    static std::pair<std::vector<net_flow::DetectionBbox>, std::vector<uint32_t>>
        transform__d2h_NMS_DETECTIONS(const uint8_t *src_ptr, const hailo_nms_info_t &nms_info)
    {
        /* Validate arguments */
        assert(NULL != src_ptr);

        std::vector<net_flow::DetectionBbox> detections;
        std::vector<uint32_t> classes_detection_count(nms_info.number_of_classes, 0);

        detections.reserve(nms_info.max_bboxes_per_class * nms_info.number_of_classes);

        const uint32_t bbox_size = sizeof(hailo_bbox_float32_t);

        float32_t class_bboxes_count = 0;

        size_t current_offset = 0;
        // Now, the merge itself
        for (size_t class_index = 0; class_index < nms_info.number_of_classes ; class_index++) {
            class_bboxes_count = *(reinterpret_cast<const float32_t*>(src_ptr + current_offset));
            classes_detection_count[class_index] += (uint32_t)class_bboxes_count;
            current_offset += sizeof(float32_t);
            for (nms_bbox_counter_t bbox_count = 0; bbox_count < class_bboxes_count; bbox_count++) {
                hailo_bbox_float32_t bbox = *(reinterpret_cast<const hailo_bbox_float32_t*>(src_ptr + current_offset));
                DetectionBbox detection_bbox;
                detection_bbox.m_class_id = (uint32_t)class_index;
                detection_bbox.m_bbox = bbox;
                detections.push_back(detection_bbox);
                current_offset += bbox_size;
            }
        }
        return std::make_pair(std::move(detections), std::move(classes_detection_count));
    }

    /*
    * For each class the layout is
    *       \code
    *       struct (packed) {
    *           float32_t bbox_count;
    *           hailo_bbox_float32_t bbox[bbox_count];
    *       };
    *       \endcode
    */
    static void fill_nms_by_class_format_buffer(MemoryView &buffer, const std::vector<DetectionBbox> &detections,
        std::vector<uint32_t> &classes_detections_count, const NmsPostProcessConfig &nms_config);

    /*
    *      For all classes the layout is
    *          \code
    *          struct (packed) {
    *              uint16_t bbox_count;
    *              hailo_detection_t bbox[bbox_count];
    *          };
    *          \endcode
    */
    static void fill_nms_by_score_format_buffer(MemoryView &buffer, std::vector<DetectionBbox> &detections,
        const NmsPostProcessConfig &nms_config, const bool should_sort = false);

protected:
    NmsPostProcessOp(std::shared_ptr<NmsOpMetadata> metadata)
        : Op(static_cast<PostProcessOpMetadataPtr>(metadata))
        , m_classes_detections_count(metadata->nms_config().number_of_classes, 0)
        , m_nms_metadata(metadata)
    {
        reserve_detections();
    }

    void reserve_detections()
    {
        if ((HailoRTCommon::is_nms_by_class(m_nms_metadata->outputs_metadata().begin()->second.format.order)) ||
            (HAILO_FORMAT_ORDER_NHWC == m_nms_metadata->outputs_metadata().begin()->second.format.order)) {
            m_detections.reserve(m_nms_metadata->nms_config().max_proposals_per_class * m_nms_metadata->nms_config().number_of_classes);
        } else if (HailoRTCommon::is_nms_by_score(m_nms_metadata->outputs_metadata().begin()->second.format.order)) {
            m_detections.reserve(m_nms_metadata->nms_config().max_proposals_total);
        } else {
            LOGGER__WARNING("Unsupported output format order for NmsPostProcessOp: {}",
                HailoRTCommon::get_format_order_str(m_nms_metadata->outputs_metadata().begin()->second.format.order));
        }
    }

    void clear_before_frame()
    {
        m_detections.clear();
        reserve_detections();
        m_classes_detections_count.assign(m_nms_metadata->nms_config().number_of_classes, 0);
    }

    template<typename DstType = float32_t, typename SrcType>
    std::pair<uint32_t, float32_t> get_max_class(const SrcType *data, uint32_t entry_idx, uint32_t classes_start_index,
        float32_t objectness, hailo_quant_info_t quant_info, uint32_t width)
    {
        auto const &nms_config = m_nms_metadata->nms_config();
        std::pair<uint32_t, float32_t> max_id_score_pair;
        for (uint32_t class_index = 0; class_index < nms_config.number_of_classes; class_index++) {
            auto class_id = class_index;
            if (nms_config.background_removal) {
                if (nms_config.background_removal_index == class_index) {
                    // Ignore if class_index is background_removal_index
                    continue;
                }
                else if (0 == nms_config.background_removal_index) {
                    // background_removal_index will always be the first or last index.
                    // If it is the first one we need to reduce all classes id's in 1.
                    // If it is the last one we just ignore it in the previous if case.
                    class_id--;
                }
            }

            auto class_entry_idx = entry_idx + ((classes_start_index + class_index) * width);
            auto class_confidence = dequantize_and_sigmoid<DstType, SrcType>(data[class_entry_idx], quant_info);
            auto class_score = class_confidence * objectness;
            if (class_score > max_id_score_pair.second) {
                max_id_score_pair.first = class_id;
                max_id_score_pair.second = class_score;
            }
        }
        return max_id_score_pair;
    }

    hailo_status hailo_nms_format(MemoryView dst_view);

    std::vector<DetectionBbox> m_detections;
    std::vector<uint32_t> m_classes_detections_count;
private:
    std::shared_ptr<NmsOpMetadata> m_nms_metadata;

};

}
}

#endif // _HAILO_NET_FLOW_NMS_POST_PROCESS_HPP_