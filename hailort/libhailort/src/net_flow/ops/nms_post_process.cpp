/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file nms_post_process.cpp
 * @brief NMS post process
 *
 * Reference code: https://github.com/winfredsu/ssd_postprocessing/blob/master/ssd_postprocessing.py
 **/

#include "net_flow/ops/nms_post_process.hpp"
#include "hailo/hailort_defaults.hpp"

namespace hailort
{
namespace net_flow
{

Expected<std::shared_ptr<OpMetadata>> NmsOpMetadata::create(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
    const std::unordered_map<std::string, BufferMetaData> &outputs_metadata, const NmsPostProcessConfig &nms_post_process_config,
    const std::string &network_name, const OperationType type, const std::string &name)
{
    auto op_metadata = std::shared_ptr<NmsOpMetadata>(new (std::nothrow) NmsOpMetadata(inputs_metadata, outputs_metadata, nms_post_process_config,
        name, network_name, type));
    CHECK_AS_EXPECTED(op_metadata != nullptr, HAILO_OUT_OF_HOST_MEMORY);

    auto status = op_metadata->validate_params();
    CHECK_SUCCESS_AS_EXPECTED(status);

    return std::shared_ptr<OpMetadata>(std::move(op_metadata));
}

std::string NmsOpMetadata::get_op_description()
{
    auto op_description = fmt::format("Op {}, Name: {}, {}", OpMetadata::get_operation_type_str(m_type), m_name, get_nms_config_description());
    return op_description;
}

hailo_status NmsOpMetadata::validate_format_info()
{
    for (const auto& output_metadata : m_outputs_metadata) {
        CHECK(((HailoRTCommon::is_nms_by_class(output_metadata.second.format.order))
            || (HAILO_FORMAT_ORDER_HAILO_NMS_BY_SCORE == output_metadata.second.format.order)),
            HAILO_INVALID_ARGUMENT, "The given output format order {} is not supported, "
            "should be HAILO_FORMAT_ORDER_HAILO_NMS_BY_CLASS or HAILO_FORMAT_ORDER_HAILO_NMS_BY_SCORE",
            HailoRTCommon::get_format_order_str(output_metadata.second.format.order));

        CHECK_SUCCESS(validate_format_type(output_metadata.second.format));

        CHECK(!(HAILO_FORMAT_FLAGS_TRANSPOSED & output_metadata.second.format.flags), HAILO_INVALID_ARGUMENT, "Output {} is marked as transposed, which is not supported for this model.",
            output_metadata.first);
    }
    if (m_type == OperationType::IOU) {
        assert(1 == m_inputs_metadata.size());
        CHECK(HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP == m_inputs_metadata.begin()->second.format.order, HAILO_INVALID_ARGUMENT, "The given input format order {} is not supported, "
            "should be HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP", HailoRTCommon::get_format_order_str(m_inputs_metadata.begin()->second.format.order));
    } else {
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
        }
    }

    return HAILO_SUCCESS;
}

hailo_status NmsOpMetadata::validate_format_type(const hailo_format_t &format)
{
    if (HailoRTCommon::is_nms_by_score(format.order)) {
        CHECK(HAILO_FORMAT_TYPE_UINT8 == format.type, HAILO_INVALID_ARGUMENT, "The given output format type {} is not supported, "
            "should be HAILO_FORMAT_TYPE_UINT8", HailoRTCommon::get_format_type_str(format.type));
    } else {
        CHECK(HAILO_FORMAT_TYPE_FLOAT32 == format.type, HAILO_INVALID_ARGUMENT, "The given output format type {} is not supported, "
            "should be HAILO_FORMAT_TYPE_FLOAT32", HailoRTCommon::get_format_type_str(format.type));
    }
    return HAILO_SUCCESS;
}

hailo_status NmsOpMetadata::validate_params()
{
    return HAILO_SUCCESS;
}

std::string NmsOpMetadata::get_nms_config_description()
{
    auto config_info = fmt::format("Score threshold: {:.3f}, IoU threshold: {:.2f}, Classes: {}",
                        m_nms_config.nms_score_th, m_nms_config.nms_iou_th, m_nms_config.number_of_classes);
    if ((HailoRTCommon::is_nms_by_class(m_outputs_metadata.begin()->second.format.order)) ||
        (HAILO_FORMAT_ORDER_NHWC == m_outputs_metadata.begin()->second.format.order)){
        config_info += fmt::format(", Max bboxes per class: {}", m_nms_config.max_proposals_per_class);
    }
    if (HailoRTCommon::is_nms_by_score(m_outputs_metadata.begin()->second.format.order)){
        config_info += fmt::format(", Max bboxes total: {}", m_nms_config.max_proposals_total);
    }
    if (m_nms_config.background_removal) {
        config_info += fmt::format(", Background removal index: {}", m_nms_config.background_removal_index);
    }
    return config_info;
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

void NmsPostProcessOp::remove_overlapping_boxes(std::vector<DetectionBbox> &detections, std::vector<uint32_t> &classes_detections_count,
    double iou_th)
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
                    && (compute_iou(detections[i].m_bbox, detections[j].m_bbox) >= iou_th)) {
                // Remove detections[j] if the iou is higher then the threshold
                detections[j].m_bbox.score = REMOVED_CLASS_SCORE;
                assert(detections[i].m_class_id < classes_detections_count.size());
                assert(classes_detections_count[detections[j].m_class_id] > 0);
                classes_detections_count[detections[j].m_class_id]--;
            }
        }
    }
}

