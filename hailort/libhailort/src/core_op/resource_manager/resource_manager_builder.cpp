/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file resource_manager_builder.cpp
 * @brief Builds a ResourcesManager object for the given CoreOp.
 **/

#include "resource_manager_builder.hpp"
#include "device_common/control.hpp"
#include "periph_calculator.hpp"
#include "hef/hef_internal.hpp"
#include "common/file_utils.hpp"
#include "vdma/memory/vdma_edge_layer.hpp"

namespace hailort
{

static Expected<LayerInfo> update_layer_info(const LayerInfo &original_layer_info,
    const CONTROL_PROTOCOL__host_buffer_info_t &buffer_info, const CONTROL_PROTOCOL__hw_consts_t &hw_consts,
    const HEFHwArch &hw_arch, const bool is_core_hw_padding_config_in_dfc)
{
    LayerInfo local_layer_info = original_layer_info;

    // TODO HRT-12099 - remove when we remove support for hefs with no max_shmifo size
    if (local_layer_info.max_shmifo_size == 0) {
        local_layer_info.max_shmifo_size = hw_consts.default_initial_credit_size;
    }

    local_layer_info.nn_stream_config.is_core_hw_padding_config_in_dfc = is_core_hw_padding_config_in_dfc;

    TRY(auto updated_periph_layer_info, PeriphCalculator::calculate_periph_registers(local_layer_info,
        buffer_info.desc_page_size, hw_arch, is_core_hw_padding_config_in_dfc));

    const auto periph_bytes_per_buffer = updated_periph_layer_info.nn_stream_config.periph_bytes_per_buffer;
    CHECK_AS_EXPECTED((static_cast<uint32_t>(periph_bytes_per_buffer) <= (hw_consts.outbound_data_stream_size - 8 - buffer_info.desc_page_size)
        || (updated_periph_layer_info.direction != HAILO_D2H_STREAM)),HAILO_INTERNAL_FAILURE,
        "Current periph_bytes_per_buffer is {} which is too high. Exiting.", periph_bytes_per_buffer);

    return updated_periph_layer_info;
}

static hailo_status fill_boundary_input_layer_impl(ContextResources &context_resources,
    ResourcesManager &resources_manager, const LayerInfo layer_info, const CONTROL_PROTOCOL__hw_consts_t &hw_consts,
    const HEFHwArch &hw_arch)
{
    const auto transfer_size = LayerInfoUtils::get_layer_transfer_size(layer_info);

    TRY(const auto vdma_channel, resources_manager.get_boundary_vdma_channel_by_stream_name(layer_info.name));

    TRY(const auto buffer_info, resources_manager.get_boundary_buffer_info(*vdma_channel, transfer_size));
    const bool is_core_hw_padding_config_in_dfc = resources_manager.get_supported_features().core_hw_padding_config_in_dfc;
    TRY(auto local_layer_info, update_layer_info(layer_info, buffer_info, hw_consts, hw_arch,
        is_core_hw_padding_config_in_dfc));

    const auto channel_id = vdma_channel->get_channel_id();
    auto status = context_resources.add_edge_layer(std::move(local_layer_info), channel_id, buffer_info,
        resources_manager.get_supported_features());
    CHECK_SUCCESS(status);

    LOGGER__TRACE("Boundary input stream: {} h2d_channel: {}.", layer_info.stream_index, channel_id);
    return HAILO_SUCCESS;
}

static hailo_status fill_boundary_input_layer(ContextResources &context_resources,
    ResourcesManager &resources_manager, const LayerInfo layer_info, const CONTROL_PROTOCOL__hw_consts_t &hw_consts,
    const HEFHwArch &hw_arch)
{
    if (layer_info.is_multi_planar) {
        for (auto &plane : layer_info.planes) {
            auto status = fill_boundary_input_layer_impl(context_resources, resources_manager, plane, hw_consts, hw_arch);
            CHECK_SUCCESS(status);
        }
        return HAILO_SUCCESS;
    }

    return fill_boundary_input_layer_impl(context_resources, resources_manager, layer_info, hw_consts, hw_arch);
}

static hailo_status fill_inter_context_input_layer(ContextResources &context_resources,
    ResourcesManager &resources_manager, const LayerInfo &layer_info, const CONTROL_PROTOCOL__hw_consts_t &hw_consts,
    const HEFHwArch &hw_arch)
{
    TRY(const auto channel_id, resources_manager.get_available_channel_id(to_layer_identifier(layer_info),
        HailoRTDriver::DmaDirection::H2D, layer_info.dma_engine_index, false));

    const auto frame_credits_in_bytes = LayerInfoUtils::get_layer_transfer_size(layer_info);

    // Get inter context edge layer previously created
    const auto &connected_context = layer_info.connected_context_info;
    auto intermediate_buffer_key = std::make_pair(connected_context.context_index, connected_context.stream_index);
    TRY(auto inter_context_buffer, resources_manager.get_intermediate_edge_layer(intermediate_buffer_key),
        "Failed to find inter context buffer for src context {}, src_stream_index {}",
        connected_context.context_index, connected_context.stream_index);

    const bool is_core_hw_padding_config_in_dfc = resources_manager.get_supported_features().core_hw_padding_config_in_dfc;
    TRY(auto local_layer_info, update_layer_info(layer_info, inter_context_buffer.get().get_host_buffer_info(frame_credits_in_bytes), hw_consts,
        hw_arch, is_core_hw_padding_config_in_dfc));

    auto status = context_resources.add_edge_layer(std::move(local_layer_info), channel_id,
        inter_context_buffer.get().get_host_buffer_info(frame_credits_in_bytes), resources_manager.get_supported_features());
    CHECK_SUCCESS(status);

    LOGGER__TRACE("Intermediate edge key: {}:{} src_context:{}, dst_context: {}, h2d_channel {}.",
        connected_context.context_index, connected_context.stream_index,
        layer_info.connected_context_info.context_index, layer_info.context_index, channel_id);

    return HAILO_SUCCESS;
}

static hailo_status fill_boundary_output_layer(ContextResources &context_resources,
    ResourcesManager &resources_manager, const LayerInfo &layer_info, const CONTROL_PROTOCOL__hw_consts_t &hw_consts,
    const HEFHwArch &hw_arch)
{
    const auto transfer_size = LayerInfoUtils::get_layer_transfer_size(layer_info);

    TRY(const auto vdma_channel, resources_manager.get_boundary_vdma_channel_by_stream_name(layer_info.name));

    TRY(const auto buffer_info, resources_manager.get_boundary_buffer_info(*vdma_channel, transfer_size));
    const bool is_core_hw_padding_config_in_dfc = resources_manager.get_supported_features().core_hw_padding_config_in_dfc;
    TRY(auto local_layer_info, update_layer_info(layer_info, buffer_info, hw_consts, hw_arch,
        is_core_hw_padding_config_in_dfc));

    const auto channel_id = vdma_channel->get_channel_id();
    auto status = context_resources.add_edge_layer(std::move(local_layer_info), channel_id, buffer_info,
        resources_manager.get_supported_features());
    CHECK_SUCCESS(status);

    LOGGER__TRACE("Boundary output stream: {} d2h_channel: {}.", layer_info.stream_index, channel_id);
    return HAILO_SUCCESS;
}

static hailo_status fill_inter_context_output_layer(ContextResources &context_resources,
    ResourcesManager &resources_manager, const LayerInfo &layer_info,
    const CONTROL_PROTOCOL__hw_consts_t &hw_consts, const HEFHwArch &hw_arch)
{
    TRY(const auto channel_id, resources_manager.get_available_channel_id(to_layer_identifier(layer_info),
        HailoRTDriver::DmaDirection::D2H, layer_info.dma_engine_index, false));

    const auto frame_credits_in_bytes = LayerInfoUtils::get_layer_transfer_size(layer_info);

    TRY(const auto network_batch_size, resources_manager.get_network_batch_size(layer_info.network_name));

    TRY(auto inter_context_buffer, resources_manager.create_intermediate_edge_layer(frame_credits_in_bytes,
        network_batch_size, layer_info.stream_index, layer_info.context_index,
        channel_id, LayerType::INTER_CONTEXT));

    const bool is_core_hw_padding_config_in_dfc = resources_manager.get_supported_features().core_hw_padding_config_in_dfc;
    TRY(auto local_layer_info, update_layer_info(layer_info,
        inter_context_buffer.get().get_host_buffer_info(frame_credits_in_bytes), hw_consts,
        hw_arch, is_core_hw_padding_config_in_dfc));

    auto status = context_resources.add_edge_layer(std::move(local_layer_info), channel_id,
        inter_context_buffer.get().get_host_buffer_info(frame_credits_in_bytes), resources_manager.get_supported_features());
    CHECK_SUCCESS(status);

    LOGGER__TRACE("Inter-context output stream {}, src_context:{}, d2h_channel {}.",
        layer_info.stream_index, layer_info.context_index, channel_id);
    return HAILO_SUCCESS;
}

static hailo_status fill_ddr_output_layer(ContextResources &context_resources,
    ResourcesManager &resources_manager, const LayerInfo &layer_info,
    const CONTROL_PROTOCOL__hw_consts_t &hw_consts, const HEFHwArch &hw_arch)
{
    CHECK(resources_manager.get_supported_features().padded_ddr_buffers, HAILO_HEF_NOT_SUPPORTED,
        "Failed opening non-compatible HEF that uses the following deprecated features: host-managed DDR buffers."
        "Please re-compile the HEF using a newer Dataflow Compiler version (v3.11.0 or newer)");

    // It is assumed that output channels are parsed before input channels.
    // Allocate vdma channel index for both edges
    const auto h2d_stream_index = layer_info.connected_context_info.stream_index;
    const auto h2d_layer_identifier = std::make_tuple(LayerType::DDR, HAILO_H2D_STREAM,
            layer_info.name, h2d_stream_index);
    TRY(const auto h2d_channel_id, resources_manager.get_available_channel_id(h2d_layer_identifier,
        HailoRTDriver::DmaDirection::H2D, layer_info.connected_context_info.dma_engine_index, false));

    const auto d2h_stream_index = layer_info.stream_index;
    const auto d2h_layer_identifier = std::make_tuple(LayerType::DDR, HAILO_D2H_STREAM,
            layer_info.name, d2h_stream_index);
    TRY(const auto d2h_channel_id, resources_manager.get_available_channel_id(d2h_layer_identifier,
        HailoRTDriver::DmaDirection::D2H, layer_info.dma_engine_index, false));

    // In DDR - always use core bytes per buffer as row size
    const auto row_size = static_cast<uint16_t>(layer_info.nn_stream_config.core_bytes_per_buffer);
    CHECK(0 == (row_size % PERIPH_BYTES_PER_BUFFER_DDR_ALIGNMENT_SIZE), HAILO_INVALID_ARGUMENT,
        "DDR Row size ({}) must be aligned to {}", row_size, PERIPH_BYTES_PER_BUFFER_DDR_ALIGNMENT_SIZE);
    const uint16_t DDR_LAYER_BATCH_SIZE = 1;

    // Create the ddr edge layer
    TRY(auto ddr_buffer, resources_manager.create_intermediate_edge_layer(row_size, DDR_LAYER_BATCH_SIZE,
        d2h_stream_index, layer_info.context_index, d2h_channel_id, LayerType::DDR));

    context_resources.add_ddr_channels_info(d2h_channel_id, d2h_stream_index, h2d_channel_id, h2d_stream_index,
        ddr_buffer.get().get_host_buffer_info(row_size), layer_info.network_index, row_size, layer_info.ddr_info.min_buffered_rows,
        layer_info.ddr_info.total_buffers_per_frame);

    const bool is_core_hw_padding_config_in_dfc = resources_manager.get_supported_features().core_hw_padding_config_in_dfc;
    TRY(auto local_layer_info, update_layer_info(layer_info, ddr_buffer.get().get_host_buffer_info(row_size), hw_consts,
        hw_arch, is_core_hw_padding_config_in_dfc));

    auto status = context_resources.add_edge_layer(std::move(local_layer_info), d2h_channel_id,
        ddr_buffer.get().get_host_buffer_info(row_size), resources_manager.get_supported_features());
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

static hailo_status fill_ddr_input_layer(ContextResources &context_resources, ResourcesManager &resources_manager,
    const LayerInfo &layer_info, const CONTROL_PROTOCOL__hw_consts_t &hw_consts, const HEFHwArch &hw_arch)
{
    auto connected_stream_index = layer_info.connected_context_info.stream_index;
    TRY(const auto ddr_info, context_resources.get_ddr_channels_info(connected_stream_index),
        "Matching DDR layer as not found for context {} src stream {}", layer_info.context_index, connected_stream_index);
    LOGGER__TRACE("DDR layer: input stream_index: {}, output stream_index: {}, h2d_channel {}, d2h_channel: {}.",
        ddr_info.h2d_stream_index, ddr_info.d2h_stream_index, ddr_info.h2d_channel_id, ddr_info.d2h_channel_id);

    CHECK(layer_info.stream_index == ddr_info.h2d_stream_index, HAILO_INVALID_HEF, "DDR channel pair mismatch in h2d channel");
    CHECK(layer_info.connected_context_info.stream_index == ddr_info.d2h_stream_index, HAILO_INVALID_HEF, "DDR channel pair mismatch in d2h channel");
    CHECK(layer_info.network_index == ddr_info.network_index, HAILO_INVALID_HEF, "DDR channel pair mismatch network_index");

    const bool is_core_hw_padding_config_in_dfc = resources_manager.get_supported_features().core_hw_padding_config_in_dfc;
    TRY(auto local_layer_info, update_layer_info(layer_info, ddr_info.host_buffer_info, hw_consts,
        hw_arch, is_core_hw_padding_config_in_dfc));

    auto status = context_resources.add_edge_layer(std::move(local_layer_info), ddr_info.h2d_channel_id,
        ddr_info.host_buffer_info, resources_manager.get_supported_features());
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

static hailo_status fill_cache_output_layer(ContextResources &context_resources, ResourcesManager &resources_manager,
    const LayerInfo &layer_info, const CONTROL_PROTOCOL__hw_consts_t &hw_consts, const HEFHwArch &hw_arch)
{
    TRY(const auto channel_id, resources_manager.get_available_channel_id(to_layer_identifier(layer_info),
        HailoRTDriver::DmaDirection::D2H, layer_info.dma_engine_index, false));

    TRY(const auto network_batch_size, resources_manager.get_network_batch_size(layer_info.network_name));
    TRY(auto cache_buffer, resources_manager.set_cache_output_channel(layer_info.cache_info.cache_id,
        network_batch_size, channel_id));

    const bool is_core_hw_padding_config_in_dfc = resources_manager.get_supported_features().core_hw_padding_config_in_dfc;
    TRY(auto local_layer_info, update_layer_info(layer_info, cache_buffer.get().get_host_output_buffer_info(), hw_consts,
        hw_arch, is_core_hw_padding_config_in_dfc));
    local_layer_info.cache_info.batch_size = cache_buffer.get().output_batch_size();

    auto status = context_resources.add_edge_layer(std::move(local_layer_info), channel_id,
        cache_buffer.get().get_host_output_buffer_info(), resources_manager.get_supported_features());
    CHECK_SUCCESS(status);

    LOGGER__TRACE("Cache id {}: output stream {}, d2h_channel {}, context {}",
        layer_info.cache_info.cache_id, layer_info.stream_index, channel_id, layer_info.context_index);
    return HAILO_SUCCESS;
}

static hailo_status fill_cache_input_layer(ContextResources &context_resources, ResourcesManager &resources_manager,
    const LayerInfo &layer_info, const CONTROL_PROTOCOL__hw_consts_t &hw_consts, const HEFHwArch &hw_arch)
{
    TRY(const auto channel_id, resources_manager.get_available_channel_id(to_layer_identifier(layer_info),
        HailoRTDriver::DmaDirection::H2D, layer_info.dma_engine_index, false));

    TRY(const auto network_batch_size, resources_manager.get_network_batch_size(layer_info.network_name));
    TRY(auto cache_buffer, resources_manager.set_cache_input_channel(layer_info.cache_info.cache_id, network_batch_size, channel_id));

    const bool is_core_hw_padding_config_in_dfc = resources_manager.get_supported_features().core_hw_padding_config_in_dfc;
    TRY(auto local_layer_info, update_layer_info(layer_info, cache_buffer.get().get_host_input_buffer_info(), hw_consts,
        hw_arch, is_core_hw_padding_config_in_dfc));

    auto status = context_resources.add_edge_layer(std::move(local_layer_info), channel_id,
        cache_buffer.get().get_host_input_buffer_info(), resources_manager.get_supported_features());
    CHECK_SUCCESS(status);

    LOGGER__TRACE("Cache id {}: input stream {}, h2d_channel {}, context {}",
        layer_info.cache_info.cache_id, layer_info.stream_index, channel_id, layer_info.context_index);

    return HAILO_SUCCESS;
}

static hailo_status add_ddr_buffers_info(std::vector<ContextSwitchConfigActionPtr> &configuration_actions,
    const ContextResources &context_resources)
{
    bool start_fw_ddr_buffer_task = false;
    for (const auto &ddr_info : context_resources.get_ddr_channels_infos()) {
        if (ddr_info.need_manual_credit_management()) {
            TRY(auto ddr_pair_action, DdrPairInfoAction::create(ddr_info.h2d_channel_id, ddr_info.d2h_channel_id,
                ddr_info.network_index, ddr_info.descriptors_per_frame(), ddr_info.descs_count()));
            configuration_actions.emplace_back(std::move(ddr_pair_action));

            start_fw_ddr_buffer_task = true;
        }
    }

    if (start_fw_ddr_buffer_task) {
        TRY(auto start_ddr_buffering_action, StartDdrBufferingTaskAction::create());
        configuration_actions.emplace_back(std::move(start_ddr_buffering_action));
    }

    return HAILO_SUCCESS;
}

static hailo_status parse_and_fill_edge_layers_mapping(
    ContextResources &context_resources,
    const ContextMetadata &context_metadata,
    ResourcesManager &resources_manager, const HEFHwArch &hw_arch)
{
    hailo_status status = HAILO_UNINITIALIZED;

    TRY(const auto hw_consts, Control::get_hw_consts(resources_manager.get_device()));

    // Parse the edge layer by order - first output edge layers, then ddr inputs and only then the input edge layers
    // In order to insure that input data can enter the chip only after all other elements are configured.
    // We parse ddr inputs before boundary/inter-context because otherwise on C2C mode we may lose some credit.
    for (const auto &output_layer_info : context_metadata.get_ddr_output_layers()) {
        status = fill_ddr_output_layer(context_resources, resources_manager, output_layer_info, hw_consts, hw_arch);
        CHECK_SUCCESS(status);
    }

    for (const auto &output_layer_info : context_metadata.get_boundary_output_layers()) {
        status = fill_boundary_output_layer(context_resources, resources_manager, output_layer_info, hw_consts, hw_arch);
        CHECK_SUCCESS(status);
    }

    for (const auto &output_layer_info : context_metadata.get_inter_context_output_layers()) {
        status = fill_inter_context_output_layer(context_resources, resources_manager, output_layer_info, hw_consts,
            hw_arch);
        CHECK_SUCCESS(status);
    }

    for (const auto &cache_layer_info : context_metadata.get_cache_output_layers()) {
        status = fill_cache_output_layer(context_resources, resources_manager, cache_layer_info, hw_consts, hw_arch);
        CHECK_SUCCESS(status);
    }

    for (const auto &input_layer_info : context_metadata.get_ddr_input_layers()) {
        status = fill_ddr_input_layer(context_resources, resources_manager, input_layer_info, hw_consts, hw_arch);
        CHECK_SUCCESS(status);
    }

    for (const auto &input_layer_info : context_metadata.get_boundary_input_layers()) {
        status = fill_boundary_input_layer(context_resources, resources_manager, input_layer_info, hw_consts, hw_arch);
        CHECK_SUCCESS(status);
    }

    for (const auto &input_layer_info : context_metadata.get_inter_context_input_layers()) {
        status = fill_inter_context_input_layer(context_resources, resources_manager, input_layer_info, hw_consts,
            hw_arch);
        CHECK_SUCCESS(status);
    }

    for (const auto &cache_layer_info : context_metadata.get_cache_input_layers()) {
        status = fill_cache_input_layer(context_resources, resources_manager, cache_layer_info, hw_consts, hw_arch);
        CHECK_SUCCESS(status);
    }

    // Unlock resources at the end of the context -
    // Inter-context channels, DDR buffer channels and cache channels
    for (const auto &input_layer_info : context_metadata.get_inter_context_input_layers()) {
        status = resources_manager.free_channel_index(to_layer_identifier(input_layer_info));
        CHECK_SUCCESS(status);
    }

    for (const auto &output_layer_info : context_metadata.get_inter_context_output_layers()) {
        status = resources_manager.free_channel_index(to_layer_identifier(output_layer_info));
        CHECK_SUCCESS(status);
    }

    for (const auto &output_layer_info : context_metadata.get_ddr_output_layers()) {
        const auto h2d_layer_identifier = std::make_tuple(LayerType::DDR, HAILO_H2D_STREAM,
                output_layer_info.name, output_layer_info.connected_context_info.stream_index);
        status = resources_manager.free_channel_index(h2d_layer_identifier);
        CHECK_SUCCESS(status);

        const auto d2h_layer_identifier = std::make_tuple(LayerType::DDR, HAILO_D2H_STREAM,
                output_layer_info.name, output_layer_info.stream_index);
        status = resources_manager.free_channel_index(d2h_layer_identifier);
        CHECK_SUCCESS(status);
    }

    for (const auto &input_layer_info : context_metadata.get_cache_input_layers()) {
        status = resources_manager.free_channel_index(to_layer_identifier(input_layer_info));
        CHECK_SUCCESS(status);
    }

    for (const auto &output_layer_info : context_metadata.get_cache_output_layers()) {
        status = resources_manager.free_channel_index(to_layer_identifier(output_layer_info));
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

// Returns pairs of form [start, end] (inclusive) of repeated 'ContextSwitchConfigAction's in the given vector
static std::vector<std::pair<uint32_t, uint32_t>> get_repreated_actions_boundary_indices(
    const std::vector<ContextSwitchConfigActionPtr> &actions)
{
    const uint32_t num_actions = static_cast<uint32_t>(actions.size());

    std::vector<std::pair<uint32_t, uint32_t>> repeated_indexes;
    uint32_t start_index = 0;
    while (start_index < num_actions) {
        auto end_index = start_index + 1;
        do
        {
            if (end_index == num_actions) {
                break;
            }
            if (actions[start_index]->get_type() != actions[end_index]->get_type()) {
                break;
            }
            end_index++;
        } while (true);

        repeated_indexes.emplace_back(start_index, end_index - 1);
        start_index = end_index;
    }

    return repeated_indexes;
}

// Returns a map from start indexes of repeated actions to the size of the chunk (number of repeated actions)
static std::map<uint32_t, uint8_t> get_start_indexes_of_repeated_actions(
    const std::vector<ContextSwitchConfigActionPtr> &actions,
    const std::vector<std::pair<uint32_t, uint32_t>> &repeated_indexes,
    // TODO: get this from HardCoded config (HRT-5352)
    const std::set<ContextSwitchConfigAction::Type> &action_types_denylist = {})
{
    std::map<uint32_t, uint8_t> result;
    for (const auto &index_pair : repeated_indexes) {
        if (!actions[index_pair.first]->supports_repeated_block()) {
            continue;
        }

        if (contains(action_types_denylist, actions[index_pair.first]->get_type())) {
            continue;
        }

        // TODO: Move merge calculation to HRT-5352
        // Merge calculation (see also - CONTEXT_SWITCH_DEFS__repeated_action_header_t in common/include/context_switch_defs.h):
        // * Assume there are x repeated actions that can be merged
        // * Let a := sizeof(action_to_be_merged) [without CONTEXT_SWITCH_DEFS__common_action_header_t]
        // * sizeof(CONTEXT_SWITCH_DEFS__common_action_header_t) is 5
        // * sizeof(CONTEXT_SWITCH_DEFS__repeated_action_header_t) is 3
        // Then:
        // * original_size = x * (5 + a) = 5x + ax
        // * new_size = 5 + 3 + ax = 8 + ax
        // * new_size < original_size <=> 8 + ax < 5x + ax <=> 8 < 5x <=> 1.6 < x
        // Hence we merge for x >= 2
        static_assert(sizeof(CONTEXT_SWITCH_DEFS__common_action_header_t) == 5,
            "Merge calculation assumes that 'sizeof(CONTEXT_SWITCH_DEFS__common_action_header_t) == 5'");
        static_assert(sizeof(CONTEXT_SWITCH_DEFS__repeated_action_header_t) == 3,
            "Merge calculation assumes that 'sizeof(CONTEXT_SWITCH_DEFS__repeated_action_header_t) == 3'");
        static const uint32_t MIN_REQUIRED_FOR_MERGING = 2;

        uint32_t start_index = index_pair.first;
        const uint32_t end_index = index_pair.second;
        while (start_index < end_index) {
            const auto curr_chunk_size = static_cast<uint8_t>(std::min(
                static_cast<uint32_t>(std::numeric_limits<uint8_t>::max()),
                end_index - start_index + 1));
            if (curr_chunk_size < MIN_REQUIRED_FOR_MERGING) {
                break;
            }

            result.emplace(start_index, curr_chunk_size);

            start_index += curr_chunk_size;
        }
    }

    return result;
}

static std::set<std::pair<uint32_t, uint32_t>> get_indexes_of_action_type(
    const std::vector<ContextSwitchConfigActionPtr> &actions,
    const std::vector<std::pair<uint32_t, uint32_t>> &repeated_indexes,
    const ContextSwitchConfigAction::Type &required_action_type)
{
    std::set<std::pair<uint32_t, uint32_t>> result;
    for (const auto &index_pair : repeated_indexes) {
        const auto curr_action_type = actions[index_pair.first]->get_type();
        if (required_action_type != curr_action_type) {
            continue;
        }

        result.emplace(index_pair);
    }

    return result;
}

static hailo_status push_fetch_config_actions(
    std::shared_ptr<ConfigBuffer> config_resources, uint8_t config_stream_index,
    uint16_t total_ccw_bursts, bool support_pre_fetch,
    std::vector<ContextSwitchConfigActionPtr> &processed_configuration_actions)
{
    if (support_pre_fetch) {
        TRY(const auto action, AddCcwBurstAction::create(config_stream_index, total_ccw_bursts));
        processed_configuration_actions.emplace_back(std::move(action));
    } else {
        CopiedConfigBuffer *copied_buffer_ptr = dynamic_cast<CopiedConfigBuffer*>(config_resources.get());
        CHECK(nullptr != copied_buffer_ptr, HAILO_INTERNAL_FAILURE,
            "When pre-fetch is not supported, CopiedConfigBuffer is expected, but got different type of ConfigBuffer.");
        TRY(const auto desc_count, copied_buffer_ptr->program_descriptors());
        TRY(const auto action, FetchCfgChannelDescriptorsAction::create(config_resources->channel_id(), desc_count));
        processed_configuration_actions.emplace_back(std::move(action));
    }

    return HAILO_SUCCESS;
}

static hailo_status proccess_write_ccw_action(ContextSwitchConfigActionPtr &configuration_action,
    std::map<uint8_t, std::shared_ptr<ConfigBuffer>> &config_resources,
    const bool support_pre_fetch, std::vector<ContextSwitchConfigActionPtr> &processed_configuration_actions,
    bool zero_copy_config_over_descs)
{
    assert(ContextSwitchConfigAction::Type::WriteDataCcw == configuration_action->get_type());
    auto &write_ccw_action = *static_cast<WriteDataCcwAction*>(configuration_action.get());

    const auto config_stream_index = write_ccw_action.config_stream_index();
    CHECK(config_resources.find(config_stream_index) != config_resources.end(),
        HAILO_INTERNAL_FAILURE, "Config stream index {} not found in config resources.", config_stream_index);

    if (!zero_copy_config_over_descs) {
        CopiedConfigBuffer *copied_buffer_ptr = dynamic_cast<CopiedConfigBuffer*>(config_resources[config_stream_index].get());
        CHECK(nullptr != copied_buffer_ptr, HAILO_INTERNAL_FAILURE,
            "Expected ConfigBuffer type is CopiedConfigBuffer, but got different type.");
        CopiedConfigBuffer &copied_buffer = *copied_buffer_ptr;
        auto status = write_ccw_action.write_to_config_buffer(copied_buffer, support_pre_fetch);
        CHECK_SUCCESS(status, "Failed on config {}", write_ccw_action.config_stream_index());
    }

    auto status = push_fetch_config_actions(config_resources[config_stream_index], config_stream_index,
        write_ccw_action.total_ccw_burst(), support_pre_fetch, processed_configuration_actions);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

static Expected<uint8_t> get_null_shmifo_id(const HEFHwArch &hw_arch)
{
    // NULL_SHMIFO_STREAM_INDEX is defined in firmawrearch.h as
    // ((1 << DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__W_SHMIFO) - 1), this are the equivalent
    uint8_t HAILO15L_NULL_STREAM_INDEX = 15;
    uint8_t HAILO1XH_NULL_STREAM_INDEX = 31;
    uint8_t HAILO12L_NULL_STREAM_INDEX = 63;
    switch (hw_arch) {
    case HEFHwArch::HW_ARCH__HAILO15L:
        return HAILO15L_NULL_STREAM_INDEX;
    case HEFHwArch::HW_ARCH__HAILO1XH:
    case HEFHwArch::HW_ARCH__HAILO15M:
        return HAILO1XH_NULL_STREAM_INDEX;
    case HEFHwArch::HW_ARCH__MARS:
        return HAILO12L_NULL_STREAM_INDEX;
    default:
        LOGGER__ERROR("Invalid hardware architecture: {}", static_cast<int>(hw_arch));
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }
}

static hailo_status add_change_vdma_to_stream_mapping_impl(const HEFHwArch &hw_arch,
    const LayerInfo &layer_info, const ResourcesManager &resources_manager,
    uint16_t context_index,
    std::vector<ContextSwitchConfigActionPtr> &processed_configuration_actions)
{
    TRY(auto vdma_channel, resources_manager.get_boundary_vdma_channel_by_stream_name(layer_info.name));

    const auto channel_id = vdma_channel->get_channel_id();
    const bool is_dummy_stream = layer_info.context_index != context_index;
    uint8_t stream_index = layer_info.stream_index;
    if (is_dummy_stream) {
        TRY(const auto dummy_stream_index, get_null_shmifo_id(hw_arch));
        stream_index = dummy_stream_index;
    }

    TRY(const auto action, ChangeVdmaToStreamMapping::create(channel_id, stream_index, is_dummy_stream));
    processed_configuration_actions.emplace_back(std::move(action));

    return HAILO_SUCCESS;
}

static hailo_status add_change_vdma_to_stream_mapping(const HEFHwArch &hw_arch,
    const CoreOpMetadata &core_op_metadata, const ResourcesManager &resources_manager,
    uint16_t context_index, std::vector<ContextSwitchConfigActionPtr> &processed_configuration_actions)
{
    for (const LayerInfo &layer_info : core_op_metadata.get_all_layer_infos()) {
        if (layer_info.is_multi_planar) {
            for (const auto &plane : layer_info.planes) {
                auto status = add_change_vdma_to_stream_mapping_impl(hw_arch, plane, resources_manager,
                    context_index, processed_configuration_actions);
                CHECK_SUCCESS(status);
            }
        } else {
                auto status = add_change_vdma_to_stream_mapping_impl(hw_arch, layer_info, resources_manager,
                    context_index, processed_configuration_actions);
                CHECK_SUCCESS(status);
        }
    }

    return HAILO_SUCCESS;
}

static hailo_status push_edge_layer_activation_actions(
    const ContextResources &context_resources,
    std::vector<ContextSwitchConfigActionPtr> &actions,
    bool push_internal_only)
{
    // Activate the edge layer by order - first output edge layers, then ddr inputs and only then the input edge layers
    // In order to insure that input data can enter the chip only after all other elements are configured.
    // We parse ddr inputs before boundary/inter-context because otherwise on C2C mode we may lose some credit.

    for (const EdgeLayer &edge_layer : context_resources.get_edge_layers(LayerType::DDR, HAILO_D2H_STREAM)) {
        TRY(const auto activate_action, ActivateDdrOutputChannelAction::create(edge_layer.channel_id,
            edge_layer.layer_info.stream_index, edge_layer.layer_info.nn_stream_config, edge_layer.buffer_info,
            edge_layer.layer_info.ddr_info.min_buffered_rows));
        actions.emplace_back(std::move(activate_action));
    }

    if (!push_internal_only) {
        for (const EdgeLayer &edge_layer : context_resources.get_edge_layers(LayerType::BOUNDARY, HAILO_D2H_STREAM)) {
            TRY(const auto activate_action, ActivateBoundaryOutputChannelAction::create(edge_layer.channel_id,
                edge_layer.layer_info.stream_index, edge_layer.layer_info.network_index,
                edge_layer.layer_info.nn_stream_config, edge_layer.buffer_info));
            actions.emplace_back(std::move(activate_action));
        }
    }

    for (const EdgeLayer &edge_layer : context_resources.get_edge_layers(LayerType::INTER_CONTEXT, HAILO_D2H_STREAM)) {
        TRY(auto activate_action, ActivateInterContextOutputChannelAction::create(edge_layer.channel_id,
            edge_layer.layer_info.stream_index, edge_layer.layer_info.network_index,
            edge_layer.layer_info.nn_stream_config, edge_layer.buffer_info));
        actions.emplace_back(std::move(activate_action));
    }

    for (const EdgeLayer &edge_layer : context_resources.get_edge_layers(LayerType::CACHE, HAILO_D2H_STREAM)) {
        // TODO: Cache edge_layers' buffer_info isn't correct - total_desc_count and bytes_in_pattern are calculated
        //       according to the cache size, not the transfer size (since the edge layer is backed by a buffer of cache_size)
        //       Think of a way of holding the correct buffer_info for cache edge_layers (or maybe removing host buffer info all together)
        //       (HRT-13775)
        TRY(const auto activate_action, ActivateCacheOutputChannelAction::create(edge_layer.channel_id,
            edge_layer.layer_info.stream_index, edge_layer.layer_info.network_index,
            edge_layer.layer_info.nn_stream_config, edge_layer.buffer_info, edge_layer.layer_info.cache_info.batch_size));
        actions.emplace_back(std::move(activate_action));
    }

    for (const EdgeLayer &edge_layer : context_resources.get_edge_layers(LayerType::DDR, HAILO_H2D_STREAM)) {
        const auto d2h_stream_index = edge_layer.layer_info.connected_context_info.stream_index;
        TRY(const auto ddr_channels_info, context_resources.get_ddr_channels_info(d2h_stream_index));
        const auto d2h_channel_id = ddr_channels_info.d2h_channel_id;

        TRY(const auto activate_action, ActivateDdrInputChannelAction::create(edge_layer.channel_id,
            edge_layer.layer_info.stream_index, edge_layer.layer_info.nn_stream_config, edge_layer.buffer_info,
            edge_layer.layer_info.max_shmifo_size, d2h_channel_id));
        actions.emplace_back(std::move(activate_action));
    }

    if (!push_internal_only) {
        for (const EdgeLayer &edge_layer : context_resources.get_edge_layers(LayerType::BOUNDARY, HAILO_H2D_STREAM)) {
            TRY(auto activate_action, ActivateBoundaryInputChannelAction::create(edge_layer.channel_id,
                edge_layer.layer_info.stream_index, edge_layer.layer_info.nn_stream_config, edge_layer.buffer_info,
                edge_layer.layer_info.max_shmifo_size));
            actions.emplace_back(std::move(activate_action));
        }
    }

    for (const EdgeLayer &edge_layer : context_resources.get_edge_layers(LayerType::INTER_CONTEXT, HAILO_H2D_STREAM)) {
        TRY(const auto activate_action, ActivateInterContextInputChannelAction::create(edge_layer.channel_id,
            edge_layer.layer_info.stream_index, edge_layer.layer_info.nn_stream_config, edge_layer.buffer_info,
            edge_layer.layer_info.max_shmifo_size));
        actions.emplace_back(std::move(activate_action));
    }

    for (const EdgeLayer &edge_layer : context_resources.get_edge_layers(LayerType::CACHE, HAILO_H2D_STREAM)) {
        // TODO: Cache edge_layers' buffer_info isn't correct - total_desc_count and bytes_in_pattern are calculated
        //       according to the cache size, not the transfer size (since the edge layer is backed by a buffer of cache_size)
        //       Think of a way of holding the correct buffer_info for cache edge_layers (or maybe removing host buffer info all together)
        //       (HRT-13775)
        TRY(const auto activate_action, ActivateCacheInputChannelAction::create(edge_layer.channel_id,
            edge_layer.layer_info.stream_index, edge_layer.layer_info.nn_stream_config, edge_layer.buffer_info,
            edge_layer.layer_info.max_shmifo_size));
        actions.emplace_back(std::move(activate_action));
    }

    return HAILO_SUCCESS;
}

static hailo_status push_pre_allow_input_data_configs(const HEFHwArch &hw_arch, const CoreOpMetadata &core_op_metadata,
    const ResourcesManager &resources_manager, ContextResources &context_resources, uint16_t context_index,
    std::vector<ContextSwitchConfigActionPtr> &processed_configuration_actions, bool is_single_context)
{
    const bool PUSH_ALL_EDGE_LAYERS = false;
    auto status = push_edge_layer_activation_actions(context_resources, processed_configuration_actions,
        PUSH_ALL_EDGE_LAYERS);
    CHECK_SUCCESS(status);

    if (!is_single_context) {
        status = add_change_vdma_to_stream_mapping(hw_arch, core_op_metadata, resources_manager,
            context_index, processed_configuration_actions);
        CHECK_SUCCESS(status);
    }

    // DDR buffer info actions need to happen after the edge layer activation actions.
    status = add_ddr_buffers_info(processed_configuration_actions, context_resources);
    CHECK_SUCCESS(status);

    /* Open the boundary input channel */
    for (const EdgeLayer &edge_layer : context_resources.get_edge_layers(LayerType::BOUNDARY, HAILO_H2D_STREAM)) {
        TRY(const auto activate_action, ResumeVdmaChannel::create(edge_layer));
        processed_configuration_actions.emplace_back(std::move(activate_action));
    }

    return HAILO_SUCCESS;
}

static hailo_status proccess_trigger_new_data_input_action(const HEFHwArch &hw_arch,
    const ContextSwitchConfigActionPtr &configuration_action,
    uint32_t trigger_new_data_from_input_start,
    uint32_t trigger_new_data_from_input_end,
    uint32_t config_channel_pre_allow_input_start,
    uint32_t action_index,
    const CoreOpMetadata &core_op_metadata,
    const ResourcesManager &resources_manager,
    ContextResources &context_resources,
    uint16_t config_context_index,
    std::vector<ContextSwitchConfigActionPtr> &processed_configuration_actions, bool is_single_context,
    ContextResources &next_context_resources)
{

    // In case of support of split allow input action all of these actions will be added from the HEF action config channel
    // Otherwise will be added from first allowinputdataflow
    // TODO: HRT-18207: Remove support for hefs without extension
    uint32_t pre_allow_input_data_index = resources_manager.get_supported_features().split_allow_input_action ?
        config_channel_pre_allow_input_start : trigger_new_data_from_input_start;
    ContextResources &config_context_resources = resources_manager.get_supported_features().split_allow_input_action ?
        next_context_resources : context_resources;
    
    // Push the pre allow input data flow channel configs
    if (pre_allow_input_data_index == action_index) {
        auto status = push_pre_allow_input_data_configs(hw_arch, core_op_metadata, resources_manager, config_context_resources,
            config_context_index, processed_configuration_actions, is_single_context);
        CHECK_SUCCESS(status);
    }

    // Add the current action
    processed_configuration_actions.emplace_back(configuration_action);

    // At the end of a consecutive group of TriggerNewDataFromDataInput actions, we can trigger the BurstCreditsTask
    // in the FW, via StartBurstCreditsTaskAction.
    if (trigger_new_data_from_input_end == action_index) {
        auto boundary_input_edge_layers = context_resources.get_edge_layers(LayerType::BOUNDARY, HAILO_H2D_STREAM);
        if (boundary_input_edge_layers.size() > 0) {
            TRY(const auto start_burst_credits_task_action, StartBurstCreditsTaskAction::create());
            processed_configuration_actions.emplace_back(std::move(start_burst_credits_task_action));
        }
    }

    return HAILO_SUCCESS;
}

// At the end of each consecutive group of WriteDataCcwAction, a FetchCfgChannelDescriptorsAction is added.
static hailo_status add_fetch_config_actions(std::vector<ContextSwitchConfigActionPtr> &configuration_actions,
    std::map<uint8_t, std::shared_ptr<ConfigBuffer>> &config_resources, bool support_pre_fetch, bool zero_copy_config_over_descs)
{
    std::vector<ContextSwitchConfigActionPtr> processed_configuration_actions;
    for (auto& configuration_action : configuration_actions) {
        if (ContextSwitchConfigAction::Type::WriteDataCcw == configuration_action->get_type()) {
            auto status = proccess_write_ccw_action(configuration_action, config_resources,
                support_pre_fetch, processed_configuration_actions, zero_copy_config_over_descs);
            CHECK_SUCCESS(status);
        } else {
            // Add the current action
            processed_configuration_actions.emplace_back(configuration_action);
        }
    }

    // Replace the original configuration actions with the processed ones.
    configuration_actions = processed_configuration_actions;

    return HAILO_SUCCESS;
}

// Push activate config channels in the beginning of the context, and deactivation on end of context.
static hailo_status add_config_channel_activation_actions(std::vector<ContextSwitchConfigActionPtr> &actions,
    const std::map<uint8_t, std::shared_ptr<ConfigBuffer>> &config_resources)
{
    std::vector<ContextSwitchConfigActionPtr> processed_actions;
    const size_t new_actions_count = 2 * config_resources.size();
    processed_actions.reserve(actions.size() + new_actions_count);

    for (const auto &config_buffer : config_resources) {
        TRY(const auto activate_action, ActivateConfigChannelAction::create(config_buffer.first, config_buffer.second->channel_id(),
            config_buffer.second->get_host_buffer_info()));
        processed_actions.push_back(std::move(activate_action));
    }

    processed_actions.insert(processed_actions.end(), actions.begin(), actions.end());

    for (const auto &config_buffer : config_resources) {
        TRY(const auto deactivate_action, DeactivateConfigChannelAction::create(config_buffer.first, config_buffer.second->channel_id()));
        processed_actions.push_back(std::move(deactivate_action));
    }

    actions = processed_actions;
    return HAILO_SUCCESS;
}

// For any context with edge layers (the preliminary context when in preliminary_run_asap mode or dynamic contexts),
// we need to add the following:
// * Activate*Channel actions (activation order is documented in push_edge_layer_activation_actions)
// * ChangeVdmaToStreamMapping for each boundary stream in the network group (even for boundaries not activated in the
//   current context).
// * DdrPairInfoActions for each ddr, followed by StartDdrBufferingTaskAction.
// * TriggerNewDataFromDataInput for each input layer (inter context/ boundary) in the context. This action is given
//   from the HEF.
// * Finally StartBurstCreditsTaskAction
static hailo_status handle_edge_layer_activation_actions(const HEFHwArch &hw_arch,
    std::vector<ContextSwitchConfigActionPtr> &configuration_actions, const CoreOpMetadata &core_op_metadata,
    const ResourcesManager &resources_manager, ContextResources &context_resources, uint16_t config_context_index,
    bool is_single_context, ContextResources &next_context_resources)
{
    const auto repeated_indexes = get_repreated_actions_boundary_indices(configuration_actions);

    uint32_t config_channel_pre_allow_input_start = 0;
    if (resources_manager.get_supported_features().split_allow_input_action) {
        const auto config_channel_pre_allow_input_data_indexes = get_indexes_of_action_type(
            configuration_actions, repeated_indexes, ContextSwitchConfigAction::Type::ConfigChannelPreAllowInputDataflowAction);
        CHECK(config_channel_pre_allow_input_data_indexes.size() == 1, HAILO_INTERNAL_FAILURE,
            "Expected only one group of TriggerNewDataFromDataInput actions");
        config_channel_pre_allow_input_start = config_channel_pre_allow_input_data_indexes.cbegin()->first;
    }

    const auto trigger_new_data_from_input_indexes = get_indexes_of_action_type(
        configuration_actions, repeated_indexes, ContextSwitchConfigAction::Type::TriggerNewDataFromDataInput);
    CHECK(trigger_new_data_from_input_indexes.size() == 1, HAILO_INTERNAL_FAILURE,
        "Expected only one group of TriggerNewDataFromDataInput actions");
    const auto trigger_new_data_from_input_start = trigger_new_data_from_input_indexes.cbegin()->first;
    const auto trigger_new_data_from_input_end = trigger_new_data_from_input_indexes.cbegin()->second;

    std::vector<ContextSwitchConfigActionPtr> processed_configuration_actions;
    for (uint32_t action_index = 0; action_index < configuration_actions.size(); action_index++) {
        const auto &configuration_action = configuration_actions[action_index];
        if ((ContextSwitchConfigAction::Type::TriggerNewDataFromDataInput == configuration_action->get_type()) ||
            (ContextSwitchConfigAction::Type::ConfigChannelPreAllowInputDataflowAction == configuration_action->get_type())) {
            auto status = proccess_trigger_new_data_input_action(hw_arch, configuration_action,
                trigger_new_data_from_input_start, trigger_new_data_from_input_end, config_channel_pre_allow_input_start,
                action_index, core_op_metadata, resources_manager, context_resources, config_context_index,
                processed_configuration_actions, is_single_context, next_context_resources);
            CHECK_SUCCESS(status);
        } else {
            // Add the current action
            processed_configuration_actions.emplace_back(configuration_action);
        }
    }

    // Replace the original configuration actions with the processed ones.
    configuration_actions = processed_configuration_actions;

    return HAILO_SUCCESS;
}

// If groups of consecutive actions can be "merged" as repeated actions (saving room the FW's
// action list) a RepeatedAction is placed before the relevant actions.
// See also: CONTEXT_SWITCH_DEFS__repeated_action_header_t's documenting in context_switch_defs.h.
static hailo_status handle_repeated_actions(std::vector<ContextSwitchConfigActionPtr> &configuration_actions)
{
    const auto repeated_indexes = get_repreated_actions_boundary_indices(configuration_actions);
    const auto start_indexes_of_repeated_actions = get_start_indexes_of_repeated_actions(
        configuration_actions, repeated_indexes);

    std::vector<ContextSwitchConfigActionPtr> processed_configuration_actions;
    processed_configuration_actions.reserve(configuration_actions.size() + start_indexes_of_repeated_actions.size());

    uint32_t action_index = 0;
    while (action_index < configuration_actions.size()){
        if (contains(start_indexes_of_repeated_actions, action_index)) {
            // A group of actions can be "merged" as repeated actions.
            // Add a RepeatedAction
            const auto num_repeated = start_indexes_of_repeated_actions.at(action_index);

            std::vector<ContextSwitchConfigActionPtr> repeated_block;
            repeated_block.reserve(num_repeated);
            for (uint32_t repeated_offset = 0; repeated_offset < num_repeated; repeated_offset++) {
                repeated_block.emplace_back(configuration_actions[action_index]);
                action_index++;
            }

            TRY(const auto repeated_header_action, RepeatedAction::create(std::move(repeated_block)));
            processed_configuration_actions.emplace_back(std::move(repeated_header_action));
        }
        else {
            processed_configuration_actions.emplace_back(configuration_actions[action_index]);
            action_index++;
        }
    }

    // Replace the original configuration actions with the processed ones.
    configuration_actions = processed_configuration_actions;

    return HAILO_SUCCESS;
}

static hailo_status write_action_list(const ContextResources & context_resources,
    std::shared_ptr<ActionListBufferBuilder> &builder, const std::vector<ContextSwitchConfigActionPtr> &actions)
{
    // Mark first action buffer of context to know when new context is starting (needed for dynamic contexts)
    bool is_first_action_buffer_of_context = true;
    for (const auto &action : actions) {
        TRY(auto action_buffers, action->serialize(context_resources));

        for (auto &action_buffer : action_buffers) {
            builder->build_context(MemoryView(action_buffer), context_resources.get_context_type(), is_first_action_buffer_of_context);
            is_first_action_buffer_of_context = false;
        }
    }

    return HAILO_SUCCESS;
}

static hailo_status insert_deactivate_channel_actions(ResourcesManager &resources_manager,
    const ContextResources &context_resources,
    std::vector<ContextSwitchConfigActionPtr> &processed_configuration_actions)
{
    for (const EdgeLayer &edge_layer : context_resources.get_edge_layers()) {
        if (resources_manager.get_device().is_extended_fw_checks()) {
            TRY(auto validate_action, ValidateChannelAction::create(edge_layer, false));
            processed_configuration_actions.emplace_back(validate_action);
        }

        if (edge_layer.layer_info.type != LayerType::BOUNDARY) {
            TRY(auto deactive_action, DeactivateChannelAction::create(edge_layer, false));
            processed_configuration_actions.emplace_back(deactive_action);
        }
    }

    return HAILO_SUCCESS;
}

static hailo_status replace_deactivate_data_channels_actions(
    ResourcesManager &resources_manager,
    std::vector<ContextSwitchConfigActionPtr> &actions,
    const ContextResources &context_resources)
{
    bool found_disable_action = false;
    // Iterate through all actions and when find DisableDataChannelsAction - insert the deactivate channel actions needed for context
    std::vector<ContextSwitchConfigActionPtr> processed_actions;
    for (const auto& action : actions) {
        if (ContextSwitchConfigAction::Type::DisableDataChannelsAction == action->get_type()) {
            found_disable_action = true;
            auto status = insert_deactivate_channel_actions(resources_manager, context_resources, processed_actions);
            CHECK_SUCCESS(status);
        } else {
            // Add the current action
            processed_actions.emplace_back(action);
        }
    }

    CHECK(found_disable_action, HAILO_INTERNAL_FAILURE, "Error, no action of disable data channesl found in HEF");

    // Replace the original actions with the processed ones.
    actions = processed_actions;
    return HAILO_SUCCESS;
}

static hailo_status add_edge_layer_end_of_context_actions(ResourcesManager &resources_manager,
    const ContextResources &context_resources,
    std::vector<ContextSwitchConfigActionPtr> &actions, bool allow_split_input_action)
{
    // Replace action DisableDataChannelsAction with all needed disables for context
    // in old flow without extension we add deactivate channels at end of context
    // TODO: HRT-18207: Remove support for hefs without extension
    auto status = allow_split_input_action ? replace_deactivate_data_channels_actions(resources_manager, actions, context_resources) :
        insert_deactivate_channel_actions(resources_manager, context_resources, actions);
    CHECK_SUCCESS(status);

    /* Pause the boundary input channel */
    // TODO: HRT-18339: Remove PauseVdmaChannel and do as part of deactivate
    for (const EdgeLayer &edge_layer : context_resources.get_edge_layers(LayerType::BOUNDARY, HAILO_H2D_STREAM)) {
        TRY(const auto activate_action, PauseVdmaChannel::create(edge_layer));
        actions.emplace_back(std::move(activate_action));
    }

    return HAILO_SUCCESS;
}

static hailo_status fill_context_recipes_for_multi_context(const HEFHwArch &hw_arch,
    ContextResources &context_resources, ResourcesManager &resources_manager,
    uint16_t config_context_index, const CoreOpMetadata &core_op_metadata, const ContextMetadata &context_metadata,
    bool is_single_context, bool is_last_context, bool caches_in_use, bool zero_copy_config_over_descs, bool enable_kv_cache,
    ContextResources &next_context_resources)
{
    // Parse context
    std::vector<ContextSwitchConfigActionPtr> actions = context_metadata.get_actions();

    const auto support_pre_fetch = true;
    hailo_status status = add_fetch_config_actions(actions, context_resources.get_config_buffers(), support_pre_fetch, zero_copy_config_over_descs);
    CHECK_SUCCESS(status);

    status = handle_edge_layer_activation_actions(hw_arch, actions, core_op_metadata, resources_manager,
        context_resources, config_context_index, is_single_context, next_context_resources);
    CHECK_SUCCESS(status);

    status = add_config_channel_activation_actions(actions, context_resources.get_config_buffers());
    CHECK_SUCCESS(status);

    // End of context actions - these must be last in their respective context
    if (is_single_context) {
        // Single context network must wait for network group change event after they finish the dynamic context.
        TRY(const auto wait_action, WaitForNetworkGroupChangeAction::create());
        actions.emplace_back(std::move(wait_action));
    } else {
        status = add_edge_layer_end_of_context_actions(resources_manager, context_resources, actions,
            resources_manager.get_supported_features().split_allow_input_action);
        CHECK_SUCCESS(status);

        if (is_last_context && caches_in_use) {
            if (enable_kv_cache) {
                // If caches are in use and KV cache is enabled, we'll wait for the caches to be updated at the end of the last context
                TRY(const auto action, WaitForCacheUpdatedAction::create());
                actions.emplace_back(std::move(action));
            } else {
                LOGGER__INFO("Skipping cache offset updates (KV-cache usage is not enabled)");
            }
        }
    }

    status = handle_repeated_actions(actions);
    CHECK_SUCCESS(status);

    return write_action_list(context_resources, resources_manager.get_action_list_buffer_builder(), actions);
}

static hailo_status create_boundary_channels(ResourcesManager &resources_manager,
    CoreOpMetadata &core_op_metadata)
{
    for (const LayerInfo &layer_info : core_op_metadata.get_all_layer_infos()) {
        if (layer_info.is_multi_planar) {
            for (const auto &plane : layer_info.planes) {
                auto status = resources_manager.create_boundary_vdma_channel(plane,
                    resources_manager.get_hw_infer_boundary_channel_mode());
                CHECK_SUCCESS(status);
            }
        } else {
            auto status = resources_manager.create_boundary_vdma_channel(layer_info,
                resources_manager.get_hw_infer_boundary_channel_mode());
            CHECK_SUCCESS(status);
        }
    }
    return HAILO_SUCCESS;
}

static hailo_status fill_activation_config_recepies_for_multi_context(
    ContextResources &context_resources, ResourcesManager &resources_manager,
    std::shared_ptr<CoreOpMetadata> core_op_metadata, const HEFHwArch &hw_arch)
{
    TRY(const auto hw_consts, Control::get_hw_consts(resources_manager.get_device()));

    for (const auto &layer_info : core_op_metadata->get_output_layer_infos()){
        auto status = fill_boundary_output_layer(context_resources, resources_manager, layer_info, hw_consts, hw_arch);
        CHECK_SUCCESS(status);
    }

    for (const auto &layer_info : core_op_metadata->get_input_layer_infos()) {
        auto status = fill_boundary_input_layer(context_resources, resources_manager, layer_info, hw_consts, hw_arch);
        CHECK_SUCCESS(status);
    }

    std::vector<ContextSwitchConfigActionPtr> actions;
    TRY(const auto reset_burst_task_action, ResetBurstCreditsTaskAction::create());
    actions.emplace_back(std::move(reset_burst_task_action));

    for (const EdgeLayer &edge_layer : context_resources.get_edge_layers()) {
        if (LayerType::BOUNDARY == edge_layer.layer_info.type) {
            auto action = edge_layer.layer_info.direction == HAILO_H2D_STREAM ?
                OpenBoundaryInputChannelAction::create(edge_layer.channel_id, edge_layer.buffer_info) :
                OpenBoundaryOutputChannelAction::create(edge_layer.channel_id, edge_layer.buffer_info);
            CHECK_EXPECTED_AS_STATUS(action); // TODO (HRT-13278): Figure out how to remove CHECK_EXPECTED here
            actions.emplace_back(action.release());
        }
    }

    return write_action_list(context_resources, resources_manager.get_action_list_buffer_builder(), actions);
}

static Expected<ContextSwitchConfigActionPtr> create_switch_lcu_batch_action(const ContextSwitchConfigActionPtr action,
    ContextResources &context_resources)
{
    uint8_t cluster_index = 0;
    uint8_t lcu_index = 0;
    uint8_t network_index = 0;
    uint32_t kernel_done_count = 0;

    CHECK_AS_EXPECTED((ContextSwitchConfigAction::Type::EnableLcuDefault == action->get_type()) ||
        (ContextSwitchConfigAction::Type::SwitchLcuBatch == action->get_type()) ||
        (ContextSwitchConfigAction::Type::EnableLcuNonDefault == action->get_type()), HAILO_INVALID_ARGUMENT,
        "Invalid action type - must be enable lcu (default or non default) or switch lcu batch, Received type {}", static_cast<int>(action->get_type()));

    TRY(const auto params_buffer, action->serialize_params(context_resources));

    if (ContextSwitchConfigAction::Type::EnableLcuDefault == action->get_type()) {
        const auto params = reinterpret_cast<const CONTEXT_SWITCH_DEFS__enable_lcu_action_default_data_t*>(params_buffer.data());
        cluster_index = CONTEXT_SWITCH_DEFS__PACKED_LCU_ID_CLUSTER_INDEX_READ(params->packed_lcu_id);
        lcu_index = CONTEXT_SWITCH_DEFS__PACKED_LCU_ID_LCU_INDEX_READ(params->packed_lcu_id);
        network_index = params->network_index;
        kernel_done_count = CONTEXT_SWITCH_DEFS__ENABLE_LCU_DEFAULT_KERNEL_COUNT;
    } else if (ContextSwitchConfigAction::Type::EnableLcuNonDefault == action->get_type()) {
        const auto params = reinterpret_cast<const CONTEXT_SWITCH_DEFS__enable_lcu_action_non_default_data_t*>(params_buffer.data());
        cluster_index = CONTEXT_SWITCH_DEFS__PACKED_LCU_ID_CLUSTER_INDEX_READ(params->packed_lcu_id);
        lcu_index = CONTEXT_SWITCH_DEFS__PACKED_LCU_ID_LCU_INDEX_READ(params->packed_lcu_id);
        network_index = params->network_index;
        kernel_done_count = params->kernel_done_count;
    } else if (ContextSwitchConfigAction::Type::SwitchLcuBatch == action->get_type()) {
        const auto params = reinterpret_cast<const CONTEXT_SWITCH_DEFS__switch_lcu_batch_action_data_t*>(params_buffer.data());
        cluster_index = CONTEXT_SWITCH_DEFS__PACKED_LCU_ID_CLUSTER_INDEX_READ(params->packed_lcu_id);
        lcu_index = CONTEXT_SWITCH_DEFS__PACKED_LCU_ID_LCU_INDEX_READ(params->packed_lcu_id);
        network_index = params->network_index;
        kernel_done_count = params->kernel_done_count;
    }

    return SwitchLcuBatchAction::create(cluster_index, lcu_index, network_index, kernel_done_count);
}

static hailo_status fill_batch_switching_context_edge_layers(ContextResources &context_resources, const CoreOpMetadata &core_op_metadata, ResourcesManager &resources_manager,
    const HEFHwArch &hw_arch)
{
    TRY(const auto hw_consts, Control::get_hw_consts(resources_manager.get_device()));

    for (const auto &output_layer_info : core_op_metadata.dynamic_contexts()[0].get_ddr_output_layers()) {
        auto status = fill_ddr_output_layer(context_resources, resources_manager, output_layer_info, hw_consts, hw_arch);
        CHECK_SUCCESS(status);
    }

    for (const auto &output_layer_info : core_op_metadata.dynamic_contexts()[0].get_boundary_output_layers()) {
        auto status = fill_boundary_output_layer(context_resources, resources_manager, output_layer_info,
            hw_consts, hw_arch);
        CHECK_SUCCESS(status);
    }

    for (const auto &output_layer_info : core_op_metadata.dynamic_contexts()[0].get_inter_context_output_layers()) {
        auto status = fill_inter_context_output_layer(context_resources, resources_manager, output_layer_info,
            hw_consts, hw_arch);
        CHECK_SUCCESS(status);
    }

    for (const auto &input_layer_info : core_op_metadata.dynamic_contexts()[0].get_ddr_input_layers()) {
        auto status = fill_ddr_input_layer(context_resources, resources_manager, input_layer_info, hw_consts, hw_arch);
        CHECK_SUCCESS(status);
    }

    for (const auto &input_layer_info : core_op_metadata.dynamic_contexts()[0].get_boundary_input_layers()) {
        auto status = fill_boundary_input_layer(context_resources, resources_manager, input_layer_info,
            hw_consts, hw_arch);
        CHECK_SUCCESS(status);
    }

    // Batch switching context is not support for networks where in the first dynamic context there is inter context input.
    assert(core_op_metadata.dynamic_contexts()[0].get_inter_context_input_layers().size() == 0);

    return HAILO_SUCCESS;
}


static hailo_status add_lcu_actions_to_batch_switch_context(ContextResources &context_resources, const CoreOpMetadata &core_op_metadata,
    std::vector<ContextSwitchConfigActionPtr> &actions)
{
    // Find all the enabled lcus from the preliminary context in order to create coresponding switch lcu batch actions to run
    // In the batch switch context
    static const std::set<ContextSwitchConfigAction::Type> ENABLE_LCU_ACTIONS = {
        ContextSwitchConfigAction::Type::EnableLcuDefault,
        ContextSwitchConfigAction::Type::EnableLcuNonDefault,
        ContextSwitchConfigAction::Type::SwitchLcuBatch
    };

    const auto lcu_batch_switch_actions = core_op_metadata.preliminary_context().get_actions_of_type(ENABLE_LCU_ACTIONS);
    for (const auto &action : lcu_batch_switch_actions) {
        TRY(const auto switch_lcu_batch_action, create_switch_lcu_batch_action(action, context_resources));
        actions.insert(actions.end(), std::move(switch_lcu_batch_action));
    }

    return HAILO_SUCCESS;
}

static hailo_status create_change_boundary_input_batch_actions(const std::vector<std::reference_wrapper<const EdgeLayer>> &boundary_input_layers,
    std::vector<ContextSwitchConfigActionPtr> &batch_switch_context_actions)
{
    for (const EdgeLayer &edge_layer : boundary_input_layers) {
        TRY(const auto change_boundary_input_batch_action, ChangeBoundaryInputBatchAction::create(edge_layer.channel_id));
        batch_switch_context_actions.emplace_back(std::move(change_boundary_input_batch_action));
    }

    TRY(const auto start_burst_credits_task_action, StartBurstCreditsTaskAction::create());
    batch_switch_context_actions.emplace_back(std::move(start_burst_credits_task_action));


    return HAILO_SUCCESS;
}

static hailo_status add_edge_layers_actions_to_batch_switch_context(ContextResources &context_resources,
    const CoreOpMetadata &core_op_metadata, ResourcesManager &resources_manager, const HEFHwArch &hw_arch,
    std::vector<ContextSwitchConfigActionPtr> &actions, const std::vector<std::reference_wrapper<const EdgeLayer>> &activation_boundary_input_layers)
{
    auto status = fill_batch_switching_context_edge_layers(context_resources, core_op_metadata, resources_manager, hw_arch);
    CHECK_SUCCESS(status);

    // Close all internal channels
    const auto BATCH_SWITCHING_CONTEXT = true;
    for (const EdgeLayer &edge_layer : context_resources.get_edge_layers()) {
        if (edge_layer.layer_info.type != LayerType::BOUNDARY) {
            TRY(const auto action, DeactivateChannelAction::create(edge_layer, BATCH_SWITCHING_CONTEXT));
            actions.emplace_back(std::move(action));
        }
    }

    // We need to reset the ddr buffering task when we change the batch_size (since it depends on the batch_size param)
    TRY(const auto reset_ddr_action, ResetDdrBufferingTaskAction::create());
    actions.emplace_back(std::move(reset_ddr_action));

    // Now re-open all the internal channels
    const bool PUSH_INTERNAL_EDGE_LAYERS = true;
    status = push_edge_layer_activation_actions(context_resources, actions, PUSH_INTERNAL_EDGE_LAYERS);
    CHECK_SUCCESS(status);

    status = add_ddr_buffers_info(actions, context_resources);
    CHECK_SUCCESS(status);

    status = create_change_boundary_input_batch_actions(activation_boundary_input_layers, actions);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

static hailo_status fill_batch_switching_context_config_recepies_for_multi_context(
    ContextResources &context_resources, const CoreOpMetadata &core_op_metadata, ResourcesManager &resources_manager,
    const HEFHwArch &hw_arch, const std::vector<std::reference_wrapper<const EdgeLayer>> &activation_boundary_input_layers)
{
    std::vector<ContextSwitchConfigActionPtr> actions;

    auto status = add_lcu_actions_to_batch_switch_context(context_resources, core_op_metadata, actions);
    CHECK_SUCCESS(status);

    status = add_edge_layers_actions_to_batch_switch_context(context_resources, core_op_metadata, resources_manager,
        hw_arch, actions, activation_boundary_input_layers);
    CHECK_SUCCESS(status);

    status = handle_repeated_actions(actions);
    CHECK_SUCCESS(status);

    return write_action_list(context_resources, resources_manager.get_action_list_buffer_builder(), actions);
}

static hailo_status fill_preliminary_config_recepies_for_multi_context(const HEFHwArch &hw_arch,
    ContextResources &context_resources, ResourcesManager &resources_manager,
    std::shared_ptr<CoreOpMetadata> core_op_metadata, const ContextMetadata &preliminary_context,
    bool is_single_context, bool zero_copy_config_over_descs)
{
    static const auto PRELIMINARY_CONTEXT_INDEX = 0; // First context in the hef

    if (resources_manager.get_supported_features().preliminary_run_asap) {
        // Add edge layers mapping (only preliminary_run_asap networks have edge layers in the preliminary context)
        assert(PRELIMINARY_CONTEXT_INDEX < core_op_metadata->dynamic_contexts().size());
        auto status = parse_and_fill_edge_layers_mapping(context_resources,
            core_op_metadata->dynamic_contexts()[PRELIMINARY_CONTEXT_INDEX], resources_manager, hw_arch);
        CHECK_SUCCESS(status);
    }

    // Parse preliminary config
    std::vector<ContextSwitchConfigActionPtr> actions = preliminary_context.get_actions();

    const auto support_pre_fetch = true;
    auto status = add_fetch_config_actions(actions, context_resources.get_config_buffers(), support_pre_fetch, zero_copy_config_over_descs);
    CHECK_SUCCESS(status);

    if (resources_manager.get_supported_features().preliminary_run_asap) {
        status = handle_edge_layer_activation_actions(hw_arch, actions, *core_op_metadata, resources_manager,
            context_resources, PRELIMINARY_CONTEXT_INDEX, is_single_context, context_resources);
        CHECK_SUCCESS(status);
    }

    status = add_config_channel_activation_actions(actions, context_resources.get_config_buffers());
    CHECK_SUCCESS(status);

    status = handle_repeated_actions(actions);
    CHECK_SUCCESS(status);

    return write_action_list(context_resources, resources_manager.get_action_list_buffer_builder(), actions);
}

hailo_status ResourcesManagerBuilder::prepare_aligned_ccws_resources(const Hef &hef, ResourcesManager &resources_manager, HailoRTDriver &driver)
{
    const size_t page_size = resources_manager.get_csm_buffer_size();
    TRY(auto hef_as_bufferptr, hef.pimpl->get_hef_as_buffer());
    auto status = resources_manager.map_and_set_ccws_section_buffer(hef_as_bufferptr, hef.pimpl->get_offset_zero_point(), hef.pimpl->get_ccws_section_size(), driver);
    CHECK_SUCCESS(status);

    // create a mapped buffer and fill it with nops - we will use it for padding each ccws dma transfer
    TRY(auto dmable_nops_buffer, vdma::DmaAbleBuffer::create_by_allocation(page_size));
    TRY(auto nops_buffer, vdma::MappedBuffer::create_shared(dmable_nops_buffer, driver, HailoRTDriver::DmaDirection::H2D));
    static constexpr uint64_t CCW_NOP = 0x0;
    std::vector<uint64_t> nops_data(page_size / sizeof(uint64_t), CCW_NOP);
    status = nops_buffer->write(nops_data.data(), page_size, 0);
    CHECK_SUCCESS(status);
    resources_manager.set_nops_mapped_buffer(nops_buffer);
    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<ResourcesManager>> ResourcesManagerBuilder::build(uint8_t current_core_op_index, VdmaDevice &device,
    HailoRTDriver &driver, CacheManagerPtr cache_manager, const ConfigureNetworkParams &config_params,
    std::shared_ptr<CoreOpMetadata> core_op_metadata, const HEFHwArch &hw_arch, const Hef &hef)
{
    // Validate HEF version before configuring the models in it
    CHECK_SUCCESS_AS_EXPECTED(hef.pimpl->validate_hef_version());

    const auto num_contexts = core_op_metadata->dynamic_contexts().size() +
        CONTROL_PROTOCOL__CONTEXT_SWITCH_NUMBER_OF_NON_DYNAMIC_CONTEXTS;
    CHECK_AS_EXPECTED(CONTROL_PROTOCOL__MAX_CONTEXTS_PER_NETWORK_GROUP >= num_contexts, HAILO_INVALID_HEF,
        "App '{}' contains more contexts than allowed ({} > {})",
        core_op_metadata->core_op_name(), num_contexts, CONTROL_PROTOCOL__MAX_CONTEXTS_PER_NETWORK_GROUP);

    for (auto &network_params : config_params.network_params_by_name) {
        CHECK(HAILO_MAX_BATCH_SIZE >= network_params.second.batch_size, make_unexpected(HAILO_INVALID_ARGUMENT),
            "Given batch size ({}) for network group {}, network {} is bigger than max allowed ({})", network_params.second.batch_size,
            core_op_metadata->core_op_name(), network_params.first, HAILO_MAX_BATCH_SIZE);
    }

    TRY(auto resources_manager, ResourcesManager::create(device, driver, config_params, cache_manager,
        core_op_metadata, current_core_op_index));

    // TODO: Use a new flag in config_params.stream_params_by_name to mark channels as async channels.
    //       will also used to mark streams as async in ConfiguredNetworkGroupBase::create_in/output_stream_from_config_params
    //       (HRT-9104)
    auto status = create_boundary_channels(resources_manager, *core_op_metadata);
    CHECK_SUCCESS_AS_EXPECTED(status);

    status = resources_manager.fill_internal_buffers_info();
    CHECK_SUCCESS_AS_EXPECTED(status);

    // NOTE: need to allocate the hw infer buffers after creating boundary channels so we can give the 
    // correct ccb dma address in case of hw infer over ccb boundary
    if (is_env_variable_on(HAILO_CONFIGURE_FOR_HW_INFER_ENV_VAR)) {
        status = resources_manager.allocate_boundary_channels_buffers_hw_infer();
        CHECK_SUCCESS_AS_EXPECTED(status);
    }

    if (hef.pimpl->zero_copy_config_over_descs()) {
        status = ResourcesManagerBuilder::prepare_aligned_ccws_resources(hef, resources_manager, driver);
        CHECK_SUCCESS_AS_EXPECTED(status);
    }

    TRY(auto activation_context, resources_manager.add_new_context(CONTROL_PROTOCOL__CONTEXT_SWITCH_CONTEXT_TYPE_ACTIVATION, hef.pimpl->zero_copy_config_over_descs()));
    status = fill_activation_config_recepies_for_multi_context(activation_context.get(),
        resources_manager, core_op_metadata, hw_arch);
    CHECK_SUCCESS_AS_EXPECTED(status);

    // Get all boundary input layers in activation context for batch switch
    const auto activation_context_boundary_input_layers =
        activation_context.get().get_edge_layers(LayerType::BOUNDARY, HAILO_H2D_STREAM);

    TRY(auto batch_switching_context, resources_manager.add_new_context(CONTROL_PROTOCOL__CONTEXT_SWITCH_CONTEXT_TYPE_BATCH_SWITCHING, hef.pimpl->zero_copy_config_over_descs()));
    status = fill_batch_switching_context_config_recepies_for_multi_context(batch_switching_context.get(),
        *core_op_metadata, resources_manager, hw_arch, activation_context_boundary_input_layers);
    CHECK_SUCCESS_AS_EXPECTED(status);

    const auto is_single_context = (core_op_metadata->dynamic_contexts().size() == 1);
    TRY(auto preliminary_context, resources_manager.add_new_context(CONTROL_PROTOCOL__CONTEXT_SWITCH_CONTEXT_TYPE_PRELIMINARY,
        hef.pimpl->zero_copy_config_over_descs(), core_op_metadata->preliminary_context().config_buffers_info()));

    status = fill_preliminary_config_recepies_for_multi_context(hw_arch, preliminary_context.get(),
        resources_manager, core_op_metadata, core_op_metadata->preliminary_context(), is_single_context,
        hef.pimpl->zero_copy_config_over_descs());
    CHECK_SUCCESS_AS_EXPECTED(status);

    const auto caches_in_use = core_op_metadata->get_cache_layers_count() > 0;
    CHECK_AS_EXPECTED(!caches_in_use || !is_single_context, HAILO_INVALID_ARGUMENT,
        "Caches are in use but the network is single context");

    const uint16_t num_dynamic_contexts = static_cast<uint16_t>(core_op_metadata->dynamic_contexts().size());
    for (uint16_t context_index = 0; context_index < num_dynamic_contexts; context_index++) {
        const auto &context_metadata = core_op_metadata->dynamic_contexts()[context_index];
        TRY(auto new_context, resources_manager.add_new_context(CONTROL_PROTOCOL__CONTEXT_SWITCH_CONTEXT_TYPE_DYNAMIC,
            hef.pimpl->zero_copy_config_over_descs(), context_metadata.config_buffers_info()));
        //Add edge layers mapping
	    status = parse_and_fill_edge_layers_mapping(new_context.get(), context_metadata, resources_manager, hw_arch);
	    CHECK_SUCCESS(status);
    }

    for (uint16_t context_index = 0; context_index < num_dynamic_contexts; context_index++) {
        const auto &context_metadata = core_op_metadata->dynamic_contexts()[context_index];
        const bool is_last_context = (context_index == (num_dynamic_contexts - 1));
        const uint16_t config_context_index = resources_manager.get_supported_features().split_allow_input_action ?
            (is_last_context ? 0 : static_cast<uint16_t>(context_index + 1)) : context_index;
	    status = fill_context_recipes_for_multi_context(hw_arch, resources_manager.get_context_resources()[
            context_index + CONTROL_PROTOCOL__CONTEXT_SWITCH_NUMBER_OF_NON_DYNAMIC_CONTEXTS], resources_manager,
            static_cast<uint16_t>(config_context_index), *core_op_metadata, context_metadata, is_single_context,
            is_last_context, caches_in_use, hef.pimpl->zero_copy_config_over_descs(), config_params.enable_kv_cache,
            resources_manager.get_context_resources()[config_context_index + CONTROL_PROTOCOL__CONTEXT_SWITCH_NUMBER_OF_NON_DYNAMIC_CONTEXTS]);
        CHECK_SUCCESS_AS_EXPECTED(status);
    }

    status = resources_manager.configure();
    CHECK_SUCCESS_AS_EXPECTED(status);

    auto resources_manager_ptr = make_shared_nothrow<ResourcesManager>(std::move(resources_manager));
    CHECK_NOT_NULL_AS_EXPECTED(resources_manager_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return resources_manager_ptr;
}

} /* namespace hailort */
