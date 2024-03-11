/**
 * Copyright (c) 2020-2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file internal_buffer_manager.hpp
 * @brief Planner for all the internal buffers of the CoreOp
 *
 * The manager will hold all the internal buffers of the CoreOp.
 * The manager can optimize the memory consumption of the core op and provide API
 * about the total internal memory consumption.
 *
  **/

#include "internal_buffer_manager.hpp"
#include "hef/layer_info.hpp"
#include "vdma/memory/sg_buffer.hpp"
#include "vdma/memory/continuous_buffer.hpp"
#include "vdma/memory/buffer_requirements.hpp"


#include <numeric>

namespace hailort
{

// Macros that check status. If status is HAILO_CANT_MEET_BUFFER_REQUIREMENTS, return without printing error to the prompt.
#define CHECK_EXPECTED_OUT_OF_CMA_MEMORY(type) if (HAILO_OUT_OF_HOST_CMA_MEMORY == (type).status()) {return make_unexpected(HAILO_OUT_OF_HOST_CMA_MEMORY);} CHECK_SUCCESS(type);

Expected<std::shared_ptr<InternalBufferManager>> InternalBufferManager::create(HailoRTDriver &driver,
    const ConfigureNetworkParams &config_params)
{

    auto buffer_manager_ptr = make_shared_nothrow<InternalBufferManager>(InternalBufferManager(driver, config_params));
    CHECK_NOT_NULL_AS_EXPECTED(buffer_manager_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return buffer_manager_ptr;
}

InternalBufferManager::InternalBufferManager(HailoRTDriver &driver, const ConfigureNetworkParams &config_params)
    : m_driver(driver),
      m_config_params(config_params),
      m_edge_layer_infos(),
      m_edge_layer_to_buffer_map()
    {}


void InternalBufferManager::add_buffer_info(const EdgeLayerKey &edge_layer_key, const EdgeLayerInfo &buffer_info)
{
    m_edge_layer_infos.emplace(edge_layer_key, buffer_info);
}

Expected<uint16_t> InternalBufferManager::get_network_batch_size(const std::string &network_name) const
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

hailo_status InternalBufferManager::add_inter_context_buffer(const LayerInfo &layer_info)
{
    // This API gets the inter context input Layer, but the key is the output layer.
    // The reason is that there is one output edge layer and multiple input edge layers.
    // We must get the info of all the inputs in order to set the right start and end contexts,
    // but the key must the the output (from the connected context info).

    // layer_info.connected_context_info.context_index == start context
    // layer_info.context_index == end context
    const auto transfer_size = LayerInfoUtils::get_layer_transfer_size(layer_info);
    TRY(auto batch_size, get_network_batch_size(layer_info.network_name));
    static const bool BUFFER_REUSE = true;

    auto edge_layer_key =
        std::make_pair(layer_info.connected_context_info.context_index, layer_info.connected_context_info.stream_index);
    // First check if there is a key (for the case of one output multiple inputs).

    const auto it = m_edge_layer_infos.find(edge_layer_key);
    if (it != m_edge_layer_infos.end()) {
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
        add_buffer_info(edge_layer_key,
            EdgeLayerInfo{
                layer_info.type,
                transfer_size,
                batch_size,
                layer_info.connected_context_info.context_index,
                layer_info.context_index,
                BUFFER_REUSE});
    }
    return HAILO_SUCCESS;
}

hailo_status InternalBufferManager::add_ddr_buffer(const LayerInfo &layer_info)
{
    // In DDR - always use core bytes per buffer as row size
    const auto row_size = static_cast<uint16_t>(layer_info.nn_stream_config.core_bytes_per_buffer);
    const auto min_buffered_rows = layer_info.ddr_info.min_buffered_rows;
    static auto const BUFFER_REUSE = true;
    auto edge_layer_key = std::make_pair(layer_info.context_index, layer_info.stream_index);

    auto it = m_edge_layer_infos.find(edge_layer_key);
    CHECK(it == m_edge_layer_infos.end(), HAILO_INTERNAL_FAILURE,
        "Found two edge layers with the same key for DDR layer. This is not supported.");

    add_buffer_info(edge_layer_key,
        EdgeLayerInfo{
            layer_info.type,
            row_size,
            min_buffered_rows,
            layer_info.context_index,
            layer_info.connected_context_info.context_index,
            BUFFER_REUSE});

    return HAILO_SUCCESS;
}

// For edge layers
hailo_status InternalBufferManager::add_layer_buffer_info(const LayerInfo &layer_info)
{
    switch (layer_info.type) {
        case LayerType::INTER_CONTEXT:
            return add_inter_context_buffer(layer_info);
        case LayerType::DDR:
            return add_ddr_buffer(layer_info);
        default:
            LOGGER__ERROR("Unsupported layer type for InternalBufferManager");
            return HAILO_INTERNAL_FAILURE;
    }
}

hailo_status InternalBufferManager::add_config_buffer_info(const uint16_t context_index, const size_t config_stream_index,
    const std::vector<uint32_t> &cfg_sizes)
{
    static const bool NO_REUSE = false;
    static const auto SINGLE_TRANSFER_PER_BATCH = 1;
    auto edge_layer_key = std::make_pair(static_cast<uint16_t>(context_index), static_cast<uint8_t>(MAX_EDGE_LAYERS_PER_CONTEXT + config_stream_index));
    const auto buffer_size = static_cast<uint32_t>(std::accumulate(cfg_sizes.begin(), cfg_sizes.end(), 0));
    add_buffer_info(edge_layer_key,
        EdgeLayerInfo{
            LayerType::CFG,
            buffer_size,
            SINGLE_TRANSFER_PER_BATCH,
            context_index,
            context_index,
            NO_REUSE});

    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<vdma::VdmaBuffer>> InternalBufferManager::create_intermediate_sg_buffer(
    const size_t buffer_size)
{
    auto buffer = vdma::SgBuffer::create(m_driver, buffer_size, HailoRTDriver::DmaDirection::BOTH);
    CHECK_EXPECTED(buffer);

    auto buffer_ptr = make_shared_nothrow<vdma::SgBuffer>(buffer.release());
    CHECK_NOT_NULL_AS_EXPECTED(buffer_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::shared_ptr<vdma::VdmaBuffer>(std::move(buffer_ptr));
}

Expected<std::shared_ptr<vdma::VdmaBuffer>> InternalBufferManager::create_intermediate_ccb_buffer(
    const size_t buffer_size)
{
    auto buffer = vdma::ContinuousBuffer::create(buffer_size, m_driver);
    CHECK_EXPECTED_OUT_OF_CMA_MEMORY(buffer);

    auto buffer_ptr = make_shared_nothrow<vdma::ContinuousBuffer>(buffer.release());
    CHECK_NOT_NULL_AS_EXPECTED(buffer_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::shared_ptr<vdma::VdmaBuffer>(std::move(buffer_ptr));
}

Expected<std::shared_ptr<vdma::VdmaBuffer>> InternalBufferManager::create_intermediate_buffer(
    vdma::VdmaBuffer::Type &buffer_type, const size_t buffer_size)
{
    if (vdma::VdmaBuffer::Type::CONTINUOUS == buffer_type) {
        return create_intermediate_ccb_buffer(buffer_size);
    }
    return create_intermediate_sg_buffer(buffer_size);
}

void InternalBufferManager::print_execution_results(const BufferPlanReport &default_planner_report,
    bool default_planner_meet_requirements, const BufferPlanReport &executed_buffers_report)
{
    if (!default_planner_meet_requirements) {
        LOGGER__INFO("Default Internal buffer planner failed to meet requirements");
    } else {
        LOGGER__INFO("Planned internal buffer memory: CMA memory {}, user memory {}. memory to edge layer usage factor is {}",
            default_planner_report.cma_memory, default_planner_report.user_memory, default_planner_report.memory_utilization_factor);
    }

    auto default_plan_executed = (default_planner_report.cma_memory == executed_buffers_report.cma_memory) &&
        (default_planner_report.user_memory == executed_buffers_report.user_memory);

    if (default_plan_executed) {
        LOGGER__INFO("Default Internal buffer planner executed successfully");
    } else {
        LOGGER__INFO("executed internal buffer memory: CMA memory {}, user memory {}. memory to edge layer usage factor is {}",
            executed_buffers_report.cma_memory, executed_buffers_report.user_memory, executed_buffers_report.memory_utilization_factor);
    }
}

hailo_status InternalBufferManager::plan_and_execute(InternalBufferPlanner::Type default_planner_type,
    const size_t number_of_contexts)
{
    // Create buffer planning
    auto planner_type = default_planner_type;
    // copy of initial edge layers
    auto edge_layers = m_edge_layer_infos;
    // Vector of executed buffers from the planning
    InternalBufferPlanning buffers_executed;
    // Default planner report
    BufferPlanReport default_planner_report {};
    bool default_planner_meet_requirements = false;

    while (!edge_layers.empty()) {
        CHECK(InternalBufferPlanner::Type::INVALID != planner_type, HAILO_CANT_MEET_BUFFER_REQUIREMENTS,
            "Cannot find an executable buffer planning for the given edge layers");

        LOGGER__DEBUG("Trying to plan with planner type {}", static_cast<uint8_t>(planner_type));
        auto buffer_planning_exp = InternalBufferPlanner::create_buffer_planning(edge_layers, planner_type,
            m_driver.dma_type(), m_driver.desc_max_page_size(), number_of_contexts);
        if (HAILO_CANT_MEET_BUFFER_REQUIREMENTS == buffer_planning_exp.status()) {
            // If planner failed, Try to go to next planner
            LOGGER__DEBUG("Can't plan with planner type {}", static_cast<uint8_t>(planner_type));
            planner_type = static_cast<InternalBufferPlanner::Type>((static_cast<uint8_t>(planner_type)) + 1);
            continue;
        }
        auto buffer_planning = buffer_planning_exp.release();

        if (planner_type == default_planner_type) {
            default_planner_meet_requirements = true;
            default_planner_report = InternalBufferPlanner::report_planning_info(buffer_planning);
        }

        std::vector<EdgeLayerKey> edge_layers_executed;
        auto status = execute_plan(buffer_planning, edge_layers_executed, buffers_executed);
        // Don't return error if out of CMA host memory. Try to go to next plan.
        if (HAILO_OUT_OF_HOST_CMA_MEMORY != status) {
            CHECK_SUCCESS(status);
        }

        // Remove executed edge layers from edge layers
        for (const auto &edge_layer_key : edge_layers_executed) {
            edge_layers.erase(edge_layer_key);
        }

        if (!edge_layers.empty()) {
            LOGGER__DEBUG("Execute of plan type {} didn't finish. Moving to next planner ", static_cast<uint8_t>(planner_type));
        } else {
            LOGGER__DEBUG("Execute finished successfully");
        }
        // Move to next planner
        planner_type = static_cast<InternalBufferPlanner::Type>((static_cast<uint8_t>(planner_type)) + 1);
    }

    const auto executed_buffers_report = InternalBufferPlanner::report_planning_info(buffers_executed);

    print_execution_results(default_planner_report, default_planner_meet_requirements, executed_buffers_report);

    return HAILO_SUCCESS;
}

hailo_status InternalBufferManager::execute_plan(InternalBufferPlanning &buffer_planning,
    std::vector<EdgeLayerKey> &edge_layers_executed, InternalBufferPlanning &buffers_executed)
{
    // Verify no buffers were allocated yet
    assert(m_edge_layer_to_buffer_map.empty());

    auto execution_status = HAILO_SUCCESS;

    // Go over plan and create buffers
    for (auto &buffer_plan : buffer_planning) {
        auto buffer_ptr = create_intermediate_buffer(buffer_plan.buffer_type, buffer_plan.buffer_size);
        if (buffer_ptr.status() == HAILO_OUT_OF_HOST_CMA_MEMORY) {
            execution_status = buffer_ptr.status();
            // If one of the buffer failed due to lack to memory, try to move to next buffer.
            continue;
        }
        for (const auto &edge_layer_offset : buffer_plan.edge_layer_offsets) {
            m_edge_layer_to_buffer_map.emplace(
                edge_layer_offset.first,
                EdgeLayerToBufferMap{buffer_ptr.value(), edge_layer_offset.second});
        }
        // Add edge layers to executed list
        for (const auto &edge_layer_info : buffer_plan.edge_layer_infos) {
            edge_layers_executed.emplace_back(edge_layer_info.first);
        }

        // Add buffer to executed list
        buffers_executed.emplace_back(buffer_plan);
    }

    return execution_status;
}

Expected<EdgeLayerToBufferMap> InternalBufferManager::get_intermediate_buffer(const EdgeLayerKey &key)
{
    const auto buffer_it = m_edge_layer_to_buffer_map.find(key);
    if (std::end(m_edge_layer_to_buffer_map) == buffer_it) {
        return make_unexpected(HAILO_NOT_FOUND);
    }

    return Expected<EdgeLayerToBufferMap>(buffer_it->second);
}

} /* namespace hailort */
