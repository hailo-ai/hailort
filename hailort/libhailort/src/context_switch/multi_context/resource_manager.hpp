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
 * └──vector of PcieDevice
 * └──vector of VdmaConfigNetworkGroup
 *              └── vector of ResourceManager <one per phys device>
 *                            └──reference to physical device
  **/

#ifndef _HAILO_CONTEXT_SWITCH_RESOURCE_MANAGER_HPP_
#define _HAILO_CONTEXT_SWITCH_RESOURCE_MANAGER_HPP_

#include "hailo/hailort.h"
#include "intermediate_buffer.hpp"
#include "config_buffer.hpp"
#include "vdma_channel.hpp"
#include "control_protocol.hpp"
#include "pcie_device.hpp"


namespace hailort
{

#define MIN_H2D_CHANNEL_INDEX (0)
#define MAX_H2D_CHANNEL_INDEX (15)
#define MIN_D2H_CHANNEL_INDEX (16)
#define MAX_D2H_CHANNEL_INDEX (31)

#define DDR_NUMBER_OF_ROWS_PER_INTERRUPT (1)
#define DDR_THREAD_DEFAULT_TIMEOUT_MS (20 * 1000)
#define DDR_THREADS_MIN_BUFFERED_ROWS_INITIAL_SCALE (1)


class DdrChannelsInfo
{
public:
    uint8_t d2h_channel_index;
    uint8_t d2h_stream_index;
    uint8_t h2d_channel_index;
    uint8_t h2d_stream_index;
    uint16_t row_size;
    uint32_t min_buffered_rows;
    uint32_t desc_list_size_mask;
    uint8_t context_index;
    uint16_t initial_programed_descs;
    uint32_t descriptors_per_frame;
    // Ref to ResourcesManager's m_ddr_buffer_channels
    VdmaChannel *h2d_ch;
    VdmaChannel *d2h_ch;

    // Ref to intermediate buffer;
    IntermediateBuffer *intermediate_buffer;
};
class ChannelInfo
{
public:
    enum class Type : uint8_t 
    {
        NOT_SET = 0,
        BOUNDARY = 1,
        INTER_CONTEXT = 2,
        DDR = 3,
        CFG = 4
    };

    ChannelInfo() : m_info(Type::NOT_SET), m_pcie_stream_index(UINT8_MAX), m_layer_name() {}

    void set_type(Type type)
    { 
        m_info = type;
    }

    bool is_type(Type type) const
    {
        return (m_info == type);
    }

    bool is_used() const
    {
        return (m_info != Type::NOT_SET); 
    }

    uint8_t get_pcie_stream_index() 
    {
        assert(UINT8_MAX != m_pcie_stream_index);
        return m_pcie_stream_index;
    }

    void set_pcie_stream_index(uint8_t pcie_channel_index) 
    {
        m_pcie_stream_index = pcie_channel_index;
    }

    void set_layer_name(const std::string &name) 
    {
        m_layer_name = name;
    }

    const std::string &get_layer_name() 
    {
        return m_layer_name;
    }

private:
    Type m_info;
    uint8_t m_pcie_stream_index;
    std::string m_layer_name;
};

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
        m_ddr_infos(std::move(other.m_ddr_infos)), m_contexts(std::move(other.m_contexts)),
        m_channels_info(std::move(other.m_channels_info)), m_vdma_device(other.m_vdma_device),
        m_driver(other.m_driver), m_config_params(other.m_config_params),
        m_preliminary_config(std::move(other.m_preliminary_config)),
        m_dynamic_config(std::move(other.m_dynamic_config)),
        m_intermediate_buffers(std::move(other.m_intermediate_buffers)),
        m_inter_context_channels(std::move(other.m_inter_context_channels)),
        m_config_channels(std::move(other.m_config_channels)), m_ddr_buffer_channels(std::move(other.m_ddr_buffer_channels)),
        m_network_group_metadata(std::move(other.m_network_group_metadata)), m_net_group_index(other.m_net_group_index),
        m_network_index_map(std::move(other.m_network_index_map)),
        m_latency_meters(std::move(other.m_latency_meters)),
        m_boundary_channels(std::move(other.m_boundary_channels)) {}

    ExpectedRef<IntermediateBuffer> create_inter_context_buffer(uint32_t transfer_size, uint8_t src_stream_index,
        uint8_t src_context_index, const std::string &network_name);
    ExpectedRef<IntermediateBuffer> get_intermediate_buffer(const IntermediateBufferKey &key);
    Expected<std::shared_ptr<VdmaChannel>> create_boundary_vdma_channel(uint8_t channel_index, uint32_t transfer_size, 
        const std::string &network_name, const std::string &stream_name,
        VdmaChannel::Direction channel_direction);