void NmsPostProcessOp::fill_nms_by_class_format_buffer(MemoryView &buffer, const std::vector<DetectionBbox> &detections,
    std::vector<uint32_t> &classes_detections_count, const NmsPostProcessConfig &nms_config)
{
    // Calculate the number of detections before each class, to help us later calculate the buffer_offset for it's detections.
    std::vector<uint32_t> num_of_detections_before(nms_config.number_of_classes, 0);
    uint32_t ignored_detections_count = 0;
    for (size_t class_idx = 0; class_idx < nms_config.number_of_classes; class_idx++) {
        if (classes_detections_count[class_idx] > nms_config.max_proposals_per_class) {
            ignored_detections_count += (classes_detections_count[class_idx] - nms_config.max_proposals_per_class);
            classes_detections_count[class_idx] = nms_config.max_proposals_per_class;
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
        *(hailo_bbox_float32_t*)(buffer.data() + buffer_offset) = *(hailo_bbox_float32_t*)&(detection.m_bbox);
        num_of_detections_before[detection.m_class_id]++;
        classes_detections_count[detection.m_class_id]--;
    }

    if (0 != ignored_detections_count) {
        LOGGER__INFO("{} Detections were ignored, due to `max_bboxes_per_class` defined as {}.",
            ignored_detections_count, nms_config.max_proposals_per_class);
    }
}

void NmsPostProcessOp::fill_nms_by_score_format_buffer(MemoryView &buffer, std::vector<DetectionBbox> &detections,
    const NmsPostProcessConfig &nms_config, const bool should_sort)
{
    if (should_sort) {
        std::sort(detections.begin(), detections.end(),
            [](DetectionBbox a, DetectionBbox b)
            { return a.m_bbox.score > b.m_bbox.score; });
    }

    uint16_t total_detections_count = 0;
    for (auto detection_bbox : detections) {
        if (REMOVED_CLASS_SCORE == detection_bbox.m_bbox.score) {
            // Detection overlapped with a higher score detection and removed in remove_overlapping_boxes()
            continue;
        }

        if (total_detections_count > nms_config.max_proposals_total) {
            LOGGER__INFO("{} detections were ignored, due to `max_bboxes_total` defined as {}.",
                detections.size() - nms_config.max_proposals_total, nms_config.max_proposals_total);
            break;
        }

        auto buffer_offset = sizeof(total_detections_count) + ((total_detections_count) * sizeof(hailo_detection_t));
        assert((buffer_offset + sizeof(hailo_detection_t)) <= buffer.size());
        hailo_detection_t detection;
        assert(detection_bbox.m_class_id <= MAX_NMS_CLASSES);
        detection.class_id = static_cast<uint16_t>(detection_bbox.m_class_id);
        detection.score = detection_bbox.m_bbox.score;
        detection.x_min = detection_bbox.m_bbox.x_min;
        detection.x_max = detection_bbox.m_bbox.x_max;
        detection.y_min = detection_bbox.m_bbox.y_min;
        detection.y_max = detection_bbox.m_bbox.y_max;

        *(hailo_detection_t*)(buffer.data() + buffer_offset) = detection;
        total_detections_count++;
    }
    *(uint16_t*)(buffer.data()) = total_detections_count;
}

hailo_status NmsPostProcessOp::hailo_nms_format(MemoryView dst_view)
{
    remove_overlapping_boxes(m_detections, m_classes_detections_count, m_nms_metadata->nms_config().nms_iou_th);
    if ((HailoRTCommon::is_nms_by_class(m_nms_metadata->outputs_metadata().begin()->second.format.order)) ||
        (HAILO_FORMAT_ORDER_NHWC == m_nms_metadata->outputs_metadata().begin()->second.format.order)) {
        fill_nms_by_class_format_buffer(dst_view, m_detections, m_classes_detections_count, m_nms_metadata->nms_config());
    } else if (HailoRTCommon::is_nms_by_score(m_nms_metadata->outputs_metadata().begin()->second.format.order)) {
        fill_nms_by_score_format_buffer(dst_view, m_detections, m_nms_metadata->nms_config());
    } else {
        LOGGER__ERROR("Unsupported output format order for NmsPostProcessOp: {}",
            HailoRTCommon::get_format_order_str(m_nms_metadata->outputs_metadata().begin()->second.format.order));
        return HAILO_INVALID_ARGUMENT;
    }
    return HAILO_SUCCESS;
}

hailo_format_t NmsOpMetadata::expand_output_format_autos_by_op_type(const hailo_format_t &output_format, OperationType type, bool bbox_only)
{
    auto format = output_format;

    if (HAILO_FORMAT_ORDER_AUTO == format.order) {
        if (OperationType::YOLOV5SEG == type) {
            format.order = HAILO_FORMAT_ORDER_HAILO_NMS_WITH_BYTE_MASK;
        } else if (bbox_only) {
            format.order = HAILO_FORMAT_ORDER_NHWC;
        } else {
            format.order = HAILO_FORMAT_ORDER_HAILO_NMS_BY_CLASS;
        }
    }

    if (HAILO_FORMAT_TYPE_AUTO == format.type) {
        format.type = HailoRTDefaults::get_default_nms_format_type(format.order);
    }

    return format;
}

Expected<hailo_vstream_info_t> NmsOpMetadata::get_output_vstream_info()
{
    CHECK_AS_EXPECTED((m_outputs_metadata.size() == 1), HAILO_INVALID_OPERATION, "{} has more than 1 output", m_name);

    hailo_vstream_info_t vstream_info{};
    strncpy(vstream_info.name, m_outputs_metadata.begin()->first.c_str(), m_outputs_metadata.begin()->first.length() + 1);
    strncpy(vstream_info.network_name, m_network_name.c_str(), m_network_name.length() + 1);
    vstream_info.direction = HAILO_D2H_STREAM;
    vstream_info.format.order = m_outputs_metadata.begin()->second.format.order;
    vstream_info.format.type = m_outputs_metadata.begin()->second.format.type;
    vstream_info.format.flags = HAILO_FORMAT_FLAGS_NONE;
    vstream_info.nms_shape.max_bboxes_total = nms_config().max_proposals_total;
    vstream_info.nms_shape.max_bboxes_per_class = nms_config().max_proposals_per_class;
    vstream_info.nms_shape.number_of_classes = nms_config().number_of_classes;
    if (nms_config().background_removal) {
        vstream_info.nms_shape.number_of_classes--;
    }

    // In order to pass is_qp_valid check in pyhailort
    vstream_info.quant_info.qp_scale = 1;

    return vstream_info;
}

hailo_nms_info_t NmsOpMetadata::nms_info()
{
    hailo_nms_info_t nms_info = {
        nms_config().number_of_classes,
        nms_config().max_proposals_per_class,
        nms_config().max_proposals_total,
        sizeof(hailo_bbox_float32_t),
        1, // input_division_factor
        false,
        hailo_nms_defuse_info_t(),
        DEFAULT_NMS_NO_BURST_SIZE,
        HAILO_BURST_TYPE_H8_BBOX
    };
    if (nms_config().background_removal) {
        nms_info.number_of_classes--;
    }

    return nms_info;
}

}
}
