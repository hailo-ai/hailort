#include "multi_context/resource_manager.hpp"
#include "control.hpp"
#include "hailort_defaults.hpp"
#include <numeric>

namespace hailort
{

static Expected<std::vector<std::string>> build_network_index_map(ProtoHEFNetworkGroupPtr network_group_proto,
    const NetworkGroupSupportedFeatures &supported_features)
{
    std::vector<std::string> network_names_vector;
    if (supported_features.multi_network_support) {
        auto network_count = network_group_proto.get()->networks_names_size();
        CHECK_AS_EXPECTED((network_count > 0), HAILO_INTERNAL_FAILURE, 
            "Hef support multiple networks, but no networks found in the proto");
        network_names_vector.reserve(network_count);
        for (uint8_t network_index = 0; network_index < network_count; network_index++) {
            auto partial_network_name = network_group_proto.get()->networks_names(network_index);
            auto network_name = HefUtils::get_network_name(*network_group_proto, partial_network_name);
            network_names_vector.push_back(network_name);
        }
    } else {
        /* In case there is no defines networks, add single network with the same name as the network group */
        network_names_vector.reserve(1);
        auto net_group_name = network_group_proto->network_group_metadata().network_group_name();
        network_names_vector.push_back(HailoRTDefaults::get_network_name(net_group_name));
    }

    return network_names_vector;
}

Expected<ResourcesManager> ResourcesManager::create(VdmaDevice &vdma_device, HailoRTDriver &driver,
    const ConfigureNetworkParams &config_params, ProtoHEFNetworkGroupPtr network_group_proto,
    std::shared_ptr<NetworkGroupMetadata> network_group_metadata, const HefParsingInfo &parsing_info,
    uint8_t net_group_index)
{
    CHECK_ARG_NOT_NULL_AS_EXPECTED(network_group_proto);

    // Backwards compatibility for HEFs without this field
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(network_group_proto->network_group_metadata().cfg_channels_count()),
        HAILO_INTERNAL_FAILURE, "Invalid cfg channels count");
    uint8_t cfg_channels_count = (0 == network_group_proto->network_group_metadata().cfg_channels_count()) ?
        1u : static_cast<uint8_t>(network_group_proto->network_group_metadata().cfg_channels_count());

    std::vector<ConfigBuffer> preliminary_configs_vector;
    auto cfg_count_preliminary = parsing_info.cfg_infos_preliminary_config.size();
    CHECK_AS_EXPECTED(cfg_channels_count >= cfg_count_preliminary, HAILO_INTERNAL_FAILURE,
        "preliminary cfg count ({}) is bigger than the size passed to the network_group ({})",
        cfg_count_preliminary, cfg_channels_count);
    preliminary_configs_vector.reserve(cfg_count_preliminary);
    for (uint8_t cfg_index = MIN_H2D_CHANNEL_INDEX; cfg_index < cfg_count_preliminary; cfg_index++) {
        CHECK_AS_EXPECTED(contains(parsing_info.cfg_infos_preliminary_config, cfg_index), HAILO_INTERNAL_FAILURE,
            "Mismmatch for cfg index {}", cfg_index);
        auto buffer_resource = ConfigBuffer::create(driver, cfg_index,
            parsing_info.cfg_infos_preliminary_config.at(cfg_index));
        CHECK_EXPECTED(buffer_resource);

        preliminary_configs_vector.emplace_back(buffer_resource.release());
    }

    std::vector<std::vector<ConfigBuffer>> dynamic_cfg_vectors;
    dynamic_cfg_vectors.reserve(network_group_proto->contexts_size());

