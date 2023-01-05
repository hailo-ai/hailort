/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file yolo_post_processing.hpp
 * @brief YOLO post processing
 *
 * https://learnopencv.com/object-detection-using-yolov5-and-opencv-dnn-in-c-and-python :
 * The headline '4.3.5 POST-PROCESSING YOLOv5 Prediction Output' contains explanations on the YOLOv5 post-processing.
 **/

#ifndef _HAILO_YOLO_POST_PROCESSING_HPP_
#define _HAILO_YOLO_POST_PROCESSING_HPP_

#include "hailo/hailort.hpp"

namespace hailort
{
namespace net_flow
{

static const float32_t REMOVED_CLASS_SCORE = 0.0f;

struct DetectionBbox
{
    DetectionBbox(float32_t x_min, float32_t y_min, float32_t width, float32_t height, float32_t score, uint32_t class_id) :
        m_class_id(class_id), m_bbox{y_min, x_min, (y_min + height), (x_min + width), score} {}

    uint32_t m_class_id;
    hailo_bbox_float32_t m_bbox;
};

/**
 * Computes the value of the sigmoid function on @a x input: f(x) = 1/(1 + e^-x)
*/
inline float32_t sigmoid(float32_t x)
{
    return 1.0f / (1.0f + expf(-x));
}

// TODO: Maybe change to dequantize entry and add @a should_sigmoid to dequantize_output_buffer in `quantization.hpp`.
// Its an API addition but does not break anything.
template<typename HostType = float32_t, typename DeviceType>
HostType apply_dequantization_activation(DeviceType value, hailo_quant_info_t quant_info, bool should_sigmoid)
{
    auto dequantized_val = Quantization::dequantize_output<HostType, DeviceType>(value, quant_info);

    if (should_sigmoid) {
        return sigmoid(dequantized_val);
    } else {
        return dequantized_val;
    }
}

class YOLOv5PostProcessingOp
{
public:

    /**
     * @param[in] anchors                       A vector of anchors, each element in the vector represents the anchors for a specific layer.
     * @param[in] image_height                  The image height.
     * @param[in] image_width                   The image width.
     * @param[in] confidence_threshold          User given confidence threshold for a bbox. A bbox will be consider as detection if the 
     *                                          (objectness * class_score) is higher then the confidence_threshold.
     * @param[in] iou_threshold                 User given IOU threshold (intersection over union). This threshold is for performing
     *                                          Non-maximum suppression (Removing overlapping boxes).
     * @param[in] num_of_classes                The model's number of classes. (This depends on the dataset that the model trained on).
     * @param[in] should_dequantize             Indicates whether the post-processing function should de-quantize the tensors data.
     * @param[in] max_bboxes_per_class          Maximum amount of bboxes per nms class.
     * @param[in] should_sigmoid                Indicates whether sigmoid() function should be performed on the @a tensors' data.
     * @param[in] one_class_per_bbox            Indicates whether the post-processing function should return only one class per detected bbox.
     *                                          If set to flase - Two different classes can have the same bbox.
     *
     * @return Upon success, returns a vector of detection objects. Otherwise, returns Unexpected of ::hailo_status error.
     *  TODO: For integrating with SDK Json - consider changing anchors vector to a vector of w,h pairs.
     *        HRT-8526 - Add post-processing support for quantized data
     */
    static Expected<YOLOv5PostProcessingOp> create(const std::vector<std::vector<int>> &anchors,
        const std::vector<hailo_3d_image_shape_t> &shapes, const std::vector<hailo_format_t> &formats,
        const std::vector<hailo_quant_info_t> &quant_infos, float32_t image_height, float32_t image_width,
        float32_t confidence_threshold, float32_t iou_threshold, uint32_t num_of_classes, bool should_dequantize,
        uint32_t max_bboxes_per_class, bool should_sigmoid, bool one_class_per_bbox=true)
    {
        return YOLOv5PostProcessingOp(anchors, shapes, formats, quant_infos, image_height, image_width, confidence_threshold, iou_threshold,
            num_of_classes, should_dequantize, max_bboxes_per_class, should_sigmoid, one_class_per_bbox);
    }

