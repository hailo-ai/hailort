/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file resource_manager.hpp
 * @brief Manager for vdma-config core-op resources, for a specific physical device
 *
 * ResourceManager is used on 2 possible flows with the following dependencies:
 *
 * !-Working with physical device-!
 * VdmaDevice (either PcieDevice or IntegratedDevice)
 * |--vector of VdmaConfigCoreOp
 *              |--ResourceManager <only one>
 *                 |--reference to physical device
 *
 * !-Working with virtual device-!
 * VDevice
 * |--vector of VdmaDevice (either PcieDevice or IntegratedDevice)
 * |--vector of VDeviceCoreOp
 *              |-- vector of VdmaConfigCoreOp <one per phys device>
 *                            |--ResourceManager <only one>
 *                               |--reference to physical device
  **/

#ifndef _HAILO_CONTEXT_SWITCH_RESOURCE_MANAGER_HPP_
#define _HAILO_CONTEXT_SWITCH_RESOURCE_MANAGER_HPP_

#include "hailo/hailort.h"

#include "core_op/resource_manager/intermediate_buffer.hpp"
#include "core_op/resource_manager/config_buffer.hpp"
#include "core_op/resource_manager/channel_allocator.hpp"
#include "core_op/resource_manager/context_switch_buffer_builder.hpp"
#include "device_common/control_protocol.hpp"
#include "vdma/channel/boundary_channel.hpp"
#include "vdma/pcie/pcie_device.hpp"


namespace hailort
{

#define DEFAULT_ACTUAL_BATCH_SIZE (1)


struct EdgeLayer {
    LayerInfo layer_info;
    vdma::ChannelId channel_id;
    CONTROL_PROTOCOL__host_buffer_info_t buffer_info;
};

struct DdrChannelsInfo
{
    vdma::ChannelId d2h_channel_id;
    uint8_t d2h_stream_index;
    vdma::ChannelId h2d_channel_id;
    uint8_t h2d_stream_index;
    CONTROL_PROTOCOL__host_buffer_info_t host_buffer_info;
    uint8_t network_index;
    uint16_t row_size;
    uint16_t min_buffered_rows;
    // total_buffers_per_frame not same as core_buffer_per frame. 
    //(In DDR core buffer per frame is 1). Used to calc total host descriptors_per_frame. 
    uint16_t total_buffers_per_frame;

    // Checks if the credits are automaticaly going from d2h channel to its h2d channel, or it needs to be done manually
    // (Using a fw task).
    bool need_manual_credit_management() const
    {
        return host_buffer_info.buffer_type == CONTROL_PROTOCOL__HOST_BUFFER_TYPE_EXTERNAL_DESC;
    }

    uint16_t descs_count() const
    {
        assert(IS_FIT_IN_UINT16(host_buffer_info.total_desc_count));
        return static_cast<uint16_t>(host_buffer_info.total_desc_count);
    }

    uint32_t descriptors_per_frame() const
    {
        return (row_size / host_buffer_info.desc_page_size) * total_buffers_per_frame;
    }
};

class ContextResources final {
public:
    static Expected<ContextResources> create(HailoRTDriver &driver, CONTROL_PROTOCOL__context_switch_context_type_t context_type,
        const std::vector<vdma::ChannelId> &config_channels_ids, const ConfigBufferInfoMap &config_buffer_infos);

    const std::vector<CONTROL_PROTOCOL__context_switch_context_info_single_control_t> &get_controls() const;
    ContextSwitchBufferBuilder &builder();

    hailo_status add_edge_layer(const LayerInfo &layer_info, vdma::ChannelId channel_id,
        const CONTROL_PROTOCOL__host_buffer_info_t &buffer_info, const SupportedFeatures &supported_features);
    void add_ddr_channels_info(const DdrChannelsInfo &ddr_info);

    std::vector<EdgeLayer> get_edge_layers() const;
    std::vector<EdgeLayer> get_edge_layers(LayerType layer_type) const;
    std::vector<EdgeLayer> get_edge_layers(hailo_stream_direction_t direction) const;
    std::vector<EdgeLayer> get_edge_layers(LayerType layer_type, hailo_stream_direction_t direction) const;

