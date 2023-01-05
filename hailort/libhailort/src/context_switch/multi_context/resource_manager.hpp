/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file resource_manager.hpp
 * @brief Manager for vdma-config network group resources, for a specific physical device
 *
 * ResourceManager is used on 2 possible flows with the following dependencies:
 *
 * !-Working with physical device-!
 * VdmaDevice (either PcieDevice or CoreDevice)
 * |--vector of VdmaConfigNetworkGroup
 *              |--ResourceManager <only one>
 *                 |--reference to physical device
 *
 * !-Working with virtual device-!
 * VDevice
 * |--vector of VdmaDevice (either PcieDevice or CoreDevice)
 * |--vector of VDeviceNetworkGroup
 *              |-- vector of VdmaConfigNetworkGroup <one per phys device>
 *                            |--ResourceManager <only one>
 *                               |--reference to physical device
  **/

#ifndef _HAILO_CONTEXT_SWITCH_RESOURCE_MANAGER_HPP_
#define _HAILO_CONTEXT_SWITCH_RESOURCE_MANAGER_HPP_

#include "hailo/hailort.h"
#include "inter_context_buffer.hpp"
#include "ddr_channels_pair.hpp"
#include "config_buffer.hpp"
#include "vdma_channel.hpp"
#include "control_protocol.hpp"
#include "pcie_device.hpp"
#include "channel_allocator.hpp"
#include "context_switch/context_switch_buffer_builder.hpp"


namespace hailort
{

#define DEFAULT_ACTUAL_BATCH_SIZE (1)


struct BoundaryEdgeLayer {
    LayerInfo layer_info;
    vdma::ChannelId channel_id;
    CONTROL_PROTOCOL__host_buffer_info_t buffer_info;
};

struct InterContextEdgeLayer {
    LayerInfo layer_info;
    vdma::ChannelId channel_id;
    CONTROL_PROTOCOL__host_buffer_info_t buffer_info;
};

struct DdrChannelEdgeLayer {
    LayerInfo layer_info;
    vdma::ChannelId channel_id;
    CONTROL_PROTOCOL__host_buffer_info_t buffer_info;
};

class ContextResources final {
public:
    static Expected<ContextResources> create(HailoRTDriver &driver, CONTROL_PROTOCOL__context_switch_context_type_t context_type,
        const std::vector<vdma::ChannelId> &config_channels_ids, const ConfigBufferInfoMap &config_buffer_infos);

    const std::vector<CONTROL_PROTOCOL__context_switch_context_info_single_control_t> &get_controls() const;
    ContextSwitchBufferBuilder &builder();

    void add_edge_layer(const BoundaryEdgeLayer &edge_layer)
    {
        m_boundary_layers.emplace_back(std::move(edge_layer));
    }

    void add_edge_layer(const InterContextEdgeLayer &edge_layer)
    {
        m_inter_context_layers.emplace_back(std::move(edge_layer));
    }

    void add_edge_layer(const DdrChannelEdgeLayer &edge_layer)
    {
        m_ddr_channel_layers.emplace_back(std::move(edge_layer));
    }

    const std::vector<BoundaryEdgeLayer> &get_boundary_layers() const;
    const std::vector<InterContextEdgeLayer> &get_inter_context_layers() const;
    const std::vector<DdrChannelEdgeLayer> &get_ddr_channel_layers() const;

    ExpectedRef<DdrChannelsPair> create_ddr_channels_pair(const DdrChannelsInfo &ddr_info);
    ExpectedRef<const DdrChannelsPair> get_ddr_channels_pair(uint8_t d2h_stream_index) const;
    const std::vector<DdrChannelsPair> &get_ddr_channels_pairs() const;

    // Gets edge layer for a specific direction
    std::vector<BoundaryEdgeLayer> get_boundary_layers(hailo_stream_direction_t direction) const;
    std::vector<InterContextEdgeLayer> get_inter_context_layers(hailo_stream_direction_t direction) const;
    std::vector<DdrChannelEdgeLayer> get_ddr_channel_layers(hailo_stream_direction_t direction) const;

    hailo_status validate_edge_layers();

    std::vector<ConfigBuffer> &get_config_buffers();

private:
    explicit ContextResources(HailoRTDriver &driver, CONTROL_PROTOCOL__context_switch_context_type_t context_type,
        std::vector<ConfigBuffer> &&config_buffers) :
        m_driver(std::ref(driver)),
        m_builder(context_type),
        m_config_buffers(std::move(config_buffers))
    {}

    std::reference_wrapper<HailoRTDriver> m_driver;
    ContextSwitchBufferBuilder m_builder;
    std::vector<ConfigBuffer> m_config_buffers;
    std::vector<DdrChannelsPair> m_ddr_channels_pairs;

