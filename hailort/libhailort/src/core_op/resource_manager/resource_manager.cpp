#include "hailo/hailort_defaults.hpp"

#include "core_op/resource_manager/resource_manager.hpp"
#include "vdma/channel/boundary_channel.hpp"
#include "vdma/memory/buffer_requirements.hpp"
#include "device_common/control.hpp"

#include <numeric>

#define HAILO15H_NMS_MAX_CLASSES (1024)

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

hailo_status ContextResources::add_edge_layer(const LayerInfo &layer_info, vdma::ChannelId channel_id,
    const CONTROL_PROTOCOL__host_buffer_info_t &buffer_info, const SupportedFeatures &supported_features)
{
    auto status = validate_edge_layer(layer_info, channel_id, supported_features);
    CHECK_SUCCESS(status);

    m_edge_layers.emplace_back(EdgeLayer{
        layer_info,
        channel_id,
        buffer_info
    });

    return HAILO_SUCCESS;
}

void ContextResources::add_ddr_channels_info(const DdrChannelsInfo &ddr_info)
{
    m_ddr_channels_infos.emplace_back(ddr_info);
}

std::vector<EdgeLayer> ContextResources::get_edge_layers() const
{
    return m_edge_layers;
}

std::vector<EdgeLayer> ContextResources::get_edge_layers(LayerType layer_type) const
{
    return get_edge_layers(layer_type, HAILO_STREAM_DIRECTION_MAX_ENUM);
}

std::vector<EdgeLayer> ContextResources::get_edge_layers(hailo_stream_direction_t direction) const
{
    return get_edge_layers(LayerType::NOT_SET, direction);
}

std::vector<EdgeLayer> ContextResources::get_edge_layers(LayerType layer_type, hailo_stream_direction_t direction) const
{
    std::vector<EdgeLayer> edge_layers;
    for (const auto &edge_layer : m_edge_layers) {
        const bool layer_type_ok = (layer_type == LayerType::NOT_SET) || (edge_layer.layer_info.type == layer_type);
        const bool direction_ok = (direction == HAILO_STREAM_DIRECTION_MAX_ENUM) || (edge_layer.layer_info.direction == direction);
        if (layer_type_ok && direction_ok) {
            edge_layers.emplace_back(edge_layer);
        }
    }
    return edge_layers;
}

Expected<EdgeLayer> ContextResources::get_edge_layer_by_stream_index(const uint8_t stream_index,
    const hailo_stream_direction_t direction) const
{
    for (const auto &edge_layer : m_edge_layers) {
        if ((stream_index == edge_layer.layer_info.stream_index) && (direction == edge_layer.layer_info.direction)) {
            return EdgeLayer(edge_layer);
        }
    }

    LOGGER__ERROR("Edge layer does not exists for stream {}", stream_index);
    return make_unexpected(HAILO_INTERNAL_FAILURE);
}

Expected<EdgeLayer> ContextResources::get_edge_layer_by_channel_id(const vdma::ChannelId channel_id) const
{
    for (const auto &edge_layer : m_edge_layers) {
        if (channel_id == edge_layer.channel_id) {
            return EdgeLayer(edge_layer);
        }
    }

    LOGGER__ERROR("Edge layer does not exists for channel id {}", channel_id);
    return make_unexpected(HAILO_INTERNAL_FAILURE);
}

Expected<DdrChannelsInfo> ContextResources::get_ddr_channels_info(uint8_t d2h_stream_index) const
{
    for (const auto &ddr_channels_info : m_ddr_channels_infos) {
        if (ddr_channels_info.d2h_stream_index == d2h_stream_index) {
            return DdrChannelsInfo{ddr_channels_info};
        }
    }

    LOGGER__ERROR("Couldn't find ddr channels pair for {}", d2h_stream_index);
    return make_unexpected(HAILO_INTERNAL_FAILURE);
}

const std::vector<DdrChannelsInfo> &ContextResources::get_ddr_channels_infos() const
{
    return m_ddr_channels_infos;
}

