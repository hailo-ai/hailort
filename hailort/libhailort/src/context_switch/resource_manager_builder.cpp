/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file resource_manager_builder.cpp
 * @brief Builds a ResourcesManager object for the given CoreOp.
 **/

#include "resource_manager_builder.hpp"
#include "control.hpp"

namespace hailort
{


static uint16_t calculate_periph_buffers_per_frame(const CONTROL_PROTOCOL__hw_consts_t &hw_consts,
    uint16_t min_periph_buffers_per_frame, uint32_t frame_size, uint16_t periph_buffers_per_frame)
{
    const auto max_periph_buffers_per_frame = MIN(frame_size, static_cast<uint32_t>(hw_consts.max_periph_buffers_per_frame));
    // Fifo copies FIFO_WORD_GRANULARITY_IN_BYTES each time from/to the fifo
    const uint32_t frame_size_words_count = frame_size / hw_consts.fifo_word_granularity_bytes;
    // Look for the highest periph_bytes_per_buffer (frame_size / periph_buffers_per_frame) that is a multiple of FIFO_WORD_GRANULARITY_IN_BYTES
    for (uint16_t i = min_periph_buffers_per_frame; i < max_periph_buffers_per_frame; i++) {
        // (0 == (frame_size_words_count % i) ensures periph_bytes_per_buffer will be a multiple of FIFO_WORD_GRANULARITY_IN_BYTES
        if ((0 == (frame_size_words_count % i)) && (hw_consts.max_periph_bytes_per_buffer >= (frame_size / i))) {
            return i;
        }
    }

    // Fallback to frame_size unless it exceeds MAX_PERIPH_BUFFERS_PER_FRAME
    if (hw_consts.max_periph_buffers_per_frame < frame_size) {
        return periph_buffers_per_frame;
    } else {
        return static_cast<uint16_t>(frame_size);
    }
}

static hailo_status calculate_credit_params(const CONTROL_PROTOCOL__hw_consts_t &hw_consts, uint16_t desc_page_size,
    hailo_stream_direction_t direction, bool should_optimize_credits, uint16_t *periph_bytes_per_buffer,
    uint16_t *periph_buffers_per_frame)
{
    // Next parameters differ between RX and TX

    auto local_periph_bytes_per_buffer = (*periph_bytes_per_buffer);
    auto local_periph_buffers_per_frame = (*periph_buffers_per_frame);
    uint32_t periph_frame_size = (*periph_bytes_per_buffer) * (*periph_buffers_per_frame);
    const auto max_bytes_per_buffer = MAX(hw_consts.max_acceptable_bytes_per_buffer, (*periph_bytes_per_buffer));

    if (0 != (local_periph_bytes_per_buffer % hw_consts.fifo_word_granularity_bytes)) {
        return HAILO_INTERNAL_FAILURE;
    }

    if (should_optimize_credits) {
        // If credits optimizations flag is on, assuming periph_buffers_per_frame * periph_bytes_per_buffer == periph_frame_size
        // Find the lowest periph_buffers_per_frame that divides periph_frame_size and is bigger than periph_frame_size / max_bytes_per_buffer
        // Also, periph_bytes_per_buffer must be a multiple of 8
        const auto min_periph_buffers_per_frame = DIV_ROUND_UP(periph_frame_size, max_bytes_per_buffer);
        local_periph_buffers_per_frame = calculate_periph_buffers_per_frame(hw_consts, static_cast<uint16_t>(min_periph_buffers_per_frame),
            periph_frame_size, local_periph_buffers_per_frame);
        assert(IS_FIT_IN_UINT16(periph_frame_size / local_periph_buffers_per_frame));
        local_periph_bytes_per_buffer = static_cast<uint16_t>(periph_frame_size / local_periph_buffers_per_frame); // Must be integer according to last function
    }
    // Periph credits size must be lower than the following value to make sure that the credit size allows
    // for at least desc_page_size bytes left in the FIFO for the last descriptor in the pattern
    if ((direction == HAILO_D2H_STREAM) &&
        (static_cast<uint32_t>(local_periph_bytes_per_buffer) > (hw_consts.outbound_data_stream_size - 8 - desc_page_size))) {
        LOGGER__ERROR("Current periph_bytes_per_buffer is {} which is too high. Exiting.", local_periph_bytes_per_buffer);
        return HAILO_INTERNAL_FAILURE;
    }

    *periph_bytes_per_buffer = local_periph_bytes_per_buffer;
    *periph_buffers_per_frame = local_periph_buffers_per_frame;
    return HAILO_SUCCESS;
}

static Expected<LayerInfo> update_layer_info(const LayerInfo &original_layer_info,
    const CONTROL_PROTOCOL__host_buffer_info_t &buffer_info,
    const CONTROL_PROTOCOL__hw_consts_t &hw_consts, bool should_optimize_credits)
{
    LayerInfo local_layer_info = original_layer_info;

    auto status = calculate_credit_params(hw_consts, buffer_info.desc_page_size, local_layer_info.direction,
        should_optimize_credits, &local_layer_info.nn_stream_config.periph_bytes_per_buffer,
        &local_layer_info.nn_stream_config.periph_buffers_per_frame);
    CHECK_SUCCESS_AS_EXPECTED(status);

    if (local_layer_info.max_shmifo_size == 0) {
        local_layer_info.max_shmifo_size = hw_consts.default_initial_credit_size;
    }

    return local_layer_info;
}

static hailo_status fill_boundary_input_layer(ContextResources &context_resources, 
    ResourcesManager &resources_manager, const LayerInfo layer_info, const CONTROL_PROTOCOL__hw_consts_t &hw_consts,
    bool should_optimize_credits)
{
    const auto transfer_size = (layer_info.nn_stream_config.periph_bytes_per_buffer *
        layer_info.nn_stream_config.core_buffers_per_frame);

    auto vdma_channel = resources_manager.get_boundary_vdma_channel_by_stream_name(layer_info.name);
    CHECK_EXPECTED_AS_STATUS(vdma_channel);

    auto buffer_info = vdma_channel.value()->get_boundary_buffer_info(transfer_size);
    CHECK_EXPECTED_AS_STATUS(buffer_info);

    auto local_layer_info = update_layer_info(layer_info, *buffer_info, hw_consts, should_optimize_credits);
    CHECK_EXPECTED_AS_STATUS(local_layer_info);

    BoundaryEdgeLayer edge_layer{};
    edge_layer.layer_info = local_layer_info.release();
    edge_layer.channel_id = vdma_channel.value()->get_channel_id();
    edge_layer.buffer_info = buffer_info.value();
    context_resources.add_edge_layer(edge_layer);

    LOGGER__DEBUG("Boundary input stream: {} h2d_channel: {}.", layer_info.stream_index, edge_layer.channel_id);
    return HAILO_SUCCESS;
}

static hailo_status fill_inter_context_input_layer(ContextResources &context_resources,
    ResourcesManager &resources_manager, const LayerInfo &layer_info, const CONTROL_PROTOCOL__hw_consts_t &hw_consts,
    bool should_optimize_credits)
{
    const auto channel_id = resources_manager.get_available_channel_id(to_layer_identifier(layer_info),
        VdmaChannel::Direction::H2D, layer_info.dma_engine_index);
    CHECK_EXPECTED_AS_STATUS(channel_id);

    /* Get inter context buffer previously created */
    const auto &connected_context = layer_info.connected_context_info;
    auto intermediate_buffer_key = std::make_pair(connected_context.context_index, connected_context.stream_index);
    auto inter_context_buffer_exp = resources_manager.get_inter_context_buffer(intermediate_buffer_key);
    CHECK_EXPECTED_AS_STATUS(inter_context_buffer_exp, "Failed to find inter context buffer for src context {}, src_stream_index {}",
        connected_context.context_index, connected_context.stream_index);
    auto &inter_context_buffer = inter_context_buffer_exp->get();

    auto local_layer_info = update_layer_info(layer_info, inter_context_buffer.get_host_buffer_info(), hw_consts,
        should_optimize_credits);
    CHECK_EXPECTED_AS_STATUS(local_layer_info);

    InterContextEdgeLayer edge_layer{};
    edge_layer.layer_info = local_layer_info.release();
    edge_layer.channel_id = channel_id.value();
    edge_layer.buffer_info = inter_context_buffer.get_host_buffer_info();
    context_resources.add_edge_layer(edge_layer);

    LOGGER__DEBUG("Intermediate input stream {}, src_context:{}, dst_context: {}, h2d_channel {}.",
        layer_info.stream_index, layer_info.context_index, layer_info.connected_context_info.context_index,
        channel_id.value());

    return HAILO_SUCCESS;
}

static hailo_status fill_boundary_output_layer(ContextResources &context_resources,
    ResourcesManager &resources_manager, const LayerInfo &layer_info, const CONTROL_PROTOCOL__hw_consts_t &hw_consts,
    bool should_optimize_credits)
{
    const auto transfer_size = (layer_info.nn_stream_config.periph_bytes_per_buffer *
        layer_info.nn_stream_config.core_buffers_per_frame);

    auto vdma_channel = resources_manager.get_boundary_vdma_channel_by_stream_name(layer_info.name);
    CHECK_EXPECTED_AS_STATUS(vdma_channel);

    auto buffer_info = vdma_channel.value()->get_boundary_buffer_info(transfer_size);
    CHECK_EXPECTED_AS_STATUS(buffer_info);

    auto local_layer_info = update_layer_info(layer_info, *buffer_info, hw_consts, should_optimize_credits);
    CHECK_EXPECTED_AS_STATUS(local_layer_info);

    BoundaryEdgeLayer edge_layer{};
    edge_layer.layer_info = local_layer_info.release();
    edge_layer.channel_id = vdma_channel.value()->get_channel_id();
    edge_layer.buffer_info = buffer_info.value();
    context_resources.add_edge_layer(edge_layer);

    LOGGER__DEBUG("Boundary output stream: {} d2h_channel: {}.", layer_info.stream_index, edge_layer.channel_id);
    return HAILO_SUCCESS;
}

static hailo_status fill_inter_context_output_layer(ContextResources &context_resources,
    ResourcesManager &resources_manager, const LayerInfo &layer_info,
    const CONTROL_PROTOCOL__hw_consts_t &hw_consts, bool should_optimize_credits)
{
    const auto channel_id = resources_manager.get_available_channel_id(to_layer_identifier(layer_info),
        VdmaChannel::Direction::D2H, layer_info.dma_engine_index);
    CHECK_EXPECTED_AS_STATUS(channel_id);

    const auto frame_credits_in_bytes = (layer_info.nn_stream_config.periph_bytes_per_buffer * 
        layer_info.nn_stream_config.core_buffers_per_frame);

    auto inter_context_buffer_exp = resources_manager.create_inter_context_buffer(frame_credits_in_bytes,
        layer_info.stream_index, layer_info.context_index, layer_info.network_name);
    CHECK_EXPECTED_AS_STATUS(inter_context_buffer_exp);
    auto &inter_context_buffer = inter_context_buffer_exp->get();

    auto local_layer_info = update_layer_info(layer_info, inter_context_buffer.get_host_buffer_info(), hw_consts,
        should_optimize_credits);
    CHECK_EXPECTED_AS_STATUS(local_layer_info);

    InterContextEdgeLayer edge_layer{};
    edge_layer.layer_info = local_layer_info.release();
    edge_layer.channel_id = channel_id.value();
    edge_layer.buffer_info = inter_context_buffer.get_host_buffer_info();
    context_resources.add_edge_layer(edge_layer);

    LOGGER__DEBUG("Inter-context output stream {}, src_context:{}, d2h_channel {}.",
        layer_info.stream_index, layer_info.context_index, channel_id.value());
    return HAILO_SUCCESS;
}

static hailo_status fill_ddr_output_layer(ContextResources &context_resources,
    ResourcesManager &resources_manager, const LayerInfo &layer_info,
    const CONTROL_PROTOCOL__hw_consts_t &hw_consts)
{
    CHECK(resources_manager.get_supported_features().padded_ddr_buffers, HAILO_INVALID_HEF,
        "Failed opening non-compatible HEF that uses the following deprecated features: host-managed DDR buffers." 
        "Please re-compile the HEF using a newer Dataflow Compiler version (v3.11.0 or newer)");
    // Allocate resources and prepare ddr_info

    DdrChannelsInfo ddr_pair_info = {};
    ddr_pair_info.h2d_stream_index = layer_info.connected_context_info.stream_index;
    ddr_pair_info.d2h_stream_index = layer_info.stream_index;
    ddr_pair_info.network_index = layer_info.network_index;

    // It is assumed that output channels are parsed before input channels. 
    // Allocate vdma channel index for both edges
    const auto h2d_layer_identifier = std::make_tuple(LayerType::DDR, layer_info.name, ddr_pair_info.h2d_stream_index);
    const auto h2d_channel_id = resources_manager.get_available_channel_id(h2d_layer_identifier,
        VdmaChannel::Direction::H2D, layer_info.connected_context_info.dma_engine_index);
    CHECK_EXPECTED_AS_STATUS(h2d_channel_id);
    ddr_pair_info.h2d_channel_id = h2d_channel_id.value();

    const auto d2h_layer_identifier = std::make_tuple(LayerType::DDR, layer_info.name, ddr_pair_info.d2h_stream_index);
    const auto d2h_channel_id = resources_manager.get_available_channel_id(d2h_layer_identifier,
        VdmaChannel::Direction::D2H, layer_info.dma_engine_index);
    CHECK_EXPECTED_AS_STATUS(d2h_channel_id);
    ddr_pair_info.d2h_channel_id = d2h_channel_id.value();

    ddr_pair_info.row_size = layer_info.nn_stream_config.core_bytes_per_buffer;
    ddr_pair_info.min_buffered_rows = layer_info.ddr_info.min_buffered_rows;
    ddr_pair_info.total_buffers_per_frame = layer_info.ddr_info.total_buffers_per_frame;

    // Create the ddr buffer
    auto ddr_channels_pair = context_resources.create_ddr_channels_pair(ddr_pair_info);
    CHECK_EXPECTED_AS_STATUS(ddr_channels_pair);

    // On ddr layers, we assume the periph credit size is aligned to the size of descriptor, so we don't want to
    // optimize the credits.
    const bool should_optimize_credits = false;
    auto local_layer_info = update_layer_info(layer_info, ddr_channels_pair->get().get_host_buffer_info(), hw_consts,
        should_optimize_credits);
    CHECK_EXPECTED_AS_STATUS(local_layer_info);

    DdrChannelEdgeLayer edge_layer{};
    edge_layer.layer_info = local_layer_info.release();
    edge_layer.channel_id = ddr_pair_info.d2h_channel_id;
    edge_layer.buffer_info = ddr_channels_pair->get().get_host_buffer_info();
    context_resources.add_edge_layer(edge_layer);

    return HAILO_SUCCESS;
}

static hailo_status fill_ddr_input_layer(ContextResources &context_resources,
    const LayerInfo &layer_info, const CONTROL_PROTOCOL__hw_consts_t &hw_consts)
{
    auto connected_stream_index = layer_info.connected_context_info.stream_index;
    auto ddr_channels_pair = context_resources.get_ddr_channels_pair(connected_stream_index);
    CHECK(ddr_channels_pair, HAILO_INVALID_HEF, "Matching DDR layer as not found for context {} src stream {}",
        layer_info.context_index, connected_stream_index);

    const auto ddr_info = ddr_channels_pair->get().info();
    LOGGER__DEBUG("DDR layer: input stream_index: {}, output stream_index: {}, h2d_channel {}, d2h_channel: {}.",
        ddr_info.h2d_stream_index, ddr_info.d2h_stream_index, ddr_info.h2d_channel_id, ddr_info.d2h_channel_id);

    CHECK(layer_info.stream_index == ddr_info.h2d_stream_index, HAILO_INVALID_HEF, "DDR channel pair mismatch in h2d channel");
    CHECK(layer_info.connected_context_info.stream_index == ddr_info.d2h_stream_index, HAILO_INVALID_HEF, "DDR channel pair mismatch in d2h channel");
    CHECK(layer_info.network_index == ddr_info.network_index, HAILO_INVALID_HEF, "DDR channel pair mismatch network_index");

    // On ddr layers, we assume the periph credit size is aligned to the size of descriptor, so we don't want to
    // optimize the credits.
    const bool should_optimize_credits = false;
    auto local_layer_info = update_layer_info(layer_info, ddr_channels_pair->get().get_host_buffer_info(), hw_consts,
        should_optimize_credits);
    CHECK_EXPECTED_AS_STATUS(local_layer_info);

    DdrChannelEdgeLayer edge_layer{};
    edge_layer.layer_info = local_layer_info.release();
    edge_layer.channel_id = ddr_info.h2d_channel_id;
    edge_layer.buffer_info = ddr_channels_pair->get().get_host_buffer_info();
    context_resources.add_edge_layer(edge_layer);

    return HAILO_SUCCESS;
}

static hailo_status add_ddr_buffers_info(std::vector<ContextSwitchConfigActionPtr> &configuration_actions,
    const ContextResources &context_resources)
{
    bool start_fw_ddr_buffer_task = false;
    for (auto& ddr_channels_pair : context_resources.get_ddr_channels_pairs()) {
        if (ddr_channels_pair.need_manual_credit_management()) {
            const auto ddr_info = ddr_channels_pair.info();
            auto ddr_pair_action = DdrPairInfoAction::create(ddr_info.h2d_channel_id, ddr_info.d2h_channel_id,
                ddr_info.network_index, ddr_channels_pair.descriptors_per_frame(), ddr_channels_pair.descs_count());
            CHECK_EXPECTED_AS_STATUS(ddr_pair_action);
            configuration_actions.emplace_back(ddr_pair_action.release());

            start_fw_ddr_buffer_task = true;
        }
    }

    if (start_fw_ddr_buffer_task) {
        auto start_ddr_buffering_action = StartDdrBufferingTaskAction::create();
        CHECK_EXPECTED_AS_STATUS(start_ddr_buffering_action);
        configuration_actions.emplace_back(start_ddr_buffering_action.release());
    }

    return HAILO_SUCCESS;
}

static hailo_status parse_and_fill_edge_layers_mapping(
    ContextResources &context_resources,
    const ContextMetadata &context_metadata,
    ResourcesManager &resources_manager)
{
    hailo_status status = HAILO_UNINITIALIZED;

    auto hw_consts = Control::get_hw_consts(resources_manager.get_device());
    CHECK_EXPECTED_AS_STATUS(hw_consts);
    const bool should_optimize_credits = hw_consts->should_optimize_credits &&
        (HAILO_POWER_MODE_PERFORMANCE == resources_manager.get_power_mode());

    // Parse the edge layer by order - first output edge layers, then ddr inputs and only then the input edge layers
    // In order to insure that input data can enter the chip only after all other elements are configured.
    // We parse ddr inputs before boundary/inter-context because otherwise on C2C mode we may lose some credit.

    for (const auto &output_layer_info : context_metadata.get_ddr_output_layers()) {
        status = fill_ddr_output_layer(context_resources, resources_manager, output_layer_info, *hw_consts);
        CHECK_SUCCESS(status);
    }

    for (const auto &output_layer_info : context_metadata.get_boundary_output_layers()) {
        status = fill_boundary_output_layer(context_resources, resources_manager, output_layer_info,
            *hw_consts, should_optimize_credits);
        CHECK_SUCCESS(status);
    }

    for (const auto &output_layer_info : context_metadata.get_inter_context_output_layers()) {
        status = fill_inter_context_output_layer(context_resources, resources_manager, output_layer_info,
            *hw_consts, should_optimize_credits);
        CHECK_SUCCESS(status);
    }

    for (const auto &input_layer_info : context_metadata.get_ddr_input_layers()) {
        status = fill_ddr_input_layer(context_resources, input_layer_info, *hw_consts);
        CHECK_SUCCESS(status);
    }

    for (const auto &input_layer_info : context_metadata.get_boundary_input_layers()) {
        status = fill_boundary_input_layer(context_resources, resources_manager, input_layer_info,
            *hw_consts, should_optimize_credits);
        CHECK_SUCCESS(status);
    }

    for (const auto &input_layer_info : context_metadata.get_inter_context_input_layers()) {
        status = fill_inter_context_input_layer(context_resources, resources_manager, input_layer_info,
            *hw_consts, should_optimize_credits);
        CHECK_SUCCESS(status);
    }

    status = context_resources.validate_edge_layers();
    CHECK_SUCCESS(status);

    /* UN-Lock resources at the end of the context - 
        h2d inter-context, d2h inter-context and DDR buffer channels */
    for (const auto &input_layer_info : context_metadata.get_inter_context_input_layers()) {
        status = resources_manager.free_channel_index(to_layer_identifier(input_layer_info));
        CHECK_SUCCESS(status);
    }

    for (const auto &output_layer_info : context_metadata.get_inter_context_output_layers()) {
        status = resources_manager.free_channel_index(to_layer_identifier(output_layer_info));
        CHECK_SUCCESS(status);
    }

    for (const auto &output_layer_info : context_metadata.get_ddr_output_layers()) {
        const auto h2d_layer_identifier = std::make_tuple(LayerType::DDR, output_layer_info.name,
            output_layer_info.connected_context_info.stream_index);
        status = resources_manager.free_channel_index(h2d_layer_identifier);
        CHECK_SUCCESS(status);

        const auto d2h_layer_identifier = std::make_tuple(LayerType::DDR, output_layer_info.name,
            output_layer_info.stream_index);
        status = resources_manager.free_channel_index(d2h_layer_identifier);
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

static std::set<uint32_t> get_end_indexes_of_action_type(
    const std::vector<ContextSwitchConfigActionPtr> &actions,
    const std::vector<std::pair<uint32_t, uint32_t>> &repeated_indexes,
    const ContextSwitchConfigAction::Type &required_action_type)
{
    std::set<uint32_t> result;
    for (const auto &index_pair : get_indexes_of_action_type(actions, repeated_indexes, required_action_type)) {
        result.insert(index_pair.second);
    }

    return result;
}

static hailo_status push_fetch_config_actions(
    std::vector<ConfigBuffer> &config_resources, const std::set<uint8_t> &pending_config_stream_indexes,
    std::vector<uint16_t> &total_ccw_bursts, const bool support_pre_fetch,
    std::vector<ContextSwitchConfigActionPtr> &processed_configuration_actions)
{
    CHECK(total_ccw_bursts.size() == config_resources.size(), HAILO_INTERNAL_FAILURE, "Invalid cfg channels count");
    for (const auto config_stream_index : pending_config_stream_indexes) {
        CHECK(config_stream_index < config_resources.size(), HAILO_INTERNAL_FAILURE, "Invalid cfg channel index");

        if (support_pre_fetch) {
            auto action = AddCcwBurstAction::create(config_stream_index, total_ccw_bursts[config_stream_index]);
            CHECK_EXPECTED_AS_STATUS(action);
            processed_configuration_actions.emplace_back(action.release());
        } else {
            const auto desc_count = config_resources[config_stream_index].program_descriptors();
            CHECK_EXPECTED_AS_STATUS(desc_count);
            CHECK(IS_FIT_IN_UINT16(desc_count.value()), HAILO_INVALID_OPERATION,
                "On cfg with continuous mode, max descriptors size must fit in uint16_t");

            auto action = FetchCfgChannelDescriptorsAction::create(config_resources[config_stream_index].channel_id(),
                static_cast<uint16_t>(desc_count.value()));
            CHECK_EXPECTED_AS_STATUS(action);
            processed_configuration_actions.emplace_back(action.release());
        }
    }

    return HAILO_SUCCESS;
}

static hailo_status write_ccw_to_buffer(ConfigBuffer& config_buffer, const WriteDataCcwAction &ccw_action,
    bool support_pre_fetch)
{
    const bool is_last_write = config_buffer.size_left() == ccw_action.data().size();
    if (support_pre_fetch && is_last_write) {
        auto status = config_buffer.pad_with_nops();
        CHECK_SUCCESS(status);
    }

    auto status = config_buffer.write(ccw_action.data());
    CHECK_SUCCESS(status);

    if (support_pre_fetch && is_last_write) {
        auto desc_count = config_buffer.program_descriptors();
        CHECK_EXPECTED_AS_STATUS(desc_count);
    }

    return HAILO_SUCCESS;
}

static hailo_status proccess_write_ccw_action(const ContextSwitchConfigActionPtr &configuration_action,
    std::vector<ConfigBuffer> &config_resources, std::set<uint8_t> &pending_config_stream_indexes,
    std::vector<uint16_t> &total_ccw_bursts, const std::set<uint32_t> &end_indexes_of_write_ccw_actions, 
    const uint32_t &action_index, const bool support_pre_fetch, 
    std::vector<ContextSwitchConfigActionPtr> &processed_configuration_actions)
{
    assert(ContextSwitchConfigAction::Type::WriteDataCcw == configuration_action->get_type());
    const auto &write_ccw_action = *static_cast<const WriteDataCcwAction*>(configuration_action.get());

    // Add the config stream index of the current WriteDataCcwAction
    const auto config_stream_index = write_ccw_action.config_stream_index();
    pending_config_stream_indexes.insert(config_stream_index);

    // TODO: get CCW headers from proto (need to add it into the proto)
    const uint16_t ccw_bursts = 1;
    auto accum_ccw_bursts = total_ccw_bursts[config_stream_index] + ccw_bursts;
    CHECK(IS_FIT_IN_UINT16(accum_ccw_bursts), HAILO_INTERNAL_FAILURE,
        "Failed to parse HEF. action fetch ccw burst supports only to 2^16 bursts.");
    assert(config_stream_index < total_ccw_bursts.size());
    total_ccw_bursts[config_stream_index] = static_cast<uint16_t>(accum_ccw_bursts);

    assert(config_stream_index < config_resources.size());
    auto status = write_ccw_to_buffer(config_resources[config_stream_index], write_ccw_action, support_pre_fetch);
    CHECK_SUCCESS(status);

    // At the end of a consecutive group of WriteDataCcwActions, we program the
    // descriptors for all the config channels used.
    if (contains(end_indexes_of_write_ccw_actions, action_index)) {
        // Add the last CCW write into the buffer
        processed_configuration_actions.emplace_back(configuration_action);

        status = push_fetch_config_actions(config_resources, pending_config_stream_indexes, total_ccw_bursts,
            support_pre_fetch, processed_configuration_actions);
        CHECK_SUCCESS(status);

        // Cleanups
        pending_config_stream_indexes.clear();
        for (uint8_t cleanup_ch_index = 0; cleanup_ch_index < total_ccw_bursts.size(); cleanup_ch_index++) {
            total_ccw_bursts[cleanup_ch_index] = 0;
        }
    } else {
        // Add the current action
        processed_configuration_actions.emplace_back(configuration_action);
    }

    return HAILO_SUCCESS;
}

static Expected<uint8_t> find_dummy_stream(const LayerInfo &layer_info, const ContextResources &context_resources)
{
    // TODO: HRT-8611 use one loop for all edge layers
    for (const auto &edge_layer : context_resources.get_boundary_layers()) {
        if (edge_layer.layer_info.direction != layer_info.direction) {
            return Expected<uint8_t>(edge_layer.layer_info.stream_index);
        }
    }
    for (const auto &edge_layer : context_resources.get_inter_context_layers()) {
        if (edge_layer.layer_info.direction != layer_info.direction) {
            return Expected<uint8_t>(edge_layer.layer_info.stream_index);
        }
    }
    for (const auto &edge_layer : context_resources.get_ddr_channel_layers()) {
        if (edge_layer.layer_info.direction != layer_info.direction) {
            return Expected<uint8_t>(edge_layer.layer_info.stream_index);
        }
    }

    LOGGER__ERROR("Couldn't find dummy stream from context");
    return make_unexpected(HAILO_INTERNAL_FAILURE);
}

static hailo_status add_change_vdma_to_stream_mapping(
    const NetworkGroupMetadata &network_group_metadata, const ResourcesManager &resources_manager,
    ContextResources &context_resources, uint8_t context_index,
    std::vector<ContextSwitchConfigActionPtr> &processed_configuration_actions)
{
    for (const auto &layer_info : network_group_metadata.get_all_layer_infos()) {
        auto vdma_channel = resources_manager.get_boundary_vdma_channel_by_stream_name(layer_info.name);
        CHECK_EXPECTED_AS_STATUS(vdma_channel);

        const auto channel_id = vdma_channel.value()->get_channel_id();
        const bool is_dummy_stream = layer_info.context_index != context_index;
        uint8_t stream_index = layer_info.stream_index;
        if (is_dummy_stream) {
            auto dummy_stream_index = find_dummy_stream(layer_info, context_resources);
            CHECK_EXPECTED_AS_STATUS(dummy_stream_index);
            stream_index = *dummy_stream_index;
        }

        auto action = ChangeVdmaToStreamMapping::create(channel_id, stream_index, is_dummy_stream);
        CHECK_EXPECTED_AS_STATUS(action);
        processed_configuration_actions.emplace_back(action.release());
    }

    return HAILO_SUCCESS;
}

static hailo_status push_edge_layer_activation_actions(
    const ContextResources &context_resources,
    std::vector<ContextSwitchConfigActionPtr> &actions)
{
    // Activate the edge layer by order - first output edge layers, then ddr inputs and only then the input edge layers
    // In order to insure that input data can enter the chip only after all other elements are configured.
    // We parse ddr inputs before boundary/inter-context because otherwise on C2C mode we may lose some credit.

    for (const auto &edge_layer : context_resources.get_ddr_channel_layers(HAILO_D2H_STREAM)) {
        auto activate_action = ActivateDdrOutputChannelAction::create(edge_layer.channel_id,
            edge_layer.layer_info.stream_index, edge_layer.layer_info.nn_stream_config, edge_layer.buffer_info,
            edge_layer.layer_info.ddr_info.min_buffered_rows);
        CHECK_EXPECTED_AS_STATUS(activate_action);
        actions.emplace_back(activate_action.release());
    }

    for (const auto &edge_layer : context_resources.get_boundary_layers(HAILO_D2H_STREAM)) {
        auto activate_action = ActivateBoundaryOutputChannelAction::create(edge_layer.channel_id,
            edge_layer.layer_info.stream_index, edge_layer.layer_info.nn_stream_config, edge_layer.buffer_info);
        CHECK_EXPECTED_AS_STATUS(activate_action);
        actions.emplace_back(activate_action.release());
    }

    for (const auto &edge_layer : context_resources.get_inter_context_layers(HAILO_D2H_STREAM)) {
        auto activate_action = ActivateInterContextOutputChannelAction::create(edge_layer.channel_id,
            edge_layer.layer_info.stream_index, edge_layer.layer_info.network_index,
            edge_layer.layer_info.nn_stream_config, edge_layer.buffer_info);
        CHECK_EXPECTED_AS_STATUS(activate_action);
        actions.emplace_back(activate_action.release());
    }

    for (const auto &edge_layer : context_resources.get_ddr_channel_layers(HAILO_H2D_STREAM)) {
        const auto d2h_stream_index = edge_layer.layer_info.connected_context_info.stream_index;
        auto pair = context_resources.get_ddr_channels_pair(d2h_stream_index);
        CHECK_EXPECTED_AS_STATUS(pair);
        const auto d2h_channel_id = pair->get().info().d2h_channel_id;

        auto activate_action = ActivateDdrInputChannelAction::create(edge_layer.channel_id,
            edge_layer.layer_info.stream_index, edge_layer.layer_info.nn_stream_config, edge_layer.buffer_info,
            edge_layer.layer_info.max_shmifo_size, d2h_channel_id);
        CHECK_EXPECTED_AS_STATUS(activate_action);
        actions.emplace_back(activate_action.release());
    }

    for (const auto &edge_layer : context_resources.get_boundary_layers(HAILO_H2D_STREAM)) {
        auto activate_action = ActivateBoundaryInputChannelAction::create(edge_layer.channel_id,
            edge_layer.layer_info.stream_index, edge_layer.layer_info.nn_stream_config, edge_layer.buffer_info,
            edge_layer.layer_info.max_shmifo_size);
        CHECK_EXPECTED_AS_STATUS(activate_action);
        actions.emplace_back(activate_action.release());
    }

    for (const auto &edge_layer : context_resources.get_inter_context_layers(HAILO_H2D_STREAM)) {
        auto activate_action = ActivateInterContextInputChannelAction::create(edge_layer.channel_id,
            edge_layer.layer_info.stream_index, edge_layer.layer_info.nn_stream_config, edge_layer.buffer_info,
            edge_layer.layer_info.max_shmifo_size);
        CHECK_EXPECTED_AS_STATUS(activate_action);
        actions.emplace_back(activate_action.release());
    }

    return HAILO_SUCCESS;
}

static hailo_status proccess_trigger_new_data_input_action(const ContextSwitchConfigActionPtr &configuration_action,
    uint32_t trigger_new_data_from_input_group_start,
    uint32_t trigger_new_data_from_input_group_end,
    const uint32_t &action_index,
    const NetworkGroupMetadata &network_group_metadata,
    const ResourcesManager &resources_manager,
    ContextResources &context_resources,
    uint8_t context_index,
    std::vector<ContextSwitchConfigActionPtr> &processed_configuration_actions, bool is_single_context)
{
    if (trigger_new_data_from_input_group_start == action_index) {
        auto status = push_edge_layer_activation_actions(context_resources, processed_configuration_actions);
        CHECK_SUCCESS(status);

        if (!is_single_context) {
            status = add_change_vdma_to_stream_mapping(network_group_metadata, resources_manager,
                context_resources, context_index, processed_configuration_actions);
            CHECK_SUCCESS(status);
        }

        // DDR buffer info actions need to happen after the edge layer activation actions.
        status = add_ddr_buffers_info(processed_configuration_actions, context_resources);
        CHECK_SUCCESS(status);
    }

    // Add the current action
    processed_configuration_actions.emplace_back(configuration_action);

    // At the end of a consecutive group of TriggerNewDataFromDataInput actions, we can trigger the BurstCreditsTask
    // in the FW, via StartBurstCreditsTaskAction.
    if (trigger_new_data_from_input_group_end == action_index) {
        auto start_burst_credits_task_action = StartBurstCreditsTaskAction::create();
        CHECK_EXPECTED_AS_STATUS(start_burst_credits_task_action);
        processed_configuration_actions.emplace_back(start_burst_credits_task_action.release());
    }

    return HAILO_SUCCESS;
}

// At the end of each consecutive group of WriteDataCcwAction, a FetchCfgChannelDescriptorsAction is added.
static hailo_status add_fetch_config_actions(std::vector<ContextSwitchConfigActionPtr> &configuration_actions,
    std::vector<ConfigBuffer> &config_resources, bool support_pre_fetch)
{
    const auto repeated_indexes = get_repreated_actions_boundary_indices(configuration_actions);
    const auto end_indexes_of_write_ccws = get_end_indexes_of_action_type(configuration_actions,
        repeated_indexes, ContextSwitchConfigAction::Type::WriteDataCcw);

    std::set<uint8_t> pending_config_stream_indexes;
    std::vector<uint16_t> total_ccw_bursts(config_resources.size(), 0);
    std::vector<ContextSwitchConfigActionPtr> processed_configuration_actions;
    for (uint32_t action_index = 0; action_index < configuration_actions.size(); action_index++) {
        const auto &configuration_action = configuration_actions[action_index];
        if (ContextSwitchConfigAction::Type::WriteDataCcw == configuration_action->get_type()) {
            auto status = proccess_write_ccw_action(configuration_action, config_resources, pending_config_stream_indexes,
                total_ccw_bursts, end_indexes_of_write_ccws, action_index, support_pre_fetch, processed_configuration_actions);
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
    const std::vector<ConfigBuffer> &config_resources)
{
    std::vector<ContextSwitchConfigActionPtr> processed_actions;
    const size_t new_actions_count = 2 * config_resources.size();
    processed_actions.reserve(actions.size() + new_actions_count);

    for (uint8_t config_stream_index = 0; config_stream_index < config_resources.size(); config_stream_index++) {
        const auto &config_buffer = config_resources[config_stream_index];
        auto activate_action = ActivateConfigChannelAction::create(config_stream_index, config_buffer.channel_id(),
            config_buffer.get_host_buffer_info());
        CHECK_EXPECTED_AS_STATUS(activate_action);
        processed_actions.push_back(activate_action.release());
    }

    processed_actions.insert(processed_actions.end(), actions.begin(), actions.end());

    for (uint8_t config_stream_index = 0; config_stream_index < config_resources.size(); config_stream_index++) {
        const auto &config_buffer = config_resources[config_stream_index];
        auto deactivate_action = DeactivateConfigChannelAction::create(config_stream_index, config_buffer.channel_id());
        CHECK_EXPECTED_AS_STATUS(deactivate_action);
        processed_actions.push_back(deactivate_action.release());
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
static hailo_status handle_edge_layer_activation_actions(std::vector<ContextSwitchConfigActionPtr> &configuration_actions,
    const NetworkGroupMetadata &network_group_metadata,
    const ResourcesManager &resources_manager, ContextResources &context_resources, uint8_t context_index,
    bool is_preliminary_context, bool is_first_operation, bool is_single_context)
{
    if (is_preliminary_context && !resources_manager.get_supported_features().preliminary_run_asap) {
        // Nothing to do - no edge layers in the preliminary context if not running in preliminary_run_asap mode.
        return HAILO_SUCCESS;
    }
    if (!is_preliminary_context && !is_first_operation) {
        // Nothing to do - edge layers in dynamic contexts only appear in the first operation.
        return HAILO_SUCCESS;
    }

    const auto repeated_indexes = get_repreated_actions_boundary_indices(configuration_actions);
    const auto trigger_new_data_from_input_group_indexes = get_indexes_of_action_type(
        configuration_actions, repeated_indexes, ContextSwitchConfigAction::Type::TriggerNewDataFromDataInput);
    CHECK(trigger_new_data_from_input_group_indexes.size() == 1, HAILO_INTERNAL_FAILURE,
        "Expected only one group of TriggerNewDataFromDataInput actions");
    const auto trigger_new_data_from_input_group_start = trigger_new_data_from_input_group_indexes.cbegin()->first;
    const auto trigger_new_data_from_input_group_end = trigger_new_data_from_input_group_indexes.cbegin()->second;

    std::vector<ContextSwitchConfigActionPtr> processed_configuration_actions;
    for (uint32_t action_index = 0; action_index < configuration_actions.size(); action_index++) {
        const auto &configuration_action = configuration_actions[action_index];
        if (ContextSwitchConfigAction::Type::TriggerNewDataFromDataInput == configuration_action->get_type()) {
            auto status = proccess_trigger_new_data_input_action(configuration_action,
                trigger_new_data_from_input_group_start, trigger_new_data_from_input_group_end, action_index,
                network_group_metadata, resources_manager, context_resources, context_index, processed_configuration_actions, is_single_context);
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

            auto repeated_header_action = RepeatedAction::create(std::move(repeated_block));
            CHECK_EXPECTED_AS_STATUS(repeated_header_action);
            processed_configuration_actions.emplace_back(repeated_header_action.value());
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

static bool is_mercury_device_type(const ProtoHEFHwArch &hw_arch)
{
    /* TODO - HRT-5067 - use one hw_arch for mercury */
    return (PROTO__HW_ARCH__MERCURY == hw_arch) || (PROTO__HW_ARCH__GINGER == hw_arch) ||
        (PROTO__HW_ARCH__LAVENDER == hw_arch);
}

static Expected<std::vector<ContextSwitchConfigActionPtr>> process_operation(const ContextSwitchOperation &operation,
    const NetworkGroupMetadata &network_group_metadata,
    const ProtoHEFHwArch &hw_arch, uint8_t context_index, bool is_preliminary_context,
    bool is_first_operation, bool is_single_context, std::vector<ConfigBuffer> &config_resources,
    ResourcesManager &resources_manager,
    ContextResources &context_resources)
{
    auto configuration_actions = operation.actions();

    // Next, we process the actions from the HEF. The resulting vector contains the configuration actions to be
    // executed in chronological order.
    const auto support_pre_fetch = is_mercury_device_type(hw_arch);
    auto status = add_fetch_config_actions(configuration_actions, config_resources, support_pre_fetch);
    CHECK_SUCCESS_AS_EXPECTED(status);

    status = handle_edge_layer_activation_actions(configuration_actions, network_group_metadata, resources_manager,
        context_resources, context_index, is_preliminary_context, is_first_operation, is_single_context);
    CHECK_SUCCESS_AS_EXPECTED(status);

    status = handle_repeated_actions(configuration_actions);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return configuration_actions;
}

static hailo_status write_action_list(const ContextResources & context_resources, ContextSwitchBufferBuilder &builder,
    const std::vector<ContextSwitchConfigActionPtr> &actions)
{
    for (const auto &action : actions) {
        auto action_buffers = action->serialize(context_resources);
        CHECK_EXPECTED_AS_STATUS(action_buffers);

        for (auto &action_buffer : action_buffers.value()) {
            builder.write_action(MemoryView(action_buffer));
        }
    }

    return HAILO_SUCCESS;
}

static hailo_status add_edge_layer_end_of_context_actions(const ContextResources &context_resources,
    std::vector<ContextSwitchConfigActionPtr> &actions)
{

    for (const auto &edge_layer : context_resources.get_boundary_layers()) {
        const bool is_inter_context = false;
        // Validate boundary channels of current context.
        auto validate_action = ValidateChannelAction::create(edge_layer.channel_id, edge_layer.layer_info.direction,
            is_inter_context, static_cast<CONTROL_PROTOCOL__HOST_BUFFER_TYPE_t>(edge_layer.buffer_info.buffer_type),
            edge_layer.layer_info.max_shmifo_size);
        CHECK_EXPECTED_AS_STATUS(validate_action);
        actions.push_back(validate_action.release());
    }

    for (const auto &edge_layer : context_resources.get_inter_context_layers()) {
        const bool is_inter_context = true;
        auto deactivate_action = DeactivateChannelAction::create(edge_layer.channel_id, edge_layer.layer_info.direction,
            is_inter_context, static_cast<CONTROL_PROTOCOL__HOST_BUFFER_TYPE_t>(edge_layer.buffer_info.buffer_type),
            edge_layer.layer_info.max_shmifo_size);
        CHECK_EXPECTED_AS_STATUS(deactivate_action);
        actions.push_back(deactivate_action.release());
    }

    for (const auto &edge_layer : context_resources.get_ddr_channel_layers()) {
        const bool is_inter_context = false;
        auto deactivate_action = DeactivateChannelAction::create(edge_layer.channel_id, edge_layer.layer_info.direction,
            is_inter_context, static_cast<CONTROL_PROTOCOL__HOST_BUFFER_TYPE_t>(edge_layer.buffer_info.buffer_type),
            edge_layer.layer_info.max_shmifo_size);
        CHECK_EXPECTED_AS_STATUS(deactivate_action);
        actions.push_back(deactivate_action.release());
    }

    return HAILO_SUCCESS;
}

static hailo_status fill_context_recipes_for_multi_context(const ProtoHEFHwArch &hw_arch,
    ContextResources &context_resources, ResourcesManager &resources_manager,
    uint8_t context_index, const NetworkGroupMetadata &network_group_metadata, const ContextMetadata &context_metadata,
    bool is_single_context)
{
    hailo_status status = HAILO_UNINITIALIZED;

    // Add edge layers mapping
    status = parse_and_fill_edge_layers_mapping(context_resources, context_metadata, resources_manager);
    CHECK_SUCCESS(status);

    // Parse context
    bool first_operation = true;
    std::vector<ContextSwitchConfigActionPtr> actions;
    for (const auto &operation : context_metadata.get_operations()) {
        static const auto NOT_PRELIMINARY_CONTEXT = false;
        auto new_actions = process_operation(operation, network_group_metadata, hw_arch, context_index, NOT_PRELIMINARY_CONTEXT,
            first_operation, is_single_context, context_resources.get_config_buffers(), resources_manager,
            context_resources);
        CHECK_EXPECTED_AS_STATUS(new_actions);

        actions.insert(actions.end(), new_actions->begin(), new_actions->end());
        first_operation = false;
    }

    status = add_config_channel_activation_actions(actions, context_resources.get_config_buffers());
    CHECK_SUCCESS(status);

    if (is_single_context) {
        // Single context network must wait for network group change event after they finish the dynamic context.
        auto wait_action = WaitForNetworkGroupChangeAction::create();
        CHECK_EXPECTED_AS_STATUS(wait_action);
        actions.emplace_back(wait_action.release());
    }
    else {
        status = add_edge_layer_end_of_context_actions(context_resources, actions);
    }

    return write_action_list(context_resources, context_resources.builder(), actions);
}

static hailo_status create_boundary_channels(ResourcesManager &resources_manager,
    NetworkGroupMetadata &network_group_metadata)
{
    for (const auto &layer_info : network_group_metadata.get_all_layer_infos()) {
        auto status = resources_manager.create_boundary_vdma_channel(layer_info);
        CHECK_SUCCESS(status);
    }
    return HAILO_SUCCESS;
}

static hailo_status fill_activation_config_recepies_for_multi_context(
    ContextResources &context_resources, ResourcesManager &resources_manager,
    std::shared_ptr<NetworkGroupMetadata> network_group_metadata)
{
    auto hw_consts = Control::get_hw_consts(resources_manager.get_device());
    CHECK_EXPECTED_AS_STATUS(hw_consts);
    const bool should_optimize_credits = hw_consts->should_optimize_credits &&
        (HAILO_POWER_MODE_PERFORMANCE == resources_manager.get_power_mode());

    for (const auto &layer_info : network_group_metadata->get_output_layer_infos()){
        auto status = fill_boundary_output_layer(context_resources, resources_manager, layer_info, *hw_consts,
            should_optimize_credits);
        CHECK_SUCCESS(status);
    }

    for (const auto &layer_info : network_group_metadata->get_input_layer_infos()) {
        auto status = fill_boundary_input_layer(context_resources, resources_manager, layer_info, *hw_consts,
            should_optimize_credits);
        CHECK_SUCCESS(status);
    }

    auto status = context_resources.validate_edge_layers();
    CHECK_SUCCESS(status);

    std::vector<ContextSwitchConfigActionPtr> actions;
    for (const auto &edge_layer : context_resources.get_boundary_layers()) {
        auto action = edge_layer.layer_info.direction == HAILO_H2D_STREAM ?
            OpenBoundaryInputChannelAction::create(edge_layer.channel_id, edge_layer.buffer_info) :
            OpenBoundaryOutputChannelAction::create(edge_layer.channel_id, edge_layer.buffer_info);
        CHECK_EXPECTED_AS_STATUS(action);
        actions.emplace_back(action.release());
    }

    return write_action_list(context_resources, context_resources.builder(), actions);
}

static hailo_status fill_batch_switching_context_config_recepies_for_multi_context(
    ContextResources &context_resources, const NetworkGroupMetadata &network_group_metadata)
{
    std::vector<ContextSwitchConfigActionPtr> actions;

    // We need to reset the ddr buffering task when we change the batch_size (since it depends on the batch_size param)
    auto reset_ddr_action = ResetDdrBufferingTaskAction::create();
    CHECK_EXPECTED_AS_STATUS(reset_ddr_action);
    actions.emplace_back(reset_ddr_action.release());

    // We need to re-enable all the lcus of the first context since some of their config regs are batch dependent.
    // => We'll filter out all of the "enable lcu" actions from the preliminary context
    static const std::set<ContextSwitchConfigAction::Type> BATCH_SWITCHING_ACTIONS = {
        ContextSwitchConfigAction::Type::EnableLcuDefault,
        ContextSwitchConfigAction::Type::EnableLcuNonDefault
    };
    for (const auto &operation : network_group_metadata.preliminary_context().get_operations()) {
        auto operation_actions = operation.get_actions_of_type(BATCH_SWITCHING_ACTIONS);

        // Allowing repeated actions
        auto status = handle_repeated_actions(operation_actions);
        CHECK_SUCCESS(status);

        actions.insert(actions.end(), operation_actions.begin(), operation_actions.end());
    }

    return write_action_list(context_resources, context_resources.builder(), actions);
}

static hailo_status fill_preliminary_config_recepies_for_multi_context(const ProtoHEFHwArch &hw_arch,
    ContextResources &context_resources, ResourcesManager &resources_manager,
    std::shared_ptr<NetworkGroupMetadata> network_group_metadata, const PreliminaryContextMetadata &preliminary_context,
    bool is_single_context)
{

    if (resources_manager.get_supported_features().preliminary_run_asap) {
        // Add edge layers mapping (only preliminary_run_asap networks have edge layers in the preliminary context)
        static const auto PRELIMINARY_CONTEXT_INDEX = 0;
        assert(PRELIMINARY_CONTEXT_INDEX < network_group_metadata->dynamic_contexts().size());
        auto status = parse_and_fill_edge_layers_mapping(context_resources,
            network_group_metadata->dynamic_contexts()[PRELIMINARY_CONTEXT_INDEX], resources_manager);
        CHECK_SUCCESS(status);
    }

    // Parse preliminary config
    std::vector<ContextSwitchConfigActionPtr> actions;
    bool first_operation = true;
    for (const auto &operation : preliminary_context.get_operations()) {
        static const auto PRELIMINARY_CONTEXT_INDEX = 0; // First context in the hef
        static const auto PRELIMINARY_CONTEXT = true;
        auto new_actions = process_operation(operation, *network_group_metadata, hw_arch, PRELIMINARY_CONTEXT_INDEX,
            PRELIMINARY_CONTEXT, first_operation, is_single_context, context_resources.get_config_buffers(), resources_manager,
            context_resources);
        CHECK_EXPECTED_AS_STATUS(new_actions);

        actions.insert(actions.end(), new_actions->begin(), new_actions->end());
        first_operation = false;
    }

    auto status = add_config_channel_activation_actions(actions, context_resources.get_config_buffers());
    CHECK_SUCCESS(status);

    return write_action_list(context_resources, context_resources.builder(), actions);
}



Expected<std::shared_ptr<ResourcesManager>> ResourcesManagerBuilder::build(uint8_t net_group_index, VdmaDevice &device,
    HailoRTDriver &driver, const ConfigureNetworkParams &config_params,
    std::shared_ptr<NetworkGroupMetadata> network_group_metadata, const ProtoHEFHwArch &hw_arch)
{
    const auto num_contexts = network_group_metadata->dynamic_contexts().size() +
        CONTROL_PROTOCOL__CONTEXT_SWITCH_NUMBER_OF_NON_DYNAMIC_CONTEXTS;
    CHECK_AS_EXPECTED(CONTROL_PROTOCOL__MAX_CONTEXTS_PER_NETWORK_GROUP >= num_contexts, HAILO_INVALID_HEF,
        "App '{}' contains more contexts than allowed ({} > {})",
        network_group_metadata->network_group_name(), num_contexts, CONTROL_PROTOCOL__MAX_CONTEXTS_PER_NETWORK_GROUP);

    for (auto &network_params : config_params.network_params_by_name) {
        CHECK(HAILO_MAX_BATCH_SIZE >= network_params.second.batch_size, make_unexpected(HAILO_INVALID_ARGUMENT),
            "Given batch size ({}) for network group {}, network {} is bigger than max allowed ({})", network_params.second.batch_size,
            network_group_metadata->network_group_name(), network_params.first, HAILO_MAX_BATCH_SIZE);
    }

    auto resources_manager = ResourcesManager::create(device, driver, config_params, network_group_metadata,
        net_group_index);
    CHECK_EXPECTED(resources_manager);

    auto status = create_boundary_channels(resources_manager.value(), *network_group_metadata);
    CHECK_SUCCESS_AS_EXPECTED(status);

    auto activation_context = resources_manager->add_new_context(CONTROL_PROTOCOL__CONTEXT_SWITCH_CONTEXT_TYPE_ACTIVATION);
    CHECK_EXPECTED(activation_context);
    status = fill_activation_config_recepies_for_multi_context(activation_context.value().get(),
        resources_manager.value(), network_group_metadata);
    CHECK_SUCCESS_AS_EXPECTED(status);

    auto batch_switching_context = resources_manager->add_new_context(CONTROL_PROTOCOL__CONTEXT_SWITCH_CONTEXT_TYPE_BATCH_SWITCHING);
    CHECK_EXPECTED(batch_switching_context);
    status = fill_batch_switching_context_config_recepies_for_multi_context(batch_switching_context.value().get(),
        *network_group_metadata);
    CHECK_SUCCESS_AS_EXPECTED(status);

    const bool is_single_context = network_group_metadata->dynamic_contexts().size() == 1;

    auto preliminary_context = resources_manager->add_new_context(CONTROL_PROTOCOL__CONTEXT_SWITCH_CONTEXT_TYPE_PRELIMINARY,
        network_group_metadata->preliminary_context().config_buffers_info());
    CHECK_EXPECTED(preliminary_context);
    status = fill_preliminary_config_recepies_for_multi_context(hw_arch, preliminary_context.value().get(),
        resources_manager.value(), network_group_metadata, network_group_metadata->preliminary_context(), is_single_context);
    CHECK_SUCCESS_AS_EXPECTED(status);

    uint8_t context_index = 0;
    for (const auto &context_metadata : network_group_metadata->dynamic_contexts()) {
        auto new_context = resources_manager->add_new_context(CONTROL_PROTOCOL__CONTEXT_SWITCH_CONTEXT_TYPE_DYNAMIC,
            context_metadata.config_buffers_info());
        CHECK_EXPECTED(new_context);

        status = fill_context_recipes_for_multi_context(hw_arch, new_context.value().get(), resources_manager.value(),
            context_index, *network_group_metadata,
            context_metadata, is_single_context);
        CHECK_SUCCESS_AS_EXPECTED(status);

        context_index++;
    }

    status = resources_manager->create_internal_vdma_channels();
    CHECK_SUCCESS_AS_EXPECTED(status);

    status = resources_manager->configure();
    CHECK_SUCCESS_AS_EXPECTED(status);

    auto resources_manager_ptr = make_shared_nothrow<ResourcesManager>(resources_manager.release());
    CHECK_NOT_NULL_AS_EXPECTED(resources_manager_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return resources_manager_ptr;
}

} /* namespace hailort */
