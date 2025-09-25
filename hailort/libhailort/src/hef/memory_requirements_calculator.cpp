/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file memory_requirements_calculator.cpp
 **/

#include "memory_requirements_calculator.hpp"
#include "common/utils.hpp"

#include "hef/hef_internal.hpp"
#include "core_op/resource_manager/internal_buffer_planner.hpp"
#include "core_op/resource_manager/config_buffer.hpp"
#include "common/internal_env_vars.hpp"
#include "vdma/memory/buffer_requirements.hpp"
#include "vdma/memory/descriptor_list.hpp"

#include <numeric>

namespace hailort
{

static EdgeTypeMemoryRequirements join_requirements(const EdgeTypeMemoryRequirements &a, const EdgeTypeMemoryRequirements &b)
{
    return EdgeTypeMemoryRequirements{a.cma_memory + b.cma_memory,
                                     a.cma_memory_for_descriptors + b.cma_memory_for_descriptors,
                                     a.pinned_memory + b.pinned_memory};
}

static Expected<EdgeTypeMemoryRequirements> get_context_cfg_requirements(const ContextMetadata &context_metadata,
    HailoRTDriver::DmaType dma_type)
{
    EdgeTypeMemoryRequirements requirment{};
    for (const auto &cfg_info : context_metadata.config_buffers_info()) {
        TRY(auto requirements, ConfigBuffer::get_buffer_requirements(cfg_info.second, dma_type, vdma::MAX_SG_PAGE_SIZE));
        if (ConfigBuffer::should_use_ccb(dma_type)) {
            requirment.cma_memory += requirements.buffer_size();
        } else {
            requirment.pinned_memory += requirements.buffer_size();
            requirment.cma_memory_for_descriptors +=
                vdma::DescriptorList::descriptors_buffer_allocation_size(requirements.descs_count());
        }
    }
    return requirment;
}

// Gets the memory requirements for the configuration buffers (weights and layer configurations)
static Expected<EdgeTypeMemoryRequirements> get_cfg_requirements(const CoreOpMetadata &core_op_metadata,
    HailoRTDriver::DmaType dma_type)
{
    TRY(auto requirment, get_context_cfg_requirements(core_op_metadata.preliminary_context(), dma_type));
    for (const auto& context : core_op_metadata.dynamic_contexts()) {
        TRY(auto context_requirment, get_context_cfg_requirements(context, dma_type));
        requirment = join_requirements(requirment, context_requirment);
    }
    return requirment;
}

// Gets the memory requirements for intermediate buffers (including inter-context and ddr buffers)
static Expected<EdgeTypeMemoryRequirements> get_intermediate_requirements(const CoreOpMetadata &core_op_metadata,
    uint16_t batch_size, HailoRTDriver::DmaType dma_type)
{
    batch_size = (batch_size == HAILO_DEFAULT_BATCH_SIZE) ? 1 : batch_size;
    TRY(auto plan, InternalBufferPlanner::create_buffer_planning(core_op_metadata, batch_size,
        InternalBufferPlanner::Type::SINGLE_BUFFER_PER_BUFFER_TYPE, dma_type, vdma::MAX_SG_PAGE_SIZE));
    auto report = InternalBufferPlanner::report_planning_info(plan);
    return EdgeTypeMemoryRequirements{report.cma_memory, report.cma_memory_for_descriptors, report.pinned_memory};
}

// Gets the memory requirements for a single model
static Expected<MemoryRequirements> get_model_memory_requirements(const CoreOpMetadata &core_op_metadata, uint16_t batch_size,
    HailoRTDriver::DmaType dma_type)
{
    TRY(auto intermediate, get_intermediate_requirements(core_op_metadata, batch_size, dma_type));
    TRY(auto config, get_cfg_requirements(core_op_metadata, dma_type));
    return MemoryRequirements{intermediate, config};
}

static Expected<HailoRTDriver::DmaType> get_dma_type(Hef &hef)
{
    TRY(auto hef_arch, hef.get_hef_device_arch());
    switch (hef_arch) {
    case HAILO_ARCH_HAILO8_A0:
    case HAILO_ARCH_HAILO8:
    case HAILO_ARCH_HAILO8L:
        return HailoRTDriver::DmaType::PCIE;
    case HAILO_ARCH_HAILO15H:
    case HAILO_ARCH_HAILO15L:
    case HAILO_ARCH_HAILO15M:
    case HAILO_ARCH_HAILO10H:
    case HAILO_ARCH_MARS:
        return HailoRTDriver::DmaType::DRAM;
    case HAILO_ARCH_MAX_ENUM:
        break;
    };

    LOGGER__ERROR("Unsupported Hailo device architecture: {}", static_cast<int>(hef_arch));
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

Expected<FullMemoryRequirements> MemoryRequirementsCalculator::get_memory_requirements(
    const std::vector<MemoryRequirementsCalculator::HefParams> &models)
{
    FullMemoryRequirements full_memory_requirements{};
    for (const auto &model : models) {
        TRY(auto hef, Hef::create(model.hef_path));
        TRY(const auto network_pair, hef.pimpl->get_network_group_and_network_name(model.network_group_name));
        TRY(auto core_op_metadata, hef.pimpl->get_core_op_metadata(network_pair.first));

        TRY(const auto dma_type, get_dma_type(hef));
        TRY(auto req, get_model_memory_requirements(*core_op_metadata, model.batch_size, dma_type));
        full_memory_requirements.hefs_memory_requirements.push_back(req);

        // Add to total
        full_memory_requirements.total_memory_requirements.intermediate_buffers = join_requirements(
            full_memory_requirements.total_memory_requirements.intermediate_buffers, req.intermediate_buffers);
        full_memory_requirements.total_memory_requirements.config_buffers = join_requirements(
            full_memory_requirements.total_memory_requirements.config_buffers, req.config_buffers);
    }

    return full_memory_requirements;
}

} /* namespace hailort */