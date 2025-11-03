/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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

#include "core_op/resource_manager/cache_buffer.hpp"
#include "core_op/resource_manager/cache_manager.hpp"
#include "core_op/resource_manager/config_buffer.hpp"
#include "core_op/resource_manager/channel_allocator.hpp"
#include "core_op/resource_manager/action_list_buffer_builder/action_list_buffer_builder.hpp"
#include "device_common/control_protocol.hpp"
#include "vdma/channel/boundary_channel.hpp"
#include "vdma/pcie/pcie_device.hpp"
#include "internal_buffer_manager.hpp"
#include "vdma/memory/continuous_buffer.hpp"

namespace hailort
{


struct EdgeLayer {
    LayerInfo layer_info;
    vdma::ChannelId channel_id;
    CONTROL_PROTOCOL__host_buffer_info_t buffer_info;

    EdgeLayer(LayerInfo &&layer_info, vdma::ChannelId channel_id, CONTROL_PROTOCOL__host_buffer_info_t buffer_info):
        layer_info(layer_info), channel_id(channel_id), buffer_info(buffer_info) {}
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

    DdrChannelsInfo(vdma::ChannelId d2h_channel_id, uint8_t d2h_stream_index, vdma::ChannelId h2d_channel_id,
        uint8_t h2d_stream_index, CONTROL_PROTOCOL__host_buffer_info_t host_buffer_info, uint8_t network_index,
        uint16_t row_size, uint16_t min_buffered_rows, uint16_t total_buffers_per_frame) :
        d2h_channel_id(d2h_channel_id), d2h_stream_index(d2h_stream_index), h2d_channel_id(h2d_channel_id),
        h2d_stream_index(h2d_stream_index), host_buffer_info(host_buffer_info), network_index(network_index),
        row_size(row_size), min_buffered_rows(min_buffered_rows), total_buffers_per_frame(total_buffers_per_frame)
    {}

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
    static Expected<ContextResources> create(HailoRTDriver &driver,
        CONTROL_PROTOCOL__context_switch_context_type_t context_type,
        const std::vector<vdma::ChannelId> &config_channels_ids, const ConfigBufferInfoMap &config_buffer_infos,
        std::shared_ptr<InternalBufferManager> internal_buffer_manager,
        bool zero_copy_config_over_descs,
        std::vector<std::shared_ptr<vdma::MappedBuffer>> mapped_buffers = {},
        std::shared_ptr<vdma::MappedBuffer> nops_buffer = nullptr);

    hailo_status add_edge_layer(LayerInfo &&layer_info, vdma::ChannelId channel_id,
        const CONTROL_PROTOCOL__host_buffer_info_t &buffer_info, const SupportedFeatures &supported_features);
    void add_ddr_channels_info(vdma::ChannelId d2h_channel_id, uint8_t d2h_stream_index, vdma::ChannelId h2d_channel_id,
        uint8_t h2d_stream_index, CONTROL_PROTOCOL__host_buffer_info_t host_buffer_info, uint8_t network_index,
        uint16_t row_size, uint16_t min_buffered_rows, uint16_t total_buffers_per_frame);

    const std::vector<EdgeLayer>& get_edge_layers() const;
    std::vector<std::reference_wrapper<const EdgeLayer>> get_edge_layers(LayerType layer_type, hailo_stream_direction_t direction) const;

    Expected<std::reference_wrapper<const EdgeLayer>> get_edge_layer_by_stream_index(const uint8_t stream_index,
        const hailo_stream_direction_t direction) const;
    Expected<std::reference_wrapper<const EdgeLayer>> get_edge_layer_by_channel_id(const vdma::ChannelId channel_id) const;

    Expected<DdrChannelsInfo> get_ddr_channels_info(uint8_t d2h_stream_index) const;
    const std::vector<DdrChannelsInfo> &get_ddr_channels_infos() const;

