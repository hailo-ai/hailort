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
    HailoRTDriver::DmaType dma_type, bool use_ccb, HailoRTDriver::DeviceBoardType board_type)
{
    EdgeTypeMemoryRequirements requirments{};
    for (const auto &cfg_info : context_metadata.config_buffers_info()) {
        TRY(auto requirements, CopiedConfigBuffer::get_buffer_requirements(cfg_info.second, dma_type, vdma::MAX_SG_PAGE_SIZE,
            board_type));
        if (use_ccb) {
            requirments.cma_memory += requirements.buffer_size();
        } else {
            requirments.pinned_memory += requirements.buffer_size();
            requirments.cma_memory_for_descriptors +=
                vdma::DescriptorList::descriptors_buffer_allocation_size(requirements.descs_count());
        }
    }
    return requirments;
}

// Gets the memory requirements for the configuration buffers (weights and layer configurations)
static Expected<EdgeTypeMemoryRequirements> get_cfg_requirements(const CoreOpMetadata &core_op_metadata,
    HailoRTDriver::DmaType dma_type, bool zero_copy_config_over_descs, HailoRTDriver::DeviceBoardType board_type)
{
    const bool use_ccb = CopiedConfigBuffer::should_use_ccb(dma_type) && !zero_copy_config_over_descs;
    TRY(auto requirments, get_context_cfg_requirements(core_op_metadata.preliminary_context(), dma_type, use_ccb,
        board_type));
    for (const auto& context : core_op_metadata.dynamic_contexts()) {
        TRY(auto context_requirments, get_context_cfg_requirements(context, dma_type, use_ccb, board_type));
        requirments = join_requirements(requirments, context_requirments);
    }
    return requirments;
}

// Gets the memory requirements for intermediate buffers (including inter-context and ddr buffers)
static Expected<EdgeTypeMemoryRequirements> get_intermediate_requirements(const CoreOpMetadata &core_op_metadata,
    uint16_t batch_size, HailoRTDriver::DmaType dma_type, HailoRTDriver::DeviceBoardType board_type)
{
    batch_size = (batch_size == HAILO_DEFAULT_BATCH_SIZE) ? 1 : batch_size;
    TRY(auto plan, InternalBufferPlanner::create_buffer_planning(core_op_metadata, batch_size,
        InternalBufferPlanner::Type::SINGLE_BUFFER_PER_BUFFER_TYPE, dma_type, vdma::MAX_SG_PAGE_SIZE, board_type));
    auto report = InternalBufferPlanner::report_planning_info(plan);
    return EdgeTypeMemoryRequirements{report.cma_memory, report.cma_memory_for_descriptors, report.pinned_memory};
}

static Expected<HailoRTDriver::DeviceBoardType> hef_arch_to_board_type(hailo_device_architecture_t hef_arch)
{
    switch (hef_arch) {
    case HAILO_ARCH_HAILO8_A0:
    case HAILO_ARCH_HAILO8:
    case HAILO_ARCH_HAILO8L:
        return HailoRTDriver::DeviceBoardType::DEVICE_BOARD_TYPE_HAILO8;
    case HAILO_ARCH_HAILO15H:
    case HAILO_ARCH_HAILO15L:
    case HAILO_ARCH_HAILO15M:
        return HailoRTDriver::DeviceBoardType::DEVICE_BOARD_TYPE_HAILO15;
    case HAILO_ARCH_HAILO10H:
        return HailoRTDriver::DeviceBoardType::DEVICE_BOARD_TYPE_HAILO10H;
    case HAILO_ARCH_MARS:
        return HailoRTDriver::DeviceBoardType::DEVICE_BOARD_TYPE_MARS;
    default:
        break;
    }
    LOGGER__ERROR("Unsupported HEF architecture: {}", static_cast<int>(hef_arch));
    return make_unexpected(HAILO_INVALID_ARGUMENT);
}

// Gets the memory requirements for a single model
static Expected<MemoryRequirements> get_model_memory_requirements(const CoreOpMetadata &core_op_metadata, uint16_t batch_size,
    HailoRTDriver::DmaType dma_type, bool zero_copy_config_over_descs, HailoRTDriver::DeviceBoardType board_type)
{
    TRY(auto intermediate, get_intermediate_requirements(core_op_metadata, batch_size, dma_type, board_type));
    TRY(auto config, get_cfg_requirements(core_op_metadata, dma_type, zero_copy_config_over_descs, board_type));
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
    // If hef supports shared_ccw, store it here to calculate the ccw once
    std::unordered_set<std::string> hefs_with_shared_ccw;

    FullMemoryRequirements full_memory_requirements{};
    for (const auto &model : models) {
        TRY(auto hef, Hef::create(model.hef_path));
        TRY(const auto network_pair, hef.pimpl->get_network_group_and_network_name(model.network_group_name));
        TRY(auto core_op_metadata, hef.pimpl->get_core_op_metadata(network_pair.first));

        TRY(const auto dma_type, get_dma_type(hef));
        TRY(auto hef_arch, hef.get_hef_device_arch());
        TRY(auto board_type, hef_arch_to_board_type(hef_arch));
        TRY(auto req, get_model_memory_requirements(*core_op_metadata, model.batch_size, dma_type,
                hef.pimpl->zero_copy_config_over_descs(), board_type));
        full_memory_requirements.hefs_memory_requirements.push_back(req);

        // Add intermediate to total
        full_memory_requirements.total_memory_requirements.intermediate_buffers = join_requirements(
            full_memory_requirements.total_memory_requirements.intermediate_buffers, req.intermediate_buffers);


        if (!hef.pimpl->zero_copy_config_over_descs()) {
            // Add config buffers to total
            full_memory_requirements.total_memory_requirements.config_buffers = join_requirements(
                full_memory_requirements.total_memory_requirements.config_buffers, req.config_buffers);
        } else {
            // If aligned ccw, add the ccw_section (but do it only once per hef)
            if (hefs_with_shared_ccw.end() == hefs_with_shared_ccw.find(model.hef_path)) {
                hefs_with_shared_ccw.insert(model.hef_path);
                const auto total_ccw_size = hef.pimpl->get_ccws_section_size();
                full_memory_requirements.total_memory_requirements.config_buffers.pinned_memory += total_ccw_size;
            }

            // Anyway, add the descriptors to the total
            full_memory_requirements.total_memory_requirements.config_buffers.cma_memory_for_descriptors +=
                req.config_buffers.cma_memory_for_descriptors;
        }
    }

    return full_memory_requirements;
}

} /* namespace hailort */