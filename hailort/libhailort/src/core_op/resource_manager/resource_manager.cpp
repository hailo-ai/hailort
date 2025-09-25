/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/

#include "hailo/hailort_defaults.hpp"
#include "core_op/resource_manager/resource_manager.hpp"
#include "vdma/channel/boundary_channel.hpp"
#include "vdma/memory/buffer_requirements.hpp"
#include "vdma/memory/vdma_edge_layer.hpp"
#include "device_common/control.hpp"
#include "core_op/resource_manager/internal_buffer_manager.hpp"
#include "common/internal_env_vars.hpp"
#include "vdma/memory/descriptor_list.hpp"
#include "vdma/memory/dma_able_buffer.hpp"
#include "hef/hef_internal.hpp"

#include <numeric>

#define HAILO15H_NMS_MAX_CLASSES (1024)
#define MAX_NUM_CONTEXTS_FOR_CONTROL_BUILDER (64)
/* The context data buffers are save to a buffer in fw which is limited to 80kb,
    After taking into consideration the headers and pointers in it, we limit the max to 75kb (instead of 80kb) */
#define CONTEXT_SWITCH_CONFIG__MAX_BUFFER_SIZE_WITHOUT_HEADERS (1024 * 75)

#define HW_INFER_CCB_DESC_PAGE_SIZE (512)

namespace hailort
{

// As a heuristic, we want to make sure NN_CORE_QUEUE_SIZE_IN_BYTES can be buffered in the nn core.
// It doesn't mean we will actually use all of it since the actual queue size is bounded by
// [MIN_ACTIVE_TRANSFERS_SCALE*batch_size, MAX_ACTIVE_TRANSFERS_SCALE*batch_size]
static const constexpr size_t NN_CORE_QUEUE_SIZE_IN_BYTES = 32 * 1024 * 1024; // 32MB

Expected<ContextResources> ContextResources::create(HailoRTDriver &driver,
    CONTROL_PROTOCOL__context_switch_context_type_t context_type,
    const std::vector<vdma::ChannelId> &config_channels_ids, const ConfigBufferInfoMap &config_buffer_infos,
    std::shared_ptr<InternalBufferManager> internal_buffer_manager, bool aligned_ccws, std::vector<std::shared_ptr<vdma::MappedBuffer>> mapped_buffers,
    std::shared_ptr<vdma::MappedBuffer> nops_buffer)
{
    CHECK_AS_EXPECTED(context_type < CONTROL_PROTOCOL__CONTEXT_SWITCH_CONTEXT_TYPE_COUNT, HAILO_INVALID_ARGUMENT);
    CHECK_AS_EXPECTED(config_buffer_infos.size() <= config_channels_ids.size(), HAILO_INTERNAL_FAILURE,
        "config_buffer_infos size ({}) is bigger than config_channels_id count  ({})",
        config_buffer_infos.size(), config_channels_ids.size());

    std::vector<ConfigBuffer> config_buffers;
    config_buffers.reserve(config_buffer_infos.size());
    if (aligned_ccws) {
        // In case of alligned ccws - we will also use the mapped buffer of the ccws_section + the nops buffer
        // Also - we will program the descriptors right after creating the descriptor list
        for (uint8_t config_stream_index = 0; config_stream_index < config_buffer_infos.size(); config_stream_index++) {
            TRY(auto buffer_resource, ConfigBuffer::create_for_aligned_ccws(driver, config_channels_ids[config_stream_index],
                config_buffer_infos.at(config_stream_index), mapped_buffers, nops_buffer));
            config_buffers.emplace_back(std::move(buffer_resource));
        }
    } else {
        // In the other case (no alligned ccws) - we will only create the config buffer (programming the descriptors will be done later) 
        for (uint8_t config_stream_index = 0; config_stream_index < config_buffer_infos.size(); config_stream_index++) {
            TRY(auto buffer_resource, ConfigBuffer::create_with_copy_descriptors(driver, config_channels_ids[config_stream_index],
                config_buffer_infos.at(config_stream_index)));
            config_buffers.emplace_back(std::move(buffer_resource));
        }
    }

    return ContextResources(driver, context_type, std::move(config_buffers), internal_buffer_manager);
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
        if (CONTROL_PROTOCOL__CONTEXT_SWITCH_CONTEXT_TYPE_ACTIVATION != get_context_type()) {
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
            if (HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP == layer.format.order) {
                LOGGER__WARNING("HW Latency measurement is not supported on NMS networks");
                return make_unexpected(HAILO_INVALID_OPERATION);
            }

            d2h_channel_names.insert(layer.name);
        } else {
            if (layer.is_multi_planar) {
                h2d_streams_count = h2d_streams_count + layer.planes.size();
            } else {
                h2d_streams_count++;
            }
        }
    }

    if (h2d_streams_count > 1) {
        LOGGER__WARNING("HW Latency measurement is supported on networks with a single input. the model has {} physical inputs.",
            h2d_streams_count);
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
            TRY(const auto layer_infos, core_op_metadata->get_all_layer_infos(network_name));
            TRY(auto latency_meter, create_hw_latency_meter(layer_infos));
            latency_meters_map.emplace(network_name, latency_meter);
            LOGGER__DEBUG("Starting hw latency measurement for network {}", network_name);
        }
    }

    return latency_meters_map;
}

