/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file yolov5_seg_op_metadata.hpp
 * @brief YOLOv5 Instance Segmentation Post-Process op metadata
 **/

#ifndef _HAILO_YOLOV5_SEG_OP_METADATA_HPP_
#define _HAILO_YOLOV5_SEG_OP_METADATA_HPP_

#include "hailo/hailort.h"
#include "net_flow/ops_metadata/yolov5_op_metadata.hpp"

namespace hailort
{
namespace net_flow
{

struct YoloV5SegPostProcessConfig
{
    // User given mask threshold. A pixel will consider part of the mask if it's value is higher then the mask_threshold.
    double mask_threshold;
    uint32_t max_accumulated_mask_size;
    std::string proto_layer_name;
};

class Yolov5SegOpMetadata : public Yolov5OpMetadata
{
public:
    static Expected<std::shared_ptr<OpMetadata>> create(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
                                                        const std::unordered_map<std::string, BufferMetaData> &outputs_metadata,
                                                        const NmsPostProcessConfig &nms_post_process_config,
                                                        const YoloPostProcessConfig &yolov5_config,
                                                        const YoloV5SegPostProcessConfig &yolov5_seg_config,
                                                        const std::string &network_name);
    hailo_status validate_format_info() override;
    std::string get_op_description() override;
    YoloV5SegPostProcessConfig &yolov5seg_config() { return m_yolo_seg_config;};
    virtual Expected<hailo_vstream_info_t> get_output_vstream_info() override;
    hailo_status validate_params() override;

private:
    Yolov5SegOpMetadata(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
                       const std::unordered_map<std::string, BufferMetaData> &outputs_metadata,
                       const NmsPostProcessConfig &nms_post_process_config,
                       const YoloPostProcessConfig &yolo_config,
                       const YoloV5SegPostProcessConfig &yolo_seg_config,
                       const std::string &network_name)
        : Yolov5OpMetadata(inputs_metadata, outputs_metadata, nms_post_process_config, "YOLOv5Seg-Post-Process",
            network_name, yolo_config, OperationType::YOLOV5SEG),
        m_yolo_seg_config(yolo_seg_config)
    {}

    YoloV5SegPostProcessConfig m_yolo_seg_config;
};

} /* namespace hailort */
} /* namespace net_flow */

#endif /* _HAILO_YOLOV5_SEG_POST_PROCESS_HPP_ */
