/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file yolox_op_metadata.hpp
 * @brief YOLOX op metadata
 **/
#ifndef _HAILO_YOLOX_OP_METADATA_HPP_
#define _HAILO_YOLOX_OP_METADATA_HPP_

#include "net_flow/ops_metadata/op_metadata.hpp"

namespace hailort
{
namespace net_flow
{

struct YoloxMatchingLayersNames
{
    // Regression layer
    std::string reg;

    // Objectness layer
    std::string obj;

    // Classifications layer
    std::string cls;
};

struct YoloxPostProcessConfig
{
    // The image height.
    float32_t image_height = 0;

    // The image width.
    float32_t image_width = 0;

    // A vector off three strings that represents the relations between the outputs names.
    std::vector<YoloxMatchingLayersNames> input_names;
};

class YoloxOpMetadata : public NmsOpMetadata
{
public:
    static Expected<std::shared_ptr<OpMetadata>> create(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
                                                        const std::unordered_map<std::string, BufferMetaData> &outputs_metadata,
                                                        const NmsPostProcessConfig &nms_post_process_config,
                                                        const YoloxPostProcessConfig &yolox_post_process_config,
                                                        const std::string &network_name);
    hailo_status validate_format_info() override;
    std::string get_op_description() override;
    YoloxPostProcessConfig &yolox_config() { return m_yolox_config;};

private:
    YoloxPostProcessConfig m_yolox_config;
    YoloxOpMetadata(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
                       const std::unordered_map<std::string, BufferMetaData> &outputs_metadata,
                       const NmsPostProcessConfig &nms_post_process_config,
                       const YoloxPostProcessConfig &yolox_post_process_config,
                       const std::string &network_name)
        : NmsOpMetadata(inputs_metadata, outputs_metadata, nms_post_process_config, "YOLOX-Post-Process", network_name, OperationType::YOLOX)
        , m_yolox_config(yolox_post_process_config)
    {}

    hailo_status validate_params() override;
};

} /* namespace hailort */
} /* namespace net_flow */

#endif /* _HAILO_YOLOX_SEG_OP_METADATA_HPP_ */