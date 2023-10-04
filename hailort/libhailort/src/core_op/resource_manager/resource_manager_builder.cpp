/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file resource_manager_builder.cpp
 * @brief Builds a ResourcesManager object for the given CoreOp.
 **/

#include "resource_manager_builder.hpp"
#include "device_common/control.hpp"


namespace hailort
{


static uint16_t calculate_power_optimized_periph_buffers_per_frame(const CONTROL_PROTOCOL__hw_consts_t &hw_consts,
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

static Expected<LayerInfo> calculate_credit_params(const CONTROL_PROTOCOL__hw_consts_t &hw_consts, uint16_t desc_page_size,
    bool should_optimize_credits, const LayerInfo &layer_info)
{
    // Next parameters differ between RX and TX

    auto local_periph_bytes_per_buffer = layer_info.nn_stream_config.periph_bytes_per_buffer;
    auto local_periph_buffers_per_frame = layer_info.nn_stream_config.periph_buffers_per_frame;
    uint32_t periph_frame_size = local_periph_bytes_per_buffer * local_periph_buffers_per_frame;
    const auto max_bytes_per_buffer = MAX(hw_consts.max_acceptable_bytes_per_buffer, local_periph_bytes_per_buffer);

    CHECK_AS_EXPECTED(0 == (local_periph_bytes_per_buffer % hw_consts.fifo_word_granularity_bytes), HAILO_INTERNAL_FAILURE,
        "Error, Invalid periph bytes ber puffer value {} must divide by {} with no remainder",
        local_periph_bytes_per_buffer, hw_consts.fifo_word_granularity_bytes);

    if (should_optimize_credits) {
        // If credits optimizations flag is on, assuming periph_buffers_per_frame * periph_bytes_per_buffer == periph_frame_size
        // Find the lowest periph_buffers_per_frame that divides periph_frame_size and is bigger than periph_frame_size / max_bytes_per_buffer
        // Also, periph_bytes_per_buffer must be a multiple of 8
        const auto min_periph_buffers_per_frame = DIV_ROUND_UP(periph_frame_size, max_bytes_per_buffer);
        local_periph_buffers_per_frame = calculate_power_optimized_periph_buffers_per_frame(hw_consts,
            static_cast<uint16_t>(min_periph_buffers_per_frame), periph_frame_size, local_periph_buffers_per_frame);
        assert(IS_FIT_IN_UINT16(periph_frame_size / local_periph_buffers_per_frame));
        local_periph_bytes_per_buffer = static_cast<uint16_t>(periph_frame_size / local_periph_buffers_per_frame); // Must be integer according to last function
    }
    // Periph credits size must be lower than the following value to make sure that the credit size allows
    // for at least desc_page_size bytes left in the FIFO for the last descriptor in the pattern
    const bool space_left_in_fifo = ((layer_info.direction != HAILO_D2H_STREAM) ||
        (static_cast<uint32_t>(local_periph_bytes_per_buffer) <= (hw_consts.outbound_data_stream_size - 8 - desc_page_size)));
    CHECK_AS_EXPECTED(space_left_in_fifo, HAILO_INTERNAL_FAILURE,
        "Current periph_bytes_per_buffer is {} which is too high. Exiting.", local_periph_bytes_per_buffer);

    auto updated_layer_info = layer_info;
    updated_layer_info.nn_stream_config.periph_bytes_per_buffer = local_periph_bytes_per_buffer;
    updated_layer_info.nn_stream_config.periph_buffers_per_frame = local_periph_buffers_per_frame;

    return updated_layer_info;
}

// NOTE: in case of ddr where periph is aligned to PERIPH_BYTES_PER_BUFFER_DDR_ALIGNMENT_SIZE we cant force that
// periph_bytes_per_buffer * periph_buffers_per_frame will equal exactly hw_frame_size.
static bool is_logical_periph_bytes_per_buffer(const uint32_t periph_bytes_per_buffer, const size_t hw_frame_size, const bool is_ddr,
    const uint32_t max_shmifo_size, const uint32_t desc_page_size, const uint32_t max_periph_bytes_value,
    const uint16_t core_bytes_per_buffer)
{
    if (0 == periph_bytes_per_buffer) {
        return false;
    }

    if (is_ddr) {
        // In DDR there is no residue of descriptor - but has to divide with no remainder by core_bytes_per_buffer
        // Calculated by DFC
        return (periph_bytes_per_buffer < max_shmifo_size) && (periph_bytes_per_buffer <= max_periph_bytes_value) &&
            (0 == (core_bytes_per_buffer % periph_bytes_per_buffer));
    }
    return ((periph_bytes_per_buffer < (max_shmifo_size - desc_page_size)) &&
        (0 == (hw_frame_size % periph_bytes_per_buffer)) && (periph_bytes_per_buffer <= max_periph_bytes_value));
}

static Expected<std::tuple<uint16_t, uint16_t>> calculate_periph_requirements(const LayerInfo &layer_info, const uint32_t desc_page_size,
    const bool is_periph_calculated_in_hailort, const uint32_t max_periph_bytes_value)
{
    // If extension for calculating periph values in hailort is false and hw padding is not supported - copy values from
    // Core registers, calculate them according to shape and other layer information
    const bool hw_padding_supported = HefConfigurator::is_hw_padding_supported(layer_info, max_periph_bytes_value);
    if (!is_periph_calculated_in_hailort && !hw_padding_supported) {
        return std::make_tuple(static_cast<uint16_t>(layer_info.nn_stream_config.core_bytes_per_buffer),
            static_cast<uint16_t>(layer_info.nn_stream_config.core_buffers_per_frame));
    }

    if (HAILO_FORMAT_ORDER_HAILO_NMS == layer_info.format.order) {
        CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(layer_info.nms_info.bbox_size * layer_info.nms_info.burst_size),
            HAILO_INVALID_HEF, "Invalid NMS parameters");
        const auto nms_periph_bytes = static_cast<uint16_t>(layer_info.nms_info.bbox_size * layer_info.nms_info.burst_size);

        const auto transfer_size = LayerInfoUtils::get_nms_layer_transfer_size(layer_info);
        CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(transfer_size / nms_periph_bytes), HAILO_INVALID_HEF, "Invalid NMS parameters");
        // Will divide with no remainder seeing as transfer size is multiple of (bbox_size * burst_size)
        assert(0 == (transfer_size % nms_periph_bytes));
        const auto nms_periph_buffers = static_cast<uint16_t>(transfer_size / nms_periph_bytes);

        // In NMS - update periph variables to represent size of frame in case of "interrupt per frame" (where we know frame size)
        // Otherwise - size of burst / bbox (transfer size)
        return std::make_tuple(nms_periph_bytes, nms_periph_buffers);
    }

    CHECK_AS_EXPECTED(IS_FIT_IN_UINT32(layer_info.hw_shape.width * layer_info.hw_shape.features *
        layer_info.hw_shape.height * layer_info.hw_data_bytes), HAILO_INVALID_HEF, "Invalid core frame size");

    const auto is_ddr = (LayerType::DDR == layer_info.type);
    const uint32_t alignment = is_ddr ? PERIPH_BYTES_PER_BUFFER_DDR_ALIGNMENT_SIZE : PERIPH_BYTES_PER_BUFFER_ALIGNMENT_SIZE;
    const auto row_size = static_cast<uint32_t>(layer_info.hw_shape.width * layer_info.hw_shape.features *
        layer_info.hw_data_bytes);
    const auto core_frame_size = layer_info.hw_shape.height * row_size;

