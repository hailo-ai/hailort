#include "multi_context/resource_manager.hpp"
#include "control.hpp"
#include "hailort_defaults.hpp"
#include <numeric>

namespace hailort
{

static Expected<std::vector<std::string>> build_network_index_map(const ProtoHEFNetworkGroup &network_group_proto,
    const NetworkGroupSupportedFeatures &supported_features)
{
    std::vector<std::string> network_names_vector;
    if (supported_features.multi_network_support) {
        auto network_count = network_group_proto.networks_names_size();
        CHECK_AS_EXPECTED((network_count > 0), HAILO_INTERNAL_FAILURE, 
            "Hef support multiple networks, but no networks found in the proto");
        network_names_vector.reserve(network_count);
        for (uint8_t network_index = 0; network_index < network_count; network_index++) {
            auto partial_network_name = network_group_proto.networks_names(network_index);
            auto network_name = HefUtils::get_network_name(network_group_proto, partial_network_name);
            network_names_vector.push_back(network_name);
        }
    } else {
        /* In case there is no defines networks, add single network with the same name as the network group */
        network_names_vector.reserve(1);
        auto net_group_name = network_group_proto.network_group_metadata().network_group_name();
        network_names_vector.push_back(HailoRTDefaults::get_network_name(net_group_name));
    }

    return network_names_vector;
}

static Expected<LatencyMeterPtr> create_hw_latency_meter(const std::vector<LayerInfo> &layers)
{
    std::set<uint32_t> d2h_channel_indexes;

    size_t h2d_streams_count = 0;
    for (const auto &layer : layers) {
        if (layer.direction == HAILO_D2H_STREAM) {
            if (HAILO_FORMAT_ORDER_HAILO_NMS == layer.format.order) {
                LOGGER__WARNING("HW Latency measurement is not supported on NMS networks");
                return make_unexpected(HAILO_INVALID_OPERATION);
            }

            d2h_channel_indexes.insert(layer.stream_index);
        }
        else {
            h2d_streams_count++;
        }
    }

    if (h2d_streams_count > 1) {
        LOGGER__WARNING("HW Latency measurement is supported on networks with a single input");
        return make_unexpected(HAILO_INVALID_OPERATION);
    }

    return make_shared_nothrow<LatencyMeter>(d2h_channel_indexes, MAX_IRQ_TIMESTAMPS_SIZE);
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
    const ConfigureNetworkParams &config_params, const ProtoHEFNetworkGroup &network_group_proto,
    std::shared_ptr<NetworkGroupMetadata> network_group_metadata, const HefParsingInfo &parsing_info,
    uint8_t net_group_index)
{
    const auto &proto_metadata = network_group_proto.network_group_metadata();

    // Backwards compatibility for HEFs without this field
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(proto_metadata.cfg_channels_count()),
        HAILO_INVALID_HEF, "Invalid cfg channels count");
    uint8_t cfg_channels_count = (0 == proto_metadata.cfg_channels_count()) ?
        1u : static_cast<uint8_t>(proto_metadata.cfg_channels_count());

    // Allocate config channels. In order to use the same channel ids for config channels in all contexts,
    // we allocate all of them here, and use in preliminary/dynamic context.
    ChannelAllocator allocator(driver.dma_engines_count());
    std::vector<vdma::ChannelId> cfg_channel_ids;
    cfg_channel_ids.reserve(cfg_channels_count);
    for (uint8_t cfg_index = 0; cfg_index < cfg_channels_count; cfg_index++) {
        const auto cfg_layer_identifier = std::make_tuple(LayerType::CFG, "", cfg_index);
        const auto engine_index = get_cfg_channel_engine(driver, cfg_index, proto_metadata);
        CHECK_EXPECTED(engine_index);
        auto channel_id = allocator.get_available_channel_id(cfg_layer_identifier, VdmaChannel::Direction::H2D,
            engine_index.value());
        CHECK_EXPECTED(channel_id);
        cfg_channel_ids.push_back(channel_id.release());
    }

