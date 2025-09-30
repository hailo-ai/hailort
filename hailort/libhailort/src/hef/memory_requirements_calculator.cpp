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

// Hardcoded desc params, normally taken from the driver
static DescSizesParams get_sg_desc_size_params()
{
    DescSizesParams desc_sizes_params{};
    desc_sizes_params.default_page_size = 512;
    desc_sizes_params.min_page_size = 64;
    desc_sizes_params.max_page_size = 4096;
    desc_sizes_params.min_descs_count = 2;
    desc_sizes_params.max_descs_count = 65536;
    return desc_sizes_params;
}

static DescSizesParams get_ccb_desc_size_params(uint32_t min_ccb_desc_count)
{
    DescSizesParams desc_sizes_params{};
    desc_sizes_params.default_page_size = 512;
    desc_sizes_params.min_page_size = 512;
    desc_sizes_params.max_page_size = 4096;
    desc_sizes_params.max_descs_count = 0x00040000;
    desc_sizes_params.min_descs_count = min_ccb_desc_count;
    return desc_sizes_params;
}

struct DescSizesParamsPerType {
    DescSizesParams ccb;
    DescSizesParams sg;
};

static EdgeTypeMemoryRequirements join_requirements(const EdgeTypeMemoryRequirements &a, const EdgeTypeMemoryRequirements &b)
{
    return EdgeTypeMemoryRequirements{a.cma_memory + b.cma_memory,
                                     a.cma_memory_for_descriptors + b.cma_memory_for_descriptors,
                                     a.pinned_memory + b.pinned_memory};
}

static Expected<EdgeTypeMemoryRequirements> get_context_cfg_requirements(const ContextMetadata &context_metadata,
    HailoRTDriver::DmaType dma_type, bool use_ccb, const DescSizesParamsPerType &desc_params)
{
    EdgeTypeMemoryRequirements requirments{};
    const auto desc_sizes_params = use_ccb ? desc_params.ccb : desc_params.sg;
    for (const auto &cfg_info : context_metadata.config_buffers_info()) {
        TRY(auto requirements, CopiedConfigBuffer::get_buffer_requirements(cfg_info.second, dma_type, desc_sizes_params));
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
    HailoRTDriver::DmaType dma_type, bool zero_copy_config_over_descs, const DescSizesParamsPerType &desc_params)
{
    const bool use_ccb = CopiedConfigBuffer::should_use_ccb(dma_type) && !zero_copy_config_over_descs;
    TRY(auto requirments, get_context_cfg_requirements(core_op_metadata.preliminary_context(), dma_type, use_ccb,
        desc_params));
    for (const auto& context : core_op_metadata.dynamic_contexts()) {
        TRY(auto context_requirments, get_context_cfg_requirements(context, dma_type, use_ccb, desc_params));
        requirments = join_requirements(requirments, context_requirments);
    }
    return requirments;
}

// Gets the memory requirements for intermediate buffers (including inter-context and ddr buffers)
static Expected<EdgeTypeMemoryRequirements> get_intermediate_requirements(const CoreOpMetadata &core_op_metadata,
    uint16_t batch_size, HailoRTDriver::DmaType dma_type, const DescSizesParamsPerType &desc_params)
{
    batch_size = (batch_size == HAILO_DEFAULT_BATCH_SIZE) ? 1 : batch_size;
    TRY(auto plan, InternalBufferPlanner::create_buffer_planning(core_op_metadata, batch_size,
        InternalBufferPlanner::Type::SINGLE_BUFFER_PER_BUFFER_TYPE, dma_type, desc_params.sg, desc_params.ccb));
    auto report = InternalBufferPlanner::report_planning_info(plan);
    return EdgeTypeMemoryRequirements{report.cma_memory, report.cma_memory_for_descriptors, report.pinned_memory};
}

static Expected<DescSizesParamsPerType> get_desc_params(Hef &hef)
{
    TRY(auto hef_archs, hef.get_compatible_device_archs());
    const auto hef_arch = hef_archs.at(0); // take the first, since dma type is defined by chip generation


    uint32_t min_ccb_desc_count = 0;
    switch (hef_arch) {
    case HAILO_ARCH_HAILO15H:
    case HAILO_ARCH_HAILO15L:
    case HAILO_ARCH_HAILO15M:
    case HAILO_ARCH_HAILO10H:
        min_ccb_desc_count = 16;
        break;
    case HAILO_ARCH_MARS:
        min_ccb_desc_count = 32;
        break;
    default:
        LOGGER__ERROR("Unsupported HEF architecture: {}", static_cast<int>(hef_arch));
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }

    return DescSizesParamsPerType{get_ccb_desc_size_params(min_ccb_desc_count), get_sg_desc_size_params()};
}

// Gets the memory requirements for a single model
static Expected<MemoryRequirements> get_model_memory_requirements(const CoreOpMetadata &core_op_metadata, uint16_t batch_size,
    HailoRTDriver::DmaType dma_type, bool zero_copy_config_over_descs, const DescSizesParamsPerType &desc_params)
{
    TRY(auto intermediate, get_intermediate_requirements(core_op_metadata, batch_size, dma_type, desc_params));
    TRY(auto config, get_cfg_requirements(core_op_metadata, dma_type, zero_copy_config_over_descs, desc_params));
    return MemoryRequirements{intermediate, config};
}

static Expected<HailoRTDriver::DmaType> get_dma_type(Hef &hef)
{
    TRY(auto hef_archs, hef.get_compatible_device_archs());
    const auto hef_arch = hef_archs.at(0); // take the first, since dma type is defined by chip generation
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
        TRY(auto desc_params, get_desc_params(hef));
        TRY(auto req, get_model_memory_requirements(*core_op_metadata, model.batch_size, dma_type,
                hef.pimpl->zero_copy_config_over_descs(), desc_params));
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