    hailo_status validate_edge_layer(const LayerInfo &layer_info, vdma::ChannelId channel_id,
        const SupportedFeatures &supported_features);

    std::map<uint8_t, std::shared_ptr<ConfigBuffer>> &get_config_buffers();
    CONTROL_PROTOCOL__context_switch_context_type_t get_context_type() const {
        return m_context_type;
    }

private:
    ContextResources(HailoRTDriver &driver, CONTROL_PROTOCOL__context_switch_context_type_t context_type,
        std::map<uint8_t, std::shared_ptr<ConfigBuffer>> &&config_buffers, std::shared_ptr<InternalBufferManager> internal_buffer_manager) :
        m_driver(std::ref(driver)),
        m_context_type(context_type),
        m_config_buffers(std::move(config_buffers)),
        m_internal_buffer_manager(std::move(internal_buffer_manager))
    {}

    std::reference_wrapper<HailoRTDriver> m_driver;
    CONTROL_PROTOCOL__context_switch_context_type_t m_context_type;
    std::map<uint8_t, std::shared_ptr<ConfigBuffer>> m_config_buffers;

    std::vector<EdgeLayer> m_edge_layers;
    std::vector<DdrChannelsInfo> m_ddr_channels_infos;
    std::shared_ptr<InternalBufferManager> m_internal_buffer_manager;
};

class ResourcesManager final
{
public:
    static Expected<ResourcesManager> create(VdmaDevice &vdma_device, HailoRTDriver &driver,
        const ConfigureNetworkParams &config_params, CacheManagerPtr cache_manager,
        std::shared_ptr<CoreOpMetadata> core_op_metadata, uint8_t core_op_index);

    // TODO: HRT-9432 needs to call stop_vdma_interrupts_dispatcher and any other resource on dtor.
    ~ResourcesManager() = default;
    ResourcesManager(const ResourcesManager &other) = delete;
    ResourcesManager &operator=(const ResourcesManager &other) = delete;
    ResourcesManager &operator=(ResourcesManager &&other) = delete;
    ResourcesManager(ResourcesManager &&other) noexcept;

    ExpectedRef<vdma::VdmaEdgeLayer> create_intermediate_edge_layer(
        uint32_t transfer_size, uint16_t batch_size, uint8_t src_stream_index, uint16_t src_context_index,
        vdma::ChannelId d2h_channel_id, LayerType layer_type);
    ExpectedRef<vdma::VdmaEdgeLayer> get_intermediate_edge_layer(const IntermediateBufferKey &key);
    ExpectedRef<CacheBuffer> set_cache_input_channel(uint32_t cache_id, uint16_t batch_size, vdma::ChannelId channel_id);
    ExpectedRef<CacheBuffer> set_cache_output_channel(uint32_t cache_id, uint16_t batch_size, vdma::ChannelId channel_id);
    ExpectedRef<std::unordered_map<uint32_t, CacheBuffer>> get_cache_buffers();

    Expected<uint16_t> calc_default_queue_size(const LayerInfo &layer_info, uint16_t batch_size, hailo_device_architecture_t device_arch);
    hailo_status create_boundary_vdma_channel(const LayerInfo &layer_info, bool use_enhanced_channel = false);

    Expected<CONTROL_PROTOCOL__application_header_t> get_control_core_op_header();

    HailoRTDriver &get_driver() { return m_driver; }

    Expected<std::reference_wrapper<ContextResources>> add_new_context(
        CONTROL_PROTOCOL__context_switch_context_type_t context_type,
        bool zero_copy_config_over_descs, const ConfigBufferInfoMap &config_info={});

    const SupportedFeatures &get_supported_features() const
    {
        return m_core_op_metadata->supported_features();
    }

    VdmaDevice &get_device()
    {
        return m_vdma_device;
    }

    Expected<vdma::ChannelId> get_available_channel_id(const LayerIdentifier &layer_identifier,
        HailoRTDriver::DmaDirection direction, uint8_t engine_index, bool use_enhanced_channel = false);
    hailo_status free_channel_index(const LayerIdentifier &layer_identifier);

