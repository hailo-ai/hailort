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
#include "vdma/memory/cma_buffer.hpp"
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
    : m_driver(driver), m_sram_buffer_allocator()
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
        vdma::CmaBuffer::create(buffer_size, m_driver));

    auto buffer_ptr = make_shared_nothrow<vdma::CmaBuffer>(std::move(buffer));
    CHECK_NOT_NULL_AS_EXPECTED(buffer_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::shared_ptr<vdma::VdmaBuffer>(std::move(buffer_ptr));
}

Expected<std::shared_ptr<vdma::VdmaBuffer>> InternalBufferManager::create_intermediate_sram_buffer(
    const size_t buffer_size)
{
    TRY_WITH_ACCEPTABLE_STATUS(HAILO_OUT_OF_FW_MEMORY, auto buffer, m_sram_buffer_allocator.allocate(buffer_size));

    auto buffer_ptr = make_shared_nothrow<vdma::SramBuffer>(std::move(buffer));
    CHECK_NOT_NULL_AS_EXPECTED(buffer_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::shared_ptr<vdma::VdmaBuffer>(std::move(buffer_ptr));
}

Expected<std::shared_ptr<vdma::VdmaBuffer>> InternalBufferManager::create_intermediate_buffer(
    vdma::BufferType &buffer_type, const size_t buffer_size)
{
    switch (buffer_type) {
    case vdma::BufferType::CMA:
        return create_intermediate_ccb_buffer(buffer_size);
    case vdma::BufferType::SCATTER_GATHER:
        return create_intermediate_sg_buffer(buffer_size);
    case vdma::BufferType::SRAM:
        return create_intermediate_sram_buffer(buffer_size);
    default:
        LOGGER__ERROR("Creating intermediate-buffer with invalid buffer-type");
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }
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

hailo_status InternalBufferManager::plan_and_execute(std::map<EdgeLayerKey, EdgeLayerInfo> &&edge_layers,
    InternalBufferPlanner::Type planner_type, const size_t number_of_contexts)
{
    InternalBufferPlanning buffers_executed {};
    std::vector<EdgeLayerKey> edge_layers_executed {};

    const DescSizesParams continuous_desc_params = m_driver.get_continuous_desc_params();
    const DescSizesParams sg_desc_params = m_driver.get_sg_desc_params();

    // TODO HRT-19648: Remove this env-var.
    if (is_env_variable_on(HAILO_HW_INFER_ALLOW_DDR_PORTALS_OVER_SRAM_ENV_VAR)) {

        // NOTE: We create a plan that makes ALL ddr-portal buffers over SRAM, without size considerations.
        // Later, when we exectute the plan, we take as many buffers as we can fit in 2MB SRAM and the rest
        // are ignored.

        TRY(auto buffer_plan, InternalBufferPlanner::create_sram_buffer_planning(edge_layers, continuous_desc_params));

        auto status = execute_plan(buffer_plan, edge_layers_executed, buffers_executed);
        if (HAILO_OUT_OF_FW_MEMORY != status) {
            // Out of FW-memory is ok; ignore and move on.
            CHECK_SUCCESS(status);
        }

        for (const auto &edge_layer_key : edge_layers_executed) {
            edge_layers.erase(edge_layer_key);
        }
        edge_layers_executed.clear();
    }

    BufferPlanReport default_planner_report {};
    bool first_pass = true;

    while (true) {
        LOGGER__DEBUG("Trying to plan with planner type {}", static_cast<uint8_t>(planner_type));

        auto buffer_planning_exp = InternalBufferPlanner::create_buffer_planning(edge_layers, planner_type,
            m_driver.dma_type(), number_of_contexts, sg_desc_params, continuous_desc_params);

        if (HAILO_CANT_MEET_BUFFER_REQUIREMENTS == buffer_planning_exp.status()) {
            LOGGER__DEBUG("Failed to meet buffer requirements for planner type.");
            continue;
        }
        TRY(auto buffer_planning, buffer_planning_exp);

        auto status = execute_plan(buffer_planning, edge_layers_executed, buffers_executed);
        if (HAILO_OUT_OF_HOST_CMA_MEMORY != status) {
            // Out of CMA-memory is ok; ignore and move on.
            CHECK_SUCCESS(status);
        }

        if (first_pass) {
            default_planner_report = InternalBufferPlanner::report_planning_info(buffers_executed);
        }

        for (const auto &edge_layer_key : edge_layers_executed) {
            edge_layers.erase(edge_layer_key);
        }
        edge_layers_executed.clear();

        if (edge_layers.empty()) {
            break;
        }

        LOGGER__DEBUG("Execute of plan didn't finish. Using next planner");

        first_pass = false;
        planner_type = static_cast<InternalBufferPlanner::Type>((static_cast<uint8_t>(planner_type)) + 1);

        CHECK(InternalBufferPlanner::Type::INVALID != planner_type, HAILO_CANT_MEET_BUFFER_REQUIREMENTS,
            "Cannot find an executable buffer planning for the given edge layers");
    }

    LOGGER__DEBUG("Execute finished successfully");

    const auto executed_buffers_report = InternalBufferPlanner::report_planning_info(buffers_executed);
    print_execution_results(default_planner_report, first_pass, executed_buffers_report);

    return HAILO_SUCCESS;
}

hailo_status InternalBufferManager::execute_plan(InternalBufferPlanning &buffer_planning,
    std::vector<EdgeLayerKey> &edge_layers_executed, InternalBufferPlanning &buffers_executed)
{
    auto execution_status = HAILO_SUCCESS;

    // Go over plan and create buffers
    for (auto &buffer_plan : buffer_planning) {
        auto buffer_exp = create_intermediate_buffer(buffer_plan.buffer_type, buffer_plan.buffer_size);
        if (buffer_exp.status() == HAILO_OUT_OF_HOST_CMA_MEMORY) {
            execution_status = buffer_exp.status();
            // If one of the buffer failed due to lack to memory, try to move to next buffer.
            continue;
        }
        TRY(auto buffer, buffer_exp);

        for (const auto &edge_layer_plan : buffer_plan.edge_layer_plans) {
            m_edge_layer_to_buffer_map.emplace(
                edge_layer_plan.key,
                EdgeLayerBuffer{buffer, edge_layer_plan});

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