    /**
     * Execute YOLOv5 post-processing on inferred data.
     * @a HostType can be uint16 or float32.
     * TODO: HRT-8525 - Add support for these types. Currently we support only in: @a HostType = float32_t
     *
     * @param[in] tensors               A vector of the input buffers for the post-processing,
     *                                  the buffer's shape and the quantization info.
     * NOTE: The Order of the @a tensors vector should be corresponding to the order of @a anchors vector given in the creation of YOLOv5PostProcessingOp.
     *
     * @return Upon success, returns a buffer containing the detection objects, in ::HAILO_FORMAT_ORDER_HAILO_NMS format.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    template<typename HostType = float32_t>
    hailo_status execute(const std::vector<MemoryView> &tensors, MemoryView dst_view)
    {
        CHECK(tensors.size() == m_anchors.size(), HAILO_INVALID_ARGUMENT,
            "Anchors vector count must be equal to data vector count. Anchors size is {}, data size is {}", m_anchors.size(), tensors.size());

        std::vector<DetectionBbox> detections;
        std::vector<uint32_t> classes_detections_count(m_num_of_classes, 0);
        detections.reserve(m_max_bboxes_per_class * m_num_of_classes);
        for (size_t i = 0; i < tensors.size(); i++) {
            hailo_status status;
            if (m_formants[i].type == HAILO_FORMAT_TYPE_UINT8) {
                status = extract_detections<HostType, uint8_t>(tensors[i], m_quant_infos[i], m_shapes[i],
                    m_anchors[i], detections, classes_detections_count);
            } else if (m_formants[i].type == HAILO_FORMAT_TYPE_UINT16) {
                status = extract_detections<HostType, uint16_t>(tensors[i], m_quant_infos[i], m_shapes[i],
                    m_anchors[i], detections, classes_detections_count);
            } else {
                CHECK_SUCCESS(HAILO_INVALID_ARGUMENT, "YOLOv5 post-process received invalid input type");
            }
            CHECK_SUCCESS(status);
        }

        // TODO: Add support for TF_FORMAT_ORDER
        return hailo_nms_format(std::move(detections), dst_view, classes_detections_count);
    }

private:
    YOLOv5PostProcessingOp(const std::vector<std::vector<int>> &anchors, const std::vector<hailo_3d_image_shape_t> &shapes,
        const std::vector<hailo_format_t> &formats, const std::vector<hailo_quant_info_t> &quant_infos, float32_t image_height, float32_t image_width,
        float32_t confidence_threshold, float32_t iou_threshold, uint32_t num_of_classes, bool should_dequantize, uint32_t max_bboxes_per_class, bool should_sigmoid, bool one_class_per_bbox) :
            m_anchors(anchors), m_shapes(shapes), m_formants(formats), m_quant_infos(quant_infos), m_image_height(image_height), m_image_width(image_width),
            m_confidence_threshold(confidence_threshold), m_iou_threshold(iou_threshold), m_num_of_classes(num_of_classes),
            m_should_dequantize(should_dequantize), m_max_bboxes_per_class(max_bboxes_per_class), m_should_sigmoid(should_sigmoid),
            m_one_class_per_bbox(one_class_per_bbox)
        {
            (void)m_should_dequantize;
        }

    template<typename HostType = float32_t, typename DeviceType>
    std::pair<uint32_t, float32_t> get_max_class(const uint8_t *data, size_t entry_classes_idx, float32_t objectness, hailo_quant_info_t quant_info)
    {
        std::pair<uint32_t, float32_t> max_id_score_pair;
        for (uint32_t class_index = 0; class_index < m_num_of_classes; class_index++) {
            auto class_confidence = apply_dequantization_activation<HostType, DeviceType>(data[entry_classes_idx + class_index], quant_info, m_should_sigmoid);
            auto class_score = class_confidence * objectness;
            if (class_score > max_id_score_pair.second) {
                max_id_score_pair.first = class_index;
                max_id_score_pair.second = class_score;
            }
        }
        return max_id_score_pair;
    }

    /**
     * Extract bboxes with confidence level higher then @a confidence_threshold from @a buffer and add them to @a detections.
     *
     * @param[in] buffer                        Buffer containing data after inference and 
     * @param[in] quant_info                    Quantization info corresponding to the @a buffer layer.
     * @param[in] shape                         Shape corresponding to the @a buffer layer.
     * @param[in] image_height                  The image height.
     * @param[in] image_width                   The image width.
     * @param[in] layer_anchors                 The layer anchors corresponding to layer receiving the @a buffer.
     *                                          Each anchor is structured as {width, height} pairs.
     * @param[in] confidence_threshold          User given confidence threshold for a bbox. A bbox will be consider as detection if the 
     *                                          (objectness * class_score) is higher then the confidence_threshold.
     * @param[in] num_of_classes                The model's number of classes.
     * @param[in] should_sigmoid                Indicates whether sigmoid() function should be performed on the @a buffer's data.
     * @param[inout] detections                 A vector of ::DetectionBbox objects, to add the detected bboxes to.
     * 
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
    */
    template<typename HostType = float32_t, typename DeviceType>
    hailo_status extract_detections(const MemoryView &buffer, hailo_quant_info_t quant_info, hailo_3d_image_shape_t shape,
        const std::vector<int> &layer_anchors, std::vector<DetectionBbox> &detections, std::vector<uint32_t> &classes_detections_count)
    {
        static const uint32_t X_INDEX = 0;
        static const uint32_t Y_INDEX = 1;
        static const uint32_t W_INDEX = 2;
        static const uint32_t H_INDEX = 3;
        static const uint32_t OBJECTNESS_INDEX = 4;
        static const uint32_t CLASSES_START_INDEX = 5;

        // Each layer anchors vector is structured as {w,h} pairs.
        // For example, if we have a vector of size 6 (default YOLOv5 vector) then we have 3 anchors for this layer.
        assert(layer_anchors.size() % 2 == 0);
        const size_t num_of_anchors = (layer_anchors.size() / 2);

        const uint32_t entry_size = CLASSES_START_INDEX + m_num_of_classes;
        auto number_of_entries = shape.height * shape.width * num_of_anchors;
        // TODO: this can also be part of the Op configuration
        auto buffer_size = number_of_entries * entry_size;
        CHECK(buffer_size == buffer.size(), HAILO_INVALID_ARGUMENT,
            "Failed to extract_detections, buffer_size should be {}, but is {}", buffer_size, buffer.size());

        auto *data = buffer.data();
        for (size_t row = 0; row < shape.height; row++) {
            for (size_t col = 0; col < shape.width; col++) {
                for (size_t anchor = 0; anchor < num_of_anchors; anchor++) {
                    auto entry_idx = entry_size * (num_of_anchors * (shape.height * row + col) + anchor);

                    auto objectness = apply_dequantization_activation<HostType, DeviceType>(data[entry_idx + OBJECTNESS_INDEX], quant_info, m_should_sigmoid);
                    if (objectness < m_confidence_threshold) {
                        continue;
                    }
                    
                    auto tx = apply_dequantization_activation<HostType, DeviceType>(data[entry_idx + X_INDEX], quant_info, m_should_sigmoid);
                    auto ty = apply_dequantization_activation<HostType, DeviceType>(data[entry_idx + Y_INDEX], quant_info, m_should_sigmoid);
                    auto tw = apply_dequantization_activation<HostType, DeviceType>(data[entry_idx + W_INDEX], quant_info, m_should_sigmoid);
                    auto th = apply_dequantization_activation<HostType, DeviceType>(data[entry_idx + H_INDEX], quant_info, m_should_sigmoid);

                    // Source for the calculations - https://github.com/ultralytics/yolov5/blob/HEAD/models/yolo.py
                    // Explanations for the calculations - https://github.com/ultralytics/yolov5/issues/471
                    auto w = pow(2.0f * tw, 2.0f) * static_cast<float32_t>(layer_anchors[anchor * 2]) / m_image_width;
                    auto h = pow(2.0f * th, 2.0f) * static_cast<float32_t>(layer_anchors[anchor * 2 + 1]) / m_image_height;
                    auto x_center = (tx * 2.0f - 0.5f + static_cast<float32_t>(col)) / static_cast<float32_t>(shape.width);
                    auto y_center = (ty * 2.0f - 0.5f + static_cast<float32_t>(row)) / static_cast<float32_t>(shape.height);
                    auto x_min = (x_center - (w / 2.0f));
                    auto y_min = (y_center - (h / 2.0f));

                    if (m_one_class_per_bbox) {
                        auto entry_classes_idx = entry_idx + CLASSES_START_INDEX;
                        auto max_id_score_pair = get_max_class<HostType, DeviceType>(data, entry_classes_idx , objectness, quant_info);
                        if (max_id_score_pair.second >= m_confidence_threshold) {
                            detections.emplace_back(x_min, y_min, w, h, max_id_score_pair.second, max_id_score_pair.first);
                            classes_detections_count[max_id_score_pair.first]++;
                        }
                    }
                    else {
                        for (uint32_t class_index = 0; class_index < m_num_of_classes; class_index++) {
                            auto class_confidence = apply_dequantization_activation<HostType, DeviceType>(
                                data[entry_idx + CLASSES_START_INDEX + class_index], quant_info, m_should_sigmoid);
                            auto class_score = class_confidence * objectness;
                            if (class_score >= m_confidence_threshold) {
                                detections.emplace_back(x_min, y_min, w, h, class_score, class_index);
                                classes_detections_count[class_index]++;
                            }
                        }
                    }
                }
            }
        }
        
        return HAILO_SUCCESS;
    }