    // Currently takes the largest periph_bytes_per_buffer that is possible with shmifo size and desc page size
    // TODO HRT-10961 : calculate optimal periph size
    auto periph_bytes_per_buffer = HailoRTCommon::align_to(row_size, alignment);
    while ((0 < periph_bytes_per_buffer) && !is_logical_periph_bytes_per_buffer(periph_bytes_per_buffer, core_frame_size,
        is_ddr, layer_info.max_shmifo_size, desc_page_size, max_periph_bytes_value, layer_info.nn_stream_config.core_bytes_per_buffer)) {
        periph_bytes_per_buffer -= alignment;
    }

    CHECK_AS_EXPECTED(0 != periph_bytes_per_buffer, HAILO_INVALID_ARGUMENT, "Error, Could not find logical periph bytes per buffer value");

    uint32_t periph_buffers_per_frame = (core_frame_size / periph_bytes_per_buffer);
    // In ddr - the core make sure that row size is aligned to PERIPH_BYTES_PER_BUFFER_DDR_ALIGNMENT_SIZE but if a row
    // Is too large to fit in core bytes per buffer - they will divide it and put it in mutliple buffers - so in order to 
    // Get the exact size in periph buffers per frame - we must muttiply core registers and divide by periph bytes per buffer 
    if (is_ddr) {
        periph_buffers_per_frame = layer_info.nn_stream_config.core_bytes_per_buffer *
            layer_info.nn_stream_config.core_buffers_per_frame / periph_bytes_per_buffer;

        // if we get a periph bytes per buffer so small that the periph buffers per frame cant fit in uint16
        // put uint16_t max - seeing as this value doesnt really affect anything and we should not fail in that case.
        if (!IS_FIT_IN_UINT16(periph_buffers_per_frame)) {
            LOGGER__WARNING("periph buffers per frame in DDR too large - putting uint16_t max (This may affect HW infer estimator results");
            periph_buffers_per_frame = UINT16_MAX;
        }
    }
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(periph_buffers_per_frame), HAILO_INVALID_ARGUMENT);

    return std::make_tuple(static_cast<uint16_t>(periph_bytes_per_buffer), static_cast<uint16_t>(periph_buffers_per_frame));
}

static Expected<LayerInfo> update_layer_info(const LayerInfo &original_layer_info,
    const CONTROL_PROTOCOL__host_buffer_info_t &buffer_info,
    const CONTROL_PROTOCOL__hw_consts_t &hw_consts, const ProtoHEFHwArch &hw_arch, const bool should_optimize_credits,
    const bool is_periph_calculated_in_hailort)
{
    LayerInfo local_layer_info = original_layer_info;

    if (local_layer_info.max_shmifo_size == 0) {
        local_layer_info.max_shmifo_size = hw_consts.default_initial_credit_size;
    }

    local_layer_info.nn_stream_config.is_periph_calculated_in_hailort = is_periph_calculated_in_hailort;

    // If Hw padding supported dont update periph registers because they were updated in get_hw_padding
    // TODO HRT-11006 : currently check is_hw_padding_supported and the feature_padding_payload because in MIPI Input stream
    // Even if is_hw_padding_supported is true we will not use hw padding.
    auto max_periph_bytes_from_hef = HefConfigurator::max_periph_bytes_value(DeviceBase::hef_arch_to_device_arch(hw_arch));
    CHECK_EXPECTED(max_periph_bytes_from_hef);
    const auto max_periph_bytes = MIN(max_periph_bytes_from_hef.value(), local_layer_info.max_shmifo_size);

    const auto periph_requirements = calculate_periph_requirements(local_layer_info, buffer_info.desc_page_size,
        is_periph_calculated_in_hailort, max_periph_bytes);
    CHECK_EXPECTED(periph_requirements);

    // Calculate and update value of periph bytes per buffer and periph buffers per frame
    local_layer_info.nn_stream_config.periph_bytes_per_buffer = std::get<0>(periph_requirements.value());
    local_layer_info.nn_stream_config.periph_buffers_per_frame = std::get<1>(periph_requirements.value());

    auto updated_local_layer_info = calculate_credit_params(hw_consts, buffer_info.desc_page_size, should_optimize_credits,
        local_layer_info);
    CHECK_EXPECTED(updated_local_layer_info);

    return updated_local_layer_info;
}

static hailo_status fill_boundary_input_layer_impl(ContextResources &context_resources,
    ResourcesManager &resources_manager, const LayerInfo layer_info, const CONTROL_PROTOCOL__hw_consts_t &hw_consts,
    const ProtoHEFHwArch &hw_arch, bool should_optimize_credits)
{
    const auto transfer_size = LayerInfoUtils::get_layer_transfer_size(layer_info);

    auto vdma_channel = resources_manager.get_boundary_vdma_channel_by_stream_name(layer_info.name);
    CHECK_EXPECTED_AS_STATUS(vdma_channel);

    const auto buffer_info = vdma_channel.value()->get_boundary_buffer_info(transfer_size);
    const bool is_periph_calculated_in_hailort = resources_manager.get_supported_features().periph_calculation_in_hailort;
    auto local_layer_info = update_layer_info(layer_info, buffer_info, hw_consts, hw_arch, should_optimize_credits,
        is_periph_calculated_in_hailort);
    CHECK_EXPECTED_AS_STATUS(local_layer_info);

    const auto channel_id = vdma_channel.value()->get_channel_id();
    auto status = context_resources.add_edge_layer(local_layer_info.value(), channel_id, buffer_info,
        resources_manager.get_supported_features());
    CHECK_SUCCESS(status);

    LOGGER__DEBUG("Boundary input stream: {} h2d_channel: {}.", layer_info.stream_index, channel_id);
    return HAILO_SUCCESS;
}

static hailo_status fill_boundary_input_layer(ContextResources &context_resources,
    ResourcesManager &resources_manager, const LayerInfo layer_info, const CONTROL_PROTOCOL__hw_consts_t &hw_consts,
    const ProtoHEFHwArch &hw_arch, bool should_optimize_credits)
{
    if (layer_info.is_multi_planar) {
        for (auto &plane : layer_info.planes) {
            auto status = fill_boundary_input_layer_impl(context_resources, resources_manager, plane, hw_consts, hw_arch, should_optimize_credits);
            CHECK_SUCCESS(status);
        }
        return HAILO_SUCCESS;
    }

    return fill_boundary_input_layer_impl(context_resources, resources_manager, layer_info, hw_consts, hw_arch, should_optimize_credits);
}

