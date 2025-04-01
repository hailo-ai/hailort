/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file yolov5_op_metadata.hpp
 * @brief YOLOV5 op metadata
 **/

#ifndef _HAILO_YOLO_OP_METADATA_HPP_
#define _HAILO_YOLO_OP_METADATA_HPP_

#include "net_flow/ops_metadata/op_metadata.hpp"

namespace hailort
{
namespace net_flow
{

struct YoloPostProcessConfig
{
    // The image height.
    float32_t image_height = 0;

    // The image width.
    float32_t image_width = 0;

    // A vector of anchors, each element in the vector represents the anchors for a specific layer
    // Each layer anchors vector is structured as {w,h} pairs.
    std::map<std::string, std::vector<int>> anchors;
};

class Yolov5OpMetadata : public NmsOpMetadata
{
public:
    static Expected<std::shared_ptr<OpMetadata>> create(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
                                                        const std::unordered_map<std::string, BufferMetaData> &outputs_metadata,
                                                        const NmsPostProcessConfig &nms_post_process_config,
                                                        const YoloPostProcessConfig &yolov5_post_process_config,
                                                        const std::string &network_name);
    std::string get_op_description() override;
    hailo_status validate_format_info() override;
    YoloPostProcessConfig &yolov5_config() { return m_yolov5_config;};
    virtual Expected<hailo_vstream_info_t> get_output_vstream_info() override;

protected:
    Yolov5OpMetadata(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
                       const std::unordered_map<std::string, BufferMetaData> &outputs_metadata,
                       const NmsPostProcessConfig &nms_post_process_config,
                       const std::string &name,
                       const std::string &network_name,
                       const YoloPostProcessConfig &yolov5_post_process_config,
                       const OperationType op_type)
        : NmsOpMetadata(inputs_metadata, outputs_metadata, nms_post_process_config, name, network_name, op_type)
        , m_yolov5_config(yolov5_post_process_config)
    {}

    hailo_status validate_params() override;

    YoloPostProcessConfig m_yolov5_config;

};

} /* namespace net_flow */
} /* namespace hailort */

#endif /* _HAILO_YOLOV5_OP_METADATA_HPP_ */



