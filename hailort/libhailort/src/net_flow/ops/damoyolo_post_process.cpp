/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file damoyolo_post_process.cpp
 * @brief DAMO-YOLO post process
 *
 * Reuses YOLOv8's DFL decoding machinery; only the grid-cell reference offset differs
 * (DAMO-YOLO: 0.0 / cell corner, YOLOv8: 0.5 / cell center).
 **/

#include "net_flow/ops/damoyolo_post_process.hpp"

namespace hailort
{
namespace net_flow
{

Expected<std::shared_ptr<OpMetadata>> DamoyoloOpMetadata::create(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
    const std::unordered_map<std::string, BufferMetaData> &outputs_metadata, const NmsPostProcessConfig &nms_post_process_config,
    const Yolov8PostProcessConfig &damoyolo_post_process_config, const std::string &network_name)
{
    auto op_metadata = std::shared_ptr<DamoyoloOpMetadata>(new (std::nothrow) DamoyoloOpMetadata(inputs_metadata, outputs_metadata,
        nms_post_process_config, damoyolo_post_process_config, network_name));
    CHECK_AS_EXPECTED(op_metadata != nullptr, HAILO_OUT_OF_HOST_MEMORY);

    auto status = op_metadata->validate_params();
    CHECK_SUCCESS_AS_EXPECTED(status);

    return std::shared_ptr<OpMetadata>(std::move(op_metadata));
}

Expected<std::shared_ptr<Op>> DAMOYOLOPostProcessOp::create(std::shared_ptr<DamoyoloOpMetadata> metadata)
{
    auto status = metadata->validate_format_info();
    CHECK_SUCCESS_AS_EXPECTED(status);

    auto op = std::shared_ptr<DAMOYOLOPostProcessOp>(new (std::nothrow) DAMOYOLOPostProcessOp(metadata));
    CHECK_AS_EXPECTED(op != nullptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::shared_ptr<Op>(std::move(op));
}

hailo_bbox_float32_t DAMOYOLOPostProcessOp::decode(float32_t d1, float32_t d2, float32_t d3, float32_t d4,
    uint32_t col, uint32_t row, uint32_t stride) const
{
    const auto &image_width = m_metadata->yolov8_config().image_width;
    const auto &image_height = m_metadata->yolov8_config().image_height;

    // DAMO-YOLO references the grid-cell corner (no +0.5 offset), unlike YOLOv8 which uses the cell center.
    auto x_center = static_cast<float32_t>(col) * static_cast<float32_t>(stride) / image_width;
    auto y_center = static_cast<float32_t>(row) * static_cast<float32_t>(stride) / image_height;

    // d1, d2, d3, d4 are the distances from the reference point to the left, top, right and bottom
    // bbox boundaries (in grid units); scale by stride and normalize by image size.
    auto x_min = x_center - (d1 * static_cast<float32_t>(stride) / image_width);
    auto y_min = y_center - (d2 * static_cast<float32_t>(stride) / image_height);
    auto x_max = x_center + (d3 * static_cast<float32_t>(stride) / image_width);
    auto y_max = y_center + (d4 * static_cast<float32_t>(stride) / image_height);

    return hailo_bbox_float32_t{y_min, x_min, y_max, x_max, 0};
}

}
}