hailo_status ContextResources::validate_edge_layer(const LayerInfo &layer_info, vdma::ChannelId channel_id,
    const SupportedFeatures &supported_features)
{
    bool stream_index_already_used = false;

    for (const auto &edge_layer : m_edge_layers) {
        CHECK(!(edge_layer.channel_id == channel_id), HAILO_INTERNAL_FAILURE,
            "Same stream use the same channel id {}", channel_id);

        // In Activation Context it is ok to have multiple edge layers with same stream index seeing as they could be for
        // Different contexts etc...
        if (CONTROL_PROTOCOL__CONTEXT_SWITCH_CONTEXT_TYPE_ACTIVATION != m_builder.get_context_type()) {
            if (edge_layer.layer_info.stream_index == layer_info.stream_index) {
                // Validate that the amount of edge layers with the same stream index per context is 2 (And with opposite directions)
                // In the case of dual direction supported feature - otherwise 1
                if (supported_features.dual_direction_stream_index) {
                    CHECK(!stream_index_already_used, HAILO_INTERNAL_FAILURE,
                        "Stream Index {} used for too many edge layers in one context", edge_layer.layer_info.stream_index);
                    CHECK(layer_info.direction != edge_layer.layer_info.direction, HAILO_INTERNAL_FAILURE,
                        "Stream Index {} used for other edge layer in same direction", edge_layer.layer_info.stream_index);
                    stream_index_already_used = true;
                } else {
                    LOGGER__ERROR("Stream Index {} used for too many edge layers in one context",
                        edge_layer.layer_info.stream_index);
                    return HAILO_INTERNAL_FAILURE;
                }
            }
        }
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

    auto res = make_shared_nothrow<LatencyMeter>(d2h_channel_names, MAX_IRQ_TIMESTAMPS_SIZE);
    CHECK_NOT_NULL_AS_EXPECTED(res, HAILO_OUT_OF_HOST_MEMORY);

    return res;
}

static Expected<LatencyMetersMap> create_latency_meters_from_config_params( 
    const ConfigureNetworkParams &config_params, std::shared_ptr<CoreOpMetadata> core_op_metadata)
{
    LatencyMetersMap latency_meters_map; 

    if ((config_params.latency & HAILO_LATENCY_MEASURE) == HAILO_LATENCY_MEASURE) {
        // Best effort for starting latency meter.
        auto networks_names = core_op_metadata->get_network_names();
        for (auto &network_name : networks_names) {
            auto layer_infos = core_op_metadata->get_all_layer_infos(network_name);
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
    const ConfigureNetworkParams &config_params, std::shared_ptr<CoreOpMetadata> core_op_metadata,
    uint8_t core_op_index)
{
    // Allocate config channels. In order to use the same channel ids for config channels in all contexts,
    // we allocate all of them here, and use in preliminary/dynamic context.
    ChannelAllocator allocator(driver.dma_engines_count());
    std::vector<vdma::ChannelId> config_channels_ids;
    const auto &config_channels_info = core_op_metadata->config_channels_info();
    config_channels_ids.reserve(config_channels_info.size());
    for (uint8_t cfg_index = 0; cfg_index < config_channels_info.size(); cfg_index++) {
        const auto layer_identifier = std::make_tuple(LayerType::CFG, HAILO_H2D_STREAM, "", cfg_index);
        const auto engine_index = config_channels_info[cfg_index].engine_index;
        auto channel_id = allocator.get_available_channel_id(layer_identifier, HailoRTDriver::DmaDirection::H2D, engine_index);
        CHECK_EXPECTED(channel_id);
        config_channels_ids.push_back(channel_id.release());
    }

    auto network_index_map = core_op_metadata->get_network_names();

    auto latency_meters = create_latency_meters_from_config_params(config_params, core_op_metadata);
    CHECK_EXPECTED(latency_meters);
    ResourcesManager resources_manager(vdma_device, driver, std::move(allocator), config_params,
        std::move(core_op_metadata), core_op_index,
        std::move(network_index_map), latency_meters.release(), std::move(config_channels_ids));

    return resources_manager;
}

ResourcesManager::ResourcesManager(VdmaDevice &vdma_device, HailoRTDriver &driver,
                                   ChannelAllocator &&channel_allocator, const ConfigureNetworkParams config_params,
                                   std::shared_ptr<CoreOpMetadata> &&core_op_metadata,
                                   uint8_t core_op_index, const std::vector<std::string> &&network_index_map,
                                   LatencyMetersMap &&latency_meters,
                                   std::vector<vdma::ChannelId> &&config_channels_ids) :
    m_contexts_resources(),
    m_channel_allocator(std::move(channel_allocator)),
    m_vdma_device(vdma_device),
    m_driver(driver),
    m_config_params(config_params),
    m_intermediate_buffers(),
    m_core_op_metadata(std::move(core_op_metadata)),
    m_core_op_index(core_op_index),
    m_dynamic_context_count(0),
    m_total_context_count(0),
    m_network_index_map(std::move(network_index_map)),
    m_latency_meters(std::move(latency_meters)),
    m_boundary_channels(),
    m_is_configured(false),
    m_config_channels_ids(std::move(config_channels_ids)),
    m_hw_only_boundary_buffers()
{}

ResourcesManager::ResourcesManager(ResourcesManager &&other) noexcept :
    m_contexts_resources(std::move(other.m_contexts_resources)),
    m_channel_allocator(std::move(other.m_channel_allocator)),
    m_vdma_device(other.m_vdma_device),
    m_driver(other.m_driver),
    m_config_params(other.m_config_params),
    m_intermediate_buffers(std::move(other.m_intermediate_buffers)),
    m_core_op_metadata(std::move(other.m_core_op_metadata)),
    m_core_op_index(other.m_core_op_index),
    m_dynamic_context_count(std::exchange(other.m_dynamic_context_count, static_cast<uint8_t>(0))),
    m_total_context_count(std::exchange(other.m_total_context_count, static_cast<uint8_t>(0))),
    m_network_index_map(std::move(other.m_network_index_map)),
    m_latency_meters(std::move(other.m_latency_meters)),
    m_boundary_channels(std::move(other.m_boundary_channels)),
    m_is_configured(std::exchange(other.m_is_configured, false)),
    m_config_channels_ids(std::move(other.m_config_channels_ids)),
    m_hw_only_boundary_buffers(std::move(other.m_hw_only_boundary_buffers))
{}

hailo_status ResourcesManager::fill_infer_features(CONTROL_PROTOCOL__application_header_t &app_header)
{
    app_header.infer_features.preliminary_run_asap = m_core_op_metadata->supported_features().preliminary_run_asap;
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

hailo_status ResourcesManager::fill_csm_buffer_size(CONTROL_PROTOCOL__application_header_t &app_header)
{
    // All config buffers on the same platform will have the same desc_page_size - because it is derived from the host
    app_header.csm_buffer_size = std::min(m_driver.desc_max_page_size(), vdma::DEFAULT_DESC_PAGE_SIZE);
    return HAILO_SUCCESS;
}

void ResourcesManager::process_interrupts(IrqData &&irq_data)
{
    assert(irq_data.channels_count <= ARRAY_ENTRIES(irq_data.channels_irq_data));
    for (uint8_t irq_index = 0; irq_index < irq_data.channels_count; irq_index++) {
        const auto &channel_irq_data = irq_data.channels_irq_data[irq_index];
        auto boundary_channel = m_boundary_channels.find(channel_irq_data.channel_id);
        if (std::end(m_boundary_channels) == boundary_channel) {
            LOGGER__ERROR("Got interrupt for channel {}, but there is no such boundary channel", channel_irq_data.channel_id);
            continue;
        }

        if ((channel_irq_data.host_error != 0) || (channel_irq_data.device_error != 0))  {
            LOGGER__CRITICAL("Got error on channel {} host_error=0x{:x} device_error=0x{:x}",
                channel_irq_data.channel_id, channel_irq_data.host_error, channel_irq_data.device_error);
            continue;
        }

        if (!channel_irq_data.is_active) {
            LOGGER__CRITICAL("Channel {} was aborted by external source", channel_irq_data.channel_id);
            continue;
        }

        auto status = boundary_channel->second->trigger_channel_completion(channel_irq_data.desc_num_processed);
        if ((status != HAILO_SUCCESS) &&
            (status != HAILO_STREAM_ABORTED_BY_USER) &&
            (status != HAILO_STREAM_NOT_ACTIVATED)) {
            // Log error and continue gracefully to process other interrupts
            LOGGER__ERROR("Trigger channel completion failed on channel {} with status {}", channel_irq_data.channel_id, status);
        }
    }
}

Expected<uint16_t> ResourcesManager::get_batch_size() const
{
    uint16_t batch_size = UINT16_MAX;
    for (auto const &network_map : m_config_params.network_params_by_name) {
        auto const network_name_from_params = network_map.first;

        if (UINT16_MAX == batch_size) {
            batch_size = network_map.second.batch_size;
        } else {
            CHECK_AS_EXPECTED(batch_size == network_map.second.batch_size, HAILO_INVALID_OPERATION,
                "The same batch size must be applied to all networks inside the network group");
        }
    }
    return batch_size;

}

hailo_status ResourcesManager::create_boundary_vdma_channel(const LayerInfo &layer_info)
{
    // TODO: put in layer info
    const auto channel_direction = layer_info.direction == HAILO_H2D_STREAM ? HailoRTDriver::DmaDirection::H2D :
                                                                              HailoRTDriver::DmaDirection::D2H;
    const auto channel_id = get_available_channel_id(to_layer_identifier(layer_info),
        channel_direction, layer_info.dma_engine_index);
    CHECK_EXPECTED_AS_STATUS(channel_id);

    const auto network_batch_size = get_network_batch_size(layer_info.network_name);
    CHECK_EXPECTED_AS_STATUS(network_batch_size);

    const auto nms_max_detections_per_frame =
        layer_info.nms_info.number_of_classes *  layer_info.nms_info.max_bboxes_per_class * layer_info.nms_info.chunks_per_frame;

    const auto max_active_transfers_scale = (layer_info.format.order == HAILO_FORMAT_ORDER_HAILO_NMS) ?
        (nms_max_detections_per_frame * MAX_ACTIVE_TRANSFERS_SCALE) : MAX_ACTIVE_TRANSFERS_SCALE;

    auto device_arch = m_vdma_device.get_architecture();
    CHECK_EXPECTED_AS_STATUS(device_arch);
    /* Add error in configure phase for invalid NMS parameters */
    if (layer_info.format.order == HAILO_FORMAT_ORDER_HAILO_NMS && 
        (device_arch.value() == HAILO_ARCH_HAILO15H || device_arch.value() == HAILO_ARCH_HAILO15M || device_arch.value() == HAILO_ARCH_PLUTO)) {
        CHECK(layer_info.nms_info.number_of_classes * layer_info.nms_info.chunks_per_frame * network_batch_size.value() < HAILO15H_NMS_MAX_CLASSES, 
        HAILO_INVALID_ARGUMENT, "Invalid NMS parameters. Number of classes ({}) * division factor ({}) * batch size ({}) must be under {}",
        layer_info.nms_info.number_of_classes, layer_info.nms_info.chunks_per_frame, network_batch_size.value(), HAILO15H_NMS_MAX_CLASSES);
    }

    const auto min_active_trans = MIN_ACTIVE_TRANSFERS_SCALE * network_batch_size.value();
    const auto max_active_trans = (layer_info.format.order == HAILO_FORMAT_ORDER_HAILO_NMS) ?
        /* NMS Case - Value be be higher than UINT16_MAX. in this case we only limit to UART16_MAX with no error */
        std::min(static_cast<uint32_t>(UINT16_MAX), max_active_transfers_scale * network_batch_size.value()) :
        max_active_transfers_scale * network_batch_size.value();

    CHECK(IS_FIT_IN_UINT16(min_active_trans), HAILO_INVALID_ARGUMENT,
        "calculated min_active_trans for vdma descriptor list is out of UINT16 range");
    CHECK(IS_FIT_IN_UINT16(max_active_trans), HAILO_INVALID_ARGUMENT,
        "calculated min_active_trans for vdma descriptor list is out of UINT16 range");

    auto latency_meter = (contains(m_latency_meters, layer_info.network_name)) ? m_latency_meters.at(layer_info.network_name) : nullptr;

    /* TODO - HRT-6829- page_size should be calculated inside the vDMA channel class create function */
    static const bool IS_CIRCULAR = true;
    static const bool IS_VDMA_ALIGNED_BUFFER = false;
    const auto transfer_size = LayerInfoUtils::get_layer_transfer_size(layer_info);

    const auto DONT_FORCE_DEFAULT_PAGE_SIZE = false;
    const auto DONT_FORCE_BATCH_SIZE = false;
    // Hack to reduce max page size if the driver page size is equal to stream size. 
    // In this case page size == stream size is invalid solution. 
    // TODO - remove this WA after HRT-11747
    const uint16_t max_page_size = (m_driver.desc_max_page_size() == layer_info.max_shmifo_size) ?
        (m_driver.desc_max_page_size() / 2) : m_driver.desc_max_page_size();
    auto buffer_sizes_requirements = vdma::BufferSizesRequirements::get_sg_buffer_requirements_single_transfer(
        max_page_size, static_cast<uint16_t>(min_active_trans), static_cast<uint16_t>(max_active_trans),
        transfer_size, IS_CIRCULAR, DONT_FORCE_DEFAULT_PAGE_SIZE, DONT_FORCE_BATCH_SIZE, IS_VDMA_ALIGNED_BUFFER);
    CHECK_EXPECTED_AS_STATUS(buffer_sizes_requirements);

    const auto page_size = buffer_sizes_requirements->desc_page_size();
    const auto descs_count = (nullptr != std::getenv("HAILO_CONFIGURE_FOR_HW_INFER")) ?
        MAX_DESCS_COUNT : buffer_sizes_requirements->descs_count();

    auto channel = vdma::BoundaryChannel::create(channel_id.value(), channel_direction, m_vdma_device, descs_count,
        page_size, layer_info.name, latency_meter);
    CHECK_EXPECTED_AS_STATUS(channel);

    m_boundary_channels.emplace(channel_id.value(), channel.release());
    return HAILO_SUCCESS;
}

Expected<vdma::BoundaryChannelPtr> ResourcesManager::get_boundary_vdma_channel_by_stream_name(const std::string &stream_name)
{
    for (const auto &boundary_channel : m_boundary_channels) {
        if (boundary_channel.second->stream_name() == stream_name) {
            return vdma::BoundaryChannelPtr(boundary_channel.second);
        }
    }

    return make_unexpected(HAILO_NOT_FOUND);
}

Expected<std::shared_ptr<const vdma::BoundaryChannel>> ResourcesManager::get_boundary_vdma_channel_by_stream_name(const std::string &stream_name) const
{
    for (const auto &boundary_channel : m_boundary_channels) {
        if (boundary_channel.second->stream_name() == stream_name) {
            return std::shared_ptr<const vdma::BoundaryChannel>(boundary_channel.second);
        }
    }

    return make_unexpected(HAILO_NOT_FOUND);
}

hailo_power_mode_t ResourcesManager::get_power_mode() const
{
    return m_config_params.power_mode;
}

ExpectedRef<IntermediateBuffer> ResourcesManager::create_intermediate_buffer(uint32_t transfer_size,
    uint16_t batch_size, uint8_t src_stream_index, uint8_t src_context_index,
    vdma::ChannelId d2h_channel_id, IntermediateBuffer::StreamingType streaming_type)
{
    auto buffer = IntermediateBuffer::create(m_driver, transfer_size, batch_size, d2h_channel_id,
        streaming_type);
    CHECK_EXPECTED(buffer);

    const auto key = std::make_pair(src_context_index, src_stream_index);
    auto emplace_res = m_intermediate_buffers.emplace(key, buffer.release());
    return std::ref(emplace_res.first->second);
}

ExpectedRef<IntermediateBuffer> ResourcesManager::get_intermediate_buffer(const IntermediateBufferKey &key)
{
    auto buffer_it = m_intermediate_buffers.find(key);
    if (std::end(m_intermediate_buffers) == buffer_it) {
        return make_unexpected(HAILO_NOT_FOUND);
    }

    return std::ref(buffer_it->second);
}

Expected<CONTROL_PROTOCOL__application_header_t> ResourcesManager::get_control_core_op_header()
{
    CONTROL_PROTOCOL__application_header_t app_header{};
    app_header.dynamic_contexts_count = m_dynamic_context_count;

    auto status = fill_infer_features(app_header);
    CHECK_SUCCESS_AS_EXPECTED(status, "Invalid infer features");
    status = fill_validation_features(app_header);
    CHECK_SUCCESS_AS_EXPECTED(status, "Invalid validation features");
    status = fill_network_batch_size(app_header);
    CHECK_SUCCESS_AS_EXPECTED(status, "Invalid network batch sizes");
    status = fill_csm_buffer_size(app_header);
    CHECK_SUCCESS_AS_EXPECTED(status, "Invalid csm buffer size");

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
    HailoRTDriver::DmaDirection direction, uint8_t engine_index)
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
    auto intermediate_buffer_it = m_intermediate_buffers.find(key);
    CHECK_AS_EXPECTED(std::end(m_intermediate_buffers) != intermediate_buffer_it,
        HAILO_NOT_FOUND, "Failed to find intermediate buffer for src_context {}, src_stream_index {}", key.first,
        key.second);
    return intermediate_buffer_it->second.read();
}

hailo_status ResourcesManager::configure()
{
    CHECK(!m_is_configured, HAILO_INTERNAL_FAILURE, "Can't configure the same core-op twice");
    m_is_configured = true;

    auto core_op_header = get_control_core_op_header();
    CHECK_EXPECTED_AS_STATUS(core_op_header);

    auto status = Control::context_switch_set_network_group_header(m_vdma_device, core_op_header.release());
    CHECK_SUCCESS(status);

    for (const auto &context : m_contexts_resources) {
        status = Control::context_switch_set_context_info(m_vdma_device, context.get_controls());
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status ResourcesManager::enable_state_machine(uint16_t dynamic_batch_size, uint16_t batch_count)
{
    return Control::enable_core_op(m_vdma_device, m_core_op_index, dynamic_batch_size, batch_count);
}

hailo_status ResourcesManager::reset_state_machine()
{
    auto status = Control::reset_context_switch_state_machine(m_vdma_device);
    CHECK_SUCCESS(status);

    if (Device::Type::INTEGRATED == m_vdma_device.get_type()) {
        // On core device, the nn_manager is not responsible to reset the nn-core so
        // we use the SCU control for that.
        status = m_vdma_device.reset(HAILO_RESET_DEVICE_MODE_NN_CORE);
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status ResourcesManager::start_vdma_interrupts_dispatcher()
{
    auto interrupts_dispatcher = m_vdma_device.get_vdma_interrupts_dispatcher();
    CHECK_EXPECTED_AS_STATUS(interrupts_dispatcher);

    ChannelsBitmap channels_bitmap{};
    for (const auto &boundary_channel : m_boundary_channels) {
        const auto channel_id = boundary_channel.first;
        channels_bitmap[channel_id.engine_index] |= (1 << channel_id.channel_index);
    }

    const bool enable_timestamp_measure = !m_latency_meters.empty();
    return interrupts_dispatcher->get().start(channels_bitmap, enable_timestamp_measure, [this](IrqData &&irq_data){
        process_interrupts(std::move(irq_data));
    });
}

hailo_status ResourcesManager::stop_vdma_interrupts_dispatcher()
{
    auto interrupts_dispatcher = m_vdma_device.get_vdma_interrupts_dispatcher();
    CHECK_EXPECTED_AS_STATUS(interrupts_dispatcher);
    return interrupts_dispatcher->get().stop();
}

Expected<uint16_t> ResourcesManager::program_desc_for_hw_only_flow(std::shared_ptr<vdma::DescriptorList> desc_list,
    const uint32_t single_transfer_size, const uint16_t dynamic_batch_size, const uint16_t batch_count)
{
    size_t acc_desc_offset = 0;
    for (uint16_t batch_index = 0; batch_index < batch_count; batch_index++) {
        for (uint16_t transfer_index = 0; transfer_index < dynamic_batch_size; transfer_index++) {
            const auto last_desc_interrupts_domain = ((dynamic_batch_size - 1) == transfer_index) ?
                vdma::InterruptsDomain::DEVICE : vdma::InterruptsDomain::NONE;
            auto desc_count_local = desc_list->program_last_descriptor(single_transfer_size,
                last_desc_interrupts_domain, acc_desc_offset);
            CHECK_EXPECTED(desc_count_local, "Failed to program descs for inter context channels. Given max_batch_size is too big.");
            acc_desc_offset += desc_count_local.value();
        }
    }
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(acc_desc_offset), HAILO_INTERNAL_FAILURE,
        "calculated acc_desc_offset for vdma descriptor list is out of UINT16 range");
    return static_cast<uint16_t>(acc_desc_offset);
}

Expected<std::pair<vdma::ChannelId, uint16_t>> ResourcesManager::create_mapped_buffer_for_hw_only_infer(
    vdma::BoundaryChannelPtr boundary_channel_ptr, const HailoRTDriver::DmaDirection direction,
    const uint32_t single_transfer_size, const uint16_t dynamic_batch_size, const uint16_t batch_count)
{
    const auto total_frames_per_run = dynamic_batch_size * batch_count;

    auto desc_list = boundary_channel_ptr->get_desc_list();
    const auto descs_per_transfer = desc_list->descriptors_in_buffer(single_transfer_size);
    const auto total_desc_count = total_frames_per_run * descs_per_transfer;

    CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(total_desc_count), HAILO_INVALID_ARGUMENT,
        "calculated total_desc_count for vdma descriptor list is out of UINT16 range");

    auto mapped_buffer = vdma::MappedBuffer::create_shared_by_allocation(
        total_desc_count * desc_list->desc_page_size(), m_driver, direction);
    CHECK_EXPECTED(mapped_buffer);
    m_hw_only_boundary_buffers.emplace_back(mapped_buffer.release());

    uint32_t STARTING_DESC = 0;
    auto status = desc_list->configure_to_use_buffer(*m_hw_only_boundary_buffers.back(), boundary_channel_ptr->get_channel_id(), STARTING_DESC);
    CHECK_SUCCESS_AS_EXPECTED(status);

    auto desc_programed = program_desc_for_hw_only_flow(desc_list, single_transfer_size, dynamic_batch_size, batch_count);
    CHECK_EXPECTED(desc_programed);
    assert(static_cast<uint16_t>(total_desc_count) == desc_programed.value());

    auto channel_info_pair = std::make_pair(boundary_channel_ptr->get_channel_id(), desc_programed.release());

    return channel_info_pair;
}

void ResourcesManager::add_channel_to_hw_infer_channel_info(std::pair<vdma::ChannelId, uint16_t> channel_info,
    CONTROL_PROTOCOL__hw_infer_channels_info_t &channels_info)
{
    auto next_chnanel_info = &channels_info.channel_info[channels_info.channel_count];
    assert(channels_info.channel_count < CONTROL_PROTOCOL__MAX_TOTAL_CHANNEL_COUNT);

    next_chnanel_info->engine_index = channel_info.first.engine_index;
    next_chnanel_info->channel_index = channel_info.first.channel_index;
    next_chnanel_info->desc_programed = channel_info.second;

    channels_info.channel_count++;
}

hailo_status ResourcesManager::set_hw_infer_done_notification(std::condition_variable &infer_done_cond)
{
    auto callback = [](Device &device, const hailo_notification_t &notification, void *opaque) {
        (void)device;

        if (HAILO_NOTIFICATION_ID_HW_INFER_MANAGER_INFER_DONE != notification.id) {
            LOGGER__ERROR("Notification id passed to hw infer callback is invalid");
        }

        static_cast<std::condition_variable *>(opaque)->notify_one();
        return;
    };

    auto status = get_device().set_notification_callback(callback,
        HAILO_NOTIFICATION_ID_HW_INFER_MANAGER_INFER_DONE, static_cast<void *>(&infer_done_cond));
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

Expected<uint16_t> ResourcesManager::calc_hw_infer_batch_count(uint16_t dynamic_batch_size)
{
    uint16_t batch_count = UINT16_MAX;
    for (const auto &layer_info : m_core_op_metadata->get_all_layer_infos()) {
        const auto &stream_infos = LayerInfoUtils::get_stream_infos_from_layer_info(layer_info);
        for (auto &stream_info : stream_infos) {
            uint32_t single_transfer_size = LayerInfoUtils::get_stream_transfer_size(stream_info, layer_info);
            auto boundary_channel_ptr_exp = get_boundary_vdma_channel_by_stream_name(layer_info.name);
            CHECK_EXPECTED(boundary_channel_ptr_exp);
            auto boundary_channel_ptr = boundary_channel_ptr_exp.release();
            const auto max_batch_transfers = boundary_channel_ptr->get_desc_list()->max_transfers(single_transfer_size * dynamic_batch_size);
            // infer batch count is the lowest number of "Max transfers" per descriptor list that for all given boundary channels.
            batch_count = MIN(batch_count, max_batch_transfers);
        }
    }
    return batch_count;
}

HwInferResults ResourcesManager::hw_infer_calc_stats(uint16_t batch_count, uint16_t dynamic_batch_size,
    size_t single_frame_transfer_size, uint32_t infer_cycles)
{
    HwInferResults hw_infer_results{};
    const size_t total_transfer_size = single_frame_transfer_size * dynamic_batch_size * batch_count;
    const size_t total_frames_passed = dynamic_batch_size * batch_count;

    // TODO - get clock rate from Chip (still not supported in VPU mode)
    const float32_t CPU_CLOCK_RATE = static_cast<float32_t>(5.0 / (1000 * 1000 * 1000));
    const float32_t time_sec = static_cast<float32_t>(infer_cycles) * CPU_CLOCK_RATE;
    const float32_t fps = static_cast<float32_t>(total_frames_passed) / time_sec;
    const float32_t BYTE_TO_BIT = 8.0;
    const float32_t BITS_TO_GBIT = static_cast<float32_t>(1.0 * 1000 * 1000 * 1000);
    const float32_t BW_Gbps = static_cast<float32_t>(total_transfer_size) * BYTE_TO_BIT / time_sec / BITS_TO_GBIT;

    /* Prepare results */
    hw_infer_results.batch_count = batch_count;
    hw_infer_results.total_transfer_size = total_transfer_size;
    hw_infer_results.total_frames_passed = total_frames_passed;
    hw_infer_results.time_sec = time_sec;
    hw_infer_results.fps = fps;
    hw_infer_results.BW_Gbps = BW_Gbps;

    return hw_infer_results;
}

Expected<HwInferResults> ResourcesManager::run_hw_only_infer()
{
    CONTROL_PROTOCOL__hw_only_infer_results_t fw_infer_results{};
    CONTROL_PROTOCOL__hw_infer_channels_info_t channels_info{};
    channels_info.channel_count = 0;
    static constexpr auto INFER_TIMEOUT = std::chrono::milliseconds(120000);

    auto batch_size = get_batch_size();
    CHECK_EXPECTED(batch_size);

    auto batch_count = calc_hw_infer_batch_count(*batch_size);
    CHECK_EXPECTED(batch_count);

    for (const auto &layer_info : m_core_op_metadata->get_all_layer_infos()) {
        auto boundary_channel_ptr = get_boundary_vdma_channel_by_stream_name(layer_info.name);
        CHECK_EXPECTED(boundary_channel_ptr);
        const auto &stream_infos = LayerInfoUtils::get_stream_infos_from_layer_info(layer_info);
        for (auto &stream_info : stream_infos) {
            auto single_transfer_size = (HAILO_FORMAT_ORDER_HAILO_NMS == stream_info.format.order) ?
                stream_info.nms_info.bbox_size : stream_info.hw_frame_size;
            const auto direction = (layer_info.direction == HAILO_H2D_STREAM) ?
                HailoRTDriver::DmaDirection::H2D : HailoRTDriver::DmaDirection::D2H;

            auto channel_info_pair = create_mapped_buffer_for_hw_only_infer(boundary_channel_ptr.release(), direction,
                single_transfer_size, *batch_size, batch_count.value());
            CHECK_EXPECTED(channel_info_pair);

            add_channel_to_hw_infer_channel_info(channel_info_pair.release(), channels_info);
        }
    }

    std::condition_variable infer_done_cond;
    auto status = set_hw_infer_done_notification(infer_done_cond);
    CHECK_SUCCESS_AS_EXPECTED(status);

    std::mutex mutex;
    std::unique_lock<std::mutex> lock(mutex);

    status = Control::start_hw_only_infer(m_vdma_device, m_core_op_index, *batch_size,
        batch_count.value(), &channels_info);
    CHECK_SUCCESS_AS_EXPECTED(status);

    infer_done_cond.wait_for(lock, INFER_TIMEOUT);

    status = Control::stop_hw_only_infer(m_vdma_device, &fw_infer_results);
    CHECK_SUCCESS_AS_EXPECTED(status);

    auto single_frame_transfer_size = m_core_op_metadata->get_total_transfer_size();
    CHECK_EXPECTED(single_frame_transfer_size);

    return hw_infer_calc_stats(batch_count.value(), *batch_size, single_frame_transfer_size.release(),
        fw_infer_results.infer_cycles);
}

} /* namespace hailort */
