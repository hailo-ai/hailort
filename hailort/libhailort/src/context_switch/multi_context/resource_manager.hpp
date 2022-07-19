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
 * └── VdmaConfigManager
 *     └──vector of VdmaConfigNetworkGroup
 *                  └──ResourceManager <only one>
 *                     └──reference to physical device
 *
 * !-Working with virtual device-!
 * VDevice
 * └──vector of VdmaDevice (either PcieDevice or CoreDevice)
 * └──vector of VdmaConfigNetworkGroup
 *              └── vector of ResourceManager <one per phys device>
 *                            └──reference to physical device
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


namespace hailort
{

class ResourcesManager final
{
public:
    static Expected<ResourcesManager> create(VdmaDevice &vdma_device, HailoRTDriver &driver,
        const ConfigureNetworkParams &config_params, ProtoHEFNetworkGroupPtr network_group_proto,
        std::shared_ptr<NetworkGroupMetadata> network_group_metadata, const HefParsingInfo &parsing_info,
        uint8_t net_group_index);

    ~ResourcesManager() = default;
    ResourcesManager(const ResourcesManager &other) = delete;
    ResourcesManager &operator=(const ResourcesManager &other) = delete;
    ResourcesManager &operator=(ResourcesManager &&other) = delete;
    ResourcesManager(ResourcesManager &&other) noexcept :
        m_contexts(std::move(other.m_contexts)),
        m_channel_allocator(std::move(other.m_channel_allocator)),
        m_vdma_device(other.m_vdma_device),
        m_driver(other.m_driver), m_config_params(other.m_config_params),
        m_preliminary_config(std::move(other.m_preliminary_config)),
        m_dynamic_config(std::move(other.m_dynamic_config)),
        m_inter_context_buffers(std::move(other.m_inter_context_buffers)),
        m_ddr_channels_pairs(std::move(other.m_ddr_channels_pairs)),
        m_fw_managed_channels(std::move(other.m_fw_managed_channels)),
        m_network_group_metadata(std::move(other.m_network_group_metadata)), m_net_group_index(other.m_net_group_index),
        m_network_index_map(std::move(other.m_network_index_map)),
        m_latency_meters(std::move(other.m_latency_meters)),
        m_boundary_channels(std::move(other.m_boundary_channels)) {}

    ExpectedRef<InterContextBuffer> create_inter_context_buffer(uint32_t transfer_size, uint8_t src_stream_index,
        uint8_t src_context_index, const std::string &network_name);
    ExpectedRef<InterContextBuffer> get_inter_context_buffer(const IntermediateBufferKey &key);
    Expected<std::shared_ptr<VdmaChannel>> create_boundary_vdma_channel(uint8_t channel_index, uint32_t transfer_size, 
        const std::string &network_name, const std::string &stream_name,
        VdmaChannel::Direction channel_direction);

    ExpectedRef<DdrChannelsPair> create_ddr_channels_pair(const DdrChannelsInfo &ddr_info, uint8_t context_index);
    ExpectedRef<DdrChannelsPair> get_ddr_channels_pair(uint8_t context_index, uint8_t d2h_stream_index);

    Expected<CONTROL_PROTOCOL__application_header_t> get_control_network_group_header();

    using context_info_t = CONTROL_PROTOCOL__context_switch_context_info_t;

    Expected<std::reference_wrapper<context_info_t>> add_new_context()
    {
        return std::ref(*m_contexts.emplace(m_contexts.end()));
    }

    const std::vector<context_info_t>& get_contexts()
    {
        return m_contexts;
    }

    std::vector<ConfigBuffer> &preliminary_config() 
    {
        return m_preliminary_config; 
    }

    std::vector<ConfigBuffer> &dynamic_config(uint8_t context_index)
    {
        assert(context_index < m_dynamic_config.size());
        return m_dynamic_config[context_index]; 
    }

    std::vector<std::reference_wrapper<const DdrChannelsPair>> get_ddr_channel_pairs_per_context(uint8_t context_index) const;

    hailo_power_mode_t get_power_mode()
    {
        return m_config_params.power_mode;
    }

    const NetworkGroupSupportedFeatures &get_supported_features() const
    {
        return m_network_group_metadata->supported_features();
    }

    uint8_t get_network_group_index()
    {
        return m_net_group_index;
    }