    std::vector<ConfigBuffer> preliminary_configs_vector;
    auto cfg_count_preliminary = parsing_info.cfg_infos_preliminary_config.size();
    CHECK_AS_EXPECTED(cfg_channels_count >= cfg_count_preliminary, HAILO_INTERNAL_FAILURE,
        "preliminary cfg count ({}) is bigger than the size passed to the network_group ({})",
        cfg_count_preliminary, cfg_channels_count);
    preliminary_configs_vector.reserve(cfg_count_preliminary);
    for (uint8_t cfg_index = MIN_H2D_CHANNEL_INDEX; cfg_index < cfg_count_preliminary; cfg_index++) {
        CHECK_AS_EXPECTED(contains(parsing_info.cfg_infos_preliminary_config, cfg_index), HAILO_INTERNAL_FAILURE,
            "Mismatch for cfg index {}", cfg_index);
        auto buffer_resource = ConfigBuffer::create(driver, cfg_channel_ids[cfg_index],
            parsing_info.cfg_infos_preliminary_config.at(cfg_index));
        CHECK_EXPECTED(buffer_resource);

        preliminary_configs_vector.emplace_back(buffer_resource.release());
    }

    std::vector<std::vector<ConfigBuffer>> dynamic_cfg_vectors;
    dynamic_cfg_vectors.reserve(network_group_proto.contexts_size());

    for (int ctxt_index = 0; ctxt_index < network_group_proto.contexts_size(); ctxt_index++) {
        std::vector<ConfigBuffer> dynamic_cfg_vector_per_context;
        auto cfg_count_ctxt = parsing_info.cfg_infos_per_context[ctxt_index].size();

        CHECK_AS_EXPECTED(cfg_channels_count >= cfg_count_ctxt, HAILO_INTERNAL_FAILURE,
            "dynamic context cfg count ({}) (context {}) is bigger than the size passed to the network_group ({})",
            cfg_count_ctxt, ctxt_index, cfg_channels_count);

        dynamic_cfg_vector_per_context.reserve(cfg_count_ctxt);
        for (uint8_t cfg_index = MIN_H2D_CHANNEL_INDEX; cfg_index < cfg_count_ctxt; cfg_index++) {
            CHECK_AS_EXPECTED(contains(parsing_info.cfg_infos_per_context[ctxt_index], cfg_index),
                HAILO_INTERNAL_FAILURE, "Mismatch for cfg index {}", cfg_index);
            auto buffer_resource = ConfigBuffer::create(driver, cfg_channel_ids[cfg_index],
                parsing_info.cfg_infos_per_context[ctxt_index].at(cfg_index));
            CHECK_EXPECTED(buffer_resource);

            dynamic_cfg_vector_per_context.emplace_back(buffer_resource.release());
        }
        dynamic_cfg_vectors.emplace_back(std::move(dynamic_cfg_vector_per_context));
    }

    auto network_index_map = build_network_index_map(network_group_proto, network_group_metadata->supported_features());
    CHECK_EXPECTED(network_index_map);

    auto latency_meters = create_latency_meters_from_config_params(config_params, network_group_metadata);
    CHECK_EXPECTED(latency_meters);
    ResourcesManager resources_manager(vdma_device, driver, std::move(allocator), config_params,
        std::move(preliminary_configs_vector), std::move(dynamic_cfg_vectors), std::move(network_group_metadata), net_group_index,
        std::move(network_index_map.release()), latency_meters.release());

    return resources_manager;
}

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

