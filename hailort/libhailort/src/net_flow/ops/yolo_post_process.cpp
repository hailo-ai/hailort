/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file yolo_post_process.cpp
 * @brief YOLO post process
 *
 * https://learnopencv.com/object-detection-using-yolov5-and-opencv-dnn-in-c-and-python :
 * The headline '4.3.5 POST-PROCESSING YOLOv5 Prediction Output' contains explanations on the YOLOv5 post-processing.
 **/

#include "net_flow/ops/yolo_post_process.hpp"

namespace hailort
{
namespace net_flow
{

hailo_status YOLOv5PostProcessOp::validate_metadata()
{
    auto status = NmsPostProcessOp::validate_metadata();
    if (HAILO_SUCCESS != status) {
        return status;
    }

    return HAILO_SUCCESS;
}

//TODO- move to a dedicated module and maybe convert all yolo function to yolov5, HRT-10858
Expected<std::shared_ptr<Op>> YOLOv5PostProcessOp::create(const std::map<std::string, BufferMetaData> &inputs_metadata,
                                                          const std::map<std::string, BufferMetaData> &outputs_metadata,
                                                          const NmsPostProcessConfig &nms_post_process_config,
                                                          const YoloPostProcessConfig &yolo_post_process_config)
{
    for (auto &name_to_inputs_metadata : inputs_metadata) {
        CHECK_AS_EXPECTED(name_to_inputs_metadata.second.format.order == HAILO_FORMAT_ORDER_NHCW, HAILO_INVALID_ARGUMENT,
            "YOLOv5PostProcessOp: Unexpected input format {}", name_to_inputs_metadata.second.format.order);
    }
    auto op = std::shared_ptr<YOLOv5PostProcessOp>(new (std::nothrow) YOLOv5PostProcessOp(inputs_metadata, outputs_metadata, nms_post_process_config, yolo_post_process_config));
    CHECK_AS_EXPECTED(op != nullptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::shared_ptr<Op>(std::move(op));
}

hailo_status YOLOPostProcessOp::execute(const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs)
{
    CHECK(inputs.size() == m_yolo_config.anchors.size(), HAILO_INVALID_ARGUMENT,
        "Anchors vector count must be equal to data vector count. Anchors size is {}, data size is {}",
            m_yolo_config.anchors.size(), inputs.size());

    std::vector<DetectionBbox> detections;
    std::vector<uint32_t> classes_detections_count(m_nms_config.number_of_classes, 0);
    detections.reserve(m_nms_config.max_proposals_per_class * m_nms_config.number_of_classes);
    for (const auto &name_to_input : inputs) {
        hailo_status status;
        auto &name = name_to_input.first;
        auto &input_metadata = m_inputs_metadata[name];
        if (input_metadata.format.type == HAILO_FORMAT_TYPE_UINT8) {
            status = extract_detections<float32_t, uint8_t>(name_to_input.second, input_metadata.quant_info, input_metadata.shape,
                input_metadata.padded_shape, m_yolo_config.anchors[name], detections, classes_detections_count);
        } else if (input_metadata.format.type == HAILO_FORMAT_TYPE_UINT16) {
            status = extract_detections<float32_t, uint16_t>(name_to_input.second, input_metadata.quant_info, input_metadata.shape,
                input_metadata.padded_shape, m_yolo_config.anchors[name], detections, classes_detections_count);
        } else {
            CHECK_SUCCESS(HAILO_INVALID_ARGUMENT, "YOLO post-process received invalid input type {}", input_metadata.format.type);
        }
        CHECK_SUCCESS(status);
    }

    // TODO: Add support for TF_FORMAT_ORDER
    return hailo_nms_format(std::move(detections), outputs.begin()->second, classes_detections_count);
}

std::string YOLOPostProcessOp::get_op_description()
{
    auto nms_config_info = get_nms_config_description();
    auto config_info = fmt::format("Name: {}, {}, Image height: {:.2f}, Image width: {:.2f}", 
                        m_name, nms_config_info, m_yolo_config.image_height, m_yolo_config.image_width);
    return config_info;
}

hailo_bbox_float32_t YOLOv5PostProcessOp::decode(float32_t tx, float32_t ty, float32_t tw, float32_t th,
    int wa, int ha, uint32_t col, uint32_t row, uint32_t w_stride, uint32_t h_stride) const
{
    auto w = pow(2.0f * tw, 2.0f) * static_cast<float32_t>(wa) / m_yolo_config.image_width;
    auto h = pow(2.0f * th, 2.0f) * static_cast<float32_t>(ha) / m_yolo_config.image_height;
    auto x_center = (tx * 2.0f - 0.5f + static_cast<float32_t>(col)) / static_cast<float32_t>(w_stride);
    auto y_center = (ty * 2.0f - 0.5f + static_cast<float32_t>(row)) / static_cast<float32_t>(h_stride);
    auto x_min = (x_center - (w / 2.0f));
    auto y_min = (y_center - (h / 2.0f));
    return hailo_bbox_float32_t{y_min, x_min, (y_min+h), (x_min+w), 0};
}

} // namespace net_flow
} // namespace hailort