Expected<ResourcesManager> ResourcesManager::create(VdmaDevice &vdma_device, HailoRTDriver &driver,
    const ConfigureNetworkParams &config_params, CacheManagerPtr cache_manager,
    std::shared_ptr<CoreOpMetadata> core_op_metadata, uint8_t core_op_index)
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
        TRY(const auto channel_id,
            allocator.get_available_channel_id(layer_identifier, HailoRTDriver::DmaDirection::H2D, engine_index, false));
        config_channels_ids.push_back(channel_id);
    }

    TRY(auto internal_buffer_manager, InternalBufferManager::create(driver));
    TRY(auto action_list_buffer_builder, ActionListBufferBuilder::create());
    TRY(auto latency_meters, create_latency_meters_from_config_params(config_params, core_op_metadata));
    auto network_index_map = core_op_metadata->get_network_names();

    ResourcesManager resources_manager(vdma_device, driver, std::move(allocator), config_params, cache_manager,
        std::move(core_op_metadata), core_op_index, std::move(network_index_map), std::move(latency_meters),
        std::move(config_channels_ids), internal_buffer_manager, std::move(action_list_buffer_builder));

    return resources_manager;
}

ResourcesManager::ResourcesManager(VdmaDevice &vdma_device, HailoRTDriver &driver,
                                   ChannelAllocator &&channel_allocator, const ConfigureNetworkParams config_params,
                                   CacheManagerPtr cache_manager, std::shared_ptr<CoreOpMetadata> &&core_op_metadata,
                                   uint8_t core_op_index, const std::vector<std::string> &&network_index_map,
                                   LatencyMetersMap &&latency_meters,
                                   std::vector<vdma::ChannelId> &&config_channels_ids,
                                   std::shared_ptr<InternalBufferManager> internal_buffer_manager,
                                   std::shared_ptr<ActionListBufferBuilder> &&action_list_buffer_builder) :
    m_contexts_resources(),
    m_channel_allocator(std::move(channel_allocator)),
    m_vdma_device(vdma_device),
    m_driver(driver),
    m_config_params(config_params),
    m_cache_manager(cache_manager),
    m_intermediate_buffers(),
    m_core_op_metadata(std::move(core_op_metadata)),
    m_core_op_index(core_op_index),
    m_dynamic_context_count(0),
    m_total_context_count(0),
    m_network_index_map(std::move(network_index_map)),
    m_latency_meters(std::move(latency_meters)),
    m_is_configured(false),
    m_is_activated(false),
    m_config_channels_ids(std::move(config_channels_ids)),
    m_hw_only_desc_boundary_buffers(),
    m_hw_only_ccb_boundary_buffers(),
    m_internal_buffer_manager(std::move(internal_buffer_manager)),
    m_action_list_buffer_builder(std::move(action_list_buffer_builder)),
    m_hw_infer_channels_info()
{}

ResourcesManager::ResourcesManager(ResourcesManager &&other) noexcept :
    m_contexts_resources(std::move(other.m_contexts_resources)),
    m_channel_allocator(std::move(other.m_channel_allocator)),
    m_vdma_device(other.m_vdma_device),
    m_driver(other.m_driver),
    m_config_params(other.m_config_params),
    m_cache_manager(std::move(other.m_cache_manager)),
    m_intermediate_buffers(std::move(other.m_intermediate_buffers)),
    m_core_op_metadata(std::move(other.m_core_op_metadata)),
    m_core_op_index(other.m_core_op_index),
    m_dynamic_context_count(std::exchange(other.m_dynamic_context_count, static_cast<uint16_t>(0))),
    m_total_context_count(std::exchange(other.m_total_context_count, static_cast<uint16_t>(0))),
    m_network_index_map(std::move(other.m_network_index_map)),
    m_latency_meters(std::move(other.m_latency_meters)),
    m_boundary_channels(std::move(other.m_boundary_channels)),
    m_is_configured(std::exchange(other.m_is_configured, false)),
    m_is_activated(std::exchange(other.m_is_activated, false)),
    m_config_channels_ids(std::move(other.m_config_channels_ids)),
    m_hw_only_desc_boundary_buffers(std::move(other.m_hw_only_desc_boundary_buffers)),
    m_hw_only_ccb_boundary_buffers(std::move(other.m_hw_only_ccb_boundary_buffers)),
    m_internal_buffer_manager(std::move(other.m_internal_buffer_manager)),
    m_action_list_buffer_builder(std::move(other.m_action_list_buffer_builder)),
    m_hw_infer_channels_info(std::move(other.m_hw_infer_channels_info))
{}