    Expected<EdgeLayer> get_edge_layer_by_stream_index(const uint8_t stream_index,
        const hailo_stream_direction_t direction) const;
    Expected<EdgeLayer> get_edge_layer_by_channel_id(const vdma::ChannelId channel_id) const;

    Expected<DdrChannelsInfo> get_ddr_channels_info(uint8_t d2h_stream_index) const;
    const std::vector<DdrChannelsInfo> &get_ddr_channels_infos() const;

    hailo_status validate_edge_layer(const LayerInfo &layer_info, vdma::ChannelId channel_id,
        const SupportedFeatures &supported_features);

    std::vector<ConfigBuffer> &get_config_buffers();

private:
    ContextResources(HailoRTDriver &driver, CONTROL_PROTOCOL__context_switch_context_type_t context_type,
        std::vector<ConfigBuffer> &&config_buffers) :
        m_driver(std::ref(driver)),
        m_builder(context_type),
        m_config_buffers(std::move(config_buffers))
    {}

    std::reference_wrapper<HailoRTDriver> m_driver;
    ContextSwitchBufferBuilder m_builder;
    std::vector<ConfigBuffer> m_config_buffers;

    std::vector<EdgeLayer> m_edge_layers;
    std::vector<DdrChannelsInfo> m_ddr_channels_infos;
};

class ResourcesManager final
{
public:
    static Expected<ResourcesManager> create(VdmaDevice &vdma_device, HailoRTDriver &driver,
        const ConfigureNetworkParams &config_params, std::shared_ptr<CoreOpMetadata> core_op_metadata,
        uint8_t core_op_index);

    // TODO: HRT-9432 needs to call stop_vdma_interrupts_dispatcher and any other resource on dtor. 
    ~ResourcesManager() = default;
    ResourcesManager(const ResourcesManager &other) = delete;
    ResourcesManager &operator=(const ResourcesManager &other) = delete;
    ResourcesManager &operator=(ResourcesManager &&other) = delete;
    ResourcesManager(ResourcesManager &&other) noexcept;

    ExpectedRef<IntermediateBuffer> create_intermediate_buffer(uint32_t transfer_size, uint16_t batch_size,
        uint8_t src_stream_index, uint8_t src_context_index, vdma::ChannelId d2h_channel_id,
        IntermediateBuffer::StreamingType streaming_type);
    ExpectedRef<IntermediateBuffer> get_intermediate_buffer(const IntermediateBufferKey &key);
    hailo_status create_boundary_vdma_channel(const LayerInfo &layer_info);

    Expected<CONTROL_PROTOCOL__application_header_t> get_control_core_op_header();

    Expected<std::reference_wrapper<ContextResources>> add_new_context(CONTROL_PROTOCOL__context_switch_context_type_t type,
        const ConfigBufferInfoMap &config_info={});

    const SupportedFeatures &get_supported_features() const
    {
        return m_core_op_metadata->supported_features();
    }

    VdmaDevice &get_device()
    {
        return m_vdma_device;
    }

    Expected<vdma::ChannelId> get_available_channel_id(const LayerIdentifier &layer_identifier,
        HailoRTDriver::DmaDirection direction, uint8_t engine_index);
    hailo_status free_channel_index(const LayerIdentifier &layer_identifier);

    const char* get_dev_id() const
    {
        return m_vdma_device.get_dev_id();
    }

    LatencyMetersMap &get_latency_meters()
    {
        return m_latency_meters;
    }

    std::map<vdma::ChannelId, vdma::BoundaryChannelPtr> get_boundary_vdma_channels() const
    {
        return m_boundary_channels;
    }

    Expected<hailo_stream_interface_t> get_default_streams_interface();

    Expected<Buffer> read_intermediate_buffer(const IntermediateBufferKey &key);