hailo_status ResourcesManager::create_fw_managed_vdma_channels()
{
    auto fw_managed_channel_ids = m_channel_allocator.get_fw_managed_channel_ids();

    m_fw_managed_channels.reserve(fw_managed_channel_ids.size());
    for (const auto &ch : fw_managed_channel_ids) {
        auto direction = (ch.channel_index < MIN_D2H_CHANNEL_INDEX) ? VdmaChannel::Direction::H2D : VdmaChannel::Direction::D2H;
        auto vdma_channel = VdmaChannel::create(ch, direction, m_driver, m_vdma_device.get_default_desc_page_size());
        CHECK_EXPECTED_AS_STATUS(vdma_channel);
        m_fw_managed_channels.emplace_back(vdma_channel.release());
    }

    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<VdmaChannel>> ResourcesManager::create_boundary_vdma_channel(
    const vdma::ChannelId &channel_id, uint32_t transfer_size, const std::string &network_name,
    const std::string &stream_name, VdmaChannel::Direction channel_direction)
{
    auto network_batch_size = get_network_batch_size(network_name);
    CHECK_EXPECTED(network_batch_size);
    uint32_t min_active_trans = MIN_ACTIVE_TRANSFERS_SCALE * network_batch_size.value();
    uint32_t max_active_trans = MAX_ACTIVE_TRANSFERS_SCALE * network_batch_size.value();

    CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(min_active_trans), HAILO_INVALID_ARGUMENT, 
        "calculated min_active_trans for vdma descriptor list is out of UINT16 range");
 
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(max_active_trans), HAILO_INVALID_ARGUMENT, 
        "calculated min_active_trans for vdma descriptor list is out of UINT16 range");
 
    auto edge_layer = m_network_group_metadata->get_layer_info_by_stream_name(stream_name);
    CHECK_EXPECTED(edge_layer);
    auto latency_meter = (contains(m_latency_meters, edge_layer->network_name)) ? m_latency_meters.at(edge_layer->network_name) : nullptr;
    auto stream_index = edge_layer.value().stream_index;

    /* TODO - HRT-6829- page_size should be calculated inside the vDMA channel class create function */
    auto desc_sizes_pair = VdmaDescriptorList::get_desc_buffer_sizes_for_single_transfer(m_driver,
        static_cast<uint16_t>(min_active_trans), static_cast<uint16_t>(max_active_trans), transfer_size);
    CHECK_EXPECTED(desc_sizes_pair);

    const auto page_size = desc_sizes_pair->first;
    const auto descs_count = desc_sizes_pair->second;
    auto channel = VdmaChannel::create(channel_id, channel_direction, m_driver, page_size,
        stream_index, latency_meter, network_batch_size.value());
    CHECK_EXPECTED(channel);
    const auto status = channel->allocate_resources(descs_count);
    CHECK_SUCCESS_AS_EXPECTED(status);

    auto channel_ptr = make_shared_nothrow<VdmaChannel>(channel.release());
    CHECK_NOT_NULL_AS_EXPECTED(channel_ptr, HAILO_OUT_OF_HOST_MEMORY);

    m_boundary_channels.emplace(stream_name, channel_ptr);

    return channel_ptr;
}

Expected<std::shared_ptr<VdmaChannel>> ResourcesManager::get_boundary_vdma_channel_by_stream_name(const std::string &stream_name)
{
    auto boundary_channel_it = m_boundary_channels.find(stream_name);
    if (std::end(m_boundary_channels) == boundary_channel_it) {
        return make_unexpected(HAILO_NOT_FOUND);
    }

    return std::shared_ptr<VdmaChannel>(m_boundary_channels[stream_name]);
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
    CONTROL_PROTOCOL__application_header_t  app_header{};
    app_header.dynamic_contexts_count = static_cast<uint8_t>(m_contexts.size() - 1);

    /* Bitmask of all boundary channels (per engine) */
    std::array<int, CONTROL_PROTOCOL__MAX_VDMA_ENGINES_COUNT> host_boundary_channels_bitmap{};

    for (const auto &ch : m_channel_allocator.get_boundary_channel_ids()) {
        CHECK_AS_EXPECTED(ch.engine_index < host_boundary_channels_bitmap.size(), HAILO_INTERNAL_FAILURE,
            "Invalid engine index {}", static_cast<uint32_t>(ch.engine_index));

        host_boundary_channels_bitmap[ch.engine_index] |= (1 << ch.channel_index);
    }

    std::copy(host_boundary_channels_bitmap.begin(), host_boundary_channels_bitmap.end(),
        app_header.host_boundary_channels_bitmap);

    app_header.power_mode = static_cast<uint8_t>(m_config_params.power_mode);
    auto status = fill_infer_features(app_header);
    CHECK_SUCCESS_AS_EXPECTED(status, "Invalid infer features");
    status = fill_validation_features(app_header);
    CHECK_SUCCESS_AS_EXPECTED(status, "Invalid validation features");
    status = fill_network_batch_size(app_header);
    CHECK_SUCCESS_AS_EXPECTED(status, "Invalid network batch sizes");

    return app_header;
}