    /**
     * Computes the IOU ratio of @a box_1 and @a box_2 
    */
    float compute_iou(const DetectionBbox &box_1, const DetectionBbox &box_2)
    {
        const float overlap_area_width = std::min(box_1.m_bbox.x_max, box_2.m_bbox.x_max) - std::max(box_1.m_bbox.x_min, box_2.m_bbox.x_min);
        const float overlap_area_height = std::min(box_1.m_bbox.y_max, box_2.m_bbox.y_max) - std::max(box_1.m_bbox.y_min, box_2.m_bbox.y_min);
        if (overlap_area_width <= 0.0f || overlap_area_height <= 0.0f) {
            return 0.0f;
        }
        const float intersection = overlap_area_width * overlap_area_height;
        const float box_1_area = (box_1.m_bbox.y_max - box_1.m_bbox.y_min) * (box_1.m_bbox.x_max - box_1.m_bbox.x_min);
        const float box_2_area = (box_2.m_bbox.y_max - box_2.m_bbox.y_min) * (box_2.m_bbox.x_max - box_2.m_bbox.x_min);
        const float union_area = (box_1_area + box_2_area - intersection);

        return (intersection / union_area);
    }

    /**
     * Removes overlapping boxes in @a detections by setting the class confidence to zero.
     * 
     * @param[in] detections            A vector of @a DetectionBbox containing the detections boxes after ::extract_detections() function.
     * 
    */
    void remove_overlapping_boxes(std::vector<DetectionBbox> &detections, std::vector<uint32_t> &classes_detections_count)
    {
        std::sort(detections.begin(), detections.end(),
                [](DetectionBbox a, DetectionBbox b)
                { return a.m_bbox.score > b.m_bbox.score; });

        for (size_t i = 0; i < detections.size(); i++) {
            if (detections[i].m_bbox.score == REMOVED_CLASS_SCORE) {
                // Detection overlapped with a higher score detection
                continue;
            }

            for (size_t j = i + 1; j < detections.size(); j++) {
                if (detections[j].m_bbox.score == REMOVED_CLASS_SCORE) {
                    // Detection overlapped with a higher score detection
                    continue;
                }

                if ((detections[i].m_class_id == detections[j].m_class_id) &&
                    (compute_iou(detections[i], detections[j]) >= m_iou_threshold)) {
                        // Remove detections[j] if the iou is higher then the threshold
                        detections[j].m_bbox.score = REMOVED_CLASS_SCORE;
                        assert(classes_detections_count[detections[j].m_class_id] > 0);
                        classes_detections_count[detections[j].m_class_id]--;
                }
            }
        }
    }

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
        std::vector<uint32_t> &classes_detections_count)
    {
        // Calculate the number of detections before each class, to help us later calculate the buffer_offset for it's detections.
        std::vector<uint32_t> num_of_detections_before;
        num_of_detections_before.reserve(m_num_of_classes);
        uint32_t ignored_detections_count = 0;
        for (size_t class_idx = 0; class_idx < m_num_of_classes; class_idx++) {
            if (classes_detections_count[class_idx] > m_max_bboxes_per_class) {
                ignored_detections_count += (classes_detections_count[class_idx] - m_max_bboxes_per_class);
                classes_detections_count[class_idx] = m_max_bboxes_per_class;
            }

            if (0 == class_idx) {
                num_of_detections_before[class_idx] = 0;
            }
            else {
                num_of_detections_before[class_idx] = num_of_detections_before[class_idx - 1] + classes_detections_count[class_idx - 1];
            }

            // Fill `bbox_count` value for class_idx in the result buffer
            float32_t bbox_count_casted = static_cast<float32_t>(classes_detections_count[class_idx]);
            auto buffer_offset = (class_idx * sizeof(bbox_count_casted)) + (num_of_detections_before[class_idx] * sizeof(hailo_bbox_float32_t));
            memcpy((buffer.data() + buffer_offset), &bbox_count_casted, sizeof(bbox_count_casted));
        }

        for (auto &detection : detections) {
            if (REMOVED_CLASS_SCORE == detection.m_bbox.score) {
                // Detection overlapped with a higher score detection and removed in remove_overlapping_boxes()
                continue;
            }
            if (0 == classes_detections_count[detection.m_class_id]) {
                // This class' detections count is higher then m_max_bboxes_per_class.
                // This detection is ignored due to having lower score (detections vector is sorted by score).
                continue;
            }

            auto buffer_offset = ((detection.m_class_id + 1) * sizeof(float32_t))
                                    + (num_of_detections_before[detection.m_class_id] * sizeof(hailo_bbox_float32_t));

            assert((buffer_offset + sizeof(hailo_bbox_float32_t)) <= buffer.size());
            memcpy((hailo_bbox_float32_t*)(buffer.data() + buffer_offset), &detection.m_bbox, sizeof(hailo_bbox_float32_t));
            num_of_detections_before[detection.m_class_id]++;
            classes_detections_count[detection.m_class_id]--;
        }

        if (0 != ignored_detections_count) {
            LOGGER__INFO("{} Detections were ignored, due to `max_bboxes_per_class` defined as {}.",
                ignored_detections_count, m_max_bboxes_per_class);
        }
    }

    hailo_status hailo_nms_format(std::vector<DetectionBbox> &&detections, MemoryView dst_view, std::vector<uint32_t> &classes_detections_count)
    {
        remove_overlapping_boxes(detections, classes_detections_count);
        fill_nms_format_buffer(dst_view, detections, classes_detections_count);
        return HAILO_SUCCESS;
    }

    std::vector<std::vector<int>> m_anchors;
    std::vector<hailo_3d_image_shape_t> m_shapes;
    std::vector<hailo_format_t> m_formants;
    std::vector<hailo_quant_info_t> m_quant_infos;
    float32_t m_image_height;
    float32_t m_image_width;
    float32_t m_confidence_threshold;
    float32_t m_iou_threshold;
    uint32_t m_num_of_classes;
    bool m_should_dequantize;
    uint32_t m_max_bboxes_per_class;
    bool m_should_sigmoid;
    bool m_one_class_per_bbox;
};

} /* namespace net_flow */
} /* namespace hailort */

#endif /* _HAILO_YOLO_POST_PROCESSING_HPP_ */
