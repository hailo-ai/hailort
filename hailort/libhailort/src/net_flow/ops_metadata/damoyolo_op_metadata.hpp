/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file damoyolo_op_metadata.hpp
 * @brief DAMO-YOLO op metadata
 *
 * DAMO-YOLO shares YOLOv8's DFL-based decoding schema (per-stride regression + classification
 * layers, distribution-focal-loss integral). The only runtime difference is the grid-cell
 * reference offset used during box decoding (DAMO-YOLO uses 0.0 instead of YOLOv8's 0.5),
 * so the metadata reuses Yolov8PostProcessConfig and only overrides the operation type/name.
 **/
#ifndef _HAILO_DAMOYOLO_OP_METADATA_HPP_
#define _HAILO_DAMOYOLO_OP_METADATA_HPP_

#include "net_flow/ops_metadata/yolov8_op_metadata.hpp"

namespace hailort
{
namespace net_flow
{

class DamoyoloOpMetadata : public Yolov8OpMetadata
{
public:
    static Expected<std::shared_ptr<OpMetadata>> create(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
                                                        const std::unordered_map<std::string, BufferMetaData> &outputs_metadata,
                                                        const NmsPostProcessConfig &nms_post_process_config,
                                                        const Yolov8PostProcessConfig &damoyolo_post_process_config,
                                                        const std::string &network_name);

private:
    DamoyoloOpMetadata(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
                       const std::unordered_map<std::string, BufferMetaData> &outputs_metadata,
                       const NmsPostProcessConfig &nms_post_process_config,
                       const Yolov8PostProcessConfig &damoyolo_post_process_config,
                       const std::string &network_name)
        : Yolov8OpMetadata(inputs_metadata, outputs_metadata, nms_post_process_config, damoyolo_post_process_config,
            network_name, "DAMOYOLO-Post-Process", OperationType::DAMOYOLO)
        {}
};

} /* namespace net_flow */
} /* namespace hailort */

#endif /* _HAILO_DAMOYOLO_OP_METADATA_HPP_ */