    const char* get_dev_id() const
    {
        return m_vdma_device.get_dev_id();
    }

    LatencyMetersMap &get_latency_meters()
    {
        return m_latency_meters;
    }

    std::shared_ptr<ActionListBufferBuilder>& get_action_list_buffer_builder()
    {
        return m_action_list_buffer_builder;
    }

    Expected<hailo_stream_interface_t> get_default_streams_interface();

    Expected<Buffer> read_intermediate_buffer(const IntermediateBufferKey &key);
    Expected<Buffer> read_cache_buffer(uint32_t cache_id);
    Expected<std::map<uint32_t, Buffer>> read_cache_buffers();

    hailo_status configure();
    hailo_status enable_state_machine(uint16_t dynamic_batch_size,
        uint16_t batch_count = CONTROL_PROTOCOL__INIFINITE_BATCH_COUNT);
    hailo_status reset_state_machine();
    hailo_status start_vdma_interrupts_dispatcher();
    hailo_status stop_vdma_interrupts_dispatcher();
    hailo_status start_vdma_transfer_launcher();
    hailo_status stop_vdma_transfer_launcher();
    Expected<uint16_t> get_network_batch_size(const std::string &network_name) const;
    Expected<vdma::BoundaryChannelPtr> get_boundary_vdma_channel_by_stream_name(const std::string &stream_name);
    Expected<std::shared_ptr<const vdma::BoundaryChannel>> get_boundary_vdma_channel_by_stream_name(
        const std::string &stream_name) const;
    hailo_power_mode_t get_power_mode() const;
    Expected<CONTROL_PROTOCOL__host_buffer_info_t> get_boundary_buffer_info(vdma::BoundaryChannel &channel,
        uint32_t transfer_size);
    hailo_status allocate_mapped_buffer_for_hw_only_infer(vdma::BoundaryChannelPtr boundary_channel_ptr,
        const HailoRTDriver::DmaDirection direction, const uint32_t single_transfer_size, uint16_t batch_size, uint16_t batch_count);
    hailo_status configure_mapped_buffer_for_hw_only_infer(vdma::BoundaryChannelPtr boundary_channel_ptr,
        const uint32_t single_transfer_size, uint16_t batch_size, uint16_t batch_count,
        CONTROL_PROTOCOL__hw_infer_channels_info_t &channels_info);
    void add_channel_to_hw_infer_channel_info(std::pair<vdma::ChannelId, uint16_t> channel_info,
        CONTROL_PROTOCOL__hw_infer_channels_info_t &channels_info);
    Expected<uint16_t> calc_hw_infer_batch_count(uint16_t dynamic_batch_size);
    HwInferResults hw_infer_calc_stats(uint16_t batch_count, uint16_t dynamic_batch_size,
        size_t single_frame_transfer_size, uint32_t infer_cycles);
    hailo_status set_hw_infer_done_notification(std::condition_variable &infer_done_cond);
    hailo_status configure_boundary_channels_for_hw_infer(uint16_t batch_size, uint16_t batch_count);
    hailo_status allocate_boundary_channels_buffers_hw_infer();
    Expected<HwInferResults> run_hw_only_infer();
    hailo_status fill_internal_buffers_info();
    static bool should_use_ddr_action_list(size_t num_contexts, HailoRTDriver::DmaType dma_type);
    Expected<uint16_t> get_batch_size() const;
    hailo_status map_and_set_ccws_section_buffer(BufferPtr hef_as_buffer, size_t offset_to_ccws_section, uint64_t ccws_section_size, HailoRTDriver &driver);

    bool get_can_fast_batch_switch()
    {
        return m_core_op_metadata->get_can_fast_batch_switch();
    }

    void set_is_activated(bool is_activated)
    {
        m_is_activated = is_activated;
    }