    VdmaDevice &get_device()
    {
        return m_vdma_device;
    }

    Expected<uint8_t> get_available_channel_index(std::set<uint8_t>& blacklist, ChannelInfo::Type required_type,
        VdmaChannel::Direction direction, const std::string &layer_name="");

    Expected<std::reference_wrapper<ChannelInfo>> get_channel_info(uint8_t index)
    {
        return m_channel_allocator.get_channel_info(index);
    }

    const char* get_dev_id() const
    {
        return m_vdma_device.get_dev_id();
    }

    LatencyMetersMap &get_latnecy_meters()
    {
        return m_latency_meters;
    }

    Expected<hailo_stream_interface_t> get_default_streams_interface();

    Expected<Buffer> read_intermediate_buffer(const IntermediateBufferKey &key);

    hailo_status set_number_of_cfg_channels(const uint8_t number_of_cfg_channels);
    void update_preliminary_config_buffer_info();
    void update_dynamic_contexts_buffer_info();

    hailo_status create_fw_managed_vdma_channels();
    hailo_status register_fw_managed_vdma_channels();
    hailo_status unregister_fw_managed_vdma_channels();
    hailo_status set_inter_context_channels_dynamic_batch_size(uint16_t dynamic_batch_size);
    hailo_status open_ddr_channels();
    void abort_ddr_channels();
    void close_ddr_channels();
    hailo_status enable_state_machine(uint16_t dynamic_batch_size);
    hailo_status reset_state_machine(bool keep_nn_config_during_reset = false);
    Expected<uint16_t> get_network_batch_size(const std::string &network_name) const;
    Expected<std::shared_ptr<VdmaChannel>> get_boundary_vdma_channel_by_stream_name(const std::string &stream_name);

private:
    void update_config_buffer_info(std::vector<ConfigBuffer> &config_buffers,
        CONTROL_PROTOCOL__context_switch_context_info_t &context);
    hailo_status fill_infer_features(CONTROL_PROTOCOL__application_header_t &app_header);
    hailo_status fill_network_batch_size(CONTROL_PROTOCOL__application_header_t &app_header);

    std::vector<CONTROL_PROTOCOL__context_switch_context_info_t> m_contexts;
    ChannelAllocator m_channel_allocator;
    VdmaDevice &m_vdma_device;
    HailoRTDriver &m_driver;
    const ConfigureNetworkParams m_config_params;
    std::vector<ConfigBuffer> m_preliminary_config;
    // m_dynamic_config[context_index][config_index]
    std::vector<std::vector<ConfigBuffer>> m_dynamic_config;
    std::map<IntermediateBufferKey, InterContextBuffer> m_inter_context_buffers;
    std::map<IntermediateBufferKey, DdrChannelsPair> m_ddr_channels_pairs;
    std::vector<VdmaChannel> m_fw_managed_channels;
    std::shared_ptr<NetworkGroupMetadata> m_network_group_metadata;
    uint8_t m_net_group_index;
    const std::vector<std::string> m_network_index_map;
    LatencyMetersMap m_latency_meters; // Latency meter per network
    std::map<std::string, std::shared_ptr<VdmaChannel>> m_boundary_channels; //map of string name and connected vDMA channel

    ResourcesManager(VdmaDevice &vdma_device, HailoRTDriver &driver,
        const ConfigureNetworkParams config_params, std::vector<ConfigBuffer> &&preliminary_config,
        std::vector<std::vector<ConfigBuffer>> &&dynamic_config, std::shared_ptr<NetworkGroupMetadata> &&network_group_metadata, uint8_t net_group_index,
        const std::vector<std::string> &&network_index_map, LatencyMetersMap &&latency_meters) :
          m_channel_allocator(network_group_metadata->supported_features()),
          m_vdma_device(vdma_device), m_driver(driver), m_config_params(config_params),
          m_preliminary_config(std::move(preliminary_config)), m_dynamic_config(std::move(dynamic_config)),
          m_network_group_metadata(std::move(network_group_metadata)), m_net_group_index(net_group_index), m_network_index_map(std::move(network_index_map)),
          m_latency_meters(std::move(latency_meters)) {};

};

} /* namespace hailort */

#endif /* _HAILO_CONTEXT_SWITCH_RESOURCE_MANAGER_HPP_ */
