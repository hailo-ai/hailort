/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file yolov8_post_process.cpp
 * @brief YOLOV8 post process
 *
 **/

#include "net_flow/ops/yolov8_post_process.hpp"
#include "net_flow/ops/softmax_post_process.hpp"

namespace hailort
{
namespace net_flow
{

Expected<std::shared_ptr<OpMetadata>> Yolov8OpMetadata::create(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
    const std::unordered_map<std::string, BufferMetaData> &outputs_metadata, const NmsPostProcessConfig &nms_post_process_config,
    const Yolov8PostProcessConfig &yolov8_post_process_config, const std::string &network_name)
{
    // Creating the meta data
    auto op_metadata = std::shared_ptr<Yolov8OpMetadata>(new (std::nothrow) Yolov8OpMetadata(inputs_metadata, outputs_metadata, nms_post_process_config,
        yolov8_post_process_config, network_name));
    CHECK_AS_EXPECTED(op_metadata != nullptr, HAILO_OUT_OF_HOST_MEMORY);

    auto status = op_metadata->validate_params();
    CHECK_SUCCESS_AS_EXPECTED(status);

    return std::shared_ptr<OpMetadata>(std::move(op_metadata));
}

std::string Yolov8OpMetadata::get_op_description()
{
    auto nms_config_info = get_nms_config_description();
    auto config_info = fmt::format("Op {}, Name: {}, {}, Image height: {:d}, Image width: {:d}",
                        OpMetadata::get_operation_type_str(m_type), m_name, nms_config_info, static_cast<int>(m_yolov8_config.image_height), static_cast<int>(m_yolov8_config.image_width));
    return config_info;
}

hailo_status Yolov8OpMetadata::validate_params()
{
    CHECK_SUCCESS(NmsOpMetadata::validate_params());

    // We go over the inputs metadata and check that it includes all of the regs and clss
    for (const auto &layer_names : m_yolov8_config.reg_to_cls_inputs) {
        CHECK(contains(m_inputs_metadata, layer_names.reg), HAILO_INVALID_ARGUMENT,
            "YOLOV8PostProcessOp: inputs_metadata does not contain regression layer {}", layer_names.reg);
        CHECK(contains(m_inputs_metadata, layer_names.cls), HAILO_INVALID_ARGUMENT,
            "YOLOV8PostProcessOp: inputs_metadata does not contain classification layer {}", layer_names.cls);

        const auto &reg_input_metadata = m_inputs_metadata.at(layer_names.reg);
        const auto &cls_input_metadata = m_inputs_metadata.at(layer_names.cls);

        // Checking that both outputs (reg and cls) has the same shape and format
        // NOTE: padded shape might be different because features might be different,
        // and padding is added when width*features % 8 != 0
        CHECK((reg_input_metadata.shape.height == cls_input_metadata.shape.height)
            && (reg_input_metadata.shape.width == cls_input_metadata.shape.width),
            HAILO_INVALID_ARGUMENT, "YOLOV8PostProcess: regression input {} has different shape than classification input {}",
                layer_names.reg, layer_names.cls);

        CHECK((cls_input_metadata.format.type == reg_input_metadata.format.type)
            && (cls_input_metadata.format.flags == reg_input_metadata.format.flags)
            && (cls_input_metadata.format.order == reg_input_metadata.format.order),
            HAILO_INVALID_ARGUMENT, "YOLOV8PostProcess: regression input {} has different format than classification input {}",
                layer_names.reg, layer_names.cls);

        // Checking that number of features of all outputs are multiples of 4
        CHECK(((reg_input_metadata.shape.features % 4) == 0),
            HAILO_INVALID_ARGUMENT, "YOLOV8PostProcess: regression input {} is not a multiple of 4",
                layer_names.reg);
    }
    return HAILO_SUCCESS;
}

hailo_status Yolov8OpMetadata::validate_format_info()
{
    return NmsOpMetadata::validate_format_info();
}

Expected<std::shared_ptr<Op>> YOLOV8PostProcessOp::create(std::shared_ptr<Yolov8OpMetadata> metadata)
{
    auto status = metadata->validate_format_info();
    CHECK_SUCCESS_AS_EXPECTED(status);

    auto op = std::shared_ptr<YOLOV8PostProcessOp>(new (std::nothrow) YOLOV8PostProcessOp(metadata));
    CHECK_AS_EXPECTED(op != nullptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::shared_ptr<Op>(std::move(op));
}

hailo_status YOLOV8PostProcessOp::execute(const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs)
{
    const auto &yolov8_config = m_metadata->yolov8_config();
    const auto &inputs_metadata = m_metadata->inputs_metadata();

    clear_before_frame();
    for (const auto &reg_to_cls_name : yolov8_config.reg_to_cls_inputs) {
        hailo_status status = HAILO_UNINITIALIZED;
        assert(contains(inputs, reg_to_cls_name.cls));
        assert(contains(inputs, reg_to_cls_name.reg));

        auto &input_metadata = inputs_metadata.at(reg_to_cls_name.reg);

        if (HAILO_FORMAT_TYPE_UINT8 == input_metadata.format.type) {
            status = extract_detections<float32_t, uint8_t>(reg_to_cls_name, inputs.at(reg_to_cls_name.reg),
                inputs.at(reg_to_cls_name.cls), reg_to_cls_name.stride);
        } else if (HAILO_FORMAT_TYPE_UINT16 == input_metadata.format.type) {
            status = extract_detections<float32_t, uint16_t>(reg_to_cls_name, inputs.at(reg_to_cls_name.reg),
                inputs.at(reg_to_cls_name.cls), reg_to_cls_name.stride);
        } else {
            CHECK_SUCCESS(HAILO_INVALID_ARGUMENT, "YOLO post-process received invalid input type {}", static_cast<int>(input_metadata.format.type));
        }

        CHECK_SUCCESS(status);
    }
    return hailo_nms_format(outputs.begin()->second);
}

hailo_bbox_float32_t YOLOV8PostProcessOp::decode(float32_t d1, float32_t d2, float32_t d3, float32_t d4,
    uint32_t col, uint32_t row, uint32_t stride) const
{
    const auto &image_width = m_metadata->yolov8_config().image_width;
    const auto &image_height = m_metadata->yolov8_config().image_height;

    auto x_center = (static_cast<float32_t>(col) + 0.5f) * static_cast<float32_t>(stride) / image_width;
    auto y_center = (static_cast<float32_t>(row) + 0.5f) * static_cast<float32_t>(stride) / image_height;

    // The values d1, d2, d3, d4 represents the four distances from the center (x_center, y_center) to each of the bbox boundaries
    // From d1, d2, d3, d4 we extract the values of x_min, y_min, x_max, y_max
    auto x_min = x_center - (d1 * static_cast<float32_t>(stride) / image_width);
    auto y_min = y_center - (d2 * static_cast<float32_t>(stride) / image_height);
    auto x_max = x_center + (d3 * static_cast<float32_t>(stride) / image_width);
    auto y_max = y_center + (d4 * static_cast<float32_t>(stride) / image_height);

    return hailo_bbox_float32_t{y_min, x_min, y_max, x_max, 0};
}

float32_t YOLOV8PostProcessOp::dot_product(std::vector<float> &values)
{
    // Performs dot product on the elements:
    // (A, B, C, ..., F, G) -> 0*A + 1*B + 2*C + ... + 14*F + 15*G
    float32_t sum = 0;
    float32_t counter = 0;
    for (const auto &element : values) {
        sum += (counter * element);
        counter += 1.0f;
    }

    return sum;
}

}
}
