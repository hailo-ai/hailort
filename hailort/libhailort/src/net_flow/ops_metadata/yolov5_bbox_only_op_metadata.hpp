/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file yolov5_bbox_only_op_metadata.hpp
 * @brief YOLOv5 Bbox Only Post-Process op metadata
 **/

#ifndef _HAILO_YOLOV5_BBOX_ONLY_OP_METADATA_HPP_
#define _HAILO_YOLOV5_BBOX_ONLY_OP_METADATA_HPP_

#include "hailo/hailort.h"
#include "net_flow/ops_metadata/yolov5_op_metadata.hpp"

namespace hailort
{
namespace net_flow
{

class Yolov5BboxOnlyOpMetadata : public Yolov5OpMetadata
{
public:
    static Expected<std::shared_ptr<OpMetadata>> create(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
                                                        const std::unordered_map<std::string, BufferMetaData> &outputs_metadata,
                                                        const NmsPostProcessConfig &nms_post_process_config,
                                                        const YoloPostProcessConfig &yolov5_config,
                                                        const std::string &network_name);
    hailo_status validate_format_info() override;
    std::string get_op_description() override;
    virtual Expected<hailo_vstream_info_t> get_output_vstream_info() override;

private:
    Yolov5BboxOnlyOpMetadata(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
                       const std::unordered_map<std::string, BufferMetaData> &outputs_metadata,
                       const NmsPostProcessConfig &nms_post_process_config,
                       const YoloPostProcessConfig &yolo_config,
                       const std::string &network_name)
        : Yolov5OpMetadata(inputs_metadata, outputs_metadata, nms_post_process_config, "YOLOv5Bbox-Only-Post-Process",
            network_name, yolo_config, OperationType::YOLOV5)
    {}

};

} /* namespace hailort */
} /* namespace net_flow */

#endif /* _HAILO_YOLOV5_BBOX_ONLY_OP_METADATA_HPP_ */
