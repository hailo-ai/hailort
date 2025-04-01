/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file yolov8_op_metadata.hpp
 * @brief YOLOV8 op metadata
 **/
#ifndef _HAILO_YOLOV8_OP_METADATA_HPP_
#define _HAILO_YOLOV8_OP_METADATA_HPP_

#include "net_flow/ops_metadata/nms_op_metadata.hpp"

namespace hailort
{
namespace net_flow
{

struct Yolov8MatchingLayersNames
{
    // Regression layer
    std::string reg;

    // Classifications layer
    std::string cls;

    uint32_t stride;
};

struct Yolov8PostProcessConfig
{
    // The image height.
    float32_t image_height = 0;

    // The image width.
    float32_t image_width = 0;

    // A vector off two strings that represents the relations between the outputs names.
    std::vector<Yolov8MatchingLayersNames> reg_to_cls_inputs;
};

class Yolov8OpMetadata : public NmsOpMetadata
{
public:
    static Expected<std::shared_ptr<OpMetadata>> create(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
                                                        const std::unordered_map<std::string, BufferMetaData> &outputs_metadata,
                                                        const NmsPostProcessConfig &nms_post_process_config,
                                                        const Yolov8PostProcessConfig &yolov8_post_process_config,
                                                        const std::string &network_name);
    hailo_status validate_format_info() override;
    std::string get_op_description() override;
    Yolov8PostProcessConfig &yolov8_config() { return m_yolov8_config;};

protected:
    Yolov8OpMetadata(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
                       const std::unordered_map<std::string, BufferMetaData> &outputs_metadata,
                       const NmsPostProcessConfig &nms_post_process_config,
                       const Yolov8PostProcessConfig &yolov8_post_process_config,
                       const std::string &network_name,
                       const std::string &name = "YOLOV8-Post-Process")
        : NmsOpMetadata(inputs_metadata, outputs_metadata, nms_post_process_config, name, network_name, OperationType::YOLOV8)
        , m_yolov8_config(yolov8_post_process_config)
    {}
    Yolov8PostProcessConfig m_yolov8_config;
    hailo_status validate_params() override;
};

} /* namespace net_flow */
} /* namespace hailort */

#endif /* _HAILO_YOLOV8_OP_METADATA_HPP_ */