    bool get_is_activated() const
    {
        return m_is_activated;
    }

    CONTROL_PROTOCOL__boundary_channel_mode_t get_hw_infer_boundary_channel_mode() const
    {
        return (is_env_variable_on(HAILO_HW_INFER_BOUNDARY_CHANNELS_OVER_CCB_ENV_VAR) ?
            CONTROL_PROTOCOL__CCB_BOUNDARY_CHANNEL : CONTROL_PROTOCOL__DESC_BOUNDARY_CHANNEL);
    }

    void set_nops_mapped_buffer(vdma::MappedBufferPtr nops_buffer)
    {
        m_nops_mapped_buffer = nops_buffer;
    }

    std::vector<ContextResources>& get_context_resources() {
        return m_contexts_resources;
    }

    uint16_t get_csm_buffer_size();

private:
    hailo_status fill_infer_features(CONTROL_PROTOCOL__application_header_t &app_header);
    hailo_status fill_validation_features(CONTROL_PROTOCOL__application_header_t &app_header);
    hailo_status fill_network_batch_size(CONTROL_PROTOCOL__application_header_t &app_header);
    void fill_config_channel_info(CONTROL_PROTOCOL__application_header_t &app_header);

    std::vector<ContextResources> m_contexts_resources;
    ChannelAllocator m_channel_allocator;
    VdmaDevice &m_vdma_device;
    HailoRTDriver &m_driver;
    const ConfigureNetworkParams m_config_params;
    CacheManagerPtr m_cache_manager;
    std::map<IntermediateBufferKey, std::unique_ptr<vdma::VdmaEdgeLayer>> m_intermediate_buffers;
    std::shared_ptr<CoreOpMetadata> m_core_op_metadata;
    uint8_t m_core_op_index;
    uint16_t m_dynamic_context_count;
    uint16_t m_total_context_count;
    const std::vector<std::string> m_network_index_map;
    LatencyMetersMap m_latency_meters; // Latency meter per network
    vdma::ChannelsGroup m_boundary_channels;
    bool m_is_configured;
    bool m_is_activated;
    std::vector<vdma::MappedBufferPtr> m_ccws_section_mapped_buffers;
    vdma::MappedBufferPtr m_nops_mapped_buffer;
    std::shared_ptr<Buffer> m_hef_as_buffer;
    // Config channels ids are shared between all context. The following vector contains the channel id for each
    // config_stream_index.
    std::vector<vdma::ChannelId> m_config_channels_ids;
    // Mapped buffers would be used only in hw only flow
    std::map<vdma::ChannelId, std::shared_ptr<vdma::MappedBuffer>> m_hw_only_desc_boundary_buffers;
    // Use ccb buffer for hw only flow
    std::map<vdma::ChannelId, std::shared_ptr<vdma::ContinuousBuffer>> m_hw_only_ccb_boundary_buffers;
    std::shared_ptr<InternalBufferManager> m_internal_buffer_manager;
    std::shared_ptr<ActionListBufferBuilder> m_action_list_buffer_builder;
    CONTROL_PROTOCOL__hw_infer_channels_info_t m_hw_infer_channels_info;

    ResourcesManager(VdmaDevice &vdma_device, HailoRTDriver &driver,
        ChannelAllocator &&channel_allocator, const ConfigureNetworkParams config_params,
        CacheManagerPtr cache_manager,
        std::shared_ptr<CoreOpMetadata> &&core_op_metadata, uint8_t core_op_index,
        const std::vector<std::string> &&network_index_map, LatencyMetersMap &&latency_meters,
        std::vector<vdma::ChannelId> &&config_channels_ids,
        std::shared_ptr<InternalBufferManager> internal_buffer_manager,
        std::shared_ptr<ActionListBufferBuilder> &&action_list_buffer_builder);
};

} /* namespace hailort */

#endif /* _HAILO_CONTEXT_SWITCH_RESOURCE_MANAGER_HPP_ */
