/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file yolox_post_process.cpp
 * @brief YOLOX post process
 *
 **/

#include "net_flow/ops/yolox_post_process.hpp"

namespace hailort
{
namespace net_flow
{

Expected<std::shared_ptr<Op>> YOLOXPostProcessOp::create(const std::map<std::string, BufferMetaData> &inputs_metadata,
                                                         const std::map<std::string, BufferMetaData> &outputs_metadata,
                                                         const NmsPostProcessConfig &nms_post_process_config,
                                                         const YoloxPostProcessConfig &yolox_post_process_config)
{
    auto op = std::shared_ptr<YOLOXPostProcessOp>(new (std::nothrow) YOLOXPostProcessOp(inputs_metadata, outputs_metadata, nms_post_process_config,
        yolox_post_process_config));
    CHECK_AS_EXPECTED(op != nullptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::shared_ptr<Op>(std::move(op));
}

hailo_status YOLOXPostProcessOp::validate_metadata()
{
    auto status = NmsPostProcessOp::validate_metadata();
    if (HAILO_SUCCESS != status) {
        return status;
    }

    // Validate regs, clss and objs matching layers have same shape
    for (const auto &layer_names : m_yolox_config.input_names) {
        CHECK(contains(m_inputs_metadata, layer_names.reg), HAILO_INVALID_ARGUMENT,
            "YOLOXPostProcessOp: inputs_metadata does not contain reg layer {}", layer_names.reg);
        CHECK(contains(m_inputs_metadata, layer_names.cls), HAILO_INVALID_ARGUMENT,
            "YOLOXPostProcessOp: inputs_metadata does not contain cls layer {}", layer_names.cls);
        CHECK(contains(m_inputs_metadata, layer_names.obj), HAILO_INVALID_ARGUMENT,
            "YOLOXPostProcessOp: inputs_metadata does not contain obj layer {}", layer_names.obj);

        const auto &reg_input_metadata = m_inputs_metadata.at(layer_names.reg);
        const auto &cls_input_metadata = m_inputs_metadata.at(layer_names.cls);
        const auto &obj_input_metadata = m_inputs_metadata.at(layer_names.obj);

        // NOTE: padded shape might be different because features might be different,
        // and padding is added when width*features % 8 != 0
        CHECK((reg_input_metadata.shape.height == cls_input_metadata.shape.height)
            && (reg_input_metadata.shape.width == cls_input_metadata.shape.width),
            HAILO_INVALID_ARGUMENT, "YOLOXPostProcess: reg input {} has different shape than cls input {}",
                layer_names.reg, layer_names.cls);
        CHECK((obj_input_metadata.shape.height == reg_input_metadata.shape.height)
            && (obj_input_metadata.shape.width == reg_input_metadata.shape.width),
            HAILO_INVALID_ARGUMENT, "YOLOXPostProcess: reg input {} has different shape than obj input {}",
                layer_names.reg, layer_names.obj);

        CHECK((cls_input_metadata.format.type == reg_input_metadata.format.type)
            && (cls_input_metadata.format.flags == reg_input_metadata.format.flags)
            && (cls_input_metadata.format.order == reg_input_metadata.format.order),
            HAILO_INVALID_ARGUMENT, "YOLOXPostProcess: reg input {} has different format than cls input {}",
                layer_names.reg, layer_names.cls);
        CHECK((obj_input_metadata.format.type == reg_input_metadata.format.type)
            && (obj_input_metadata.format.flags == reg_input_metadata.format.flags)
            && (obj_input_metadata.format.order == reg_input_metadata.format.order),
            HAILO_INVALID_ARGUMENT, "YOLOXPostProcess: reg input {} has different format than obj input {}",
                layer_names.reg, layer_names.obj);

    }

    return HAILO_SUCCESS;
}

hailo_status YOLOXPostProcessOp::execute(const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs)
{
    std::vector<DetectionBbox> detections;
    std::vector<uint32_t> classes_detections_count(m_nms_config.number_of_classes, 0);
    detections.reserve(m_nms_config.max_proposals_per_class * m_nms_config.number_of_classes);
    for (const auto &layers_names_triplet : m_yolox_config.input_names) {
        hailo_status status;
        assert(contains(inputs, layers_names_triplet.cls));
        assert(contains(inputs, layers_names_triplet.obj));
        assert(contains(inputs, layers_names_triplet.reg));

        auto &input_metadata = m_inputs_metadata[layers_names_triplet.reg];
        if (input_metadata.format.type == HAILO_FORMAT_TYPE_UINT8) {
            status = extract_detections<float32_t, uint8_t>(layers_names_triplet, inputs.at(layers_names_triplet.reg), inputs.at(layers_names_triplet.cls), 
                inputs.at(layers_names_triplet.obj), detections, classes_detections_count);
        } else if (input_metadata.format.type == HAILO_FORMAT_TYPE_UINT16) {
            status = extract_detections<float32_t, uint16_t>(layers_names_triplet, inputs.at(layers_names_triplet.reg), inputs.at(layers_names_triplet.cls), 
                inputs.at(layers_names_triplet.obj), detections, classes_detections_count);
        } else {
            CHECK_SUCCESS(HAILO_INVALID_ARGUMENT, "YOLO post-process received invalid input type {}", input_metadata.format.type);
        }

        CHECK_SUCCESS(status);
    }

    return hailo_nms_format(std::move(detections), outputs.begin()->second, classes_detections_count);
}

hailo_bbox_float32_t YOLOXPostProcessOp::decode(float32_t tx, float32_t ty, float32_t tw, float32_t th,
    uint32_t col, uint32_t row, float32_t reg_shape_width, float32_t reg_shape_height) const
{
    /**
     * Note that the calculations are bit different from the source (In order to save some run time)
     * Each "/ reg_shape_width" is equivalent to "* w_stride / m_yolox_config.image_width".
     * Each "/ reg_shape_height" is equivalent to "* h_stride / m_yolox_config.image_height".
    **/
    auto w = exp(tw) / reg_shape_width;
    auto h = exp(th) / reg_shape_height;
    auto x_center = (tx + static_cast<float32_t>(col)) / reg_shape_width;
    auto y_center = (ty + static_cast<float32_t>(row)) / reg_shape_height;
    auto x_min = (x_center - (w / 2.0f));
    auto y_min = (y_center - (h / 2.0f));

    return hailo_bbox_float32_t{y_min, x_min, (y_min+h), (x_min+w), 0};
}

std::string YOLOXPostProcessOp::get_op_description()
{
    auto nms_config_info = get_nms_config_description();
    auto config_info = fmt::format("Name: {}, {}, Image height: {:.2f}, Image width: {:.2f}",
                        m_name, nms_config_info, m_yolox_config.image_height, m_yolox_config.image_width);
    return config_info;
}

}
}