    ExpectedRef<IntermediateBuffer> create_ddr_buffer(DdrChannelsInfo &ddr_info, uint8_t context_index);

    Expected<CONTROL_PROTOCOL__application_header_t> get_control_network_group_header(bool is_scheduler_used);

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

    std::vector<DdrChannelsInfo> &ddr_infos()
    {
        return m_ddr_infos;
    }

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
        CHECK_AS_EXPECTED(index < m_channels_info.max_size(), HAILO_INVALID_ARGUMENT);
        return std::ref(m_channels_info[index]);
    }

    const char* get_dev_id() const
    {
        return m_vdma_device.get_dev_id();
    }

    LatencyMetersMap &get_latnecy_meters()
    {
        return m_latency_meters;
    }

    Expected<uint8_t> get_boundary_channel_index(uint8_t stream_index,
        hailo_stream_direction_t direction, const std::string &layer_name);
    Expected<hailo_stream_interface_t> get_default_streams_interface();

    Expected<Buffer> read_intermediate_buffer(const IntermediateBufferKey &key);

    hailo_status set_number_of_cfg_channels(const uint8_t number_of_cfg_channels);
    void update_preliminary_config_buffer_info();
    void update_dynamic_contexts_buffer_info();

    hailo_status create_internal_vdma_channels();
    hailo_status register_fw_managed_vdma_channels();
    hailo_status unregister_fw_managed_vdma_channels();
    hailo_status set_inter_context_channels_dynamic_batch_size(uint16_t dynamic_batch_size);
    hailo_status open_ddr_channels();
    void abort_ddr_channels();
    void close_ddr_channels();
    hailo_status enable_state_machine(uint16_t dynamic_batch_size);
    hailo_status reset_state_machine();
    Expected<uint16_t> get_network_batch_size(const std::string &network_name) const;
    Expected<std::shared_ptr<VdmaChannel>> get_boundary_vdma_channel_by_stream_name(const std::string &stream_name);

private:
    ExpectedRef<IntermediateBuffer> create_intermediate_buffer(IntermediateBuffer::ChannelType channel_type,
        uint32_t transfer_size, uint16_t batch_size, const IntermediateBufferKey &key);
    void update_config_buffer_info(std::vector<ConfigBuffer> &config_buffers,
        CONTROL_PROTOCOL__context_switch_context_info_t &context);
    hailo_status fill_infer_features(CONTROL_PROTOCOL__application_header_t &app_header);
    hailo_status fill_network_batch_size(CONTROL_PROTOCOL__application_header_t &app_header, bool is_scheduler_used);

    std::vector<DdrChannelsInfo> m_ddr_infos;
    std::vector<CONTROL_PROTOCOL__context_switch_context_info_t> m_contexts;
    std::array<ChannelInfo, MAX_HOST_CHANNELS_COUNT> m_channels_info;
    VdmaDevice &m_vdma_device;
    HailoRTDriver &m_driver;
    const ConfigureNetworkParams m_config_params;
    std::vector<ConfigBuffer> m_preliminary_config;
    // m_dynamic_config[context_index][config_index]
    std::vector<std::vector<ConfigBuffer>> m_dynamic_config;
    std::map<IntermediateBufferKey, IntermediateBuffer> m_intermediate_buffers;
    std::vector<VdmaChannel> m_inter_context_channels;
    std::vector<VdmaChannel> m_config_channels;
    std::vector<VdmaChannel> m_ddr_buffer_channels;
    std::shared_ptr<NetworkGroupMetadata> m_network_group_metadata;
    uint8_t m_net_group_index;
    const std::vector<std::string> m_network_index_map;
    LatencyMetersMap m_latency_meters; // Latency meter per network
    std::map<std::string, std::shared_ptr<VdmaChannel>> m_boundary_channels; //map of string name and connected vDMA channel

    ResourcesManager(VdmaDevice &vdma_device, HailoRTDriver &driver,
        const ConfigureNetworkParams config_params, std::vector<ConfigBuffer> &&preliminary_config,
        std::vector<std::vector<ConfigBuffer>> &&dynamic_config, std::shared_ptr<NetworkGroupMetadata> &&network_group_metadata, uint8_t net_group_index,
        const std::vector<std::string> &&network_index_map, LatencyMetersMap &&latency_meters) :
          m_vdma_device(vdma_device), m_driver(driver), m_config_params(config_params),
          m_preliminary_config(std::move(preliminary_config)), m_dynamic_config(std::move(dynamic_config)),
          m_network_group_metadata(std::move(network_group_metadata)), m_net_group_index(net_group_index), m_network_index_map(std::move(network_index_map)),
          m_latency_meters(std::move(latency_meters)) {};

};

} /* namespace hailort */

#endif /* _HAILO_CONTEXT_SWITCH_RESOURCE_MANAGER_HPP_ */
