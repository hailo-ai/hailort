/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file nms_post_process.cpp
 * @brief NMS post process
 *
 * Reference code: https://github.com/winfredsu/ssd_postprocessing/blob/master/ssd_postprocessing.py
 **/

#include "net_flow/ops/nms_post_process.hpp"

namespace hailort
{
namespace net_flow
{

    hailo_status NmsPostProcessOp::validate_metadata()
    {
        for (const auto& output_metadata : m_outputs_metadata) {
            CHECK(HAILO_FORMAT_ORDER_HAILO_NMS == output_metadata.second.format.order, HAILO_INVALID_ARGUMENT, "The given output format order {} is not supported, "
                "should be HAILO_FORMAT_ORDER_HAILO_NMS", HailoRTCommon::get_format_order_str(output_metadata.second.format.order));

            CHECK(HAILO_FORMAT_TYPE_FLOAT32 == output_metadata.second.format.type, HAILO_INVALID_ARGUMENT, "The given output format type {} is not supported, "
                "should be HAILO_FORMAT_TYPE_FLOAT32", HailoRTCommon::get_format_type_str(output_metadata.second.format.type));

            CHECK(!(HAILO_FORMAT_FLAGS_TRANSPOSED & output_metadata.second.format.flags), HAILO_INVALID_ARGUMENT, "Output {} is marked as transposed, which is not supported for this model.",
                output_metadata.first);
            CHECK(!(HAILO_FORMAT_FLAGS_HOST_ARGMAX & output_metadata.second.format.flags), HAILO_INVALID_ARGUMENT, "Output {} is marked as argmax, which is not supported for this model.",
                output_metadata.first);
            CHECK(!(HAILO_FORMAT_FLAGS_QUANTIZED & output_metadata.second.format.flags), HAILO_INVALID_ARGUMENT, "Output {} is marked as quantized, which is not supported for this model.",
                output_metadata.first);
        }

        assert(1 <= m_inputs_metadata.size());
        const hailo_format_type_t& first_input_type = m_inputs_metadata.begin()->second.format.type;
        for (const auto& input_metadata : m_inputs_metadata) {
            CHECK(HAILO_FORMAT_ORDER_NHCW == input_metadata.second.format.order, HAILO_INVALID_ARGUMENT, "The given input format order {} is not supported, "
                "should be HAILO_FORMAT_ORDER_NHCW", HailoRTCommon::get_format_order_str(input_metadata.second.format.order));

            CHECK((HAILO_FORMAT_TYPE_UINT8 == input_metadata.second.format.type) ||
                (HAILO_FORMAT_TYPE_UINT16 == input_metadata.second.format.type),
                HAILO_INVALID_ARGUMENT, "The given input format type {} is not supported, should be HAILO_FORMAT_TYPE_UINT8 or HAILO_FORMAT_TYPE_UINT16",
                HailoRTCommon::get_format_type_str(input_metadata.second.format.type));

            CHECK(input_metadata.second.format.type == first_input_type, HAILO_INVALID_ARGUMENT,"All inputs format type should be the same");

            CHECK(HAILO_FORMAT_FLAGS_QUANTIZED == input_metadata.second.format.flags, HAILO_INVALID_ARGUMENT, "The given input format flag is not supported,"
                "should be HAILO_FORMAT_FLAGS_QUANTIZED");
        }

        return HAILO_SUCCESS;
    }

    float NmsPostProcessOp::compute_iou(const hailo_bbox_float32_t &box_1, const hailo_bbox_float32_t &box_2)
    {
        const float overlap_area_width = std::min(box_1.x_max, box_2.x_max) - std::max(box_1.x_min, box_2.x_min);
        const float overlap_area_height = std::min(box_1.y_max, box_2.y_max) - std::max(box_1.y_min, box_2.y_min);
        if (overlap_area_width <= 0.0f || overlap_area_height <= 0.0f) {
            return 0.0f;
        }
        const float intersection = overlap_area_width * overlap_area_height;
        const float box_1_area = (box_1.y_max - box_1.y_min) * (box_1.x_max - box_1.x_min);
        const float box_2_area = (box_2.y_max - box_2.y_min) * (box_2.x_max - box_2.x_min);
        const float union_area = (box_1_area + box_2_area - intersection);

        return (intersection / union_area);
    }

    void NmsPostProcessOp::remove_overlapping_boxes(std::vector<DetectionBbox> &detections, std::vector<uint32_t> &classes_detections_count)
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

                if (detections[i].m_class_id == detections[j].m_class_id
                        && (compute_iou(detections[i].m_bbox, detections[j].m_bbox) >= m_nms_config.nms_iou_th)) {
                    // Remove detections[j] if the iou is higher then the threshold
                    detections[j].m_bbox.score = REMOVED_CLASS_SCORE;
                    assert(detections[i].m_class_id < classes_detections_count.size());
                    assert(classes_detections_count[detections[j].m_class_id] > 0);
                    classes_detections_count[detections[j].m_class_id]--;
                }
            }
        }
    }

    void NmsPostProcessOp::fill_nms_format_buffer(MemoryView &buffer, const std::vector<DetectionBbox> &detections,
        std::vector<uint32_t> &classes_detections_count)
    {
        // Calculate the number of detections before each class, to help us later calculate the buffer_offset for it's detections.
        std::vector<uint32_t> num_of_detections_before(m_nms_config.number_of_classes, 0);
        uint32_t ignored_detections_count = 0;
        for (size_t class_idx = 0; class_idx < m_nms_config.number_of_classes; class_idx++) {
            if (classes_detections_count[class_idx] > m_nms_config.max_proposals_per_class) {
                ignored_detections_count += (classes_detections_count[class_idx] - m_nms_config.max_proposals_per_class);
                classes_detections_count[class_idx] = m_nms_config.max_proposals_per_class;
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
                // This class' detections count is higher then m_nms_config.max_proposals_per_class.
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
                ignored_detections_count, m_nms_config.max_proposals_per_class);
        }
    }

    hailo_status NmsPostProcessOp::hailo_nms_format(std::vector<DetectionBbox> &&detections,
        MemoryView dst_view, std::vector<uint32_t> &classes_detections_count)
    {
        remove_overlapping_boxes(detections, classes_detections_count);
        fill_nms_format_buffer(dst_view, detections, classes_detections_count);
        return HAILO_SUCCESS;
    }

    std::string NmsPostProcessOp::get_nms_config_description()
    {
        auto config_info = fmt::format("Score threshold: {:.3f}, Iou threshold: {:.2f}, Classes: {}, Cross classes: {}", 
                            m_nms_config.nms_score_th, m_nms_config.nms_iou_th, m_nms_config.number_of_classes, m_nms_config.cross_classes);
        if (m_nms_config.background_removal) {
            config_info += fmt::format(", Background removal index: {}", m_nms_config.background_removal_index);
        }
        return config_info;
    }
}
}