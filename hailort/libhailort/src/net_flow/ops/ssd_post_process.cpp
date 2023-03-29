/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file ssd_post_process.cpp
 * @brief SSD post process
 *
 * Reference code: https://github.com/winfredsu/ssd_postprocessing/blob/master/ssd_postprocessing.py
 **/

#include "net_flow/ops/ssd_post_process.hpp"

namespace hailort
{
namespace net_flow
{

Expected<std::shared_ptr<Op>> SSDPostProcessOp::create(const std::map<std::string, BufferMetaData> &inputs_metadata,
                                                       const std::map<std::string, BufferMetaData> &outputs_metadata,
                                                       const NmsPostProcessConfig &nms_post_process_config,
                                                       const SSDPostProcessConfig &ssd_post_process_config)
{
    for (auto &name_to_inputs_metadata : inputs_metadata) {
        CHECK_AS_EXPECTED(name_to_inputs_metadata.second.format.order == HAILO_FORMAT_ORDER_NHCW, HAILO_INVALID_ARGUMENT,
            "SSDPostProcessOp: Unexpected input format {}", name_to_inputs_metadata.second.format.order);
    }

    // Validate each anchor is mapped by reg and cls inputs
    for (const auto &reg_to_cls_name : ssd_post_process_config.reg_to_cls_inputs) {
        CHECK_AS_EXPECTED(ssd_post_process_config.anchors.count(reg_to_cls_name.first), HAILO_INVALID_ARGUMENT,
            "SSDPostProcessOp: anchors does not contain reg layer {}", reg_to_cls_name.first);
        CHECK_AS_EXPECTED(ssd_post_process_config.anchors.count(reg_to_cls_name.second), HAILO_INVALID_ARGUMENT,
            "SSDPostProcessOp: anchors does not contain cls layer {}", reg_to_cls_name.second);
        const auto &reg_anchors = ssd_post_process_config.anchors.at(reg_to_cls_name.first);
        const auto &cls_anchors = ssd_post_process_config.anchors.at(reg_to_cls_name.second);
        CHECK_AS_EXPECTED(reg_anchors.size() == cls_anchors.size(), HAILO_INVALID_ARGUMENT,
            "SSDPostProcessOp: reg and cls layers have different number of anchors. reg: #{}, cls: #{}",
                reg_anchors.size(), cls_anchors.size());
        for (size_t i = 0; i < reg_anchors.size(); ++i) {
            auto reg_anchor = reg_anchors[i];
            auto cls_anchor = cls_anchors[i];
            CHECK_AS_EXPECTED(reg_anchor == cls_anchor, HAILO_INVALID_ARGUMENT,
                "SSDPostProcessOp: reg and cls layers have differenet anchors. reg: {}, cls: {}",
                    reg_anchor, cls_anchor);
        }
    }

    // Validate regs and clss pairs have same shapes
    for (const auto &reg_to_cls_name : ssd_post_process_config.reg_to_cls_inputs) {
        CHECK_AS_EXPECTED(inputs_metadata.count(reg_to_cls_name.first), HAILO_INVALID_ARGUMENT,
            "SSDPostProcessOp: inputs_metadata does not contain reg layer {}", reg_to_cls_name.first);
        CHECK_AS_EXPECTED(inputs_metadata.count(reg_to_cls_name.second), HAILO_INVALID_ARGUMENT,
            "SSDPostProcessOp: inputs_metadata does not contain cls layer {}", reg_to_cls_name.second);
        const auto &reg_input_metadata = inputs_metadata.at(reg_to_cls_name.first);
        const auto &cls_input_metadata = inputs_metadata.at(reg_to_cls_name.second);
        // NOTE: padded shape might be different because features might be different,
        // and padding is added when width*features % 8 != 0
        CHECK_AS_EXPECTED((reg_input_metadata.shape.height == cls_input_metadata.shape.height)
            && (reg_input_metadata.shape.width == cls_input_metadata.shape.width),
            HAILO_INVALID_ARGUMENT, "SSDPostProcessOp: reg input {} has different shape than cls input {}",
                reg_to_cls_name.first, reg_to_cls_name.second);
    }
    auto op = std::shared_ptr<SSDPostProcessOp>(new (std::nothrow) SSDPostProcessOp(inputs_metadata, outputs_metadata, nms_post_process_config, ssd_post_process_config));
    CHECK_AS_EXPECTED(op != nullptr, HAILO_OUT_OF_HOST_MEMORY);
    return std::shared_ptr<Op>(std::move(op));
}

hailo_status SSDPostProcessOp::execute(const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs)
{
    CHECK(inputs.size() == m_ssd_config.anchors.size(), HAILO_INVALID_ARGUMENT,
        "Anchors vector count must be equal to data vector count. Anchors size is {}, data size is {}",
            m_ssd_config.anchors.size(), inputs.size());

    std::vector<DetectionBbox> detections;
    std::vector<uint32_t> classes_detections_count(m_nms_config.classes, 0);
    detections.reserve(m_nms_config.max_proposals_per_class * m_nms_config.classes);
    for (const auto &reg_to_cls : m_ssd_config.reg_to_cls_inputs) {
        assert(inputs.count(reg_to_cls.first));
        assert(inputs.count(reg_to_cls.second));
        auto status = extract_detections(reg_to_cls.first, reg_to_cls.second,
            inputs.at(reg_to_cls.first), inputs.at(reg_to_cls.second),
            detections, classes_detections_count);
        CHECK_SUCCESS(status);
    }

    // TODO: Add support for TF_FORMAT_ORDER
    return hailo_nms_format(std::move(detections), outputs.begin()->second, classes_detections_count);
}

hailo_status SSDPostProcessOp::extract_detections(const std::string &reg_input_name, const std::string &cls_input_name,
    const MemoryView &reg_buffer, const MemoryView &cls_buffer,
    std::vector<DetectionBbox> &detections, std::vector<uint32_t> &classes_detections_count)
{
    const auto &reg_shape = m_inputs_metadata[reg_input_name].shape;
    const auto &reg_padded_shape = m_inputs_metadata[reg_input_name].padded_shape;
    const auto &cls_padded_shape = m_inputs_metadata[cls_input_name].padded_shape;

    const uint32_t X_INDEX = m_ssd_config.tx_index;
    const uint32_t Y_INDEX = m_ssd_config.ty_index;
    const uint32_t W_INDEX = m_ssd_config.tw_index;
    const uint32_t H_INDEX = m_ssd_config.th_index;

    const uint32_t X_OFFSET = X_INDEX * reg_padded_shape.width;
    const uint32_t Y_OFFSET = Y_INDEX * reg_padded_shape.width;
    const uint32_t W_OFFSET = W_INDEX * reg_padded_shape.width;
    const uint32_t H_OFFSET = H_INDEX * reg_padded_shape.width;

    // Each layer anchors vector is structured as {w,h} pairs.
    // For example, if we have a vector of size 6 (default SSD vector) then we have 3 anchors for this layer.
    assert(m_ssd_config.anchors.count(reg_input_name));
    assert(m_ssd_config.anchors.count(cls_input_name));
    const auto &layer_anchors = m_ssd_config.anchors[reg_input_name];
    assert(layer_anchors.size() % 2 == 0);
    const size_t num_of_anchors = (layer_anchors.size() / 2);

    // Validate reg buffer size
    static const uint32_t reg_entry_size = 4;
    auto number_of_entries = reg_padded_shape.height * reg_padded_shape.width * num_of_anchors;
    auto buffer_size = number_of_entries * reg_entry_size;
    CHECK(buffer_size == reg_buffer.size(), HAILO_INVALID_ARGUMENT,
        "Failed to extract_detections, reg {} buffer_size should be {}, but is {}", reg_input_name, buffer_size, reg_buffer.size());

    // Validate cls buffer size
    const uint32_t cls_entry_size = m_nms_config.classes;
    number_of_entries = cls_padded_shape.height * cls_padded_shape.width * num_of_anchors;
    buffer_size = number_of_entries * cls_entry_size;
    CHECK(buffer_size == cls_buffer.size(), HAILO_INVALID_ARGUMENT,
        "Failed to extract_detections, cls {} buffer_size should be {}, but is {}", cls_input_name, buffer_size, cls_buffer.size());

    auto reg_row_size = reg_padded_shape.width * reg_padded_shape.features;
    auto cls_row_size = cls_padded_shape.width * cls_padded_shape.features;
    for (uint32_t row = 0; row < reg_shape.height; row++) {
        for (uint32_t col = 0; col < reg_shape.width; col++) {
            for (uint32_t anchor = 0; anchor < num_of_anchors; anchor++) {
                auto reg_idx = (reg_row_size * row) + col + ((anchor * reg_entry_size) * reg_padded_shape.width);
                auto cls_idx = (cls_row_size * row) + col + ((anchor * cls_entry_size) * cls_padded_shape.width);
                const auto &wa = layer_anchors[anchor * 2];
                const auto &ha = layer_anchors[anchor * 2 + 1];
                auto anchor_w_stride = 1.0f / static_cast<float32_t>(reg_shape.width);
                auto anchor_h_stride = 1.0f / static_cast<float32_t>(reg_shape.height);
                auto anchor_w_offset = 0.5f * anchor_w_stride;
                auto anchor_h_offset = 0.5f * anchor_h_stride;
                auto xcenter_a = static_cast<float32_t>(col) * anchor_w_stride + anchor_w_offset;
                auto ycenter_a = static_cast<float32_t>(row) * anchor_h_stride + anchor_h_offset;
                // Decode bboxes
                if (m_inputs_metadata[reg_input_name].format.type == HAILO_FORMAT_TYPE_UINT8) {
                    auto status = extract_bbox_detections<float32_t, uint8_t>(
                        reg_input_name, cls_input_name,
                        reg_buffer, cls_buffer,
                        reg_idx + X_OFFSET,
                        reg_idx + Y_OFFSET,
                        reg_idx + W_OFFSET,
                        reg_idx + H_OFFSET,
                        cls_idx, wa, ha, xcenter_a, ycenter_a,
                        detections, classes_detections_count);
                    CHECK_SUCCESS(status);
                } else if (m_inputs_metadata[reg_input_name].format.type == HAILO_FORMAT_TYPE_UINT16) {
                    auto status = extract_bbox_detections<float32_t, uint16_t>(
                        reg_input_name, cls_input_name,
                        reg_buffer, cls_buffer,
                        reg_idx + X_OFFSET,
                        reg_idx + Y_OFFSET,
                        reg_idx + W_OFFSET,
                        reg_idx + H_OFFSET,
                        cls_idx, wa, ha, xcenter_a, ycenter_a,
                        detections, classes_detections_count);
                    CHECK_SUCCESS(status);
                } else if (m_inputs_metadata[reg_input_name].format.type == HAILO_FORMAT_TYPE_FLOAT32) {
                    // For testing - TODO: Remove after generator tests are in, and return error.
                    auto status = extract_bbox_detections<float32_t, float32_t>(
                        reg_input_name, cls_input_name,
                        reg_buffer, cls_buffer,
                        reg_idx + X_OFFSET,
                        reg_idx + Y_OFFSET,
                        reg_idx + W_OFFSET,
                        reg_idx + H_OFFSET,
                        cls_idx, wa, ha, xcenter_a, ycenter_a,
                        detections, classes_detections_count);
                    CHECK_SUCCESS(status);
                } else {
                    CHECK_SUCCESS(HAILO_INVALID_ARGUMENT, "SSD post-process received invalid reg input type: {}",
                        m_inputs_metadata[reg_input_name].format.type);
                }
            }
        }
    }
    
    return HAILO_SUCCESS;
}

std::string SSDPostProcessOp::get_op_description()
{
    auto nms_config_info = get_nms_config_description();
    auto config_info = fmt::format("Name: {}, {}, Image height: {:.2f}, Image width: {:.2f}, Centers scales factor: {}, "
                        "Bbox dimension scale factor: {}, Normalize boxes: {}", m_name, nms_config_info, m_ssd_config.image_height, m_ssd_config.image_width,
                        m_ssd_config.centers_scale_factor, m_ssd_config.bbox_dimensions_scale_factor, m_ssd_config.normalize_boxes);
    return config_info;
}

}
}