    for (int ctxt_index = 0; ctxt_index < network_group_proto->contexts_size(); ctxt_index++) {
        std::vector<ConfigBuffer> dynamic_cfg_vector_per_context;
        auto cfg_count_ctxt = parsing_info.cfg_infos_per_context[ctxt_index].size();

        CHECK_AS_EXPECTED(cfg_channels_count >= cfg_count_ctxt, HAILO_INTERNAL_FAILURE,
            "dynamic context cfg count ({}) (context {}) is bigger than the size passed to the network_group ({})",
            cfg_count_ctxt, ctxt_index, cfg_channels_count);

        dynamic_cfg_vector_per_context.reserve(cfg_count_ctxt);
        for (uint8_t cfg_index = MIN_H2D_CHANNEL_INDEX; cfg_index < cfg_count_ctxt; cfg_index++) {
            CHECK_AS_EXPECTED(contains(parsing_info.cfg_infos_per_context[ctxt_index], cfg_index),
                HAILO_INTERNAL_FAILURE, "Mismmatch for cfg index {}", cfg_index);
            auto buffer_resource = ConfigBuffer::create(driver, cfg_index,
                parsing_info.cfg_infos_per_context[ctxt_index].at(cfg_index));
            CHECK_EXPECTED(buffer_resource);

            dynamic_cfg_vector_per_context.emplace_back(buffer_resource.release());
        }
        dynamic_cfg_vectors.emplace_back(std::move(dynamic_cfg_vector_per_context));
    }

    auto network_index_map = build_network_index_map(network_group_proto, network_group_metadata->supported_features());
    CHECK_EXPECTED(network_index_map);
    ResourcesManager resources_manager(vdma_device, driver, config_params,
        std::move(preliminary_configs_vector), std::move(dynamic_cfg_vectors), std::move(network_group_metadata), net_group_index,
        std::move(network_index_map.release()));

    auto status = resources_manager.set_number_of_cfg_channels(cfg_channels_count);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return resources_manager;
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
                app_header.batch_size[network_index] = network_pair.second.batch_size;
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

hailo_status ResourcesManager::create_vdma_channels()
{
    std::vector<uint8_t> intermediate_channels_idx;
    std::vector<uint8_t> cfg_channels_idx;
    std::vector<uint8_t> ddr_channels_idx;

    for (uint8_t i = 0; i < m_channels_info.max_size(); ++i) {
        if (m_channels_info[i].is_type(ChannelInfo::Type::INTER_CONTEXT)) {
            intermediate_channels_idx.push_back(i);
        } else if (m_channels_info[i].is_type(ChannelInfo::Type::CFG)) {
            cfg_channels_idx.push_back(i);
        } else if (m_channels_info[i].is_type(ChannelInfo::Type::DDR)) {
            ddr_channels_idx.push_back(i);
        }
    }

    m_config_channels.reserve(cfg_channels_idx.size());
    m_inter_context_channels.reserve(intermediate_channels_idx.size());
    m_ddr_buffer_channels.reserve(ddr_channels_idx.size());

    for (const auto &ch : cfg_channels_idx) {
        auto config_channel = VdmaChannel::create(ch, VdmaChannel::Direction::H2D, m_driver,
            m_vdma_device.get_default_desc_page_size());
        CHECK_EXPECTED_AS_STATUS(config_channel);
        m_config_channels.emplace_back(config_channel.release());
    }

    for (const auto &ch : intermediate_channels_idx) {
        auto direction = (ch < MIN_D2H_CHANNEL_INDEX) ? VdmaChannel::Direction::H2D : VdmaChannel::Direction::D2H;
        auto vdma_channel = VdmaChannel::create(ch, direction, m_driver, m_vdma_device.get_default_desc_page_size());
        CHECK_EXPECTED_AS_STATUS(vdma_channel);
        m_inter_context_channels.emplace_back(vdma_channel.release());
    }

    for (const auto &ch : ddr_channels_idx) {
        auto direction = (ch < MIN_D2H_CHANNEL_INDEX) ? VdmaChannel::Direction::H2D : VdmaChannel::Direction::D2H;
        auto vdma_channel = VdmaChannel::create(ch, direction, m_driver, m_vdma_device.get_default_desc_page_size());
        CHECK_EXPECTED_AS_STATUS(vdma_channel);
        m_ddr_buffer_channels.emplace_back(vdma_channel.release());
    }

    return HAILO_SUCCESS;
}

ExpectedRef<IntermediateBuffer> ResourcesManager::create_inter_context_buffer(uint32_t transfer_size,
    uint8_t src_stream_index, uint8_t src_context_index, const std::string &network_name)
{
    auto network_batch_size_exp = get_network_batch_size(network_name);
    CHECK_EXPECTED(network_batch_size_exp);
    auto network_batch_size = network_batch_size_exp.value();

    const auto intermediate_buffer_key = std::make_pair(src_context_index, src_stream_index);
    auto intermediate_buffer = create_intermediate_buffer(transfer_size, network_batch_size, intermediate_buffer_key);
    CHECK_EXPECTED(intermediate_buffer);
    auto intermediate_buffer_ref = intermediate_buffer.release();

    auto status = intermediate_buffer_ref.get().program_inter_context();
    CHECK_SUCCESS_AS_EXPECTED(status);

    return intermediate_buffer_ref;
}

ExpectedRef<IntermediateBuffer> ResourcesManager::get_intermediate_buffer(const IntermediateBufferKey &key)
{
    auto intermediate_buffer_it = m_intermediate_buffers.find(key);
    if (std::end(m_intermediate_buffers) == intermediate_buffer_it) {
        return make_unexpected(HAILO_NOT_FOUND);
    }

    return std::ref(intermediate_buffer_it->second);
}

Expected<std::pair<uint16_t, uint32_t>> ResourcesManager::get_desc_buffer_sizes_for_boundary_channel(
    uint32_t transfer_size, const std::string &network_name)
{
    auto network_batch_size = get_network_batch_size(network_name);
    CHECK_EXPECTED(network_batch_size);
    uint32_t min_active_trans = MIN_ACTIVE_TRANSFERS_SCALE * network_batch_size.value();
    uint32_t max_active_trans = MAX_ACTIVE_TRANSFERS_SCALE * network_batch_size.value();

    CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(min_active_trans), HAILO_INVALID_ARGUMENT, 
        "calculated min_active_trans for vdma descriptor list is out of UINT16 range");

    CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(max_active_trans), HAILO_INVALID_ARGUMENT, 
        "calculated min_active_trans for vdma descriptor list is out of UINT16 range");

    return VdmaDescriptorList::get_desc_buffer_sizes_for_single_transfer(m_driver,
        static_cast<uint16_t>(min_active_trans), static_cast<uint16_t>(max_active_trans), transfer_size);
}

Expected<CONTROL_PROTOCOL__application_header_t> ResourcesManager::get_control_network_group_header()
{
    CONTROL_PROTOCOL__application_header_t  app_header = {};
    app_header.dynamic_contexts_count = static_cast<uint8_t>(m_contexts.size() - 1);
    app_header.host_boundary_channels_bitmap = 0;
    app_header.host_ddr_channels_bitmap = 0;

    /* Bitmask of all boundary and DDR channels*/
    int host_boundary_channels_bitmap_local = 0;
    int host_ddr_channels_bitmap_local = 0;
    for (size_t i = MIN_H2D_CHANNEL_INDEX; i <= MAX_D2H_CHANNEL_INDEX; i++) {
        /* Set boundary channels */
        if (m_channels_info[i].is_type(ChannelInfo::Type::BOUNDARY) && m_channels_info[i].is_used()) {
            host_boundary_channels_bitmap_local |= 1 << i;
        }
        /* DDR buffer channels are host controlled only if the HEF does not support padded ddr buffers */
        else if ((m_channels_info[i].is_type(ChannelInfo::Type::DDR) && (!get_supported_features().padded_ddr_buffers)) &&
            (m_channels_info[i].is_used())) {
            host_ddr_channels_bitmap_local |= 1 << i;
        }
    }

    app_header.host_boundary_channels_bitmap = static_cast<uint32_t>(host_boundary_channels_bitmap_local);
    app_header.host_ddr_channels_bitmap = static_cast<uint32_t>(host_ddr_channels_bitmap_local);

    uint8_t cfg_handle_idx = 0;
    for (uint8_t ch_idx = MIN_H2D_CHANNEL_INDEX; ch_idx <= MAX_H2D_CHANNEL_INDEX; ch_idx++) {
        if ((m_channels_info[ch_idx].is_type(ChannelInfo::Type::CFG)) &&
            (m_channels_info[ch_idx].is_used())) {
            assert(cfg_handle_idx < CONTROL_PROTOCOL__MAX_CFG_CHANNELS);
            app_header.cfg_channel_numbers[cfg_handle_idx] = ch_idx;
            cfg_handle_idx++;
        }
    }
    app_header.power_mode = static_cast<uint8_t>(m_config_params.power_mode);
    fill_network_batch_size(app_header);
    return app_header;
}

