/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file network_group_metadata.hpp
 * @brief Contains all relevant information about a network group from the hef.
 **/

#ifndef _HAILO_NETWORK_GROUP_METADATA_HPP_
#define _HAILO_NETWORK_GROUP_METADATA_HPP_

#include "layer_info.hpp"
#include "context_switch/context_switch_actions.hpp"

namespace hailort
{

constexpr const uint32_t PARTIAL_CLUSTERS_LAYOUT_IGNORE = static_cast<uint32_t>(-1);

struct SupportedFeatures {
    bool padded_ddr_buffers = false;
    bool multi_network_support = false;
    bool multi_context = false;
    bool preliminary_run_asap = false;
    bool hailo_net_flow = false;
};

// For each config_stream_index we store vector of all ccw write length. The vector is used to build the config buffer.g
using ConfigBufferInfoMap = std::unordered_map<uint8_t, std::vector<uint32_t>>;

class PreliminaryContextMetadata final {
public:
    PreliminaryContextMetadata() = default; // TODO HRT-8478: remove
    PreliminaryContextMetadata(std::vector<ContextSwitchOperation> &&operations,
        ConfigBufferInfoMap&& config_buffers_info);
    const std::vector<ContextSwitchOperation> &get_operations() const;
    const ConfigBufferInfoMap &config_buffers_info() const;

private:
    std::vector<ContextSwitchOperation> m_operations;
    ConfigBufferInfoMap m_config_buffers_info;
};

class ContextMetadata final {
public:
    explicit ContextMetadata(std::vector<ContextSwitchOperation> &&operations,
        ConfigBufferInfoMap&& config_buffers_info);

    const std::vector<ContextSwitchOperation> &get_operations() const;
    const ConfigBufferInfoMap &config_buffers_info() const;

    void add_boundary_layer(const LayerInfo &layer_info);
    void add_inter_context_layer(const LayerInfo &layer_info);
    void add_ddr_layer(const LayerInfo &layer_info);

    const std::vector<LayerInfo> &get_boundary_input_layers() const;
    const std::vector<LayerInfo> &get_boundary_output_layers() const;
    const std::vector<LayerInfo> &get_inter_context_input_layers() const;
    const std::vector<LayerInfo> &get_inter_context_output_layers() const;
    const std::vector<LayerInfo> &get_ddr_input_layers() const;
    const std::vector<LayerInfo> &get_ddr_output_layers() const;

private:
    std::vector<ContextSwitchOperation> m_operations;
    ConfigBufferInfoMap m_config_buffers_info;

    std::vector<LayerInfo> m_boundary_input_layers;
    std::vector<LayerInfo> m_boundary_output_layers;
    std::vector<LayerInfo> m_inter_context_input_layers;
    std::vector<LayerInfo> m_inter_context_output_layers;
    std::vector<LayerInfo> m_ddr_input_layers;
    std::vector<LayerInfo> m_ddr_output_layers;
};

struct ConfigChannelInfo {
    uint8_t engine_index;
};

class NetworkGroupMetadata final {
public:
    NetworkGroupMetadata() = default; // TODO HRT-8478: remove
    NetworkGroupMetadata(const std::string &network_group_name,
        PreliminaryContextMetadata &&preliminary_context,
        std::vector<ContextMetadata> &&dynamic_contexts,
        std::vector<ConfigChannelInfo> &&config_channels_info,
        std::vector<std::string> &&sorted_output_names,
        SupportedFeatures &supported_features,
        const std::vector<std::string> &sorted_network_names);

    std::vector<LayerInfo> get_input_layer_infos() const;
    std::vector<LayerInfo> get_output_layer_infos() const;
    std::vector<LayerInfo> get_all_layer_infos() const;

    Expected<std::vector<LayerInfo>> get_input_layer_infos(const std::string &network_name) const;
    Expected<std::vector<LayerInfo>> get_output_layer_infos(const std::string &network_name) const;
    Expected<std::vector<LayerInfo>> get_all_layer_infos(const std::string &network_name) const;
    Expected<LayerInfo> get_layer_info_by_stream_name(const std::string &stream_name) const;

    const PreliminaryContextMetadata &preliminary_context() const;
    const std::vector<ContextMetadata> &dynamic_contexts() const;

    const std::vector<ConfigChannelInfo> &config_channels_info() const;

    Expected<std::vector<hailo_stream_info_t>> get_input_stream_infos(const std::string &network_name = "") const;
    Expected<std::vector<hailo_stream_info_t>> get_output_stream_infos(const std::string &network_name = "") const;
    Expected<std::vector<hailo_stream_info_t>> get_all_stream_infos(const std::string &network_name = "") const;

    Expected<std::vector<hailo_vstream_info_t>> get_input_vstream_infos(const std::string &network_name = "") const;
    Expected<std::vector<hailo_vstream_info_t>> get_output_vstream_infos(const std::string &network_name = "") const;
    Expected<std::vector<hailo_vstream_info_t>> get_all_vstream_infos(const std::string &network_name = "") const;

    Expected<std::vector<std::string>> get_vstream_names_from_stream_name(const std::string &stream_name) const;
    Expected<std::vector<std::string>> get_stream_names_from_vstream_name(const std::string &vstream_name) const;

    Expected<std::vector<hailo_network_info_t>> get_network_infos() const;

    const std::string &network_group_name() const
    {
        return m_network_group_name;
    }

    const std::string default_network_name() const
    {
        return HailoRTDefaults::get_network_name(m_network_group_name);
    }

    const std::vector<std::string> get_sorted_output_names() const
    {
        return m_sorted_output_names;
    }

    const SupportedFeatures &supported_features() const
    {
        return m_supported_features;
    }

    const std::vector<std::string> &get_network_names() const
    {
        return m_sorted_network_names;
    }

    void add_output_vstream_info(const hailo_vstream_info_t &output_vstream_info) {
        m_output_vstreams_infos.push_back(output_vstream_info);
    }

private:
    std::vector<hailo_stream_info_t> convert_layer_infos_to_stream_infos(const std::vector<LayerInfo> &layer_infos) const;
    std::vector<hailo_vstream_info_t> convert_layer_infos_to_vstream_infos(const std::vector<LayerInfo> &layer_infos) const;

    PreliminaryContextMetadata m_preliminary_context;
    std::vector<ContextMetadata> m_dynamic_contexts;
    std::vector<ConfigChannelInfo> m_config_channels_info;
    std::string m_network_group_name;
    std::vector<std::string> m_sorted_output_names;
    SupportedFeatures m_supported_features;
    std::vector<std::string> m_sorted_network_names;
    // TODO: remove this from here! NetworkGroupMetadata should be CoreOpMetadata and contain no net_flow information! (HRT-8639)
    // To add insult to injury, this is being constructed lazyly by add_output_layer_info
    std::vector<hailo_vstream_info_t> m_output_vstreams_infos; // Valid only in case of post process
};


class NetworkGroupMetadataPerArch final
{
public:
    NetworkGroupMetadataPerArch() = default;

    Expected<NetworkGroupMetadata> get_metadata(uint32_t partial_clusters_layout_bitmap);
    void add_metadata(const NetworkGroupMetadata &metadata, uint32_t partial_clusters_layout_bitmap);

private:
    std::map<uint32_t, NetworkGroupMetadata> m_metadata_per_arch;
};

} /* namespace hailort */

#endif /* _HAILO_NETWORK_GROUP_METADATA_HPP_ */