static hailo_status fill_inter_context_input_layer(ContextResources &context_resources,
    ResourcesManager &resources_manager, const LayerInfo &layer_info, const CONTROL_PROTOCOL__hw_consts_t &hw_consts,
    const ProtoHEFHwArch &hw_arch, bool should_optimize_credits)
{
    const auto channel_id = resources_manager.get_available_channel_id(to_layer_identifier(layer_info),
        HailoRTDriver::DmaDirection::H2D, layer_info.dma_engine_index);
    CHECK_EXPECTED_AS_STATUS(channel_id);

    /* Get inter context buffer previously created */
    const auto &connected_context = layer_info.connected_context_info;
    auto intermediate_buffer_key = std::make_pair(connected_context.context_index, connected_context.stream_index);
    auto inter_context_buffer_exp = resources_manager.get_intermediate_buffer(intermediate_buffer_key);
    CHECK_EXPECTED_AS_STATUS(inter_context_buffer_exp, "Failed to find inter context buffer for src context {}, src_stream_index {}",
        connected_context.context_index, connected_context.stream_index);
    auto &inter_context_buffer = inter_context_buffer_exp->get();

    const bool is_periph_calculated_in_hailort = resources_manager.get_supported_features().periph_calculation_in_hailort;
    auto local_layer_info = update_layer_info(layer_info, inter_context_buffer.get_host_buffer_info(), hw_consts,
        hw_arch, should_optimize_credits, is_periph_calculated_in_hailort);
    CHECK_EXPECTED_AS_STATUS(local_layer_info);

    auto status = context_resources.add_edge_layer(local_layer_info.value(), channel_id.value(),
        inter_context_buffer.get_host_buffer_info(), resources_manager.get_supported_features());
    CHECK_SUCCESS(status);

    LOGGER__DEBUG("Intermediate input stream {}, src_context:{}, dst_context: {}, h2d_channel {}.",
        layer_info.stream_index, layer_info.context_index, layer_info.connected_context_info.context_index,
        channel_id.value());

    return HAILO_SUCCESS;
}

static hailo_status fill_boundary_output_layer(ContextResources &context_resources,
    ResourcesManager &resources_manager, const LayerInfo &layer_info, const CONTROL_PROTOCOL__hw_consts_t &hw_consts,
    const ProtoHEFHwArch &hw_arch, bool should_optimize_credits)
{
    const auto transfer_size = LayerInfoUtils::get_layer_transfer_size(layer_info);

    auto vdma_channel = resources_manager.get_boundary_vdma_channel_by_stream_name(layer_info.name);
    CHECK_EXPECTED_AS_STATUS(vdma_channel);

    const auto buffer_info = vdma_channel.value()->get_boundary_buffer_info(transfer_size);
    const bool is_periph_calculated_in_hailort = resources_manager.get_supported_features().periph_calculation_in_hailort;
    auto local_layer_info = update_layer_info(layer_info, buffer_info, hw_consts, hw_arch, should_optimize_credits,
        is_periph_calculated_in_hailort);
    CHECK_EXPECTED_AS_STATUS(local_layer_info);

    const auto channel_id = vdma_channel.value()->get_channel_id();
    auto status = context_resources.add_edge_layer(local_layer_info.value(), channel_id, buffer_info,
        resources_manager.get_supported_features());
    CHECK_SUCCESS(status);

    LOGGER__DEBUG("Boundary output stream: {} d2h_channel: {}.", layer_info.stream_index, channel_id);
    return HAILO_SUCCESS;
}

static hailo_status fill_inter_context_output_layer(ContextResources &context_resources,
    ResourcesManager &resources_manager, const LayerInfo &layer_info,
    const CONTROL_PROTOCOL__hw_consts_t &hw_consts, const ProtoHEFHwArch &hw_arch, bool should_optimize_credits)
{
    const auto channel_id = resources_manager.get_available_channel_id(to_layer_identifier(layer_info),
        HailoRTDriver::DmaDirection::D2H, layer_info.dma_engine_index);
    CHECK_EXPECTED_AS_STATUS(channel_id);

    const auto frame_credits_in_bytes = LayerInfoUtils::get_layer_transfer_size(layer_info);

    auto network_batch_size = resources_manager.get_network_batch_size(layer_info.network_name);
    CHECK_EXPECTED_AS_STATUS(network_batch_size);

    auto inter_context_buffer_exp = resources_manager.create_intermediate_buffer(frame_credits_in_bytes,
        network_batch_size.value(), layer_info.stream_index, layer_info.context_index, channel_id.value(),
        IntermediateBuffer::StreamingType::BURST);
    CHECK_EXPECTED_AS_STATUS(inter_context_buffer_exp);
    auto &inter_context_buffer = inter_context_buffer_exp->get();

    const bool is_periph_calculated_in_hailort = resources_manager.get_supported_features().periph_calculation_in_hailort;
    auto local_layer_info = update_layer_info(layer_info, inter_context_buffer.get_host_buffer_info(), hw_consts,
        hw_arch, should_optimize_credits, is_periph_calculated_in_hailort);
    CHECK_EXPECTED_AS_STATUS(local_layer_info);

    auto status = context_resources.add_edge_layer(local_layer_info.value(), channel_id.value(),
        inter_context_buffer.get_host_buffer_info(), resources_manager.get_supported_features());
    CHECK_SUCCESS(status);

    LOGGER__DEBUG("Inter-context output stream {}, src_context:{}, d2h_channel {}.",
        layer_info.stream_index, layer_info.context_index, channel_id.value());
    return HAILO_SUCCESS;
}