    hailo_status configure();
    hailo_status enable_state_machine(uint16_t dynamic_batch_size, 
        uint16_t batch_count = CONTROL_PROTOCOL__INIFINITE_BATCH_COUNT);
    hailo_status reset_state_machine();
    hailo_status cancel_pending_transfers();
    hailo_status start_vdma_interrupts_dispatcher();
    hailo_status stop_vdma_interrupts_dispatcher();
    Expected<uint16_t> get_network_batch_size(const std::string &network_name) const;
    Expected<vdma::BoundaryChannelPtr> get_boundary_vdma_channel_by_stream_name(const std::string &stream_name);
    Expected<std::shared_ptr<const vdma::BoundaryChannel>> get_boundary_vdma_channel_by_stream_name(const std::string &stream_name) const;
    hailo_power_mode_t get_power_mode() const;
    Expected<uint16_t> program_desc_for_hw_only_flow(std::shared_ptr<vdma::DescriptorList> desc_list,
        const uint32_t single_transfer_size, const uint16_t dynamic_batch_size, const uint16_t batch_count);
    Expected<std::pair<vdma::ChannelId, uint16_t>> create_mapped_buffer_for_hw_only_infer(
        vdma::BoundaryChannelPtr boundary_channel_ptr, const HailoRTDriver::DmaDirection direction,
        const uint32_t single_transfer_size, const uint16_t dynamic_batch_size, const uint16_t batch_count);
    void add_channel_to_hw_infer_channel_info(std::pair<vdma::ChannelId, uint16_t> channel_info,
        CONTROL_PROTOCOL__hw_infer_channels_info_t &channels_info);
    Expected<uint16_t> calc_hw_infer_batch_count(uint16_t dynamic_batch_size);
    HwInferResults hw_infer_calc_stats(uint16_t batch_count, uint16_t dynamic_batch_size,
        size_t single_frame_transfer_size, uint32_t infer_cycles);
    hailo_status set_hw_infer_done_notification(std::condition_variable &infer_done_cond);
    Expected<HwInferResults> run_hw_only_infer();

private:
    hailo_status fill_infer_features(CONTROL_PROTOCOL__application_header_t &app_header);
    hailo_status fill_validation_features(CONTROL_PROTOCOL__application_header_t &app_header);
    hailo_status fill_network_batch_size(CONTROL_PROTOCOL__application_header_t &app_header);
    hailo_status fill_csm_buffer_size(CONTROL_PROTOCOL__application_header_t &app_header);
    void process_interrupts(IrqData &&irq_data);
    Expected<uint16_t> get_batch_size() const;

    std::vector<ContextResources> m_contexts_resources;
    ChannelAllocator m_channel_allocator;
    VdmaDevice &m_vdma_device;
    HailoRTDriver &m_driver;
    const ConfigureNetworkParams m_config_params;
    std::map<IntermediateBufferKey, IntermediateBuffer> m_intermediate_buffers;
    std::shared_ptr<CoreOpMetadata> m_core_op_metadata;
    uint8_t m_core_op_index;
    uint8_t m_dynamic_context_count;
    uint8_t m_total_context_count;
    const std::vector<std::string> m_network_index_map;
    LatencyMetersMap m_latency_meters; // Latency meter per network
    // TODO: HRT-9429 - fast access to channel by id, using array, using engine_index and channel_index.
    std::map<vdma::ChannelId, vdma::BoundaryChannelPtr> m_boundary_channels;
    bool m_is_configured;
    // Config channels ids are shared between all context. The following vector contains the channel id for each
    // config_stream_index.
    std::vector<vdma::ChannelId> m_config_channels_ids;
    // Mapped buffers would be used only in hw only flow
    std::vector<std::shared_ptr<vdma::MappedBuffer>> m_hw_only_boundary_buffers;

    ResourcesManager(VdmaDevice &vdma_device, HailoRTDriver &driver,
        ChannelAllocator &&channel_allocator, const ConfigureNetworkParams config_params,
        std::shared_ptr<CoreOpMetadata> &&core_op_metadata, uint8_t core_op_index,
        const std::vector<std::string> &&network_index_map, LatencyMetersMap &&latency_meters,
        std::vector<vdma::ChannelId> &&config_channels_ids);
};

} /* namespace hailort */

#endif /* _HAILO_CONTEXT_SWITCH_RESOURCE_MANAGER_HPP_ */
