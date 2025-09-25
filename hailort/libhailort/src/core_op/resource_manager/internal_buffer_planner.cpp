/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file internal_buffer_planner.cpp
 * @brief Planner for all the internal buffers of the CoreOp
 *
 * The planner hold the algorithms to plan connection between buffers and edge layer
 *
  **/

#include "vdma/memory/buffer_requirements.hpp"
#include "vdma/memory/descriptor_list.hpp"
#include "internal_buffer_planner.hpp"
#include "common/internal_env_vars.hpp"

#include <numeric>

constexpr size_t NAIVE_PLANNING_EDGE_LAYER_OFFSET = 0;

// Macro that check status. If status is HAILO_CANT_MEET_BUFFER_REQUIREMENTS, return without printing error to the prompt.
#define CHECK_STATUS_CANT_MEET_REQUIREMENTS(status) if (HAILO_CANT_MEET_BUFFER_REQUIREMENTS == status) {return make_unexpected(status);} CHECK_SUCCESS(status);

namespace hailort
{

bool InternalBufferPlanner::should_edge_layer_use_ccb(const LayerType &layer_type, HailoRTDriver::DmaType dma_type,
    bool force_sg_buffer_type)
{
    if (HailoRTDriver::DmaType::PCIE == dma_type) {
        // CCB not supported on PCIe
        return false;
    }

    if (force_sg_buffer_type) {
        return false;
    }

    switch (layer_type) {
    case LayerType::INTER_CONTEXT:
        // On burst (aka inter-context), because the buffers are big (And depends on the max_batch_size), we currently
        // don't want to use CCB by default.
        if (is_env_variable_on(HAILO_FORCE_INFER_CONTEXT_CHANNEL_OVER_DESC_ENV_VAR)) {
            LOGGER__WARNING("Using desc instead of CCB for inter context channels is not optimal for performance.");
            return false;
        } else {
            return true;
        }
    case LayerType::DDR:
        // On circular_continuous (aka ddr), the buffers are relatively small and we want to verify the C2C mechanism,
        // therefore the CCB is the default behaviour.
        // Due to request from the DFC group (Memory issues) - DDR buffers would run over DESC and not CCB buffers.
        if (is_env_variable_on(HAILO_FORCE_DDR_CHANNEL_OVER_CCB_ENV_VAR)) {
            LOGGER__WARNING("Using Non default buffer type (CCB instead of DESC) for ddr channel.");
            return true;
        } else {
            return false;
        }
    default:
        // Shouldn't reach here
        assert(false);
        return false;
    }
}

Expected<InternalBufferPlanning> InternalBufferPlanner::create_naive_buffer_planning(
    const std::map<EdgeLayerKey, EdgeLayerInfo> &edge_layer_infos, HailoRTDriver::DmaType dma_type,
    uint16_t max_page_size, bool force_sg_buffer_type)
{
    InternalBufferPlanning buffer_planning;

    // Sort edge layers by size - Start with the biggest buffer
    auto sorted_edge_layer_vector = sort_edge_layers_by_size(edge_layer_infos);
    for (const auto &edge_layer_info : sorted_edge_layer_vector) {
        // Naive planning - Buffer holds only one transfer pattern and one edge layer
        vdma::VdmaBuffer::Type buffer_type = should_edge_layer_use_ccb(edge_layer_info.second.type, dma_type, force_sg_buffer_type) ?
            vdma::VdmaBuffer::Type::CONTINUOUS : vdma::VdmaBuffer::Type::SCATTER_GATHER;
        TRY_WITH_ACCEPTABLE_STATUS(HAILO_CANT_MEET_BUFFER_REQUIREMENTS, const auto buffer_requirements,
            return_buffer_requirements(edge_layer_info.second, buffer_type, max_page_size));

        const std::vector<EdgeLayerPlan> edge_layer_plan{
            EdgeLayerPlan{edge_layer_info.first, NAIVE_PLANNING_EDGE_LAYER_OFFSET, buffer_requirements}
        };

        buffer_planning.emplace_back(
            BufferPlan{
                buffer_type,
                buffer_requirements.buffer_size(),
                edge_layer_plan});
    }
    return buffer_planning;
}

std::vector<std::pair<EdgeLayerKey, EdgeLayerInfo>> InternalBufferPlanner::sort_edge_layers_by_size(
    const std::map<EdgeLayerKey, EdgeLayerInfo> &edge_layers)
{
    std::vector<std::pair<EdgeLayerKey, EdgeLayerInfo>> sorted_edge_layers;
    std::copy(edge_layers.begin(), edge_layers.end(), std::back_inserter<std::vector<std::pair<EdgeLayerKey, EdgeLayerInfo>>>(sorted_edge_layers));
    std::sort(sorted_edge_layers.begin(), sorted_edge_layers.end(),
        [](const std::pair<EdgeLayerKey, EdgeLayerInfo> &a, const std::pair<EdgeLayerKey, EdgeLayerInfo> &b) {
            return a.second.transfer_size > b.second.transfer_size;
        });
    return sorted_edge_layers;
}

Expected<vdma::BufferSizesRequirements> InternalBufferPlanner::return_buffer_requirements(const EdgeLayerInfo &edge_layer,
    const vdma::VdmaBuffer::Type buffer_type, uint16_t max_page_size)
{
    // Calc actual size
    static const auto DONT_FORCE_DEFAULT_PAGE_SIZE = false;
    static const auto FORCE_BATCH_SIZE = true;
    static const auto IS_VDMA_ALIGNED_BUFFER = true;
    const auto is_circular = (LayerType::DDR == edge_layer.type);
    const auto is_ddr = (LayerType::DDR == edge_layer.type);
    auto buffer_requirements = vdma::BufferSizesRequirements::get_buffer_requirements_single_transfer(
        buffer_type, max_page_size, edge_layer.max_transfers_in_batch,
        edge_layer.max_transfers_in_batch, edge_layer.transfer_size, is_circular, DONT_FORCE_DEFAULT_PAGE_SIZE,
        FORCE_BATCH_SIZE, IS_VDMA_ALIGNED_BUFFER, is_ddr);
    return buffer_requirements;
}

ContextBufferUsageSegments InternalBufferPlanner::merge_context_buffer_events(
    ContextBufferUsageSegments& combined, const ContextBufferUsageSegments& added_buffers)
{
    // Combine the two vectors into one
    combined.insert(combined.end(), added_buffers.begin(), added_buffers.end());

    // Sort the combined vector by offset
    std::sort(combined.begin(), combined.end(), [](const BufferUsageSegment& a, const BufferUsageSegment& b) {
        return a.offset < b.offset;
    });

    // Merge overlapping buffers
    ContextBufferUsageSegments merged;
    for (const auto& buffer : combined) {
        if (!merged.empty() && (merged.back().offset + merged.back().size >= buffer.offset)) {
            // If the current buffer overlaps with the last buffer in the merged list,
            // extend the size of the last buffer to include the current buffer
            merged.back().size = std::max(merged.back().size, buffer.offset + buffer.size - merged.back().offset);
        } else {
            // If the current buffer does not overlap with the last buffer in the merged list,
            // add it to the list
            merged.push_back(buffer);
        }
    }

    return merged;
}

size_t InternalBufferPlanner::find_new_buffer_offset(const ContextBufferUsageSegments& unified_buffers, size_t new_buffer_size,
    uint16_t buffer_offset_alignment)
{
    // Try to find a gap in the list that is large enough to hold the new buffer
    // If first buffer starts after 0, check the gap at the beginning of the list
    const auto aligned_first_buffer_offset =
        !unified_buffers.empty() ? (DIV_ROUND_DOWN(unified_buffers[0].offset, buffer_offset_alignment) * buffer_offset_alignment) : 0;
    if (!unified_buffers.empty() && aligned_first_buffer_offset >= new_buffer_size) {
        return 0;
    }

    const auto max_size = unified_buffers.empty() ? 0 : unified_buffers.back().offset + unified_buffers.back().size;
    const auto aligned_max_size =  DIV_ROUND_UP(max_size, buffer_offset_alignment) * buffer_offset_alignment;
    for (auto it = unified_buffers.begin(); it != unified_buffers.end(); ++it) {
        const auto aligned_end_of_buffer =  DIV_ROUND_UP((it->offset + it->size), buffer_offset_alignment) * buffer_offset_alignment;
        // Calculate the gap between the current buffer and the next buffer
        size_t gap = ((it + 1 != unified_buffers.end()) ? ((it + 1)->offset) : (max_size)) - aligned_end_of_buffer;

        // If the gap is large enough to hold the new buffer, insert the new buffer there
        if (gap >= new_buffer_size) {
            return aligned_end_of_buffer;
        }
    }

    // If no suitable gap was found, add the new buffer to the end of the list (but aligned to page size).
    return aligned_max_size;
}

std::vector<BufferUsageSegment> InternalBufferPlanner::build_availibility_map(
    const std::vector<ContextBufferUsageSegments> &context_buffer_usage_vector, uint16_t start_context, uint16_t end_context)
{
    // Start with empty event vector
    std::vector<BufferUsageSegment> unified_buffer_events = {};
    for (size_t context_index = start_context; context_index <= end_context; context_index++) {
        unified_buffer_events = merge_context_buffer_events(unified_buffer_events, context_buffer_usage_vector[context_index]);
    }

    return unified_buffer_events;
}

void update_buffer_to_context_map(std::vector<std::vector<BufferUsageSegment>> &context_buffer_usage_vector,
    uint16_t start_context, uint16_t end_context, size_t buffer_offset, size_t buffer_size)
{
    // Don't have to sort here. Only the combined vector needs to be sorted.
    for (uint16_t context_index = start_context; context_index <= end_context; context_index++) {
        context_buffer_usage_vector[context_index].emplace_back(BufferUsageSegment{buffer_offset, buffer_size});
    }
}

hailo_status InternalBufferPlanner::add_edge_layer_to_planning(
    const std::pair<EdgeLayerKey, EdgeLayerInfo> &edge_layer,
    std::vector<std::vector<BufferUsageSegment>> &context_buffer_usage_vector, BufferPlan &buffer_plan,
    const vdma::VdmaBuffer::Type buffer_type, uint16_t max_page_size)
{
    TRY_WITH_ACCEPTABLE_STATUS(HAILO_CANT_MEET_BUFFER_REQUIREMENTS, const auto buffer_requirements,
        return_buffer_requirements(edge_layer.second, buffer_type, max_page_size));

    // Check if there is enough space in the current context buffer.
    const auto start_context = edge_layer.second.start_context;
    const auto end_context = edge_layer.second.end_context;
    const auto buffer_map = build_availibility_map(context_buffer_usage_vector, start_context, end_context);

    const auto edge_layer_size = buffer_requirements.buffer_size();
    const auto buffer_offset_alignment = buffer_requirements.desc_page_size();
    const auto buffer_offset = find_new_buffer_offset(buffer_map, edge_layer_size, buffer_offset_alignment);

    auto end_of_edge_layer_offset = buffer_offset + edge_layer_size;
    // Update buffer size if needed
    buffer_plan.buffer_size = std::max(end_of_edge_layer_offset, buffer_plan.buffer_size);

    // Add the buffer to the buffer plan
    buffer_plan.edge_layer_plans.emplace_back(EdgeLayerPlan{edge_layer.first, buffer_offset, buffer_requirements});

    update_buffer_to_context_map(context_buffer_usage_vector, start_context, end_context, buffer_offset, edge_layer_size);

    LOGGER__DEBUG("Added edge layer key {}:{} with size {} from context {} to context {} to offset {}",
        edge_layer.first.first, edge_layer.first.second, edge_layer_size, start_context, end_context, buffer_offset);

    return HAILO_SUCCESS;
}

Expected<InternalBufferPlanning> InternalBufferPlanner::create_single_buffer_planning(
    const std::map<EdgeLayerKey, EdgeLayerInfo> &sg_edge_layers, size_t number_of_contexts,
    const vdma::VdmaBuffer::Type buffer_type, uint16_t max_page_size)
{
    InternalBufferPlanning buffer_planning;
    // Trying to reserve one buffer only.
    buffer_planning.reserve(1);
    // Allocate plan for one buffer
    BufferPlan buffer_plan;
    // Buffer type is SG
    buffer_plan.buffer_type = buffer_type;
    // Init buffer with size 0
    buffer_plan.buffer_size = 0;

    auto sorted_edge_layer_vector = sort_edge_layers_by_size(sg_edge_layers);
    std::vector<std::vector<BufferUsageSegment>> context_buffer_usage_vector(number_of_contexts);

    for (auto &edge_layer : sorted_edge_layer_vector) {
        auto status = add_edge_layer_to_planning(edge_layer, context_buffer_usage_vector, buffer_plan, buffer_type, max_page_size);
        CHECK_STATUS_CANT_MEET_REQUIREMENTS(status);
    }

    // Update buffer planning
    buffer_planning.emplace_back(buffer_plan);

    return buffer_planning;
}

Expected<InternalBufferPlanning> InternalBufferPlanner::create_optimized_buffer_planning(
    const std::map<EdgeLayerKey, EdgeLayerInfo> &edge_layer_infos, HailoRTDriver::DmaType dma_type,
    uint16_t max_page_size, size_t number_of_contexts, bool force_sg_buffer_type)
{
    std::map<EdgeLayerKey, EdgeLayerInfo> ccb_edge_layers;
    std::map<EdgeLayerKey, EdgeLayerInfo> sg_edge_layers;

    // First - split between CCB and SG buffers
    for (const auto &edge_layer_info : edge_layer_infos) {
        if (should_edge_layer_use_ccb(edge_layer_info.second.type, dma_type, force_sg_buffer_type)) {
            ccb_edge_layers.emplace(edge_layer_info.first, edge_layer_info.second);
        } else {
            sg_edge_layers.emplace(edge_layer_info.first, edge_layer_info.second);
        }
    }

    InternalBufferPlanning buffer_planning;
    // Second - create buffer planning for each buffer type
    if (!ccb_edge_layers.empty()) {
        TRY_WITH_ACCEPTABLE_STATUS(HAILO_CANT_MEET_BUFFER_REQUIREMENTS, const auto ccb_buffer_planning,
            create_single_buffer_planning(ccb_edge_layers, number_of_contexts, vdma::VdmaBuffer::Type::CONTINUOUS, max_page_size));
        buffer_planning.insert(buffer_planning.end(), ccb_buffer_planning.begin(), ccb_buffer_planning.end());
    }

    if (!sg_edge_layers.empty()) {
        TRY_WITH_ACCEPTABLE_STATUS(HAILO_CANT_MEET_BUFFER_REQUIREMENTS, auto sg_buffer_planning,
            create_single_buffer_planning(sg_edge_layers, number_of_contexts, vdma::VdmaBuffer::Type::SCATTER_GATHER, max_page_size));
        buffer_planning.insert(buffer_planning.end(), sg_buffer_planning.begin(), sg_buffer_planning.end());
    }

    return buffer_planning;
}

static hailo_status add_ddr_buffer(std::map<EdgeLayerKey, EdgeLayerInfo>& edge_layer_infos, const LayerInfo &layer_info)
{
    // In DDR - always use core bytes per buffer as row size
    const auto row_size = static_cast<uint16_t>(layer_info.nn_stream_config.core_bytes_per_buffer);
    const auto min_buffered_rows = layer_info.ddr_info.min_buffered_rows;
    auto edge_layer_key = std::make_pair(layer_info.context_index, layer_info.stream_index);

    auto it = edge_layer_infos.find(edge_layer_key);
    CHECK(it == edge_layer_infos.end(), HAILO_INTERNAL_FAILURE,
        "Found two edge layers with the same key for DDR layer. This is not supported.");

    edge_layer_infos.emplace(edge_layer_key,
        EdgeLayerInfo{
            layer_info.type,
            row_size,
            min_buffered_rows,
            layer_info.context_index,
            layer_info.connected_context_info.context_index,
        });

    return HAILO_SUCCESS;
}

static hailo_status add_inter_context_buffer(std::map<EdgeLayerKey, EdgeLayerInfo>& edge_layer_infos,
    const LayerInfo &layer_info, uint16_t batch_size)
{
    // layer_info.connected_context_info.context_index == start context (output stream)
    // layer_info.context_index == end context
    const auto transfer_size = LayerInfoUtils::get_layer_transfer_size(layer_info);

    assert(layer_info.direction == HAILO_H2D_STREAM);
    const auto output_context_index = layer_info.connected_context_info.context_index;
    const auto output_stream_index = layer_info.connected_context_info.stream_index;
    const auto edge_layer_key = std::make_pair(output_context_index, output_stream_index);

    const auto it = edge_layer_infos.find(edge_layer_key);
    if (it != edge_layer_infos.end()) {
        CHECK(it->second.transfer_size == transfer_size, HAILO_INTERNAL_FAILURE,
            "Found two edge layers with the same key but different transfer size");
        CHECK(it->second.max_transfers_in_batch == batch_size, HAILO_INTERNAL_FAILURE,
            "Found two edge layers with the same key but different batch size");
        // Now if the new end context is bigger than the old one, update it.
        if (it->second.end_context < layer_info.context_index) {
            it->second.end_context = layer_info.context_index;
        }
    } else {
        LOGGER__DEBUG("Adding edge layer with key ({}, {}) to the internal buffer manager", edge_layer_key.first, edge_layer_key.second);
        edge_layer_infos.emplace(edge_layer_key,
            EdgeLayerInfo{
                layer_info.type,
                transfer_size,
                batch_size,
                layer_info.connected_context_info.context_index,
                layer_info.context_index,
            });
    }
    return HAILO_SUCCESS;
}

Expected<std::map<EdgeLayerKey, EdgeLayerInfo>> InternalBufferPlanner::get_edge_layer_infos(const CoreOpMetadata& core_op,
    const ConfigureNetworkParams& config_params)
{
    std::map<EdgeLayerKey, EdgeLayerInfo> edge_layer_infos;

    for (const auto &context_metadata : core_op.dynamic_contexts()) {
        for (const auto &layer_info : context_metadata.get_ddr_output_layers()) {
            CHECK_SUCCESS(add_ddr_buffer(edge_layer_infos, layer_info));
        }

        for (const auto &layer_info : context_metadata.get_inter_context_input_layers()) {
            TRY(auto batch_size, get_network_batch_size(config_params, layer_info.network_name));
            // This API gets the inter context input Layer, but the key is the output layer.
            // The reason is that there is one output edge layer and multiple input edge layers.
            // We must get the info of all the inputs in order to set the right start and end contexts,
            // but the key must the output (from the connected context info).
            CHECK_SUCCESS(add_inter_context_buffer(edge_layer_infos, layer_info, batch_size));
        }
    }

    return edge_layer_infos;
}

Expected<InternalBufferPlanning> InternalBufferPlanner::create_buffer_planning(
    const CoreOpMetadata& core_op, uint16_t batch_size, Type plan_type, HailoRTDriver::DmaType dma_type,
    uint16_t max_page_size)
{
    ConfigureNetworkParams config_params{};
    config_params.batch_size = batch_size;
    for (const auto &network_name : core_op.get_network_names()) {
        config_params.network_params_by_name[network_name].batch_size = batch_size;
    }
    TRY(auto edge_layer_infos, get_edge_layer_infos(core_op, config_params));
    return create_buffer_planning(edge_layer_infos, plan_type, dma_type, max_page_size, core_op.dynamic_contexts().size());
}

Expected<InternalBufferPlanning> InternalBufferPlanner::create_buffer_planning(
    const std::map<EdgeLayerKey, EdgeLayerInfo> &edge_layer_infos, Type plan_type,
    HailoRTDriver::DmaType dma_type, uint16_t max_page_size, size_t number_of_contexts)
{
    static const bool FORCE_SG_BUFFER_TYPE = true;
    // Force plan by user flag
    if (is_env_variable_on(HAILO_FORCE_NAIVE_PER_BUFFER_TYPE_ALOCATION_ENV_VAR)) {
        LOGGER__INFO("Forced buffer planning of type 'NAIVE_PER_BUFFER_TYPE.");
        plan_type = Type::NAIVE_PER_BUFFER_TYPE;
    }

    switch (plan_type) {
    case Type::SINGLE_BUFFER_PER_BUFFER_TYPE:
        return create_optimized_buffer_planning(edge_layer_infos, dma_type, max_page_size, number_of_contexts);
    case Type::SINGLE_SG_BUFFER:
        return create_optimized_buffer_planning(edge_layer_infos, dma_type, max_page_size, number_of_contexts, FORCE_SG_BUFFER_TYPE);
    case Type::NAIVE_PER_BUFFER_TYPE:
        return create_naive_buffer_planning(edge_layer_infos, dma_type, max_page_size);
    case Type::NAIVE_SG_BUFFER:
        return create_naive_buffer_planning(edge_layer_infos, dma_type, max_page_size, FORCE_SG_BUFFER_TYPE);
    default:
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }
}

static size_t total_descriptors_cma_memory(const BufferPlan &buffer_plan)
{
    return std::accumulate(buffer_plan.edge_layer_plans.begin(), buffer_plan.edge_layer_plans.end(), size_t{0},
        [](size_t acc, const EdgeLayerPlan &edge_plan) {
            return acc + vdma::DescriptorList::descriptors_buffer_allocation_size(edge_plan.buffer_requirements.descs_count());
        });
}

BufferPlanReport InternalBufferPlanner::report_planning_info(const InternalBufferPlanning &buffer_planning)
{
    BufferPlanReport report = {};
    report.cma_memory = 0;
    report.pinned_memory = 0;
    report.edge_layer_size = 0;

    for (const auto &buffer_plan : buffer_planning) {
        if (vdma::VdmaBuffer::Type::CONTINUOUS == buffer_plan.buffer_type) {
            report.cma_memory += buffer_plan.buffer_size;
        } else {
            report.pinned_memory += buffer_plan.buffer_size;
            report.cma_memory_for_descriptors += total_descriptors_cma_memory(buffer_plan);
        }

        for (const auto &edge_plan : buffer_plan.edge_layer_plans) {
            report.edge_layer_size += edge_plan.buffer_requirements.buffer_size();
        }
    }

    const auto total_memory = report.cma_memory + report.pinned_memory;
    report.memory_utilization_factor = (report.edge_layer_size > 0) ?
        (static_cast<float>(total_memory) / static_cast<float>(report.edge_layer_size)) : 1;

    return report;
}

} /* namespace hailort */