    std::vector<BoundaryEdgeLayer> m_boundary_layers;
    std::vector<InterContextEdgeLayer> m_inter_context_layers;
    std::vector<DdrChannelEdgeLayer> m_ddr_channel_layers;
};

class ResourcesManager final
{
public:
    static Expected<ResourcesManager> create(VdmaDevice &vdma_device, HailoRTDriver &driver,
        const ConfigureNetworkParams &config_params, std::shared_ptr<NetworkGroupMetadata> network_group_metadata,
        uint8_t net_group_index);

    ~ResourcesManager() = default;
    ResourcesManager(const ResourcesManager &other) = delete;
    ResourcesManager &operator=(const ResourcesManager &other) = delete;
    ResourcesManager &operator=(ResourcesManager &&other) = delete;
    ResourcesManager(ResourcesManager &&other) noexcept;

    ExpectedRef<InterContextBuffer> create_inter_context_buffer(uint32_t transfer_size, uint8_t src_stream_index,
        uint8_t src_context_index, const std::string &network_name);
    ExpectedRef<InterContextBuffer> get_inter_context_buffer(const IntermediateBufferKey &key);
    hailo_status create_boundary_vdma_channel(const LayerInfo &layer_info);

    Expected<CONTROL_PROTOCOL__application_header_t> get_control_network_group_header();

    Expected<std::reference_wrapper<ContextResources>> add_new_context(CONTROL_PROTOCOL__context_switch_context_type_t type,
        const ConfigBufferInfoMap &config_info={});

    const SupportedFeatures &get_supported_features() const
    {
        return m_network_group_metadata->supported_features();
    }

    VdmaDevice &get_device()
    {
        return m_vdma_device;
    }

    Expected<vdma::ChannelId> get_available_channel_id(const LayerIdentifier &layer_identifier,
        VdmaChannel::Direction direction, uint8_t engine_index);
    hailo_status free_channel_index(const LayerIdentifier &layer_identifier);

    const char* get_dev_id() const
    {
        return m_vdma_device.get_dev_id();
    }

    LatencyMetersMap &get_latency_meters()
    {
        return m_latency_meters;
    }

    Expected<hailo_stream_interface_t> get_default_streams_interface();

    Expected<Buffer> read_intermediate_buffer(const IntermediateBufferKey &key);

    hailo_status create_internal_vdma_channels();
    hailo_status register_fw_managed_vdma_channels();
    hailo_status unregister_fw_managed_vdma_channels();
    hailo_status set_inter_context_channels_dynamic_batch_size(uint16_t dynamic_batch_size);
    hailo_status open_ddr_channels();
    void abort_ddr_channels();
    void close_ddr_channels();
    hailo_status configure();
    hailo_status enable_state_machine(uint16_t dynamic_batch_size);
    hailo_status reset_state_machine(bool keep_nn_config_during_reset = false);
    Expected<uint16_t> get_network_batch_size(const std::string &network_name) const;
    Expected<std::shared_ptr<VdmaChannel>> get_boundary_vdma_channel_by_stream_name(const std::string &stream_name);
    Expected<std::shared_ptr<const VdmaChannel>> get_boundary_vdma_channel_by_stream_name(const std::string &stream_name) const;
    hailo_power_mode_t get_power_mode() const;

private:
    hailo_status fill_infer_features(CONTROL_PROTOCOL__application_header_t &app_header);
    hailo_status fill_validation_features(CONTROL_PROTOCOL__application_header_t &app_header);
    hailo_status fill_network_batch_size(CONTROL_PROTOCOL__application_header_t &app_header);

    std::vector<ContextResources> m_contexts_resources;
    ChannelAllocator m_channel_allocator;
    VdmaDevice &m_vdma_device;
    HailoRTDriver &m_driver;
    const ConfigureNetworkParams m_config_params;
    std::map<IntermediateBufferKey, InterContextBuffer> m_inter_context_buffers;
    std::vector<VdmaChannel> m_internal_channels;
    std::shared_ptr<NetworkGroupMetadata> m_network_group_metadata;
    uint8_t m_net_group_index;
    uint8_t m_dynamic_context_count;
    uint8_t m_total_context_count;
    const std::vector<std::string> m_network_index_map;
    LatencyMetersMap m_latency_meters; // Latency meter per network
    std::map<std::string, std::shared_ptr<VdmaChannel>> m_boundary_channels; //map of string name and connected vDMA channel
    bool m_is_configured;
    // Config channels ids are shared between all context. The following vector contains the channel id for each
    // config_stream_index.
    std::vector<vdma::ChannelId> m_config_channels_ids;

    ResourcesManager(VdmaDevice &vdma_device, HailoRTDriver &driver,
        ChannelAllocator &&channel_allocator, const ConfigureNetworkParams config_params,
        std::shared_ptr<NetworkGroupMetadata> &&network_group_metadata, uint8_t net_group_index,
        const std::vector<std::string> &&network_index_map, LatencyMetersMap &&latency_meters,
        std::vector<vdma::ChannelId> &&config_channels_ids);
};

} /* namespace hailort */

#endif /* _HAILO_CONTEXT_SWITCH_RESOURCE_MANAGER_HPP_ */
