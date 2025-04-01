/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file yolov5_post_process.cpp
 * @brief YOLOv5 post process
 *
 * https://learnopencv.com/object-detection-using-yolov5-and-opencv-dnn-in-c-and-python :
 * The headline '4.3.5 POST-PROCESSING YOLOv5 Prediction Output' contains explanations on the YOLOv5 post-processing.
 **/

#include "net_flow/ops/yolov5_post_process.hpp"

namespace hailort
{
namespace net_flow
{

Expected<std::shared_ptr<OpMetadata>> Yolov5OpMetadata::create(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
                                                            const std::unordered_map<std::string, BufferMetaData> &outputs_metadata,
                                                            const NmsPostProcessConfig &nms_post_process_config,
                                                            const YoloPostProcessConfig &yolov5_post_process_config,
                                                            const std::string &network_name)
{
    auto op_metadata = std::shared_ptr<Yolov5OpMetadata>(new (std::nothrow) Yolov5OpMetadata(inputs_metadata, outputs_metadata,
        nms_post_process_config, "YOLOv5-Post-Process", network_name, yolov5_post_process_config, OperationType::YOLOV5));
    CHECK_AS_EXPECTED(op_metadata != nullptr, HAILO_OUT_OF_HOST_MEMORY);

    auto status = op_metadata->validate_params();
    CHECK_SUCCESS_AS_EXPECTED(status);

    return std::shared_ptr<OpMetadata>(std::move(op_metadata));
}

hailo_status Yolov5OpMetadata::validate_params()
{
    return(NmsOpMetadata::validate_params());
}

hailo_status Yolov5OpMetadata::validate_format_info()
{
    return NmsOpMetadata::validate_format_info();
}

Expected<hailo_vstream_info_t> Yolov5OpMetadata::get_output_vstream_info()
{
    return NmsOpMetadata::get_output_vstream_info();
}

std::string Yolov5OpMetadata::get_op_description()
{
    auto nms_config_info = get_nms_config_description();
    auto config_info = fmt::format("Op {}, Name: {}, {}, Image height: {:d}, Image width: {:d}",
        OpMetadata::get_operation_type_str(m_type), m_name, nms_config_info, static_cast<int>(m_yolov5_config.image_height), static_cast<int>(m_yolov5_config.image_width));
    return config_info;
}

Expected<std::shared_ptr<Op>> YOLOv5PostProcessOp::create(std::shared_ptr<Yolov5OpMetadata> metadata)
{
    auto status = metadata->validate_format_info();
    CHECK_SUCCESS_AS_EXPECTED(status);

    auto op = std::shared_ptr<YOLOv5PostProcessOp>(new (std::nothrow) YOLOv5PostProcessOp(metadata));
    CHECK_AS_EXPECTED(op != nullptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::shared_ptr<Op>(std::move(op));
}

hailo_status YOLOv5PostProcessOp::execute(const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs)
{
    const auto &inputs_metadata = m_metadata->inputs_metadata();
    const auto &yolo_config = m_metadata->yolov5_config();
    CHECK(inputs.size() == yolo_config.anchors.size(), HAILO_INVALID_ARGUMENT,
        "Anchors vector count must be equal to data vector count. Anchors size is {}, data size is {}",
            yolo_config.anchors.size(), inputs.size());

    clear_before_frame();
    for (const auto &name_to_input : inputs) {
        hailo_status status = HAILO_UNINITIALIZED;
        auto &name = name_to_input.first;
        assert(contains(inputs_metadata, name));
        auto &input_metadata = inputs_metadata.at(name);
        assert(contains(yolo_config.anchors, name));
        if (input_metadata.format.type == HAILO_FORMAT_TYPE_UINT8) {
            status = extract_detections<float32_t, uint8_t>(name_to_input.second, input_metadata.quant_info, input_metadata.shape,
                input_metadata.padded_shape, yolo_config.anchors.at(name));
        } else if (input_metadata.format.type == HAILO_FORMAT_TYPE_UINT16) {
            status = extract_detections<float32_t, uint16_t>(name_to_input.second, input_metadata.quant_info, input_metadata.shape,
                input_metadata.padded_shape, yolo_config.anchors.at(name));
        } else {
            CHECK_SUCCESS(HAILO_INVALID_ARGUMENT, "YOLO post-process received invalid input type {}", static_cast<int>(input_metadata.format.type));
        }
        CHECK_SUCCESS(status);
    }

    // TODO: Add support for TF_FORMAT_ORDER
    return hailo_nms_format(outputs.begin()->second);
}

hailo_bbox_float32_t YOLOv5PostProcessOp::decode(float32_t tx, float32_t ty, float32_t tw, float32_t th,
    int wa, int ha, uint32_t col, uint32_t row, uint32_t w_stride, uint32_t h_stride) const
{
    // Source for the calculations - https://github.com/ultralytics/yolov5/blob/HEAD/models/yolo.py
    // Explanations for the calculations - https://github.com/ultralytics/yolov5/issues/471
    auto w = pow(2.0f * tw, 2.0f) * static_cast<float32_t>(wa) / m_metadata->yolov5_config().image_width;
    auto h = pow(2.0f * th, 2.0f) * static_cast<float32_t>(ha) / m_metadata->yolov5_config().image_height;
    auto x_center = (tx * 2.0f - 0.5f + static_cast<float32_t>(col)) / static_cast<float32_t>(w_stride);
    auto y_center = (ty * 2.0f - 0.5f + static_cast<float32_t>(row)) / static_cast<float32_t>(h_stride);
    auto x_min = (x_center - (w / 2.0f));
    auto y_min = (y_center - (h / 2.0f));
    return hailo_bbox_float32_t{y_min, x_min, (y_min+h), (x_min+w), 0};
}

uint32_t YOLOv5PostProcessOp::get_entry_size()
{
    return (CLASSES_START_INDEX + m_metadata->nms_config().number_of_classes);
}

size_t YOLOv5PostProcessOp::get_num_of_anchors(const std::vector<int> &layer_anchors)
{
    // Each layer anchors vector is structured as {w,h} pairs.
    // For example, if we have a vector of size 6 (default YOLOv5 vector) then we have 3 anchors for this layer.
    assert(layer_anchors.size() % 2 == 0);
    size_t num_of_anchors = (layer_anchors.size() / 2);
    return num_of_anchors;
}

} // namespace net_flow
} // namespace hailort

