#include "multi_context/resource_manager.hpp"
#include "control.hpp"
#include "hailort_defaults.hpp"
#include <numeric>

namespace hailort
{


Expected<ContextResources> ContextResources::create(HailoRTDriver &driver,
    CONTROL_PROTOCOL__context_switch_context_type_t context_type, const std::vector<vdma::ChannelId> &config_channels_ids,
    const ConfigBufferInfoMap &config_buffer_infos)
{
    CHECK_AS_EXPECTED(context_type < CONTROL_PROTOCOL__CONTEXT_SWITCH_CONTEXT_TYPE_COUNT, HAILO_INVALID_ARGUMENT);

    CHECK_AS_EXPECTED(config_buffer_infos.size() <= config_channels_ids.size(), HAILO_INTERNAL_FAILURE,
        "config_buffer_infos size ({}) is bigger than config_channels_id count  ({})",
        config_buffer_infos.size(), config_channels_ids.size());

    std::vector<ConfigBuffer> config_buffers;
    config_buffers.reserve(config_buffer_infos.size());
    for (uint8_t config_stream_index = 0; config_stream_index < config_buffer_infos.size(); config_stream_index++) {
        auto buffer_resource = ConfigBuffer::create(driver, config_channels_ids[config_stream_index],
            config_buffer_infos.at(config_stream_index));
        CHECK_EXPECTED(buffer_resource);
        config_buffers.emplace_back(buffer_resource.release());
    }

    return ContextResources(driver, context_type, std::move(config_buffers));
}

const std::vector<CONTROL_PROTOCOL__context_switch_context_info_single_control_t> &ContextResources::get_controls() const
{
    return m_builder.get_controls();
}

ContextSwitchBufferBuilder &ContextResources::builder()
{
    return m_builder;
}

const std::vector<BoundaryEdgeLayer> &ContextResources::get_boundary_layers() const
{
    return m_boundary_layers;
}

const std::vector<InterContextEdgeLayer> &ContextResources::get_inter_context_layers() const
{
    return m_inter_context_layers;
}

const std::vector<DdrChannelEdgeLayer> &ContextResources::get_ddr_channel_layers() const
{
    return m_ddr_channel_layers;
}

ExpectedRef<DdrChannelsPair> ContextResources::create_ddr_channels_pair(const DdrChannelsInfo &ddr_info)
{
    auto buffer = DdrChannelsPair::create(m_driver, ddr_info);
    CHECK_EXPECTED(buffer);

    m_ddr_channels_pairs.emplace_back(buffer.release());
    return std::ref(m_ddr_channels_pairs.back());
}

ExpectedRef<const DdrChannelsPair> ContextResources::get_ddr_channels_pair(uint8_t d2h_stream_index) const
{
    for (auto &ddr_channels_pair : m_ddr_channels_pairs) {
        if (ddr_channels_pair.info().d2h_stream_index == d2h_stream_index) {
            return std::ref(ddr_channels_pair);
        }
    }

    LOGGER__ERROR("Couldn't find ddr channels pair for {}", d2h_stream_index);
    return make_unexpected(HAILO_INTERNAL_FAILURE);
}

const std::vector<DdrChannelsPair> &ContextResources::get_ddr_channels_pairs() const
{
    return m_ddr_channels_pairs;
}

std::vector<BoundaryEdgeLayer> ContextResources::get_boundary_layers(hailo_stream_direction_t direction) const
{
    std::vector<BoundaryEdgeLayer> edge_layers;
    for (const auto &edge_layer : m_boundary_layers) {
        if (edge_layer.layer_info.direction == direction) {
            edge_layers.push_back(edge_layer);
        }
    }
    return edge_layers;
}

std::vector<InterContextEdgeLayer> ContextResources::get_inter_context_layers(hailo_stream_direction_t direction) const
{
    std::vector<InterContextEdgeLayer> edge_layers;
    for (const auto &edge_layer : m_inter_context_layers) {
        if (edge_layer.layer_info.direction == direction) {
            edge_layers.push_back(edge_layer);
        }
    }
    return edge_layers;
}

std::vector<DdrChannelEdgeLayer> ContextResources::get_ddr_channel_layers(hailo_stream_direction_t direction) const
{
    std::vector<DdrChannelEdgeLayer> edge_layers;
    for (const auto &edge_layer : m_ddr_channel_layers) {
        if (edge_layer.layer_info.direction == direction) {
            edge_layers.push_back(edge_layer);
        }
    }
    return edge_layers;
}

hailo_status ContextResources::validate_edge_layers()
{
    std::set<vdma::ChannelId> used_channel_ids;
    for (const auto &edge_layer : get_boundary_layers()) {
        CHECK(used_channel_ids.find(edge_layer.channel_id) == used_channel_ids.end(), HAILO_INTERNAL_FAILURE,
            "Same stream use the same channel id {}", edge_layer.channel_id);
        used_channel_ids.insert(edge_layer.channel_id);
    }

    for (const auto &edge_layer : get_inter_context_layers()) {
        CHECK(used_channel_ids.find(edge_layer.channel_id) == used_channel_ids.end(), HAILO_INTERNAL_FAILURE,
            "Same stream use the same channel id {}", edge_layer.channel_id);
        used_channel_ids.insert(edge_layer.channel_id);
    }

    for (const auto &edge_layer : get_ddr_channel_layers()) {
        CHECK(used_channel_ids.find(edge_layer.channel_id) == used_channel_ids.end(), HAILO_INTERNAL_FAILURE,
            "Same stream use the same channel id {}", edge_layer.channel_id);
        used_channel_ids.insert(edge_layer.channel_id);
    }

    return HAILO_SUCCESS;
}

std::vector<ConfigBuffer> &ContextResources::get_config_buffers()
{
    return m_config_buffers;
}

static Expected<LatencyMeterPtr> create_hw_latency_meter(const std::vector<LayerInfo> &layers)
{
    std::set<std::string> d2h_channel_names;

    size_t h2d_streams_count = 0;
    for (const auto &layer : layers) {
        if (layer.direction == HAILO_D2H_STREAM) {
            if (HAILO_FORMAT_ORDER_HAILO_NMS == layer.format.order) {
                LOGGER__WARNING("HW Latency measurement is not supported on NMS networks");
                return make_unexpected(HAILO_INVALID_OPERATION);
            }

            d2h_channel_names.insert(layer.name);
        }
        else {
            h2d_streams_count++;
        }
    }

    if (h2d_streams_count > 1) {
        LOGGER__WARNING("HW Latency measurement is supported on networks with a single input");
        return make_unexpected(HAILO_INVALID_OPERATION);
    }

    return make_shared_nothrow<LatencyMeter>(d2h_channel_names, MAX_IRQ_TIMESTAMPS_SIZE);
}

static Expected<LatencyMetersMap> create_latency_meters_from_config_params( 
    const ConfigureNetworkParams &config_params, std::shared_ptr<NetworkGroupMetadata> network_group_metadata)
{
    LatencyMetersMap latency_meters_map; 

    if ((config_params.latency & HAILO_LATENCY_MEASURE) == HAILO_LATENCY_MEASURE) {
        // Best affort for starting latency meter.
        auto networks_names = network_group_metadata->get_network_names();
        for (auto &network_name : networks_names) {
            auto layer_infos = network_group_metadata->get_all_layer_infos(network_name);
            CHECK_EXPECTED(layer_infos);
            auto latency_meter = create_hw_latency_meter(layer_infos.value());
            if (latency_meter) {
                latency_meters_map.emplace(network_name, latency_meter.release());
                LOGGER__DEBUG("Starting hw latency measurement for network {}", network_name);
            }
        }
    }

    return latency_meters_map;
}

Expected<ResourcesManager> ResourcesManager::create(VdmaDevice &vdma_device, HailoRTDriver &driver,
    const ConfigureNetworkParams &config_params, std::shared_ptr<NetworkGroupMetadata> network_group_metadata,
    uint8_t net_group_index)
{
    // Allocate config channels. In order to use the same channel ids for config channels in all contexts,
    // we allocate all of them here, and use in preliminary/dynamic context.
    ChannelAllocator allocator(driver.dma_engines_count());
    std::vector<vdma::ChannelId> config_channels_ids;
    const auto &config_channels_info = network_group_metadata->config_channels_info();
    config_channels_ids.reserve(config_channels_info.size());
    for (uint8_t cfg_index = 0; cfg_index < config_channels_info.size(); cfg_index++) {
        const auto layer_identifier = std::make_tuple(LayerType::CFG, "", cfg_index);
        const auto engine_index = config_channels_info[cfg_index].engine_index;
        auto channel_id = allocator.get_available_channel_id(layer_identifier, VdmaChannel::Direction::H2D, engine_index);
        CHECK_EXPECTED(channel_id);
        config_channels_ids.push_back(channel_id.release());
    }

    auto network_index_map = network_group_metadata->get_network_names();

    auto latency_meters = create_latency_meters_from_config_params(config_params, network_group_metadata);
    CHECK_EXPECTED(latency_meters);
    ResourcesManager resources_manager(vdma_device, driver, std::move(allocator), config_params,
        std::move(network_group_metadata), net_group_index,
        std::move(network_index_map), latency_meters.release(), std::move(config_channels_ids));

    return resources_manager;
}

ResourcesManager::ResourcesManager(VdmaDevice &vdma_device, HailoRTDriver &driver,
                                   ChannelAllocator &&channel_allocator, const ConfigureNetworkParams config_params,
                                   std::shared_ptr<NetworkGroupMetadata> &&network_group_metadata,
                                   uint8_t net_group_index, const std::vector<std::string> &&network_index_map,
                                   LatencyMetersMap &&latency_meters,
                                   std::vector<vdma::ChannelId> &&config_channels_ids) :
    m_contexts_resources(),
    m_channel_allocator(std::move(channel_allocator)),
    m_vdma_device(vdma_device),
    m_driver(driver),
    m_config_params(config_params),
    m_inter_context_buffers(),
    m_internal_channels(),
    m_network_group_metadata(std::move(network_group_metadata)),
    m_net_group_index(net_group_index),
    m_dynamic_context_count(0),
    m_total_context_count(0),
    m_network_index_map(std::move(network_index_map)),
    m_latency_meters(std::move(latency_meters)),
    m_boundary_channels(),
    m_is_configured(false),
    m_config_channels_ids(std::move(config_channels_ids))
{}

ResourcesManager::ResourcesManager(ResourcesManager &&other) noexcept :
    m_contexts_resources(std::move(other.m_contexts_resources)),
    m_channel_allocator(std::move(other.m_channel_allocator)),
    m_vdma_device(other.m_vdma_device),
    m_driver(other.m_driver),
    m_config_params(other.m_config_params),
    m_inter_context_buffers(std::move(other.m_inter_context_buffers)),
    m_internal_channels(std::move(other.m_internal_channels)),
    m_network_group_metadata(std::move(other.m_network_group_metadata)),
    m_net_group_index(other.m_net_group_index),
    m_dynamic_context_count(std::exchange(other.m_dynamic_context_count, static_cast<uint8_t>(0))),
    m_total_context_count(std::exchange(other.m_total_context_count, static_cast<uint8_t>(0))),
    m_network_index_map(std::move(other.m_network_index_map)),
    m_latency_meters(std::move(other.m_latency_meters)),
    m_boundary_channels(std::move(other.m_boundary_channels)),
    m_is_configured(std::exchange(other.m_is_configured, false)),
    m_config_channels_ids(std::move(other.m_config_channels_ids))
{}

hailo_status ResourcesManager::fill_infer_features(CONTROL_PROTOCOL__application_header_t &app_header)
{
    app_header.infer_features.preliminary_run_asap = m_network_group_metadata->supported_features().preliminary_run_asap;
    return HAILO_SUCCESS;
}


hailo_status ResourcesManager::fill_validation_features(CONTROL_PROTOCOL__application_header_t &app_header)
{
    static const auto ABBALE_NOT_SUPPORTED = false;
    // TODO: fix is_abbale_supported
    // auto proto_message = hef.pimpl.proto_message();
    // auto has_included_features = proto_message->has_included_features();
    // if (has_included_features) {
    //     is_abbale_supported = proto_message->included_features().abbale();
    // }
    app_header.validation_features.is_abbale_supported = ABBALE_NOT_SUPPORTED;
    return HAILO_SUCCESS;
}

hailo_status ResourcesManager::fill_network_batch_size(CONTROL_PROTOCOL__application_header_t &app_header)
{
    app_header.networks_count = static_cast<uint8_t>(m_config_params.network_params_by_name.size());
    for (const auto &network_pair : m_config_params.network_params_by_name) {
        auto network_name_from_params = network_pair.first;
        uint8_t network_index = 0;
        for (network_index = 0; network_index < m_network_index_map.size(); network_index++) {
            auto const network_name_from_map = m_network_index_map[network_index];
            if (network_name_from_map == network_name_from_params) {
                auto batch_size = get_network_batch_size(network_name_from_params);
                CHECK_EXPECTED_AS_STATUS(batch_size);
                app_header.batch_size[network_index] = batch_size.value();
                break;
            }
        }
        if (m_network_index_map.size() == network_index) {
            LOGGER__ERROR("Failed to find network with network name {}", network_name_from_params);
            return HAILO_NOT_FOUND;
        }
    }

    return HAILO_SUCCESS;
}

hailo_status ResourcesManager::create_internal_vdma_channels()
{
    auto internal_channel_ids = m_channel_allocator.get_internal_channel_ids();

    m_internal_channels.reserve(internal_channel_ids.size());
    for (const auto &ch : internal_channel_ids) {
        auto direction = (ch.channel_index < MIN_D2H_CHANNEL_INDEX) ? VdmaChannel::Direction::H2D : VdmaChannel::Direction::D2H;
        auto vdma_channel = VdmaChannel::create(ch, direction, m_driver, m_vdma_device.get_default_desc_page_size());
        CHECK_EXPECTED_AS_STATUS(vdma_channel);
        m_internal_channels.emplace_back(vdma_channel.release());
    }

    return HAILO_SUCCESS;
}

hailo_status ResourcesManager::create_boundary_vdma_channel(const LayerInfo &layer_info)
{
    // TODO: put in layer info
    const auto channel_direction = layer_info.direction == HAILO_H2D_STREAM ? VdmaChannel::Direction::H2D :
                                                                              VdmaChannel::Direction::D2H;
    const auto channel_id = get_available_channel_id(to_layer_identifier(layer_info),
        channel_direction, layer_info.dma_engine_index);
    CHECK_EXPECTED_AS_STATUS(channel_id);

    auto network_batch_size = get_network_batch_size(layer_info.network_name);
    CHECK_EXPECTED_AS_STATUS(network_batch_size);

    uint32_t min_active_trans = MIN_ACTIVE_TRANSFERS_SCALE * network_batch_size.value();
    uint32_t max_active_trans = MAX_ACTIVE_TRANSFERS_SCALE * network_batch_size.value();

    CHECK(IS_FIT_IN_UINT16(min_active_trans), HAILO_INVALID_ARGUMENT, 
        "calculated min_active_trans for vdma descriptor list is out of UINT16 range");
    CHECK(IS_FIT_IN_UINT16(max_active_trans), HAILO_INVALID_ARGUMENT, 
        "calculated min_active_trans for vdma descriptor list is out of UINT16 range");

    auto latency_meter = (contains(m_latency_meters, layer_info.network_name)) ? m_latency_meters.at(layer_info.network_name) : nullptr;

    /* TODO - HRT-6829- page_size should be calculated inside the vDMA channel class create function */
    const auto transfer_size = (layer_info.nn_stream_config.periph_bytes_per_buffer * 
        layer_info.nn_stream_config.core_buffers_per_frame);
    auto desc_sizes_pair = VdmaDescriptorList::get_desc_buffer_sizes_for_single_transfer(m_driver,
        static_cast<uint16_t>(min_active_trans), static_cast<uint16_t>(max_active_trans), transfer_size);
    CHECK_EXPECTED_AS_STATUS(desc_sizes_pair);

    const auto page_size = desc_sizes_pair->first;
    const auto descs_count = desc_sizes_pair->second;
    auto channel = VdmaChannel::create(channel_id.value(), channel_direction, m_driver, page_size,
        layer_info.name, latency_meter, network_batch_size.value());
    CHECK_EXPECTED_AS_STATUS(channel);
    const auto status = channel->allocate_resources(descs_count);
    CHECK_SUCCESS(status);

    auto channel_ptr = make_shared_nothrow<VdmaChannel>(channel.release());
    CHECK_NOT_NULL(channel_ptr, HAILO_OUT_OF_HOST_MEMORY);

    m_boundary_channels.emplace(layer_info.name, channel_ptr);
    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<VdmaChannel>> ResourcesManager::get_boundary_vdma_channel_by_stream_name(const std::string &stream_name)
{
    auto boundary_channel_it = m_boundary_channels.find(stream_name);
    if (std::end(m_boundary_channels) == boundary_channel_it) {
        return make_unexpected(HAILO_NOT_FOUND);
    }

    return std::shared_ptr<VdmaChannel>(boundary_channel_it->second);
}

Expected<std::shared_ptr<const VdmaChannel>> ResourcesManager::get_boundary_vdma_channel_by_stream_name(const std::string &stream_name) const
{
    auto boundary_channel_it = m_boundary_channels.find(stream_name);
    if (std::end(m_boundary_channels) == boundary_channel_it) {
        return make_unexpected(HAILO_NOT_FOUND);
    }

    return std::shared_ptr<const VdmaChannel>(boundary_channel_it->second);
}

hailo_power_mode_t ResourcesManager::get_power_mode() const
{
    return m_config_params.power_mode;
}

ExpectedRef<InterContextBuffer> ResourcesManager::create_inter_context_buffer(uint32_t transfer_size,
    uint8_t src_stream_index, uint8_t src_context_index, const std::string &network_name)
{
    auto network_batch_size_exp = get_network_batch_size(network_name);
    CHECK_EXPECTED(network_batch_size_exp);
    auto network_batch_size = network_batch_size_exp.value();

    auto buffer = InterContextBuffer::create(m_driver, transfer_size, network_batch_size);
    CHECK_EXPECTED(buffer);

    const auto key = std::make_pair(src_context_index, src_stream_index);
    auto emplace_res = m_inter_context_buffers.emplace(key, buffer.release());
    return std::ref(emplace_res.first->second);
}

ExpectedRef<InterContextBuffer> ResourcesManager::get_inter_context_buffer(const IntermediateBufferKey &key)
{
    auto buffer_it = m_inter_context_buffers.find(key);
    if (std::end(m_inter_context_buffers) == buffer_it) {
        return make_unexpected(HAILO_NOT_FOUND);
    }

    return std::ref(buffer_it->second);
}

Expected<CONTROL_PROTOCOL__application_header_t> ResourcesManager::get_control_network_group_header()
{
    CONTROL_PROTOCOL__application_header_t app_header{};
    app_header.dynamic_contexts_count = m_dynamic_context_count;

    auto status = fill_infer_features(app_header);
    CHECK_SUCCESS_AS_EXPECTED(status, "Invalid infer features");
    status = fill_validation_features(app_header);
    CHECK_SUCCESS_AS_EXPECTED(status, "Invalid validation features");
    status = fill_network_batch_size(app_header);
    CHECK_SUCCESS_AS_EXPECTED(status, "Invalid network batch sizes");

    return app_header;
}

Expected<std::reference_wrapper<ContextResources>> ResourcesManager::add_new_context(CONTROL_PROTOCOL__context_switch_context_type_t type,
    const ConfigBufferInfoMap &config_info)
{
    CHECK_AS_EXPECTED(m_total_context_count < std::numeric_limits<uint8_t>::max(), HAILO_INVALID_CONTEXT_COUNT);

    auto context_resources = ContextResources::create(m_driver, type, m_config_channels_ids, config_info);
    CHECK_EXPECTED(context_resources);

    m_contexts_resources.emplace_back(context_resources.release());
    m_total_context_count++;
    if (CONTROL_PROTOCOL__CONTEXT_SWITCH_CONTEXT_TYPE_DYNAMIC == type) {
        m_dynamic_context_count++;
    }

    return std::ref(m_contexts_resources.back());
}

Expected<vdma::ChannelId> ResourcesManager::get_available_channel_id(const LayerIdentifier &layer_identifier,
    VdmaChannel::Direction direction, uint8_t engine_index)
{
    if (m_driver.dma_type() == HailoRTDriver::DmaType::PCIE) {
        // On PCIe we have only 1 engine. To support the same HEF with both PCIe and DRAM, we use default engine here
        engine_index = vdma::DEFAULT_ENGINE_INDEX;
    }

    return m_channel_allocator.get_available_channel_id(layer_identifier, direction, engine_index);
}

hailo_status ResourcesManager::free_channel_index(const LayerIdentifier &layer_identifier)
{
    return m_channel_allocator.free_channel_index(layer_identifier);
}

Expected<hailo_stream_interface_t> ResourcesManager::get_default_streams_interface()
{
    return m_vdma_device.get_default_streams_interface();
}

hailo_status ResourcesManager::register_fw_managed_vdma_channels()
{
    hailo_status status = HAILO_UNINITIALIZED;

    for (auto &ch : m_internal_channels) {
        status = ch.register_fw_controlled_channel();
        CHECK_SUCCESS(status);
    }

    for (auto &ch : m_boundary_channels) {
        status = ch.second->register_fw_controlled_channel();
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status ResourcesManager::unregister_fw_managed_vdma_channels()
{
    hailo_status status = HAILO_UNINITIALIZED;

    // Note: We don't "unregister" the m_boundary_channels here, beacuse the Vdma*Stream objects will unregister their
    //       own channels.
    // TODO: Add one icotl to stop all channels at once (HRT-6097)
    for (auto &ch : m_internal_channels) {
        status = ch.unregister_fw_controlled_channel();
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status ResourcesManager::set_inter_context_channels_dynamic_batch_size(uint16_t dynamic_batch_size)
{
    for (auto &key_buff_pair : m_inter_context_buffers) {
        const auto status = key_buff_pair.second.reprogram(dynamic_batch_size);
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

Expected<uint16_t> ResourcesManager::get_network_batch_size(const std::string &network_name) const
{
    for (auto const &network_map : m_config_params.network_params_by_name) {
        auto const network_name_from_params = network_map.first;
        if (network_name_from_params == network_name) {
            auto actual_batch_size = network_map.second.batch_size;
            if (HAILO_DEFAULT_BATCH_SIZE == actual_batch_size) {
                actual_batch_size = DEFAULT_ACTUAL_BATCH_SIZE;
            }
            return actual_batch_size;
        }
    }

    LOGGER__ERROR("Failed to find network with network name {}", network_name);

    return make_unexpected(HAILO_NOT_FOUND);
}

Expected<Buffer> ResourcesManager::read_intermediate_buffer(const IntermediateBufferKey &key)
{
    auto inter_context_buffer_it = m_inter_context_buffers.find(key);
    if (std::end(m_inter_context_buffers) != inter_context_buffer_it) {
        return inter_context_buffer_it->second.read();
    }

    const auto dynamic_context_index = key.first;
    const size_t context_index = dynamic_context_index + CONTROL_PROTOCOL__CONTEXT_SWITCH_NUMBER_OF_NON_DYNAMIC_CONTEXTS;
    CHECK_AS_EXPECTED(context_index < m_contexts_resources.size(), HAILO_NOT_FOUND, "Context index {} out of range",
        dynamic_context_index);
    const auto d2h_stream_index = key.second;
    if (auto ddr_channels_pair = m_contexts_resources[context_index].get_ddr_channels_pair(d2h_stream_index)) {
        return ddr_channels_pair->get().read();
    }

    LOGGER__ERROR("Failed to find intermediate buffer for src_context {}, src_stream_index {}", key.first,
        key.second);
    return make_unexpected(HAILO_NOT_FOUND);

}

hailo_status ResourcesManager::configure()
{
    CHECK(!m_is_configured, HAILO_INTERNAL_FAILURE, "Can't configure the same network group twice");
    m_is_configured = true;

    auto net_group_header = get_control_network_group_header();
    CHECK_EXPECTED_AS_STATUS(net_group_header);

    auto status = Control::context_switch_set_network_group_header(m_vdma_device, net_group_header.release());
    CHECK_SUCCESS(status);

    for (const auto &context : m_contexts_resources) {
        status = Control::context_switch_set_context_info(m_vdma_device, context.get_controls());
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status ResourcesManager::enable_state_machine(uint16_t dynamic_batch_size)
{
    return Control::enable_network_group(m_vdma_device, m_net_group_index, dynamic_batch_size);
}

hailo_status ResourcesManager::reset_state_machine(bool keep_nn_config_during_reset)
{
    auto status = Control::reset_context_switch_state_machine(m_vdma_device, keep_nn_config_during_reset);
    CHECK_SUCCESS(status);

    if (!keep_nn_config_during_reset && (Device::Type::CORE == m_vdma_device.get_type())) {
        // On core device, the nn_manager is not responsible to reset the nn-core so
        // we use the SCU control for that.
        status = m_vdma_device.reset(HAILO_RESET_DEVICE_MODE_NN_CORE);
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

} /* namespace hailort */