static hailo_status fill_ddr_output_layer(ContextResources &context_resources,
    ResourcesManager &resources_manager, const LayerInfo &layer_info,
    const CONTROL_PROTOCOL__hw_consts_t &hw_consts, const ProtoHEFHwArch &hw_arch)
{
    CHECK(resources_manager.get_supported_features().padded_ddr_buffers, HAILO_INVALID_HEF,
        "Failed opening non-compatible HEF that uses the following deprecated features: host-managed DDR buffers." 
        "Please re-compile the HEF using a newer Dataflow Compiler version (v3.11.0 or newer)");

    // It is assumed that output channels are parsed before input channels.
    // Allocate vdma channel index for both edges
    const auto h2d_stream_index = layer_info.connected_context_info.stream_index;
    const auto h2d_layer_identifier = std::make_tuple(LayerType::DDR, HAILO_H2D_STREAM, 
            layer_info.name, h2d_stream_index);
    const auto h2d_channel_id = resources_manager.get_available_channel_id(h2d_layer_identifier,
        HailoRTDriver::DmaDirection::H2D, layer_info.connected_context_info.dma_engine_index);
    CHECK_EXPECTED_AS_STATUS(h2d_channel_id);

    const auto d2h_stream_index = layer_info.stream_index;
    const auto d2h_layer_identifier = std::make_tuple(LayerType::DDR, HAILO_D2H_STREAM, 
            layer_info.name, d2h_stream_index);
    const auto d2h_channel_id = resources_manager.get_available_channel_id(d2h_layer_identifier,
        HailoRTDriver::DmaDirection::D2H, layer_info.dma_engine_index);
    CHECK_EXPECTED_AS_STATUS(d2h_channel_id);

    // In DDR - always use core bytes per buffer as row size
    const auto row_size = static_cast<uint16_t>(layer_info.nn_stream_config.core_bytes_per_buffer);
    CHECK(0 == (row_size % PERIPH_BYTES_PER_BUFFER_DDR_ALIGNMENT_SIZE), HAILO_INVALID_ARGUMENT,
        "DDR Row size ({}) must be aligned to {}", row_size, PERIPH_BYTES_PER_BUFFER_DDR_ALIGNMENT_SIZE);
    const auto min_buffered_rows = layer_info.ddr_info.min_buffered_rows;

    // Allocate the ddr buffer
    auto ddr_buffer = resources_manager.create_intermediate_buffer(row_size, min_buffered_rows,
        d2h_stream_index, layer_info.context_index, d2h_channel_id.value(),
        IntermediateBuffer::StreamingType::CIRCULAR_CONTINUOS);
    CHECK_EXPECTED_AS_STATUS(ddr_buffer);

    DdrChannelsInfo ddr_pair_info{};
    ddr_pair_info.h2d_stream_index = h2d_stream_index;
    ddr_pair_info.d2h_stream_index = d2h_stream_index;
    ddr_pair_info.network_index = layer_info.network_index;
    ddr_pair_info.h2d_channel_id = h2d_channel_id.value();
    ddr_pair_info.d2h_channel_id = d2h_channel_id.value();
    ddr_pair_info.row_size = row_size;
    ddr_pair_info.min_buffered_rows = min_buffered_rows;
    ddr_pair_info.total_buffers_per_frame = layer_info.ddr_info.total_buffers_per_frame;
    ddr_pair_info.host_buffer_info = ddr_buffer->get().get_host_buffer_info();
    context_resources.add_ddr_channels_info(ddr_pair_info);

    // On ddr layers, we assume the periph credit size is aligned to the size of descriptor, so we don't want to
    // optimize the credits.
    const bool should_optimize_credits = false;
    const bool is_periph_calculated_in_hailort = resources_manager.get_supported_features().periph_calculation_in_hailort;
    auto local_layer_info = update_layer_info(layer_info, ddr_buffer->get().get_host_buffer_info(), hw_consts,
        hw_arch, should_optimize_credits, is_periph_calculated_in_hailort);
    CHECK_EXPECTED_AS_STATUS(local_layer_info);

    auto status = context_resources.add_edge_layer(local_layer_info.value(), ddr_pair_info.d2h_channel_id,
        ddr_buffer->get().get_host_buffer_info(), resources_manager.get_supported_features());
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

static hailo_status fill_ddr_input_layer(ContextResources &context_resources, ResourcesManager &resources_manager,
    const LayerInfo &layer_info, const CONTROL_PROTOCOL__hw_consts_t &hw_consts, const ProtoHEFHwArch &hw_arch)
{
    auto connected_stream_index = layer_info.connected_context_info.stream_index;
    auto ddr_info = context_resources.get_ddr_channels_info(connected_stream_index);
    CHECK_EXPECTED_AS_STATUS(ddr_info, "Matching DDR layer as not found for context {} src stream {}",
        layer_info.context_index, connected_stream_index);
    LOGGER__DEBUG("DDR layer: input stream_index: {}, output stream_index: {}, h2d_channel {}, d2h_channel: {}.",
        ddr_info->h2d_stream_index, ddr_info->d2h_stream_index, ddr_info->h2d_channel_id, ddr_info->d2h_channel_id);

    CHECK(layer_info.stream_index == ddr_info->h2d_stream_index, HAILO_INVALID_HEF, "DDR channel pair mismatch in h2d channel");
    CHECK(layer_info.connected_context_info.stream_index == ddr_info->d2h_stream_index, HAILO_INVALID_HEF, "DDR channel pair mismatch in d2h channel");
    CHECK(layer_info.network_index == ddr_info->network_index, HAILO_INVALID_HEF, "DDR channel pair mismatch network_index");

    // On ddr layers, we assume the periph credit size is aligned to the size of descriptor, so we don't want to
    // optimize the credits.
    const bool should_optimize_credits = false;
    const bool is_periph_calculated_in_hailort = resources_manager.get_supported_features().periph_calculation_in_hailort;
    auto local_layer_info = update_layer_info(layer_info, ddr_info->host_buffer_info, hw_consts,
        hw_arch, should_optimize_credits, is_periph_calculated_in_hailort);
    CHECK_EXPECTED_AS_STATUS(local_layer_info);

    auto status = context_resources.add_edge_layer(local_layer_info.value(), ddr_info->h2d_channel_id,
        ddr_info->host_buffer_info, resources_manager.get_supported_features());
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

static hailo_status add_ddr_buffers_info(std::vector<ContextSwitchConfigActionPtr> &configuration_actions,
    const ContextResources &context_resources)
{
    bool start_fw_ddr_buffer_task = false;
    for (const auto &ddr_info : context_resources.get_ddr_channels_infos()) {
        if (ddr_info.need_manual_credit_management()) {
            auto ddr_pair_action = DdrPairInfoAction::create(ddr_info.h2d_channel_id, ddr_info.d2h_channel_id,
                ddr_info.network_index, ddr_info.descriptors_per_frame(), ddr_info.descs_count());
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
    ResourcesManager &resources_manager, const ProtoHEFHwArch &hw_arch)
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
        status = fill_ddr_output_layer(context_resources, resources_manager, output_layer_info, *hw_consts, hw_arch);
        CHECK_SUCCESS(status);
    }

    for (const auto &output_layer_info : context_metadata.get_boundary_output_layers()) {
        status = fill_boundary_output_layer(context_resources, resources_manager, output_layer_info,
            *hw_consts, hw_arch, should_optimize_credits);
        CHECK_SUCCESS(status);
    }

    for (const auto &output_layer_info : context_metadata.get_inter_context_output_layers()) {
        status = fill_inter_context_output_layer(context_resources, resources_manager, output_layer_info,
            *hw_consts, hw_arch, should_optimize_credits);
        CHECK_SUCCESS(status);
    }

    for (const auto &input_layer_info : context_metadata.get_ddr_input_layers()) {
        status = fill_ddr_input_layer(context_resources, resources_manager, input_layer_info, *hw_consts, hw_arch);
        CHECK_SUCCESS(status);
    }

    for (const auto &input_layer_info : context_metadata.get_boundary_input_layers()) {
        status = fill_boundary_input_layer(context_resources, resources_manager, input_layer_info,
            *hw_consts, hw_arch, should_optimize_credits);
        CHECK_SUCCESS(status);
    }

    for (const auto &input_layer_info : context_metadata.get_inter_context_input_layers()) {
        status = fill_inter_context_input_layer(context_resources, resources_manager, input_layer_info,
            *hw_consts, hw_arch, should_optimize_credits);
        CHECK_SUCCESS(status);
    }

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
        const auto h2d_layer_identifier = std::make_tuple(LayerType::DDR, HAILO_H2D_STREAM, 
                output_layer_info.name, output_layer_info.connected_context_info.stream_index);
        status = resources_manager.free_channel_index(h2d_layer_identifier);
        CHECK_SUCCESS(status);

        const auto d2h_layer_identifier = std::make_tuple(LayerType::DDR, HAILO_D2H_STREAM, 
                output_layer_info.name, output_layer_info.stream_index);
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

static hailo_status push_fetch_config_actions(
    ConfigBuffer &config_resources, uint8_t config_stream_index,
    uint16_t total_ccw_bursts, bool support_pre_fetch,
    std::vector<ContextSwitchConfigActionPtr> &processed_configuration_actions)
{
    if (support_pre_fetch) {
        auto action = AddCcwBurstAction::create(config_stream_index, total_ccw_bursts);
        CHECK_EXPECTED_AS_STATUS(action);
        processed_configuration_actions.emplace_back(action.release());
    } else {
        const auto desc_count = config_resources.program_descriptors();
        CHECK_EXPECTED_AS_STATUS(desc_count);

        auto action = FetchCfgChannelDescriptorsAction::create(config_resources.channel_id(), desc_count.value());
        CHECK_EXPECTED_AS_STATUS(action);
        processed_configuration_actions.emplace_back(action.release());
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
    std::vector<ConfigBuffer> &config_resources,
    const bool support_pre_fetch,
    std::vector<ContextSwitchConfigActionPtr> &processed_configuration_actions)
{
    assert(ContextSwitchConfigAction::Type::WriteDataCcw == configuration_action->get_type());
    const auto &write_ccw_action = *static_cast<const WriteDataCcwAction*>(configuration_action.get());

    const auto config_stream_index = write_ccw_action.config_stream_index();
    assert(config_stream_index < config_resources.size());
    auto status = write_ccw_to_buffer(config_resources[config_stream_index], write_ccw_action, support_pre_fetch);
    CHECK_SUCCESS(status);

    status = push_fetch_config_actions(config_resources[config_stream_index], config_stream_index,
        write_ccw_action.total_ccw_burst(), support_pre_fetch, processed_configuration_actions);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

// TODO HRT-10073: change to supported features list
static bool is_hailo1x_device_type(const hailo_device_architecture_t dev_arch)
{
    // Compare with HAILO1X device archs
    return (HAILO_ARCH_HAILO15H == dev_arch) || (HAILO_ARCH_PLUTO == dev_arch);
}

static Expected<uint8_t> find_dummy_stream(const LayerInfo &layer_info, const ContextResources &context_resources,
    const bool is_null_shmifo_supported)
{
    if (is_null_shmifo_supported) {
        static const uint8_t DUMMY_STREAM_INDEX = 31;
        return Expected<uint8_t>(DUMMY_STREAM_INDEX);
    } else {
        const auto other_direction = (HAILO_H2D_STREAM == layer_info.direction) ? HAILO_D2H_STREAM : HAILO_H2D_STREAM;
        const auto other_direction_edge_layers = context_resources.get_edge_layers(other_direction);
        CHECK_AS_EXPECTED(!other_direction_edge_layers.empty(), HAILO_INTERNAL_FAILURE, "Couldn't find dummy stream");
        return Expected<uint8_t>(other_direction_edge_layers.front().layer_info.stream_index);
    }
}

static hailo_status add_change_vdma_to_stream_mapping_impl(const ProtoHEFHwArch &hw_arch,
    const LayerInfo &layer_info, const ResourcesManager &resources_manager,
    ContextResources &context_resources, uint8_t context_index,
    std::vector<ContextSwitchConfigActionPtr> &processed_configuration_actions)
{
    auto vdma_channel = resources_manager.get_boundary_vdma_channel_by_stream_name(layer_info.name);
    CHECK_EXPECTED_AS_STATUS(vdma_channel);

    const auto channel_id = vdma_channel.value()->get_channel_id();
    const bool is_dummy_stream = layer_info.context_index != context_index;
    uint8_t stream_index = layer_info.stream_index;
    if (is_dummy_stream) {
        auto dummy_stream_index = find_dummy_stream(layer_info, context_resources,
            is_hailo1x_device_type(DeviceBase::hef_arch_to_device_arch(hw_arch)));
        CHECK_EXPECTED_AS_STATUS(dummy_stream_index);
        stream_index = *dummy_stream_index;
    }

    auto action = ChangeVdmaToStreamMapping::create(channel_id, stream_index, is_dummy_stream);
    CHECK_EXPECTED_AS_STATUS(action);
    processed_configuration_actions.emplace_back(action.release());

    return HAILO_SUCCESS;
}

static hailo_status add_change_vdma_to_stream_mapping(const ProtoHEFHwArch &hw_arch,
    const CoreOpMetadata &core_op_metadata, const ResourcesManager &resources_manager,
    ContextResources &context_resources, uint8_t context_index,
    std::vector<ContextSwitchConfigActionPtr> &processed_configuration_actions)
{
    for (const auto &layer_info : core_op_metadata.get_all_layer_infos()) {
        if (layer_info.is_multi_planar) {
            for (const auto &plane : layer_info.planes) {
                auto status = add_change_vdma_to_stream_mapping_impl(hw_arch, plane, resources_manager,
                    context_resources, context_index, processed_configuration_actions);
                CHECK_SUCCESS(status);
            }
        } else {
                auto status = add_change_vdma_to_stream_mapping_impl(hw_arch, layer_info, resources_manager,
                    context_resources, context_index, processed_configuration_actions);
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

    for (const auto &edge_layer : context_resources.get_edge_layers(LayerType::DDR, HAILO_D2H_STREAM)) {
        auto activate_action = ActivateDdrOutputChannelAction::create(edge_layer.channel_id,
            edge_layer.layer_info.stream_index, edge_layer.layer_info.nn_stream_config, edge_layer.buffer_info,
            edge_layer.layer_info.ddr_info.min_buffered_rows);
        CHECK_EXPECTED_AS_STATUS(activate_action);
        actions.emplace_back(activate_action.release());
    }

    if (!push_internal_only) {
        for (const auto &edge_layer : context_resources.get_edge_layers(LayerType::BOUNDARY, HAILO_D2H_STREAM)) {
            auto activate_action = ActivateBoundaryOutputChannelAction::create(edge_layer.channel_id,
                edge_layer.layer_info.stream_index, edge_layer.layer_info.network_index,
                edge_layer.layer_info.nn_stream_config, edge_layer.buffer_info);
            CHECK_EXPECTED_AS_STATUS(activate_action);
            actions.emplace_back(activate_action.release());
        }
    }

    for (const auto &edge_layer : context_resources.get_edge_layers(LayerType::INTER_CONTEXT, HAILO_D2H_STREAM)) {
        auto activate_action = ActivateInterContextOutputChannelAction::create(edge_layer.channel_id,
            edge_layer.layer_info.stream_index, edge_layer.layer_info.network_index,
            edge_layer.layer_info.nn_stream_config, edge_layer.buffer_info);
        CHECK_EXPECTED_AS_STATUS(activate_action);
        actions.emplace_back(activate_action.release());
    }

    for (const auto &edge_layer : context_resources.get_edge_layers(LayerType::DDR, HAILO_H2D_STREAM)) {
        const auto d2h_stream_index = edge_layer.layer_info.connected_context_info.stream_index;
        auto ddr_channels_info = context_resources.get_ddr_channels_info(d2h_stream_index);
        CHECK_EXPECTED_AS_STATUS(ddr_channels_info);
        const auto d2h_channel_id = ddr_channels_info->d2h_channel_id;

        auto activate_action = ActivateDdrInputChannelAction::create(edge_layer.channel_id,
            edge_layer.layer_info.stream_index, edge_layer.layer_info.nn_stream_config, edge_layer.buffer_info,
            edge_layer.layer_info.max_shmifo_size, d2h_channel_id);
        CHECK_EXPECTED_AS_STATUS(activate_action);
        actions.emplace_back(activate_action.release());
    }

    if (!push_internal_only) {
        for (const auto &edge_layer : context_resources.get_edge_layers(LayerType::BOUNDARY, HAILO_H2D_STREAM)) {
            auto activate_action = ActivateBoundaryInputChannelAction::create(edge_layer.channel_id,
                edge_layer.layer_info.stream_index, edge_layer.layer_info.nn_stream_config, edge_layer.buffer_info,
                edge_layer.layer_info.max_shmifo_size);
            CHECK_EXPECTED_AS_STATUS(activate_action);
            actions.emplace_back(activate_action.release());
        }
    }

    for (const auto &edge_layer : context_resources.get_edge_layers(LayerType::INTER_CONTEXT, HAILO_H2D_STREAM)) {
        auto activate_action = ActivateInterContextInputChannelAction::create(edge_layer.channel_id,
            edge_layer.layer_info.stream_index, edge_layer.layer_info.nn_stream_config, edge_layer.buffer_info,
            edge_layer.layer_info.max_shmifo_size);
        CHECK_EXPECTED_AS_STATUS(activate_action);
        actions.emplace_back(activate_action.release());
    }

    return HAILO_SUCCESS;
}

static hailo_status proccess_trigger_new_data_input_action(const ProtoHEFHwArch &hw_arch,
    const ContextSwitchConfigActionPtr &configuration_action,
    uint32_t trigger_new_data_from_input_group_start,
    uint32_t trigger_new_data_from_input_group_end,
    const uint32_t &action_index,
    const CoreOpMetadata &core_op_metadata,
    const ResourcesManager &resources_manager,
    ContextResources &context_resources,
    uint8_t context_index,
    std::vector<ContextSwitchConfigActionPtr> &processed_configuration_actions, bool is_single_context)
{
    const bool PUSH_ALL_EDGE_LAYERS = false;
    if (trigger_new_data_from_input_group_start == action_index) {
        auto status = push_edge_layer_activation_actions(context_resources, processed_configuration_actions,
            PUSH_ALL_EDGE_LAYERS);
        CHECK_SUCCESS(status);

        if (!is_single_context) {
            status = add_change_vdma_to_stream_mapping(hw_arch, core_op_metadata, resources_manager,
                context_resources, context_index, processed_configuration_actions);
            CHECK_SUCCESS(status);
        }

        // DDR buffer info actions need to happen after the edge layer activation actions.
        status = add_ddr_buffers_info(processed_configuration_actions, context_resources);
        CHECK_SUCCESS(status);

        /* Open the boundary input channel */
        for (const auto &edge_layer : context_resources.get_edge_layers(LayerType::BOUNDARY, HAILO_H2D_STREAM)) {
            auto activate_action = ResumeVdmaChannel::create(edge_layer);
            CHECK_EXPECTED_AS_STATUS(activate_action);
            processed_configuration_actions.emplace_back(activate_action.release());
        }
    }

    // Add the current action
    processed_configuration_actions.emplace_back(configuration_action);

    // At the end of a consecutive group of TriggerNewDataFromDataInput actions, we can trigger the BurstCreditsTask
    // in the FW, via StartBurstCreditsTaskAction.
    if (trigger_new_data_from_input_group_end == action_index) {
        auto boundary_input_edge_layers = context_resources.get_edge_layers(LayerType::BOUNDARY, HAILO_H2D_STREAM);
        if (boundary_input_edge_layers.size() > 0) {
            auto start_burst_credits_task_action = StartBurstCreditsTaskAction::create();
            CHECK_EXPECTED_AS_STATUS(start_burst_credits_task_action);
            processed_configuration_actions.emplace_back(start_burst_credits_task_action.release());
        }
    }

    return HAILO_SUCCESS;
}

// At the end of each consecutive group of WriteDataCcwAction, a FetchCfgChannelDescriptorsAction is added.
static hailo_status add_fetch_config_actions(std::vector<ContextSwitchConfigActionPtr> &configuration_actions,
    std::vector<ConfigBuffer> &config_resources, bool support_pre_fetch)
{

    std::vector<ContextSwitchConfigActionPtr> processed_configuration_actions;
    for (uint32_t action_index = 0; action_index < configuration_actions.size(); action_index++) {
        const auto &configuration_action = configuration_actions[action_index];
        if (ContextSwitchConfigAction::Type::WriteDataCcw == configuration_action->get_type()) {
            auto status = proccess_write_ccw_action(configuration_action, config_resources,
                support_pre_fetch, processed_configuration_actions);
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
static hailo_status handle_edge_layer_activation_actions(const ProtoHEFHwArch &hw_arch,
    std::vector<ContextSwitchConfigActionPtr> &configuration_actions, const CoreOpMetadata &core_op_metadata,
    const ResourcesManager &resources_manager, ContextResources &context_resources, uint8_t context_index,
    bool is_single_context)
{
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
            auto status = proccess_trigger_new_data_input_action(hw_arch, configuration_action,
                trigger_new_data_from_input_group_start, trigger_new_data_from_input_group_end, action_index,
                core_op_metadata, resources_manager, context_resources, context_index, processed_configuration_actions, is_single_context);
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
    std::vector<ContextSwitchConfigActionPtr> &actions, const bool is_batch_switch_context)
{
    for (const auto &edge_layer : context_resources.get_edge_layers()) {
        const bool should_validate = (edge_layer.layer_info.type == LayerType::BOUNDARY);
        auto action = should_validate ?
            ValidateChannelAction::create(edge_layer, is_batch_switch_context) :
            DeactivateChannelAction::create(edge_layer, is_batch_switch_context);
        CHECK_EXPECTED_AS_STATUS(action);
        actions.emplace_back(action.release());
    }

    /* Pause the boundary input channel */
    for (const auto &edge_layer : context_resources.get_edge_layers(LayerType::BOUNDARY, HAILO_H2D_STREAM)) {
        auto activate_action = PauseVdmaChannel::create(edge_layer);
        CHECK_EXPECTED_AS_STATUS(activate_action);
        actions.emplace_back(activate_action.release());
    }

    return HAILO_SUCCESS;
}

static hailo_status fill_context_recipes_for_multi_context(const ProtoHEFHwArch &hw_arch,
    ContextResources &context_resources, ResourcesManager &resources_manager,
    uint8_t context_index, const CoreOpMetadata &core_op_metadata, const ContextMetadata &context_metadata,
    bool is_single_context)
{
    hailo_status status = HAILO_UNINITIALIZED;

    // Add edge layers mapping
    status = parse_and_fill_edge_layers_mapping(context_resources, context_metadata, resources_manager, hw_arch);
    CHECK_SUCCESS(status);

    // Parse context
    std::vector<ContextSwitchConfigActionPtr> actions = context_metadata.get_actions();

    const auto support_pre_fetch = is_hailo1x_device_type(DeviceBase::hef_arch_to_device_arch(hw_arch));
    status = add_fetch_config_actions(actions, context_resources.get_config_buffers(), support_pre_fetch);
    CHECK_SUCCESS(status);

    status = handle_edge_layer_activation_actions(hw_arch, actions, core_op_metadata, resources_manager,
        context_resources, context_index, is_single_context);
    CHECK_SUCCESS(status);

    status = add_config_channel_activation_actions(actions, context_resources.get_config_buffers());
    CHECK_SUCCESS(status);

    if (is_single_context) {
        // Single context network must wait for network group change event after they finish the dynamic context.
        auto wait_action = WaitForNetworkGroupChangeAction::create();
        CHECK_EXPECTED_AS_STATUS(wait_action);
        actions.emplace_back(wait_action.release());
    }
    else {
        const bool NOT_BATCH_SWITCH_CONTEXT = false;
        status = add_edge_layer_end_of_context_actions(context_resources, actions, NOT_BATCH_SWITCH_CONTEXT);
    }

    status = handle_repeated_actions(actions);
    CHECK_SUCCESS(status);

    return write_action_list(context_resources, context_resources.builder(), actions);
}

static hailo_status create_boundary_channels(ResourcesManager &resources_manager,
    CoreOpMetadata &core_op_metadata)
{
    for (const auto &layer_info : core_op_metadata.get_all_layer_infos()) {
        if (layer_info.is_multi_planar) {
            for (const auto &plane : layer_info.planes) {
                auto status = resources_manager.create_boundary_vdma_channel(plane);
                CHECK_SUCCESS(status);
            }
        } else {
            auto status = resources_manager.create_boundary_vdma_channel(layer_info);
            CHECK_SUCCESS(status);
        }
    }
    return HAILO_SUCCESS;
}

static hailo_status fill_activation_config_recepies_for_multi_context(
    ContextResources &context_resources, ResourcesManager &resources_manager,
    std::shared_ptr<CoreOpMetadata> core_op_metadata, const ProtoHEFHwArch &hw_arch)
{
    auto hw_consts = Control::get_hw_consts(resources_manager.get_device());
    CHECK_EXPECTED_AS_STATUS(hw_consts);
    const bool should_optimize_credits = hw_consts->should_optimize_credits &&
        (HAILO_POWER_MODE_PERFORMANCE == resources_manager.get_power_mode());

    for (const auto &layer_info : core_op_metadata->get_output_layer_infos()){
        auto status = fill_boundary_output_layer(context_resources, resources_manager, layer_info, *hw_consts,
            hw_arch, should_optimize_credits);
        CHECK_SUCCESS(status);
    }

    for (const auto &layer_info : core_op_metadata->get_input_layer_infos()) {
        auto status = fill_boundary_input_layer(context_resources, resources_manager, layer_info, *hw_consts,
            hw_arch, should_optimize_credits);
        CHECK_SUCCESS(status);
    }

    std::vector<ContextSwitchConfigActionPtr> actions;
    auto reset_burst_task_action = ResetBurstCreditsTaskAction::create();
    CHECK_EXPECTED_AS_STATUS(reset_burst_task_action);
    actions.emplace_back(reset_burst_task_action.release());

    for (const auto &edge_layer : context_resources.get_edge_layers(LayerType::BOUNDARY)) {
        auto action = edge_layer.layer_info.direction == HAILO_H2D_STREAM ?
            OpenBoundaryInputChannelAction::create(edge_layer.channel_id, edge_layer.buffer_info) :
            OpenBoundaryOutputChannelAction::create(edge_layer.channel_id, edge_layer.buffer_info);
        CHECK_EXPECTED_AS_STATUS(action);
        actions.emplace_back(action.release());
    }

    return write_action_list(context_resources, context_resources.builder(), actions);
}

static Expected<ContextSwitchConfigActionPtr> create_switch_lcu_batch_action(const ContextSwitchConfigActionPtr action,
    ContextResources &context_resources)
{
    uint8_t cluster_index = 0;
    uint8_t lcu_index = 0;
    uint8_t network_index = 0;
    uint32_t kernel_done_count = 0;

    CHECK_AS_EXPECTED((ContextSwitchConfigAction::Type::EnableLcuDefault == action->get_type()) ||
        (ContextSwitchConfigAction::Type::EnableLcuNonDefault == action->get_type()), HAILO_INVALID_ARGUMENT,
        "Invalid action type - must be enable lcu (default or non default), Received type {}", action->get_type());

    const auto params_buffer = action->serialize_params(context_resources);
    CHECK_EXPECTED(params_buffer);

    if (ContextSwitchConfigAction::Type::EnableLcuDefault == action->get_type()) {
        const auto params = reinterpret_cast<const CONTEXT_SWITCH_DEFS__enable_lcu_action_default_data_t*>(params_buffer.value().data());
        cluster_index = CONTEXT_SWITCH_DEFS__PACKED_LCU_ID_CLUSTER_INDEX_READ(params->packed_lcu_id);
        lcu_index = CONTEXT_SWITCH_DEFS__PACKED_LCU_ID_LCU_INDEX_READ(params->packed_lcu_id);
        network_index = params->network_index;
        kernel_done_count = CONTEXT_SWITCH_DEFS__ENABLE_LCU_DEFAULT_KERNEL_COUNT;
    } else {
        const auto params = reinterpret_cast<const CONTEXT_SWITCH_DEFS__enable_lcu_action_non_default_data_t*>(params_buffer.value().data());
        cluster_index = CONTEXT_SWITCH_DEFS__PACKED_LCU_ID_CLUSTER_INDEX_READ(params->packed_lcu_id);
        lcu_index = CONTEXT_SWITCH_DEFS__PACKED_LCU_ID_LCU_INDEX_READ(params->packed_lcu_id);
        network_index = params->network_index;
        kernel_done_count = params->kernel_done_count;
    }

    return SwitchLcuBatchAction::create(cluster_index, lcu_index, network_index, kernel_done_count);
}

static hailo_status fill_batch_switching_context_edge_layers(ContextResources &context_resources, const CoreOpMetadata &core_op_metadata, ResourcesManager &resources_manager,
    const ProtoHEFHwArch &hw_arch)
{
    auto hw_consts = Control::get_hw_consts(resources_manager.get_device());
    CHECK_EXPECTED_AS_STATUS(hw_consts);
    const bool should_optimize_credits = hw_consts->should_optimize_credits &&
        (HAILO_POWER_MODE_PERFORMANCE == resources_manager.get_power_mode());

    for (const auto &output_layer_info : core_op_metadata.dynamic_contexts()[0].get_ddr_output_layers()) {
        auto status = fill_ddr_output_layer(context_resources, resources_manager, output_layer_info, *hw_consts, hw_arch);
        CHECK_SUCCESS(status);
    }

    for (const auto &output_layer_info : core_op_metadata.dynamic_contexts()[0].get_boundary_output_layers()) {
        auto status = fill_boundary_output_layer(context_resources, resources_manager, output_layer_info,
            *hw_consts, hw_arch, should_optimize_credits);
        CHECK_SUCCESS(status);
    }

    for (const auto &output_layer_info : core_op_metadata.dynamic_contexts()[0].get_inter_context_output_layers()) {
        auto status = fill_inter_context_output_layer(context_resources, resources_manager, output_layer_info,
            *hw_consts, hw_arch, should_optimize_credits);
        CHECK_SUCCESS(status);
    }

    for (const auto &input_layer_info : core_op_metadata.dynamic_contexts()[0].get_ddr_input_layers()) {
        auto status = fill_ddr_input_layer(context_resources, resources_manager, input_layer_info, *hw_consts, hw_arch);
        CHECK_SUCCESS(status);
    }

    for (const auto &input_layer_info : core_op_metadata.dynamic_contexts()[0].get_boundary_input_layers()) {
        auto status = fill_boundary_input_layer(context_resources, resources_manager, input_layer_info,
            *hw_consts, hw_arch, should_optimize_credits);
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
        ContextSwitchConfigAction::Type::EnableLcuNonDefault
    };

    const auto lcu_batch_switch_actions = core_op_metadata.preliminary_context().get_actions_of_type(ENABLE_LCU_ACTIONS);
    for (const auto &action : lcu_batch_switch_actions) {
        auto switch_lcu_batch_action = create_switch_lcu_batch_action(action, context_resources);
        CHECK_EXPECTED_AS_STATUS(switch_lcu_batch_action);
        actions.insert(actions.end(), switch_lcu_batch_action.release());
    }

    return HAILO_SUCCESS;
}

static hailo_status create_change_boundary_input_batch_actions(const ContextResources &context_resources,
    std::vector<ContextSwitchConfigActionPtr> &batch_switch_context_actions)
{
    for (const auto &edge_layer : context_resources.get_edge_layers(LayerType::BOUNDARY, HAILO_H2D_STREAM)) {
        auto change_boundary_input_batch_action = ChangeBoundaryInputBatchAction::create(edge_layer.channel_id);
        CHECK_EXPECTED_AS_STATUS(change_boundary_input_batch_action);
        batch_switch_context_actions.emplace_back(change_boundary_input_batch_action.release());
    }

    auto start_burst_credits_task_action = StartBurstCreditsTaskAction::create();
    CHECK_EXPECTED_AS_STATUS(start_burst_credits_task_action);
    batch_switch_context_actions.emplace_back(start_burst_credits_task_action.release());


    return HAILO_SUCCESS;
}

static hailo_status add_edge_layers_actions_to_batch_switch_context(ContextResources &context_resources, const CoreOpMetadata &core_op_metadata,
    ResourcesManager &resources_manager, const ProtoHEFHwArch &hw_arch, std::vector<ContextSwitchConfigActionPtr> &actions)
{
    auto status = fill_batch_switching_context_edge_layers(context_resources, core_op_metadata, resources_manager, hw_arch);
    CHECK_SUCCESS(status);

    // Close all internal channels
    const auto BATCH_SWITCHING_CONTEXT = true;
    for (const auto &edge_layer : context_resources.get_edge_layers()) {
        if (edge_layer.layer_info.type != LayerType::BOUNDARY) {
            auto action = DeactivateChannelAction::create(edge_layer, BATCH_SWITCHING_CONTEXT);
            CHECK_EXPECTED_AS_STATUS(action);
            actions.emplace_back(action.release());
        }
    }

    // We need to reset the ddr buffering task when we change the batch_size (since it depends on the batch_size param)
    auto reset_ddr_action = ResetDdrBufferingTaskAction::create();
    CHECK_EXPECTED_AS_STATUS(reset_ddr_action);
    actions.emplace_back(reset_ddr_action.release());

    // Now re-open all the internal channels
    const bool PUSH_INTERNAL_EDGE_LAYERS = true;
    status = push_edge_layer_activation_actions(context_resources, actions, PUSH_INTERNAL_EDGE_LAYERS);
    CHECK_SUCCESS(status);

    status = add_ddr_buffers_info(actions, context_resources);
    CHECK_SUCCESS(status);

    status = create_change_boundary_input_batch_actions(context_resources, actions);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

static hailo_status fill_batch_switching_context_config_recepies_for_multi_context(
    ContextResources &context_resources, const CoreOpMetadata &core_op_metadata, ResourcesManager &resources_manager,
    const ProtoHEFHwArch &hw_arch)
{
    std::vector<ContextSwitchConfigActionPtr> actions;

    auto status = add_lcu_actions_to_batch_switch_context(context_resources, core_op_metadata, actions);
    CHECK_SUCCESS(status);

    status = add_edge_layers_actions_to_batch_switch_context(context_resources, core_op_metadata, resources_manager, hw_arch, actions);
    CHECK_SUCCESS(status);

    status = handle_repeated_actions(actions);
    CHECK_SUCCESS(status);

    return write_action_list(context_resources, context_resources.builder(), actions);
}

static hailo_status fill_preliminary_config_recepies_for_multi_context(const ProtoHEFHwArch &hw_arch,
    ContextResources &context_resources, ResourcesManager &resources_manager,
    std::shared_ptr<CoreOpMetadata> core_op_metadata, const ContextMetadata &preliminary_context,
    bool is_single_context)
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

    const auto support_pre_fetch = is_hailo1x_device_type(DeviceBase::hef_arch_to_device_arch(hw_arch));
    auto status = add_fetch_config_actions(actions, context_resources.get_config_buffers(), support_pre_fetch);
    CHECK_SUCCESS(status);

    if (resources_manager.get_supported_features().preliminary_run_asap) {
        status = handle_edge_layer_activation_actions(hw_arch, actions, *core_op_metadata, resources_manager,
            context_resources, PRELIMINARY_CONTEXT_INDEX, is_single_context);
        CHECK_SUCCESS(status);
    }

    status = add_config_channel_activation_actions(actions, context_resources.get_config_buffers());
    CHECK_SUCCESS(status);

    status = handle_repeated_actions(actions);
    CHECK_SUCCESS(status);

    return write_action_list(context_resources, context_resources.builder(), actions);
}



Expected<std::shared_ptr<ResourcesManager>> ResourcesManagerBuilder::build(uint8_t current_core_op_index, VdmaDevice &device,
    HailoRTDriver &driver, const ConfigureNetworkParams &config_params,
    std::shared_ptr<CoreOpMetadata> core_op_metadata, const ProtoHEFHwArch &hw_arch)
{
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

    auto resources_manager = ResourcesManager::create(device, driver, config_params, core_op_metadata,
        current_core_op_index);
    CHECK_EXPECTED(resources_manager);

    // TODO: Use a new flag in config_params.stream_params_by_name to mark channels as async channels.
    //       will also used to mark streams as async in ConfiguredNetworkGroupBase::create_in/output_stream_from_config_params
    //       (HRT-9104)
    auto status = create_boundary_channels(resources_manager.value(), *core_op_metadata);
    CHECK_SUCCESS_AS_EXPECTED(status);

    auto activation_context = resources_manager->add_new_context(CONTROL_PROTOCOL__CONTEXT_SWITCH_CONTEXT_TYPE_ACTIVATION);
    CHECK_EXPECTED(activation_context);
    status = fill_activation_config_recepies_for_multi_context(activation_context.value().get(),
        resources_manager.value(), core_op_metadata, hw_arch);
    CHECK_SUCCESS_AS_EXPECTED(status);

    auto batch_switching_context = resources_manager->add_new_context(CONTROL_PROTOCOL__CONTEXT_SWITCH_CONTEXT_TYPE_BATCH_SWITCHING);
    CHECK_EXPECTED(batch_switching_context);
    status = fill_batch_switching_context_config_recepies_for_multi_context(batch_switching_context.value().get(),
        *core_op_metadata, resources_manager.value(), hw_arch);
    CHECK_SUCCESS_AS_EXPECTED(status);

    const bool is_single_context = core_op_metadata->dynamic_contexts().size() == 1;

    auto preliminary_context = resources_manager->add_new_context(CONTROL_PROTOCOL__CONTEXT_SWITCH_CONTEXT_TYPE_PRELIMINARY,
        core_op_metadata->preliminary_context().config_buffers_info());
    CHECK_EXPECTED(preliminary_context);
    status = fill_preliminary_config_recepies_for_multi_context(hw_arch, preliminary_context.value().get(),
        resources_manager.value(), core_op_metadata, core_op_metadata->preliminary_context(), is_single_context);
    CHECK_SUCCESS_AS_EXPECTED(status);

    uint8_t context_index = 0;
    for (const auto &context_metadata : core_op_metadata->dynamic_contexts()) {
        auto new_context = resources_manager->add_new_context(CONTROL_PROTOCOL__CONTEXT_SWITCH_CONTEXT_TYPE_DYNAMIC,
            context_metadata.config_buffers_info());
        CHECK_EXPECTED(new_context);

        status = fill_context_recipes_for_multi_context(hw_arch, new_context.value().get(), resources_manager.value(),
            context_index, *core_op_metadata,
            context_metadata, is_single_context);
        CHECK_SUCCESS_AS_EXPECTED(status);

        context_index++;
    }

    status = resources_manager->configure();
    CHECK_SUCCESS_AS_EXPECTED(status);

    auto resources_manager_ptr = make_shared_nothrow<ResourcesManager>(resources_manager.release());
    CHECK_NOT_NULL_AS_EXPECTED(resources_manager_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return resources_manager_ptr;
}

} /* namespace hailort */
