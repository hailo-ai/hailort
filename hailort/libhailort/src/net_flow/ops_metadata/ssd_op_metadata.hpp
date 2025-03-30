/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file ssd_op_metadata.hpp
 * @brief SSD op metadata
 *
 **/

#ifndef _HAILO_SSD_OP_METADATA_HPP_
#define _HAILO_SSD_OP_METADATA_HPP_

#include "net_flow/ops_metadata/op_metadata.hpp"

namespace hailort
{
namespace net_flow
{

struct SSDPostProcessConfig
{
    // The image height.
    float32_t image_height = 0;

    // The image width.
    float32_t image_width = 0;

    uint32_t centers_scale_factor = 0;

    uint32_t bbox_dimensions_scale_factor = 0;

    uint32_t ty_index = 0;
    uint32_t tx_index = 0;
    uint32_t th_index = 0;
    uint32_t tw_index = 0;

    std::map<std::string, std::string> reg_to_cls_inputs;

    // A vector of anchors, each element in the vector represents the anchors for a specific layer
    // Each layer anchors vector is structured as {w,h} pairs.
    // Each anchor is mapped by 2 keys:
    //     1. reg input
    //     2. cls input
    std::map<std::string, std::vector<float32_t>> anchors;

    // Indicates whether boxes should be normalized (and clipped)
    bool normalize_boxes = false;
};

class SSDOpMetadata : public NmsOpMetadata
{
public:
    static Expected<std::shared_ptr<OpMetadata>> create(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
                                                        const std::unordered_map<std::string, BufferMetaData> &outputs_metadata,
                                                        const NmsPostProcessConfig &nms_post_process_config,
                                                        const SSDPostProcessConfig &ssd_post_process_config,
                                                        const std::string &network_name);
    std::string get_op_description() override;
    hailo_status validate_format_info() override;
    SSDPostProcessConfig &ssd_config() { return m_ssd_config;};

private:
    SSDPostProcessConfig m_ssd_config;
    SSDOpMetadata(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
                       const std::unordered_map<std::string, BufferMetaData> &outputs_metadata,
                       const NmsPostProcessConfig &nms_post_process_config,
                       const SSDPostProcessConfig &ssd_post_process_config,
                       const std::string &network_name)
        : NmsOpMetadata(inputs_metadata, outputs_metadata, nms_post_process_config, "SSD-Post-Process", network_name, OperationType::SSD)
        , m_ssd_config(ssd_post_process_config)
    {}

    hailo_status validate_params() override;
};

} /* namespace net_flow */
} /* namespace hailort */

#endif /* _HAILO_SSD_OP_METADATA_HPP_ */