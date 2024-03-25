/**
 * Copyright (c) 2020-2023 Hailo Technologies Ltd. All rights reserved.
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
#include "internal_buffer_planner.hpp"

#include <numeric>

constexpr size_t NAIVE_PLANNING_EDGE_LAYER_OFFSET = 0;

// Macros that check status. If status is HAILO_CANT_MEET_BUFFER_REQUIREMENTS, return without printing error to the prompt.
#define CHECK_EXPECTED_CANT_MEET_REQUIREMENTS(type) if (HAILO_CANT_MEET_BUFFER_REQUIREMENTS == type.status()) {return make_unexpected(HAILO_CANT_MEET_BUFFER_REQUIREMENTS);} CHECK_SUCCESS(type);
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
        if (nullptr != std::getenv("HAILO_FORCE_INFER_CONTEXT_CHANNEL_OVER_DESC")) {
            LOGGER__WARNING("Using desc instead of CCB for inter context channels is not optimal for performance.");
            return false;
        } else {
            return true;
        }
    case LayerType::DDR:
        // On circular_continuous (aka ddr), the buffers are relatively small and we want to verify the C2C mechanism,
        // therefore the CCB is the default behaviour.
        // Due to request from the DFC group (Memory issues) - DDR buffers would run over DESC and not CCB buffers.
        if (nullptr != std::getenv("HAILO_FORCE_DDR_CHANNEL_OVER_CCB")) {
            LOGGER__WARNING("Using Non default buffer type (CCB instead of DESC) for ddr channel.");
            return true;
        } else {
            return false;
        }
    case LayerType::CFG:
        if (nullptr != std::getenv("HAILO_FORCE_CONF_CHANNEL_OVER_DESC")) {
            LOGGER__WARNING("Using desc instead of CCB for config channel is not optimal for performance.");
            return false;
        }
        else {
            return true;
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
        std::vector<std::pair<EdgeLayerKey, size_t>> edge_layer_offsets;
        std::map<EdgeLayerKey, EdgeLayerInfo> plan_edge_layer_infos;
        plan_edge_layer_infos.emplace(edge_layer_info.first, edge_layer_info.second);
        edge_layer_offsets.emplace_back(edge_layer_info.first, NAIVE_PLANNING_EDGE_LAYER_OFFSET);
        vdma::VdmaBuffer::Type buffer_type = should_edge_layer_use_ccb(edge_layer_info.second.type, dma_type, force_sg_buffer_type) ?
            vdma::VdmaBuffer::Type::CONTINUOUS : vdma::VdmaBuffer::Type::SCATTER_GATHER;
        const auto buffer_requirements = return_buffer_requirements(edge_layer_info.second, buffer_type, max_page_size);
        CHECK_EXPECTED_CANT_MEET_REQUIREMENTS(buffer_requirements);

        buffer_planning.emplace_back(
            BufferPlan{
                buffer_type,
                buffer_requirements->buffer_size(),
                buffer_requirements->buffer_size(),
                edge_layer_offsets,
                plan_edge_layer_infos});
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
    auto buffer_requirements = vdma::BufferSizesRequirements::get_buffer_requirements_single_transfer(
        buffer_type, max_page_size, edge_layer.max_transfers_in_batch,
        edge_layer.max_transfers_in_batch, edge_layer.transfer_size, is_circular, DONT_FORCE_DEFAULT_PAGE_SIZE,
        FORCE_BATCH_SIZE, IS_VDMA_ALIGNED_BUFFER);
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
    const auto buffer_requirements = return_buffer_requirements(edge_layer.second, buffer_type, max_page_size);
    CHECK_EXPECTED_CANT_MEET_REQUIREMENTS(buffer_requirements);

    // Check if there is enough space in the current context buffer.
    const auto start_context = edge_layer.second.start_context;
    const auto end_context = edge_layer.second.end_context;
    const auto buffer_map = build_availibility_map(context_buffer_usage_vector, start_context, end_context);

    const auto edge_layer_size = buffer_requirements->buffer_size();
    const auto buffer_offset_alignment = buffer_requirements->desc_page_size();
    const auto buffer_offset = find_new_buffer_offset(buffer_map, edge_layer_size, buffer_offset_alignment);

    auto end_of_edge_layer_offset = buffer_offset + edge_layer_size;
    // Update buffer size if needed
    buffer_plan.buffer_size = std::max(end_of_edge_layer_offset, buffer_plan.buffer_size);
    // Update total edge layer size
    buffer_plan.total_edge_layer_size += edge_layer_size;

    // Add the buffer to the buffer plan
    buffer_plan.edge_layer_offsets.emplace_back(edge_layer.first, buffer_offset);
    buffer_plan.edge_layer_infos.emplace(edge_layer.first, edge_layer.second);

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
    buffer_plan.total_edge_layer_size = 0;

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
        auto ccb_buffer_planning =
            create_single_buffer_planning(ccb_edge_layers, number_of_contexts, vdma::VdmaBuffer::Type::CONTINUOUS, max_page_size);
        CHECK_EXPECTED_CANT_MEET_REQUIREMENTS(ccb_buffer_planning);
        buffer_planning.insert(buffer_planning.end(), ccb_buffer_planning->begin(), ccb_buffer_planning->end());
    }

    if (!sg_edge_layers.empty()) {
        auto sg_buffer_planning =
            create_single_buffer_planning(sg_edge_layers, number_of_contexts, vdma::VdmaBuffer::Type::SCATTER_GATHER, max_page_size);
        CHECK_EXPECTED_CANT_MEET_REQUIREMENTS(sg_buffer_planning);
        buffer_planning.insert(buffer_planning.end(), sg_buffer_planning->begin(), sg_buffer_planning->end());
    }

    return buffer_planning;
}

Expected<InternalBufferPlanning> InternalBufferPlanner::create_buffer_planning(
    const std::map<EdgeLayerKey, EdgeLayerInfo> &edge_layer_infos, Type plan_type,
    HailoRTDriver::DmaType dma_type, uint16_t max_page_size, size_t number_of_contexts)
{
    static const bool FORCE_SG_BUFFER_TYPE = true;
    // Force plan by user flag
    if (nullptr != std::getenv("HAILO_FORCE_NAIVE_PER_BUFFER_TYPE_ALOCATION")) {
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

BufferPlanReport InternalBufferPlanner::report_planning_info(const InternalBufferPlanning &buffer_planning)
{
    BufferPlanReport report = {};
    report.cma_memory = 0;
    report.user_memory = 0;
    report.edge_layer_size = 0;

    for (const auto &buffer_plan : buffer_planning) {
        if (vdma::VdmaBuffer::Type::CONTINUOUS == buffer_plan.buffer_type) {
            report.cma_memory += buffer_plan.buffer_size;
        } else {
            report.user_memory += buffer_plan.buffer_size;
        }
        report.edge_layer_size += buffer_plan.total_edge_layer_size;
    }

    report.memory_utilization_factor = (report.edge_layer_size > 0) ?
        (static_cast<float>(report.cma_memory + report.user_memory) / static_cast<float>(report.edge_layer_size)) : 1;

    return report;
}

Expected<EdgeLayerInfo> InternalBufferPlanner::get_edge_info_from_buffer_plan(const InternalBufferPlanning &buffer_planning,
    const EdgeLayerKey &edge_layer_key)
{
    for (const auto &buffer_plan : buffer_planning) {
        auto it = buffer_plan.edge_layer_infos.find(edge_layer_key);
        if (it != buffer_plan.edge_layer_infos.end()) {
            return Expected<EdgeLayerInfo>(it->second);
        }
    }
    return make_unexpected(HAILO_NOT_FOUND);
}

hailo_status InternalBufferPlanner::change_edge_layer_buffer_offset(InternalBufferPlanning &buffer_planning,
    const EdgeLayerKey &edge_layer_key, size_t new_offset, uint16_t max_page_size)
{
    TRY(auto edge_layer_info, get_edge_info_from_buffer_plan(buffer_planning, edge_layer_key));
    for (auto &buffer_plan : buffer_planning) {
        const auto buffer_requirements =  return_buffer_requirements(edge_layer_info, buffer_plan.buffer_type, max_page_size);
        CHECK_EXPECTED_CANT_MEET_REQUIREMENTS(buffer_requirements);

        for (auto &edge_layer_offset : buffer_plan.edge_layer_offsets) {
            if (edge_layer_offset.first == edge_layer_key) {
                edge_layer_offset.second = new_offset;
                if (edge_layer_offset.second + buffer_requirements->buffer_size() > buffer_plan.buffer_size) {
                    buffer_plan.buffer_size = edge_layer_offset.second + buffer_requirements->buffer_size();
                }
                return HAILO_SUCCESS;
            }
        }
    }
    return HAILO_INVALID_ARGUMENT;
}

Expected<size_t> InternalBufferPlanner::get_edge_layer_buffer_offset(const InternalBufferPlanning &buffer_planning,
    const EdgeLayerKey &edge_layer_key)
{
    for (auto &buffer_plan : buffer_planning) {
        auto it = buffer_plan.edge_layer_offsets.begin();
        while (it != buffer_plan.edge_layer_offsets.end()) {
            if (it->first == edge_layer_key) {
                return Expected<size_t>(it->second);
            }
            it++;
        }
    }
    return make_unexpected(HAILO_NOT_FOUND);
}


} /* namespace hailort */