std::vector<std::reference_wrapper<const DdrChannelsPair>>
ResourcesManager::get_ddr_channel_pairs_per_context(uint8_t context_index) const
{
    std::vector<std::reference_wrapper<const DdrChannelsPair>> ddr_channels_pairs;
    for (auto &ddr_channels_pair : m_ddr_channels_pairs) {
        if (ddr_channels_pair.first.first == context_index) {
            ddr_channels_pairs.push_back(std::ref(ddr_channels_pair.second));
        }
    }

    return ddr_channels_pairs;
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

void ResourcesManager::update_preliminary_config_buffer_info()
{
    // Preliminary_config is the first 'context' m_contexts vector
    update_config_buffer_info(m_preliminary_config, m_contexts[0]);
}

void ResourcesManager::update_dynamic_contexts_buffer_info()
{
    // Preliminary_config is the first 'context' m_contexts vector
    assert((m_dynamic_config.size() + 1) == m_contexts.size());
    int ctxt_index = 1;
    for (auto &cfg_context : m_dynamic_config) {
        update_config_buffer_info(cfg_context, m_contexts[ctxt_index]);
        ctxt_index++;
    }
}

ExpectedRef<DdrChannelsPair> ResourcesManager::create_ddr_channels_pair(const DdrChannelsInfo &ddr_info, uint8_t context_index)
{
    auto buffer = DdrChannelsPair::create(m_driver, ddr_info);
    CHECK_EXPECTED(buffer);

    const auto key = std::make_pair(context_index, ddr_info.d2h_stream_index);
    auto emplace_res = m_ddr_channels_pairs.emplace(key, buffer.release());
    return std::ref(emplace_res.first->second);
}

ExpectedRef<DdrChannelsPair> ResourcesManager::get_ddr_channels_pair(uint8_t context_index, uint8_t d2h_stream_index)
{
    const auto key = std::make_pair(context_index, d2h_stream_index);
    auto ddr_channels_pair = m_ddr_channels_pairs.find(key);
    
    if (m_ddr_channels_pairs.end() == ddr_channels_pair) {
        return make_unexpected(HAILO_NOT_FOUND);
    }

    return std::ref(ddr_channels_pair->second);
}

Expected<hailo_stream_interface_t> ResourcesManager::get_default_streams_interface()
{
    return m_vdma_device.get_default_streams_interface();
}

hailo_status ResourcesManager::register_fw_managed_vdma_channels()
{
    hailo_status status = HAILO_UNINITIALIZED;

    for (auto &ch : m_fw_managed_channels) {
        status = ch.register_fw_controlled_channel();
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status ResourcesManager::unregister_fw_managed_vdma_channels()
{
    hailo_status status = HAILO_UNINITIALIZED;

    // TODO: Add one icotl to stop all channels at once (HRT-6097)
    for (auto &ch : m_fw_managed_channels) {
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
    for (auto const &network_map: m_config_params.network_params_by_name) {
        auto const network_name_from_params = network_map.first;
        if (network_name_from_params == network_name) {
            return Expected<uint16_t>(network_map.second.batch_size);
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

    auto ddr_channels_pair_it = m_ddr_channels_pairs.find(key);
    if (std::end(m_ddr_channels_pairs) != ddr_channels_pair_it) {
        return ddr_channels_pair_it->second.read();
    }

    LOGGER__ERROR("Failed to find intermediate buffer for src_context {}, src_stream_index {}", key.first,
        key.second);
    return make_unexpected(HAILO_NOT_FOUND);

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

void ResourcesManager::update_config_buffer_info(std::vector<ConfigBuffer> &config_buffers,
    CONTROL_PROTOCOL__context_switch_context_info_t &context)
{
    assert(CONTROL_PROTOCOL__MAX_CFG_CHANNELS >= config_buffers.size());
    context.cfg_channels_count = static_cast<uint8_t>(config_buffers.size());

    auto i = 0;
    for (const auto &config : config_buffers) {
        context.config_channel_infos[i] = config.get_config_channel_info();
        i++;
    }
}

Expected<uint8_t> ResourcesManager::get_cfg_channel_engine(HailoRTDriver &driver, uint8_t config_stream_index,
    const ProtoHEFNetworkGroupMetadata &proto_metadata)
{
    if (driver.dma_type() == HailoRTDriver::DmaType::PCIE) {
        // On PCIe we have only 1 engine. To support the same HEF with both PCIe and DRAM, we use default engine here.
        return uint8_t(vdma::DEFAULT_ENGINE_INDEX);
    }

    const auto &cfg_channels_config = proto_metadata.cfg_channels_config();
    if (cfg_channels_config.empty()) {
        // Old hef, return default value
        return uint8_t(vdma::DEFAULT_ENGINE_INDEX);
    }

    auto cfg_info = std::find_if(cfg_channels_config.begin(), cfg_channels_config.end(),
        [config_stream_index](const auto &cfg_info)
        {
            return cfg_info.cfg_channel_index() == config_stream_index;
        });
    CHECK_AS_EXPECTED(cfg_info != cfg_channels_config.end(), HAILO_INVALID_HEF, "Cfg channel {} not found", config_stream_index);
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(cfg_info->engine_id()), HAILO_INVALID_HEF, "Invalid dma engine index");
    return static_cast<uint8_t>(cfg_info->engine_id());
}

} /* namespace hailort */
