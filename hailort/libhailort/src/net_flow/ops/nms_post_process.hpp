/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file op.hpp
 * @brief Net-Flow op
 *
 * https://learnopencv.com/object-detection-using-yolov5-and-opencv-dnn-in-c-and-python :
 * The headline '4.3.5 POST-PROCESSING YOLOv5 Prediction Output' contains explanations on the YOLOv5 post-process.
 **/

#ifndef _HAILO_NET_FLOW_NMS_POST_PROCESS_HPP_
#define _HAILO_NET_FLOW_NMS_POST_PROCESS_HPP_

#include "hailo/hailort.h"
#include "hailo/quantization.hpp"
#include "hailo/buffer.hpp"

#include "common/utils.hpp"
#include "common/logger_macros.hpp"

#include "net_flow/ops/op.hpp"


namespace hailort
{
namespace net_flow
{

#define INVALID_BBOX_DIM (std::numeric_limits<float32_t>::max())
#define INVALID_NMS_DETECTION (std::numeric_limits<uint32_t>::max())
#define INVALID_NMS_SCORE (std::numeric_limits<float32_t>::max())

struct DetectionBbox
{
    DetectionBbox(float32_t x_min, float32_t y_min, float32_t width, float32_t height, float32_t score, uint32_t class_id)
        : m_class_id(class_id), m_bbox{y_min, x_min, (y_min + height), (x_min + width), score} {}

    DetectionBbox(const hailo_bbox_float32_t &bbox, uint32_t class_id)
        : m_class_id(class_id), m_bbox(bbox) {}

    DetectionBbox() : DetectionBbox(hailo_bbox_float32_t{
        INVALID_BBOX_DIM,
        INVALID_BBOX_DIM,
        INVALID_BBOX_DIM,
        INVALID_BBOX_DIM,
        INVALID_BBOX_DIM
    }, INVALID_NMS_DETECTION) {}

    uint32_t m_class_id;
    hailo_bbox_float32_t m_bbox;
};

struct NmsPostProcessConfig
{
    // User given confidence threshold for a bbox. A bbox will be consider as detection if the 
    // (objectness * class_score) is higher then the confidence_threshold.
    double nms_score_th = 0;

    // User given IOU threshold (intersection over union). This threshold is for performing
    // Non-maximum suppression (Removing overlapping boxes).
    double nms_iou_th = 0;

    // Maximum amount of bboxes per nms class.
    uint32_t max_proposals_per_class = 0;

    // The model's number of classes. (This depends on the dataset that the model trained on).
    uint32_t classes = 0;

    // Toggle background class removal from results
    bool background_removal = false;

    // Index of background class for background removal
    uint32_t background_removal_index = 0;

    // Indicates whether or not NMS performs IOU over different classes for the same box.
    // If set to false - NMS won't intersect different classes, and a box could have multiple labels.
    bool cross_classes = false;
};

static const float32_t REMOVED_CLASS_SCORE = 0.0f;

class NmsPostProcessOp : public Op
{
public:
    virtual ~NmsPostProcessOp() = default;
    
    /**
     * Computes the IOU ratio of @a box_1 and @a box_2 
    */
    static float compute_iou(const hailo_bbox_float32_t &box_1, const hailo_bbox_float32_t &box_2);

protected:
    NmsPostProcessOp(const std::map<std::string, BufferMetaData> &inputs_metadata,
                     const std::map<std::string, BufferMetaData> &outputs_metadata,
                     const NmsPostProcessConfig &nms_post_process_config,
                     const std::string &name)
        : Op(inputs_metadata, outputs_metadata, name)
        , m_nms_config(nms_post_process_config)
    {}

    NmsPostProcessConfig m_nms_config;

    template<typename HostType = float32_t, typename DeviceType>
    std::pair<uint32_t, float32_t> get_max_class(const DeviceType *data, uint32_t entry_idx, uint32_t classes_start_index,
        float32_t objectness, hailo_quant_info_t quant_info, uint32_t width)
    {
        std::pair<uint32_t, float32_t> max_id_score_pair;
        for (uint32_t class_index = 0; class_index < m_nms_config.classes; class_index++) {
            auto class_id = class_index;
            if (m_nms_config.background_removal) {
                if (m_nms_config.background_removal_index == class_index) {
                    // Ignore if class_index is background_removal_index
                    continue;
                }
                else if (0 == m_nms_config.background_removal_index) {
                    // background_removal_index will always be the first or last index.
                    // If it is the first one we need to reduce all classes id's in 1.
                    // If it is the last one we just ignore it in the previous if case.
                    class_id--;
                }
            }

            auto class_entry_idx = entry_idx + ((classes_start_index + class_index) * width);
            auto class_confidence = Quantization::dequantize_output<HostType, DeviceType>(data[class_entry_idx], quant_info);
            auto class_score = class_confidence * objectness;
            if (class_score > max_id_score_pair.second) {
                max_id_score_pair.first = class_id;
                max_id_score_pair.second = class_score;
            }
        }
        return max_id_score_pair;
    }

    /**
     * Removes overlapping boxes in @a detections by setting the class confidence to zero.
     * 
     * @param[in] detections            A vector of @a DetectionBbox containing the detections boxes after ::extract_detections() function.
     * 
    */
    void remove_overlapping_boxes(std::vector<DetectionBbox> &detections, std::vector<uint32_t> &classes_detections_count);

    /*
    * For each class the layout is
    *       \code
    *       struct (packed) {
    *           uint16_t/float32_t bbox_count;
    *           hailo_bbox_t/hailo_bbox_float32_t bbox[bbox_count];
    *       };
    *       \endcode
    */
    void fill_nms_format_buffer(MemoryView &buffer, const std::vector<DetectionBbox> &detections,
        std::vector<uint32_t> &classes_detections_count);

    hailo_status hailo_nms_format(std::vector<DetectionBbox> &&detections,
        MemoryView dst_view, std::vector<uint32_t> &classes_detections_count);

    std::string get_nms_config_description();

};

}
}

#endif // _HAILO_NET_FLOW_NMS_POST_PROCESS_HPP_