hailo_status ResourcesManager::fill_infer_features(CONTROL_PROTOCOL__application_header_t &app_header)
{
    app_header.infer_features.preliminary_run_asap = m_core_op_metadata->supported_features().preliminary_run_asap;
    app_header.infer_features.batch_register_config = m_core_op_metadata->supported_features().batch_register_config;
    app_header.infer_features.can_fast_batch_switch = m_core_op_metadata->get_can_fast_batch_switch();
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
                TRY(const auto batch_size, get_network_batch_size(network_name_from_params));
                app_header.batch_size = batch_size;
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

uint16_t ResourcesManager::get_csm_buffer_size()
{
    // All config buffers on the same platform will have the same desc_page_size - because it is derived from the host
    return std::min(m_driver.desc_max_page_size(), vdma::DEFAULT_SG_PAGE_SIZE);
}

void ResourcesManager::fill_config_channel_info(CONTROL_PROTOCOL__application_header_t &app_header)
{
    app_header.config_channels_count = static_cast<uint8_t>(m_config_channels_ids.size());
    for (uint8_t config_channel_index = 0; config_channel_index < m_config_channels_ids.size(); config_channel_index++) {
        CONTEXT_SWITCH_DEFS__PACKED_VDMA_CHANNEL_ID__SET(app_header.config_channel_info[config_channel_index].packed_vdma_channel_id,
        m_config_channels_ids.at(config_channel_index).engine_index,
        m_config_channels_ids.at(config_channel_index).channel_index);
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
    return batch_size == HAILO_DEFAULT_BATCH_SIZE ? 1 : batch_size;

}

Expected<size_t> ResourcesManager::calc_default_queue_size(const LayerInfo &layer_info, uint16_t batch_size)
{
    const size_t transfers_per_frame = (layer_info.format.order == HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP) ?
        LayerInfoUtils::get_nms_layer_max_transfers_per_frame(layer_info) : 1;
    const size_t transfer_size = LayerInfoUtils::get_layer_transfer_size(layer_info);
    const size_t bytes_per_transfer = transfers_per_frame * transfer_size;

    // The minimum size of the queue is to have at least MIN_ACTIVE_TRANSFERS_SCALE * batch_size transfers
    const size_t min_active_transfers = (transfers_per_frame * MIN_ACTIVE_TRANSFERS_SCALE * batch_size);

    // The maximum size of the queue is to have at most MAX_ACTIVE_TRANSFERS_SCALE (notices that the batch is not
    // included here since otherwise the queue will be too big).
    const size_t max_active_transfers = std::max(min_active_transfers, (transfers_per_frame * MAX_ACTIVE_TRANSFERS_SCALE));

    // Amount of transfers optimal for the nn core queue size. We clamp it with min/max to make sure the boundaries are
    // good for us (enough frames can be queued but not too many)
    const size_t nn_core_queue_size = NN_CORE_QUEUE_SIZE_IN_BYTES / bytes_per_transfer;
    return clamp(nn_core_queue_size, min_active_transfers, max_active_transfers);
}

hailo_status ResourcesManager::create_boundary_vdma_channel(const LayerInfo &layer_info, bool use_enhanced_channel)
{
    const auto channel_direction = layer_info.direction == HAILO_H2D_STREAM ? HailoRTDriver::DmaDirection::H2D :
                                                                              HailoRTDriver::DmaDirection::D2H;
    TRY(const auto channel_id, get_available_channel_id(to_layer_identifier(layer_info), channel_direction,
        layer_info.dma_engine_index, use_enhanced_channel &&(HAILO_D2H_STREAM == layer_info.direction)));
    TRY(const auto network_batch_size, get_network_batch_size(layer_info.network_name));

    TRY(const auto device_arch, m_vdma_device.get_architecture());
    /* Add error in configure phase for invalid NMS parameters */
    if ((layer_info.format.order == HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP) && (HailoRTCommon::is_hailo1x_device_type(device_arch))) {
        CHECK(layer_info.nms_info.number_of_classes * layer_info.nms_info.chunks_per_frame * network_batch_size < HAILO15H_NMS_MAX_CLASSES, 
            HAILO_INVALID_ARGUMENT, "Invalid NMS parameters. Number of classes ({}) * division factor ({}) * batch size ({}) must be under {}",
            layer_info.nms_info.number_of_classes, layer_info.nms_info.chunks_per_frame, network_batch_size, HAILO15H_NMS_MAX_CLASSES);
    }

    TRY(const auto queue_size, calc_default_queue_size(layer_info, network_batch_size));
    CHECK(IS_FIT_IN_UINT16(queue_size), HAILO_INVALID_ARGUMENT,
        "calculated min_active_trans for vdma descriptor list is out of UINT16 range");

    const auto transfer_size = LayerInfoUtils::get_layer_transfer_size(layer_info);
    TRY(auto buffer_requirements, vdma::BufferSizesRequirements::get_buffer_requirements_for_boundary_channels(m_driver,
        layer_info.max_shmifo_size, static_cast<uint16_t>(MIN_ACTIVE_TRANSFERS_SCALE), static_cast<uint16_t>(queue_size),
        transfer_size));

    // TODO HRT-16020: Try not to allocate descriptors in ccb boundary hw infer
    const bool CIRCULAR = true;
    TRY(auto desc_list, vdma::DescriptorList::create(buffer_requirements.descs_count(), buffer_requirements.desc_page_size(),
        CIRCULAR, m_driver));

    auto latency_meter = (contains(m_latency_meters, layer_info.network_name)) ? m_latency_meters.at(layer_info.network_name) : nullptr;

    TRY(auto vdma_transfer_launcher, m_vdma_device.get_vdma_transfer_launcher());
    TRY(auto channel, vdma::BoundaryChannel::create(m_driver, channel_id, channel_direction, std::move(desc_list),
        vdma_transfer_launcher.get(), queue_size, false, layer_info.name, latency_meter));

    m_boundary_channels.add_channel(std::move(channel));
    return HAILO_SUCCESS;
}

Expected<vdma::BoundaryChannelPtr> ResourcesManager::get_boundary_vdma_channel_by_stream_name(const std::string &stream_name)
{
    return m_boundary_channels.get_by_name(stream_name);
}

Expected<std::shared_ptr<const vdma::BoundaryChannel>> ResourcesManager::get_boundary_vdma_channel_by_stream_name(const std::string &stream_name) const
{
    return const_cast<vdma::ChannelsGroup &>(m_boundary_channels).get_by_name(stream_name);
}

hailo_power_mode_t ResourcesManager::get_power_mode() const
{
    return m_config_params.power_mode;
}

ExpectedRef<vdma::VdmaEdgeLayer> ResourcesManager::create_intermediate_edge_layer(
    uint32_t transfer_size, uint16_t batch_size, uint8_t src_stream_index, uint16_t src_context_index,
    vdma::ChannelId d2h_channel_id, LayerType layer_type)
{
    auto edge_layer_key = std::make_pair(src_context_index, src_stream_index);
    TRY(auto buffer_info, m_internal_buffer_manager->get_intermediate_buffer(edge_layer_key));

    CHECK((layer_type == LayerType::DDR) || (layer_type == LayerType::INTER_CONTEXT), HAILO_INTERNAL_FAILURE,
        "Invalid layer type for intermediate buffer got {}", static_cast<int>(layer_type));

    const bool is_circular = (layer_type == LayerType::DDR);
    TRY(auto edge_layer, vdma::VdmaEdgeLayer::create(m_driver, buffer_info.buffer, buffer_info.edge_layer_plan.offset,
        buffer_info.edge_layer_plan.buffer_requirements.buffer_size(),
        buffer_info.edge_layer_plan.buffer_requirements.desc_page_size(),
        buffer_info.edge_layer_plan.buffer_requirements.descs_count(),
        is_circular, d2h_channel_id));

    if (LayerType::INTER_CONTEXT == layer_type) {
        // We have max_batch_size transfers, so we program them one by one. The last transfer should report interrupt
        // to the device.
        TRY(const auto desc_count_local, edge_layer->program_descriptors(transfer_size, InterruptsDomain::DEVICE, 0, 0,
            batch_size));
        (void)desc_count_local;
    } else {
        // Program all descriptors, no need for interrupt.
        const auto interrupts_domain = InterruptsDomain::NONE;
        const auto total_size = edge_layer->descs_count() * edge_layer->desc_page_size();
        TRY(const auto desc_count_local, edge_layer->program_descriptors(total_size, interrupts_domain, 0));
        (void)desc_count_local;
    }

    const auto key = std::make_pair(src_context_index, src_stream_index);
    auto emplace_res = m_intermediate_buffers.emplace(key, std::move(edge_layer));
    return std::ref(*emplace_res.first->second);
}

ExpectedRef<vdma::VdmaEdgeLayer> ResourcesManager::get_intermediate_edge_layer(const IntermediateBufferKey &key)
{
    auto buffer_it = m_intermediate_buffers.find(key);
    if (std::end(m_intermediate_buffers) == buffer_it) {
        return make_unexpected(HAILO_NOT_FOUND);
    }

    return std::ref(*buffer_it->second);
}

ExpectedRef<CacheBuffer> ResourcesManager::set_cache_input_channel(uint32_t cache_id, uint16_t batch_size,
    vdma::ChannelId channel_id)
{
    return m_cache_manager->set_cache_input_channel(m_core_op_metadata->core_op_name(), cache_id, batch_size, channel_id);
}

ExpectedRef<CacheBuffer> ResourcesManager::set_cache_output_channel(uint32_t cache_id, uint16_t batch_size,
    vdma::ChannelId channel_id)
{
    return m_cache_manager->set_cache_output_channel(m_core_op_metadata->core_op_name(), cache_id, batch_size, channel_id);
}

ExpectedRef<std::unordered_map<uint32_t, CacheBuffer>> ResourcesManager::get_cache_buffers()
{
    return m_cache_manager->get_cache_buffers(m_core_op_metadata->core_op_name());
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
    const uint16_t csm_buffer_size = get_csm_buffer_size();
    app_header.csm_buffer_size = csm_buffer_size;
    fill_config_channel_info(app_header);
    app_header.external_action_list_address = CONTEXT_SWITCH_DEFS__INVALID_DDR_CONTEXTS_BUFFER_ADDRESS;
    return app_header;
}

Expected<std::reference_wrapper<ContextResources>> ResourcesManager::add_new_context(
    CONTROL_PROTOCOL__context_switch_context_type_t context_type,
    bool is_aligned_ccws_on, const ConfigBufferInfoMap &config_info)
{
    CHECK_AS_EXPECTED(m_total_context_count < std::numeric_limits<uint16_t>::max(), HAILO_INVALID_CONTEXT_COUNT);

    TRY(auto context_resources, ContextResources::create(m_driver, context_type,
        m_config_channels_ids, config_info, m_internal_buffer_manager, is_aligned_ccws_on, m_ccws_section_mapped_buffers,
        m_nops_mapped_buffer));
    m_contexts_resources.emplace_back(std::move(context_resources));
    m_total_context_count++;
    if (CONTROL_PROTOCOL__CONTEXT_SWITCH_CONTEXT_TYPE_DYNAMIC == context_type) {
        m_dynamic_context_count++;
    }

    return std::ref(m_contexts_resources.back());
}

Expected<vdma::ChannelId> ResourcesManager::get_available_channel_id(const LayerIdentifier &layer_identifier,
    HailoRTDriver::DmaDirection direction, uint8_t engine_index, bool use_enhanced_channel)
{
    if (m_driver.dma_type() == HailoRTDriver::DmaType::PCIE) {
        // On PCIe we have only 1 engine. To support the same HEF with both PCIe and DRAM, we use default engine here
        engine_index = vdma::DEFAULT_ENGINE_INDEX;
    }

    return m_channel_allocator.get_available_channel_id(layer_identifier, direction, engine_index, use_enhanced_channel);
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
    return hailort::get_network_batch_size(m_config_params, network_name);
}

Expected<Buffer> ResourcesManager::read_intermediate_buffer(const IntermediateBufferKey &key)
{
    auto intermediate_buffer_it = m_intermediate_buffers.find(key);
    CHECK_AS_EXPECTED(std::end(m_intermediate_buffers) != intermediate_buffer_it,
        HAILO_NOT_FOUND, "Failed to find intermediate buffer for src_context {}, src_stream_index {}", key.first,
        key.second);

    TRY(auto buffer, Buffer::create(intermediate_buffer_it->second->size()));
    CHECK_SUCCESS(intermediate_buffer_it->second->read(buffer.data(), buffer.size(), 0));
    return buffer;
}

Expected<Buffer> ResourcesManager::read_cache_buffer(uint32_t cache_id)
{
    TRY(auto cache_buffers, get_cache_buffers());
    auto cache_buffer_it = cache_buffers.get().find(cache_id);
    CHECK_AS_EXPECTED(std::end(cache_buffers.get()) != cache_buffer_it, HAILO_NOT_FOUND,
        "Failed to find cache buffer for cache_id {}", cache_id);
    return cache_buffer_it->second.read_cache();
}

Expected<std::map<uint32_t, Buffer>> ResourcesManager::read_cache_buffers()
{
    std::map<uint32_t, Buffer> result;
    TRY(auto cache_buffers, get_cache_buffers());
    for (auto &cache_buffer : cache_buffers.get()) {
        TRY(auto buffer, cache_buffer.second.read_cache());
        result.emplace(cache_buffer.first, std::move(buffer));
    }

    return result;
}

hailo_status ResourcesManager::configure()
{
    m_is_configured = true;

    TRY(auto core_op_header, get_control_core_op_header());
    if ((Device::Type::INTEGRATED == m_vdma_device.get_type())
        && ((CONTEXT_SWITCH_CONFIG__MAX_BUFFER_SIZE_WITHOUT_HEADERS < get_action_list_buffer_builder()->get_action_list_buffer_size())
        || (is_env_variable_on(DDR_ACTION_LIST_ENV_VAR, DDR_ACTION_LIST_ENV_VAR_VALUE)))) {
        TRY(auto dma_address ,get_action_list_buffer_builder()->write_controls_to_ddr(m_driver));
        CHECK(IS_FIT_IN_UINT32(dma_address), HAILO_INVALID_ARGUMENT, "Invalid Mapped Address {} must fit in uint32",
            dma_address);
        core_op_header.external_action_list_address = static_cast<uint32_t>(dma_address);

        auto status = Control::context_switch_set_network_group_header(m_vdma_device, core_op_header);
        CHECK_SUCCESS(status);
    } else {
        auto status = Control::context_switch_set_network_group_header(m_vdma_device, core_op_header);
        CHECK_SUCCESS(status);
        status = Control::context_switch_set_context_info(m_vdma_device, get_action_list_buffer_builder()->get_controls());
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status ResourcesManager::enable_state_machine(uint16_t dynamic_batch_size, uint16_t batch_count)
{
    CHECK_SUCCESS(Control::enable_core_op(m_vdma_device, m_core_op_index, dynamic_batch_size, batch_count));
    // Enable over enable is possible (batch switch in the same NG), so there is no need to verify the state.
    set_is_activated(true);

    return HAILO_SUCCESS;
}

hailo_status ResourcesManager::reset_state_machine()
{
    if (!get_is_activated()) {
        return HAILO_SUCCESS;
    }

    set_is_activated(false);

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
    CHECK(get_is_activated(), HAILO_INTERNAL_FAILURE, "Cannot call start_vdma_interrupts_dispatcher when core-op already deactivated");

    TRY(auto interrupts_dispatcher, m_vdma_device.get_vdma_interrupts_dispatcher());
    return interrupts_dispatcher.get().start(m_boundary_channels);
}

hailo_status ResourcesManager::stop_vdma_interrupts_dispatcher()
{
    if (!get_is_activated()) {
        return HAILO_SUCCESS;
    }

    TRY(auto interrupts_dispatcher, m_vdma_device.get_vdma_interrupts_dispatcher());
    return interrupts_dispatcher.get().stop();
}

hailo_status ResourcesManager::start_vdma_transfer_launcher()
{
    CHECK(get_is_activated(), HAILO_INTERNAL_FAILURE, "Cannot call start_vdma_transfer_launcher when core-op already deactivated");
    TRY(auto vdma_transfer_launcher, m_vdma_device.get_vdma_transfer_launcher());
    vdma_transfer_launcher.get().start();
    return HAILO_SUCCESS;
}

hailo_status ResourcesManager::stop_vdma_transfer_launcher()
{
    if (!get_is_activated()) {
        return HAILO_SUCCESS;
    }

    TRY(auto vdma_transfer_launcher, m_vdma_device.get_vdma_transfer_launcher());
    vdma_transfer_launcher.get().stop();
    return HAILO_SUCCESS;
}

Expected<CONTROL_PROTOCOL__host_buffer_info_t> ResourcesManager::get_boundary_buffer_info(vdma::BoundaryChannel &channel,
    uint32_t transfer_size)
{
    if (CONTROL_PROTOCOL__DESC_BOUNDARY_CHANNEL == get_hw_infer_boundary_channel_mode()) {
        auto &desc_list = channel.get_desc_list();
        return vdma::VdmaEdgeLayer::get_host_buffer_info(vdma::VdmaEdgeLayer::Type::SCATTER_GATHER, desc_list.dma_address(),
            desc_list.desc_page_size(), desc_list.count(), transfer_size);
    } else {
        CHECK(m_hw_only_ccb_boundary_buffers.end() != m_hw_only_ccb_boundary_buffers.find(channel.get_channel_id()),
            HAILO_INTERNAL_FAILURE, "Error could not find channel info for channel {}", channel.get_channel_id());
        const auto &ccb_boundary_channel_buffer = m_hw_only_ccb_boundary_buffers[channel.get_channel_id()];
        const uint32_t desc_count = static_cast<uint32_t>(DIV_ROUND_UP(ccb_boundary_channel_buffer->size(),
            HW_INFER_CCB_DESC_PAGE_SIZE));
        return vdma::VdmaEdgeLayer::get_host_buffer_info(vdma::VdmaEdgeLayer::Type::CONTINUOUS,
            ccb_boundary_channel_buffer->dma_address(), HW_INFER_CCB_DESC_PAGE_SIZE, desc_count,
            static_cast<uint32_t>(ccb_boundary_channel_buffer->size()));
    }
}

Expected<uint16_t> ResourcesManager::program_desc_for_hw_only_flow(vdma::DescriptorList &desc_list,
    vdma::MappedBuffer &mapped_buffer, vdma::ChannelId channel_id,
    const uint32_t single_transfer_size, const uint16_t dynamic_batch_size, const uint16_t batch_count)
{
    size_t acc_desc_offset = 0;
    for (uint16_t batch_index = 0; batch_index < batch_count; batch_index++) {
        for (uint16_t transfer_index = 0; transfer_index < dynamic_batch_size; transfer_index++) {
            const auto last_desc_interrupts_domain = ((dynamic_batch_size - 1) == transfer_index) ?
                InterruptsDomain::DEVICE : InterruptsDomain::NONE;
            const bool should_bind = false;
            CHECK_SUCCESS(desc_list.program(mapped_buffer, single_transfer_size,
                (acc_desc_offset * desc_list.desc_page_size()), channel_id, static_cast<uint32_t>(acc_desc_offset), 1,
                should_bind, last_desc_interrupts_domain));
            acc_desc_offset += desc_list.descriptors_in_buffer(single_transfer_size);
        }
    }
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(acc_desc_offset), HAILO_INTERNAL_FAILURE,
        "calculated acc_desc_offset for vdma descriptor list is out of UINT16 range");
    return static_cast<uint16_t>(acc_desc_offset);
}

hailo_status ResourcesManager::allocate_mapped_buffer_for_hw_only_infer(
    vdma::BoundaryChannelPtr boundary_channel_ptr, const HailoRTDriver::DmaDirection direction,
    const uint32_t single_transfer_size, uint16_t batch_size, uint16_t batch_count)
{
    const auto total_frames_per_run = batch_size * batch_count;

    switch (get_hw_infer_boundary_channel_mode()) {
        case CONTROL_PROTOCOL__DESC_BOUNDARY_CHANNEL:
        {
            auto &desc_list = boundary_channel_ptr->get_desc_list();
            const auto descs_per_transfer = desc_list.descriptors_in_buffer(single_transfer_size);
            const auto total_desc_count = total_frames_per_run * descs_per_transfer;

            CHECK(IS_FIT_IN_UINT16(total_desc_count), HAILO_INVALID_ARGUMENT,
                "calculated total_desc_count for vdma descriptor list is out of UINT16 range");
            
            TRY(auto mapped_buffer, vdma::MappedBuffer::create_shared_by_allocation(
                total_desc_count * desc_list.desc_page_size(), m_driver, direction));
            m_hw_only_desc_boundary_buffers[boundary_channel_ptr->get_channel_id()] = std::move(mapped_buffer);
            break;
        }
        case CONTROL_PROTOCOL__CCB_BOUNDARY_CHANNEL:
        {
            // Divide with round up
            const auto descs_per_transfer = DIV_ROUND_UP(single_transfer_size, HW_INFER_CCB_DESC_PAGE_SIZE);
            const auto total_desc_count = total_frames_per_run * descs_per_transfer;
            CHECK(IS_FIT_IN_UINT16(total_desc_count), HAILO_INVALID_ARGUMENT,
                "calculated total_desc_count for ccb buffer is out of UINT16 range");

            // Use ccb buffer for boundary layer hw only infer instead of normal buffer
            TRY(auto ccb_bufer, vdma::ContinuousBuffer::create(total_desc_count * HW_INFER_CCB_DESC_PAGE_SIZE, m_driver));
            auto buffer_ptr = make_shared_nothrow<vdma::ContinuousBuffer>(std::move(ccb_bufer));
            CHECK(nullptr != buffer_ptr, HAILO_OUT_OF_HOST_MEMORY);

            m_hw_only_ccb_boundary_buffers[boundary_channel_ptr->get_channel_id()] = std::move(buffer_ptr);
            break;
        }
        default:
        {
            LOGGER__ERROR("Invalid boundary channel mode {}", static_cast<uint8_t>(get_hw_infer_boundary_channel_mode()));
            return HAILO_INVALID_ARGUMENT;
        }
    }

    return HAILO_SUCCESS;
}

hailo_status ResourcesManager::configure_mapped_buffer_for_hw_only_infer(vdma::BoundaryChannelPtr boundary_channel_ptr,
    const uint32_t single_transfer_size, uint16_t batch_size, uint16_t batch_count,
    CONTROL_PROTOCOL__hw_infer_channels_info_t &channels_info)
{
    std::pair<vdma::ChannelId, uint16_t> channel_info_pair;

    switch (get_hw_infer_boundary_channel_mode()) {
        case CONTROL_PROTOCOL__DESC_BOUNDARY_CHANNEL:
        {
            auto &desc_list = boundary_channel_ptr->get_desc_list();
            CHECK(m_hw_only_desc_boundary_buffers.end() != m_hw_only_desc_boundary_buffers.find(
                boundary_channel_ptr->get_channel_id()), HAILO_INTERNAL_FAILURE);
            auto hw_infer_mapped_buffer = m_hw_only_desc_boundary_buffers[boundary_channel_ptr->get_channel_id()];

            static const auto DEFAULT_BUFFER_OFFSET = 0;
            auto status = desc_list.program(*hw_infer_mapped_buffer, hw_infer_mapped_buffer->size(),
                DEFAULT_BUFFER_OFFSET, boundary_channel_ptr->get_channel_id());
            CHECK_SUCCESS(status);

            TRY(auto desc_programed, program_desc_for_hw_only_flow(desc_list, *hw_infer_mapped_buffer,
                boundary_channel_ptr->get_channel_id(), single_transfer_size, batch_size, batch_count));
            assert(static_cast<uint16_t>(desc_programed == DIV_ROUND_UP(hw_infer_mapped_buffer->size(),
                desc_list.desc_page_size())));

            channel_info_pair = std::make_pair(boundary_channel_ptr->get_channel_id(), desc_programed);
            break;
        }
        case CONTROL_PROTOCOL__CCB_BOUNDARY_CHANNEL:
        {
            // Divide with round up
            const auto descs_per_transfer = DIV_ROUND_UP(single_transfer_size, HW_INFER_CCB_DESC_PAGE_SIZE);
            const auto total_desc_count = batch_size * batch_count * descs_per_transfer;

            CHECK(IS_FIT_IN_UINT16(total_desc_count), HAILO_INVALID_ARGUMENT,
                "calculated total_desc_count for ccb buffer is out of UINT16 range");

            channel_info_pair = std::make_pair(boundary_channel_ptr->get_channel_id(),
                static_cast<uint16_t>(total_desc_count));
            break;
        }
        default:
        {
            LOGGER__ERROR("Invalid boundary channel mode {}", static_cast<uint8_t>(get_hw_infer_boundary_channel_mode()));
            return HAILO_INVALID_ARGUMENT;
        }
    }

    add_channel_to_hw_infer_channel_info(std::move(channel_info_pair), channels_info);
    return HAILO_SUCCESS;
}

void ResourcesManager::add_channel_to_hw_infer_channel_info(std::pair<vdma::ChannelId, uint16_t> channel_info,
    CONTROL_PROTOCOL__hw_infer_channels_info_t &channels_info)
{
    auto next_channel_info = &channels_info.channel_info[channels_info.channel_count];
    assert(channels_info.channel_count < CONTROL_PROTOCOL__MAX_TOTAL_CHANNEL_COUNT);

    next_channel_info->engine_index = channel_info.first.engine_index;
    next_channel_info->channel_index = channel_info.first.channel_index;
    next_channel_info->desc_programed = channel_info.second;

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

hailo_status ResourcesManager::configure_boundary_channels_for_hw_infer(uint16_t batch_size, uint16_t batch_count)
{
    hailo_status status = HAILO_UNINITIALIZED;
    // Function should only be called in hw infer mode
    CHECK(is_env_variable_on(HAILO_CONFIGURE_FOR_HW_INFER_ENV_VAR), HAILO_INTERNAL_FAILURE,
        "Error called hw infer without env variable for hw infer set - please set environment variable and try again");
        
    CONTROL_PROTOCOL__hw_infer_channels_info_t channels_info = {};
    channels_info.channel_count = 0;
    for (const auto &metadata_layer_info : m_core_op_metadata->get_all_layer_infos()) {
        const std::vector<LayerInfo> &layers = metadata_layer_info.is_multi_planar ? metadata_layer_info.planes :
            std::vector<LayerInfo>{metadata_layer_info};
        for (const auto &layer_info : layers) {
            const auto &stream_infos = LayerInfoUtils::get_stream_infos_from_layer_info(layer_info);
            for (auto &stream_info : stream_infos) {
                auto single_transfer_size = (HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP == stream_info.format.order) ?
                    stream_info.nms_info.bbox_size : stream_info.hw_frame_size;
                TRY(auto boundary_channel_ptr, get_boundary_vdma_channel_by_stream_name(layer_info.name));
                status = configure_mapped_buffer_for_hw_only_infer(boundary_channel_ptr, single_transfer_size, batch_size,
                    batch_count, channels_info);
                CHECK_SUCCESS(status);
            }
        }
    }

    m_hw_infer_channels_info = channels_info;
    return HAILO_SUCCESS;
}

hailo_status ResourcesManager::allocate_boundary_channels_buffers_hw_infer()
{
    hailo_status status = HAILO_UNINITIALIZED;
    // Function should only be called in hw infer mode
    CHECK(is_env_variable_on(HAILO_CONFIGURE_FOR_HW_INFER_ENV_VAR), HAILO_INTERNAL_FAILURE,
        "Error called hw infer without env variable for hw infer set - please set environment variable and try again");

    TRY(const auto batch_size, get_batch_size());
    TRY(const auto batch_count, calc_hw_infer_batch_count(batch_size));

    for (const auto &metadata_layer_info : m_core_op_metadata->get_all_layer_infos()) {
        const std::vector<LayerInfo> &layers = metadata_layer_info.is_multi_planar ? metadata_layer_info.planes :
            std::vector<LayerInfo>{metadata_layer_info};
        for (const auto &layer_info : layers) {
            const auto &stream_infos = LayerInfoUtils::get_stream_infos_from_layer_info(layer_info);
            for (auto &stream_info : stream_infos) {
                auto single_transfer_size = (HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP == stream_info.format.order) ?
                    stream_info.nms_info.bbox_size : stream_info.hw_frame_size;
                TRY(auto boundary_channel_ptr, get_boundary_vdma_channel_by_stream_name(layer_info.name));
                const auto direction = (layer_info.direction == HAILO_H2D_STREAM) ?
                    HailoRTDriver::DmaDirection::H2D : HailoRTDriver::DmaDirection::D2H;
                status = allocate_mapped_buffer_for_hw_only_infer(boundary_channel_ptr, direction,
                    single_transfer_size, batch_size, batch_count);
                CHECK_SUCCESS(status);
            }
        }
    }

    return HAILO_SUCCESS;
}

Expected<uint16_t> ResourcesManager::calc_hw_infer_batch_count(uint16_t dynamic_batch_size)
{
    uint16_t batch_count = UINT16_MAX;

    for (const auto &metadata_layer_info : m_core_op_metadata->get_all_layer_infos()) {
        const std::vector<LayerInfo> &layers = metadata_layer_info.is_multi_planar ? metadata_layer_info.planes :
            std::vector<LayerInfo>{metadata_layer_info};
        for (const auto &layer_info : layers) {
            const auto &stream_infos = LayerInfoUtils::get_stream_infos_from_layer_info(layer_info);
            for (auto &stream_info : stream_infos) {
                uint32_t single_transfer_size = LayerInfoUtils::get_stream_transfer_size(stream_info, layer_info);
                TRY(auto boundary_channel_ptr, get_boundary_vdma_channel_by_stream_name(layer_info.name));
                const auto max_batch_transfers = boundary_channel_ptr->get_desc_list().max_transfers(single_transfer_size * dynamic_batch_size);
                // infer batch count is the lowest number of "Max transfers" per descriptor list that for all given boundary channels.
                batch_count = MIN(batch_count, max_batch_transfers);
            }
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
    static constexpr auto INFER_TIMEOUT = std::chrono::milliseconds(120000);

    CONTROL_PROTOCOL__boundary_channel_mode_t boundary_channel_mode = get_hw_infer_boundary_channel_mode();
    TRY(const auto batch_size, get_batch_size());
    TRY(const auto batch_count, calc_hw_infer_batch_count(batch_size));

    configure_boundary_channels_for_hw_infer(batch_size, batch_count);

    std::condition_variable infer_done_cond;
    auto status = set_hw_infer_done_notification(infer_done_cond);
    CHECK_SUCCESS_AS_EXPECTED(status);

    std::mutex mutex;
    std::unique_lock<std::mutex> lock(mutex);

    status = Control::start_hw_only_infer(m_vdma_device, m_core_op_index, batch_size,
        batch_count, &m_hw_infer_channels_info, boundary_channel_mode);
    CHECK_SUCCESS_AS_EXPECTED(status);

    infer_done_cond.wait_for(lock, INFER_TIMEOUT);

    status = Control::stop_hw_only_infer(m_vdma_device, &fw_infer_results);
    CHECK_SUCCESS_AS_EXPECTED(status);

    TRY(const auto single_frame_transfer_size, m_core_op_metadata->get_total_transfer_size());

    return hw_infer_calc_stats(batch_count, batch_size, single_frame_transfer_size,
        fw_infer_results.infer_cycles);
}


hailo_status ResourcesManager::fill_internal_buffers_info()
{
    TRY(const auto edge_layer_info, InternalBufferPlanner::get_edge_layer_infos(*m_core_op_metadata, m_config_params));

    auto status = m_internal_buffer_manager->plan_and_execute(edge_layer_info,
        InternalBufferPlanner::Type::SINGLE_BUFFER_PER_BUFFER_TYPE,
        m_core_op_metadata->dynamic_contexts().size());
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

static Expected<size_t> parse_buffer_size_from_env_var() {
    auto expected_mapped_bufffer_size = get_env_variable_as_size(HAILO_ALIGNED_CCWS_MAPPED_BUFFER_SIZE_ENV_VAR);
    if (HAILO_NOT_FOUND == expected_mapped_bufffer_size.status()) {
        // Environment variable not set, return the default.
        return HAILO_ALIGNED_CCWS_MAPPED_BUFFER_SIZE;
    } else {
        return expected_mapped_bufffer_size;
    }
}

hailo_status ResourcesManager::map_and_set_ccws_section_buffer(BufferPtr hef_as_buffer, size_t offset_to_ccws_section, uint64_t ccws_section_size, HailoRTDriver &driver)
{
    assert(m_ccws_section_mapped_buffers.empty());

    m_hef_as_buffer = hef_as_buffer; // keep the hef buffer alive

    /*
    * Optimization for the aligned_ccws feature.
    *
    * Previously, we used to have a single huge mapped buffer for the entire CCWS section, that is splitted to sg entries (each sg entry is offset + size).
    * That way - if we configure from offset 1Gb in the ccws section - we will have to iterate over all of the entries until that offset to find the right sg entry - it was very inefficient.
    *
    * With this optimization, we split the huge mapped buffer to smaller buffers, such that the search for each sg entry will be much faster.
    */

    TRY(const auto buffer_size, parse_buffer_size_from_env_var());
    const auto buffers_count = DIV_ROUND_UP(ccws_section_size, buffer_size);
    for (size_t i = 0; i < buffers_count; i++) {
        auto current_buffer_size = (i == (buffers_count - 1)) ? (ccws_section_size % buffer_size) : buffer_size;
        TRY(auto dmable_buffer_ptr, vdma::DmaAbleBuffer::create_from_user_address(
            hef_as_buffer->data() + offset_to_ccws_section + i * buffer_size, current_buffer_size));
        TRY(auto mapped_buffer_ptr, vdma::MappedBuffer::create_shared(dmable_buffer_ptr, driver,
            HailoRTDriver::DmaDirection::H2D));
        m_ccws_section_mapped_buffers.push_back(mapped_buffer_ptr);
    }

    return HAILO_SUCCESS;
}

} /* namespace hailort */
