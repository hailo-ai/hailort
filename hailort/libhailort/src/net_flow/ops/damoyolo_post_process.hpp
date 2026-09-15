/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file damoyolo_post_process.hpp
 * @brief DAMO-YOLO post process
 *
 * DAMO-YOLO's ZeroHead (GFLv2) is anchor-free with no objectness, using the same
 * distribution-focal-loss (DFL) regression + classification layout as YOLOv8. The entire
 * DFL machinery (softmax over bins, integral dot-product, per-class NMS) is inherited from
 * YOLOV8PostProcessOp; only the bbox decode differs: DAMO-YOLO references the grid-cell
 * corner (offset 0.0) whereas YOLOv8 references the cell center (offset 0.5).
 **/

#ifndef _HAILO_DAMOYOLO_POST_PROCESS_HPP_
#define _HAILO_DAMOYOLO_POST_PROCESS_HPP_

#include "net_flow/ops/yolov8_post_process.hpp"
#include "net_flow/ops_metadata/damoyolo_op_metadata.hpp"

namespace hailort
{
namespace net_flow
{

class DAMOYOLOPostProcessOp : public YOLOV8PostProcessOp
{
public:
    static Expected<std::shared_ptr<Op>> create(std::shared_ptr<DamoyoloOpMetadata> metadata);

private:
    DAMOYOLOPostProcessOp(std::shared_ptr<DamoyoloOpMetadata> metadata)
        : YOLOV8PostProcessOp(metadata)
    {}

    // DAMO-YOLO decodes distances relative to the grid-cell corner (no +0.5 offset).
    hailo_bbox_float32_t decode(float32_t d1, float32_t d2, float32_t d3, float32_t d4,
        uint32_t col, uint32_t row, uint32_t stride) const override;
};

} // namespace net_flow
} // namespace hailort

#endif // _HAILO_DAMOYOLO_POST_PROCESS_HPP_