Expected<uint8_t> ResourcesManager::get_available_channel_index(std::set<uint8_t> &blacklist,
    ChannelInfo::Type required_type, VdmaChannel::Direction direction, const std::string &layer_name)
{
    uint8_t min_channel_index =
        (direction == VdmaChannel::Direction::H2D) ? MIN_H2D_CHANNEL_INDEX : MIN_D2H_CHANNEL_INDEX;
    uint8_t max_channel_index =
        (direction == VdmaChannel::Direction::H2D) ? MAX_H2D_CHANNEL_INDEX : MAX_D2H_CHANNEL_INDEX;

    for (uint8_t index = min_channel_index; index <= max_channel_index; ++index) {
        // Skip index that are on the blacklist
        if (contains(blacklist, index)) {
            continue;
        }

        // In preliminary_run_asap, channels are reused across contexts (same channels in the preliminary
        // context and first dynamic context).
        if (get_supported_features().preliminary_run_asap) {
            if (m_channels_info[index].is_used() &&
               (m_channels_info[index].get_layer_name() == layer_name) &&
               (m_channels_info[index].is_type(required_type))) {
                LOGGER__TRACE("Reusing channel {} for layer {} (running in preliminary_run_asap mode)",
                    index, layer_name);
                return index;
            }
        }

        // Use the empty channel if available
        if (!m_channels_info[index].is_used()) {
            m_channels_info[index].set_type(required_type);
            m_channels_info[index].set_layer_name(layer_name);
            return index;
        }

        // If DDR is managed by the FW - allow reuse of channels for DDR / inter_context
        if (get_supported_features().padded_ddr_buffers) {
            if (((ChannelInfo::Type::BOUNDARY != required_type) && (ChannelInfo::Type::CFG != required_type)) &&
                ((m_channels_info[index].is_type(ChannelInfo::Type::DDR)) || (m_channels_info[index].is_type(ChannelInfo::Type::INTER_CONTEXT)))) {
                m_channels_info[index].set_type(required_type);
                m_channels_info[index].set_layer_name(layer_name);
                return index;
            }
        } else {
            // If not - allow reuse of channels for within the same type
            if (m_channels_info[index].is_type(required_type)) {
                m_channels_info[index].set_type(required_type);
                m_channels_info[index].set_layer_name(layer_name);
                return index;
            }
        }
    }

    LOGGER__ERROR("Failed to get available channel_index");
    return make_unexpected(HAILO_INTERNAL_FAILURE);
}

