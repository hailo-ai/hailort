/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file yolov8_bbox_only_post_process.cpp
 * @brief YOLOv8 bbox only post process
 *
 **/

#include "net_flow/ops/yolov8_bbox_only_post_process.hpp"

namespace hailort
{
namespace net_flow
{

Expected<std::shared_ptr<Op>> YOLOv8BboxOnlyPostProcessOp::create(std::shared_ptr<Yolov8BboxOnlyOpMetadata> metadata)
{
    auto status = metadata->validate_format_info();
    CHECK_SUCCESS_AS_EXPECTED(status);

    auto op = std::shared_ptr<YOLOv8BboxOnlyPostProcessOp>(new (std::nothrow) YOLOv8BboxOnlyPostProcessOp(metadata));
    CHECK_AS_EXPECTED(op != nullptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::shared_ptr<Op>(std::move(op));
}

Expected<hailo_vstream_info_t> Yolov8BboxOnlyOpMetadata::get_output_vstream_info()
{
    TRY(auto vstream_info, NmsOpMetadata::get_output_vstream_info());

    vstream_info.shape = m_outputs_metadata.begin()->second.shape;
    return vstream_info;
}

hailo_status Yolov8BboxOnlyOpMetadata::validate_format_info()
{
    for (const auto& output_metadata : m_outputs_metadata) {

        CHECK(HAILO_FORMAT_TYPE_FLOAT32 == output_metadata.second.format.type, HAILO_INVALID_ARGUMENT, "The given output format type {} is not supported, "
            "should be HAILO_FORMAT_TYPE_FLOAT32", HailoRTCommon::get_format_type_str(output_metadata.second.format.type));

        CHECK(HAILO_FORMAT_ORDER_NHWC == output_metadata.second.format.order, HAILO_INVALID_ARGUMENT, "The given output format order {} is not supported, "
            "should be HAILO_FORMAT_ORDER_NHWC", HailoRTCommon::get_format_order_str(output_metadata.second.format.order));

        CHECK(!(HAILO_FORMAT_FLAGS_TRANSPOSED & output_metadata.second.format.flags), HAILO_INVALID_ARGUMENT, "Output {} is marked as transposed, which is not supported for this model.",
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
    }

    return HAILO_SUCCESS;
}

std::string Yolov8BboxOnlyOpMetadata::get_op_description()
{
    auto nms_config_info = fmt::format("Classes: {}",
                            nms_config().number_of_classes);
    auto config_info = fmt::format("Op {}, Name: {}, {}, Image height: {:d}, Image width: {:d}",
        OpMetadata::get_operation_type_str(m_type), m_name, nms_config_info,
            static_cast<int>(m_yolov8_config.image_height), static_cast<int>(m_yolov8_config.image_width));
    return config_info;
}

Expected<std::shared_ptr<OpMetadata>> Yolov8BboxOnlyOpMetadata::create(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
                                                            const std::unordered_map<std::string, BufferMetaData> &outputs_metadata,
                                                            const NmsPostProcessConfig &nms_post_process_config,
                                                            const Yolov8PostProcessConfig &yolov8_post_process_config,
                                                            const std::string &network_name)
{
    auto op_metadata = std::shared_ptr<Yolov8BboxOnlyOpMetadata>(new (std::nothrow) Yolov8BboxOnlyOpMetadata(inputs_metadata, outputs_metadata,
        nms_post_process_config, yolov8_post_process_config, network_name));
    CHECK_AS_EXPECTED(op_metadata != nullptr, HAILO_OUT_OF_HOST_MEMORY);

    auto status = op_metadata->validate_params();
    CHECK_SUCCESS_AS_EXPECTED(status);

    return std::shared_ptr<OpMetadata>(std::move(op_metadata));
}

hailo_status YOLOv8BboxOnlyPostProcessOp::execute(const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs)
{
    const auto &yolov8_config = m_metadata->yolov8_config();
    const auto &inputs_metadata = m_metadata->inputs_metadata();

    auto dst_ptr = (float32_t*)outputs.begin()->second.data();

    size_t next_bbox_output_offset = 0;
    for (const auto &reg_to_cls_name : yolov8_config.reg_to_cls_inputs) {
        hailo_status status = HAILO_UNINITIALIZED;
        assert(contains(inputs, reg_to_cls_name.cls));
        assert(contains(inputs, reg_to_cls_name.reg));

        auto &input_metadata = inputs_metadata.at(reg_to_cls_name.reg);

        if (HAILO_FORMAT_TYPE_UINT8 == input_metadata.format.type) {
            status = add_bboxes<float32_t, uint8_t>(dst_ptr, next_bbox_output_offset, reg_to_cls_name,
                inputs.at(reg_to_cls_name.reg), inputs.at(reg_to_cls_name.cls), reg_to_cls_name.stride);
        } else if (HAILO_FORMAT_TYPE_UINT16 == input_metadata.format.type) {
            status = add_bboxes<float32_t, uint16_t>(dst_ptr, next_bbox_output_offset, reg_to_cls_name,
                inputs.at(reg_to_cls_name.reg), inputs.at(reg_to_cls_name.cls), reg_to_cls_name.stride);
        } else {
            CHECK_SUCCESS(HAILO_INVALID_ARGUMENT, "YOLOV8 bbox only post-process received invalid input type {}", static_cast<int>(input_metadata.format.type));
        }

        CHECK_SUCCESS(status);
    }
    return HAILO_SUCCESS;
}

} // namespace net_flow
} // namespace hailort