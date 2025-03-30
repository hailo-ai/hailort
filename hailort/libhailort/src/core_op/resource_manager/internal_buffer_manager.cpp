/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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

Expected<std::shared_ptr<InternalBufferManager>> InternalBufferManager::create(HailoRTDriver &driver)
{
    auto buffer_manager_ptr = make_shared_nothrow<InternalBufferManager>(InternalBufferManager(driver));
    CHECK_NOT_NULL_AS_EXPECTED(buffer_manager_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return buffer_manager_ptr;
}

InternalBufferManager::InternalBufferManager(HailoRTDriver &driver)
    : m_driver(driver)
    {}

Expected<std::shared_ptr<vdma::VdmaBuffer>> InternalBufferManager::create_intermediate_sg_buffer(
    const size_t buffer_size)
{
    TRY(auto buffer, vdma::SgBuffer::create(m_driver, buffer_size, HailoRTDriver::DmaDirection::BOTH));

    auto buffer_ptr = make_shared_nothrow<vdma::SgBuffer>(std::move(buffer));
    CHECK_NOT_NULL_AS_EXPECTED(buffer_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::shared_ptr<vdma::VdmaBuffer>(std::move(buffer_ptr));
}

Expected<std::shared_ptr<vdma::VdmaBuffer>> InternalBufferManager::create_intermediate_ccb_buffer(
    const size_t buffer_size)
{
    TRY_WITH_ACCEPTABLE_STATUS(HAILO_OUT_OF_HOST_CMA_MEMORY, auto buffer,
        vdma::ContinuousBuffer::create(buffer_size, m_driver));

    auto buffer_ptr = make_shared_nothrow<vdma::ContinuousBuffer>(std::move(buffer));
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
        LOGGER__INFO("Planned internal buffer memory: CMA={} CMA-Desc={} Pinned={}. memory to edge layer usage factor is {}",
            default_planner_report.cma_memory, default_planner_report.cma_memory_for_descriptors,
            default_planner_report.pinned_memory, default_planner_report.memory_utilization_factor);
    }

    auto default_plan_executed = (default_planner_report.cma_memory == executed_buffers_report.cma_memory) &&
        (default_planner_report.pinned_memory == executed_buffers_report.pinned_memory);

    if (default_plan_executed) {
        LOGGER__INFO("Default Internal buffer planner executed successfully");
    } else {
        LOGGER__INFO("executed internal buffer memory: CMA={} CMA-Desc={} Pinned={}. memory to edge layer usage factor is {}",
            executed_buffers_report.cma_memory, default_planner_report.cma_memory_for_descriptors,
            executed_buffers_report.pinned_memory, executed_buffers_report.memory_utilization_factor);
    }
}

hailo_status InternalBufferManager::plan_and_execute(const std::map<EdgeLayerKey, EdgeLayerInfo> &edge_layer_infos,
    InternalBufferPlanner::Type default_planner_type,
    const size_t number_of_contexts)
{
    // Create buffer planning
    auto planner_type = default_planner_type;
    // copy of initial edge layers
    auto edge_layers = edge_layer_infos;
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
        TRY(auto buffer_planning, buffer_planning_exp);

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
        for (const auto &edge_layer_plan : buffer_plan.edge_layer_plans) {
            m_edge_layer_to_buffer_map.emplace(
                edge_layer_plan.key,
                EdgeLayerBuffer{buffer_ptr.value(), edge_layer_plan});

            // Add edge layers to executed list
            edge_layers_executed.emplace_back(edge_layer_plan.key);
        }

        // Add buffer to executed list
        buffers_executed.emplace_back(buffer_plan);
    }

    return execution_status;
}

Expected<EdgeLayerBuffer> InternalBufferManager::get_intermediate_buffer(const EdgeLayerKey &key)
{
    const auto buffer_it = m_edge_layer_to_buffer_map.find(key);
    if (std::end(m_edge_layer_to_buffer_map) == buffer_it) {
        return make_unexpected(HAILO_NOT_FOUND);
    }

    return Expected<EdgeLayerBuffer>(buffer_it->second);
}

} /* namespace hailort */