Expected<uint8_t> ResourcesManager::get_boundary_channel_index(uint8_t stream_index,
    hailo_stream_direction_t direction, const std::string &layer_name)
{
    uint8_t min_channel_index =
        (direction == HAILO_H2D_STREAM) ? MIN_H2D_CHANNEL_INDEX : MIN_D2H_CHANNEL_INDEX;
    uint8_t max_channel_index =
        (direction == HAILO_H2D_STREAM) ? MAX_H2D_CHANNEL_INDEX : MAX_D2H_CHANNEL_INDEX;

    for (uint8_t channel_index = min_channel_index; channel_index <= max_channel_index; channel_index++) {
        auto info = m_channels_info[channel_index];
        if ((info.is_type(ChannelInfo::Type::BOUNDARY) && (stream_index == info.get_pcie_stream_index()) && 
            layer_name == info.get_layer_name())) {
            return channel_index;
        }
    }

    return make_unexpected(HAILO_INVALID_ARGUMENT);
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

ExpectedRef<IntermediateBuffer> ResourcesManager::create_ddr_buffer(DdrChannelsInfo &ddr_info, uint8_t context_index)
{
    const uint32_t number_of_transfers = ddr_info.min_buffered_rows * DDR_THREADS_MIN_BUFFERED_ROWS_INITIAL_SCALE;
    CHECK(IS_FIT_IN_UINT16(number_of_transfers), make_unexpected(HAILO_INVALID_ARGUMENT), 
        "calculated number of transfers for DDR buffer is out of UINT16_T range");

    const uint32_t transfer_size = ddr_info.row_size * DDR_NUMBER_OF_ROWS_PER_INTERRUPT;

    auto intermediate_buffer_key = std::make_pair(context_index, ddr_info.d2h_stream_index);
    return create_intermediate_buffer(transfer_size, static_cast<uint16_t>(number_of_transfers),
        intermediate_buffer_key);
}

hailo_status ResourcesManager::set_number_of_cfg_channels(const uint8_t number_of_cfg_channels)
{
    CHECK(number_of_cfg_channels <= CONTROL_PROTOCOL__MAX_CFG_CHANNELS, HAILO_INVALID_HEF, "Too many cfg channels");
    size_t channels_count = 0;
    for (uint8_t index = MIN_H2D_CHANNEL_INDEX; index <= MAX_H2D_CHANNEL_INDEX; ++index) {
        // use the empty channel if avaialble
        if (!m_channels_info[index].is_used()) {
            m_channels_info[index].set_type(ChannelInfo::Type::CFG);
            channels_count++;
        }
        if (number_of_cfg_channels == channels_count) {
            return HAILO_SUCCESS;
        }
    }

    LOGGER__ERROR("Failed to set cfg channels");
    return HAILO_INTERNAL_FAILURE;
}

Expected<hailo_stream_interface_t> ResourcesManager::get_default_streams_interface()
{
    return m_vdma_device.get_default_streams_interface();
}

hailo_status ResourcesManager::register_fw_managed_vdma_channels()
{
    hailo_status status = HAILO_UNINITIALIZED;

    for (auto &ch : m_inter_context_channels) {
        status = ch.register_fw_controlled_channel();
        CHECK_SUCCESS(status);
    }

    for (auto &ch : m_config_channels) {
        status = ch.register_fw_controlled_channel();
        CHECK_SUCCESS(status);
    }

    /* If ddr supported padded buffers - DDR buffers are managed by the FW */
    if (get_supported_features().padded_ddr_buffers) {
        for (auto &ch : m_ddr_buffer_channels) {
            status = ch.register_fw_controlled_channel();
            CHECK_SUCCESS(status);
        }
    }

    return HAILO_SUCCESS;
}

hailo_status ResourcesManager::unregister_fw_managed_vdma_channels()
{
    hailo_status status = HAILO_UNINITIALIZED;

    // TODO: Add one icotl to stop all channels at once (HRT-6097)
    for (auto &ch : m_inter_context_channels) {
        status = ch.unregister_fw_controlled_channel();
        CHECK_SUCCESS(status);
    }

    for (auto &ch : m_config_channels) {
        status = ch.unregister_fw_controlled_channel();
        CHECK_SUCCESS(status);
    }

    /* If ddr supported padded buffers - DDR buffers are managed by the FW */
    if (get_supported_features().padded_ddr_buffers) {
        for (auto &ch : m_ddr_buffer_channels) {
            status = ch.unregister_fw_controlled_channel();
            CHECK_SUCCESS(status);
        }
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
    auto intermediate_buffer_it = m_intermediate_buffers.find(key);
    if (std::end(m_intermediate_buffers) == intermediate_buffer_it) {
        LOGGER__ERROR("Failed to find intermediate buffer for src_context {}, src_stream_index {}", key.first,
            key.second);
        return make_unexpected(HAILO_NOT_FOUND);
    }

    auto &intermediate_buffer = intermediate_buffer_it->second;
    return intermediate_buffer.read();
}

hailo_status ResourcesManager::enable_state_machine(uint16_t dynamic_batch_size)
{
    if (Device::Type::CORE == m_vdma_device.get_type()) {
        // On core device, the nn_manager is not responsible to reset the nn-core so
        // we use the SCU control for that.
        auto status = m_vdma_device.reset(HAILO_RESET_DEVICE_MODE_NN_CORE);
        CHECK_SUCCESS(status);
    }

    return Control::enable_network_group(m_vdma_device, m_net_group_index, dynamic_batch_size);
}

hailo_status ResourcesManager::reset_state_machine()
{
    return Control::reset_context_switch_state_machine(m_vdma_device);
}

hailo_status ResourcesManager::open_ddr_channels()
{
    for (auto& ddr_info : m_ddr_infos) {
        for (auto &ch : m_ddr_buffer_channels) {
            auto desc_list = ddr_info.intermediate_buffer->get_desc_list();
            CHECK_EXPECTED_AS_STATUS(desc_list, "user managed DDR is not supported");
            if (ddr_info.d2h_channel_index == ch.channel_index) {
                auto status = ch.start_channel(*desc_list);
                CHECK_SUCCESS(status);
                ddr_info.d2h_ch = &ch;
            }
            if (ddr_info.h2d_channel_index == ch.channel_index) {
                auto status = ch.start_channel(*desc_list);
                CHECK_SUCCESS(status);
                ddr_info.h2d_ch = &ch;
            }
        }
    }
    return HAILO_SUCCESS;
}

void ResourcesManager::abort_ddr_channels() // Best effort func
{
    for (auto &ch : m_ddr_buffer_channels) {
        auto status = ch.abort();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to abort DDR channel {}", ch.channel_index);
        }
    }
}

void ResourcesManager::close_ddr_channels() // Best effort func
{
    m_ddr_buffer_channels.clear();
}

ExpectedRef<IntermediateBuffer> ResourcesManager::create_intermediate_buffer(uint32_t transfer_size, uint16_t batch_size,
    const IntermediateBufferKey &key)
{
    auto intermediate_buffer = IntermediateBuffer::create(m_driver, transfer_size, batch_size);
    CHECK_EXPECTED(intermediate_buffer);

    auto emplace_res = m_intermediate_buffers.emplace(key, intermediate_buffer.release());
    return std::ref(emplace_res.first->second);
}

void ResourcesManager::update_config_buffer_info(std::vector<ConfigBuffer> &config_buffers,
    CONTROL_PROTOCOL__context_switch_context_info_t &context)
{
    assert(CONTROL_PROTOCOL__MAX_CFG_CHANNELS >= config_buffers.size());
    context.cfg_channels_count = static_cast<uint8_t>(config_buffers.size());

    auto i = 0;
    for (const auto &config : config_buffers) {
        context.config_buffer_infos[i].buffer_type = static_cast<uint8_t>(
            (config.buffer_type() == vdma::VdmaBuffer::Type::SCATTER_GATHER) ?
                CONTROL_PROTOCOL__HOST_BUFFER_TYPE_EXTERNAL_DESC : CONTROL_PROTOCOL__HOST_BUFFER_TYPE_CCB);
        context.config_buffer_infos[i].dma_address = config.dma_address();
        context.config_buffer_infos[i].desc_page_size = config.desc_page_size();
        context.config_buffer_infos[i].total_desc_count = config.total_desc_count();
        // Initial desc count is used only when the cfg channel is confifo prefetch mode.
        context.config_buffer_infos[i].initial_desc_count = config.acc_desc_count();

        i++;
    }
}

} /* namespace hailort */
