/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file buffer_requirements.cpp
 **/

#include "buffer_requirements.hpp"
#include "vdma/memory/descriptor_list.hpp"
#include "vdma/memory/continuous_edge_layer.hpp"
#include "utils.h"
#include "common/internal_env_vars.hpp"

#include <numeric>

namespace hailort {
namespace vdma {

Expected<BufferSizesRequirements> BufferSizesRequirements::get_buffer_requirements_for_boundary_channels(
    HailoRTDriver &driver, uint32_t max_shmifo_size, uint16_t min_active_trans, uint16_t max_active_trans,
    uint32_t transfer_size)
{
    // TODO: Simplify this code + get rid of the for loop (?) (HRT-14822)
    // We'll first try to use the default page size. Next we'll try to increase the page size until we find a valid
    // page size that fits the requirements.
    // uint32_t to avoid overflow
    uint32_t max_page_size = is_env_variable_on(HAILO_LEGACY_BOUNDARY_CHANNEL_PAGE_SIZE_ENV_VAR) ?
        driver.desc_max_page_size() : std::min(DEFAULT_SG_PAGE_SIZE, driver.desc_max_page_size());
    while (true) {
        if (max_page_size > driver.desc_max_page_size()) {
            // We exceeded the driver's max page size
            return make_unexpected(HAILO_CANT_MEET_BUFFER_REQUIREMENTS);
        }

        if (max_page_size == max_shmifo_size) {
            // Hack to reduce max page size if the driver page size is equal to stream size.
            // In this case page size == stream size is invalid solution.
            // TODO - remove this WA after HRT-11747
            max_page_size /= 2;
        }

        const auto DONT_FORCE_DEFAULT_PAGE_SIZE = false;
        const auto DONT_FORCE_BATCH_SIZE = false;
        static const bool IS_CIRCULAR = true;
        static const bool IS_VDMA_ALIGNED_BUFFER = false;
        static const bool IS_NOT_DDR = false;
        auto buffer_sizes_requirements_exp = vdma::BufferSizesRequirements::get_buffer_requirements_single_transfer(
            vdma::VdmaBuffer::Type::SCATTER_GATHER, static_cast<uint16_t>(max_page_size), min_active_trans,
            max_active_trans, transfer_size, IS_CIRCULAR, DONT_FORCE_DEFAULT_PAGE_SIZE, DONT_FORCE_BATCH_SIZE,
            IS_VDMA_ALIGNED_BUFFER, IS_NOT_DDR);
        if (HAILO_SUCCESS == buffer_sizes_requirements_exp.status()) {
            // We found a valid page size
            const auto desc_page_size = buffer_sizes_requirements_exp->desc_page_size();
            const auto descs_count = buffer_sizes_requirements_exp->descs_count();
            return BufferSizesRequirements(descs_count, desc_page_size);
        } else if (HAILO_CANT_MEET_BUFFER_REQUIREMENTS == buffer_sizes_requirements_exp.status()) {
            // If we can't meet the requirements, try to double the page size and try again
            max_page_size <<= static_cast<uint32_t>(1);
        } else {
            // Unexpected error
            return buffer_sizes_requirements_exp;
        }
    }
}

Expected<BufferSizesRequirements> BufferSizesRequirements::get_buffer_requirements_multiple_transfers(
    vdma::VdmaBuffer::Type buffer_type, uint16_t max_desc_page_size, uint16_t batch_size,
    const std::vector<uint32_t> &transfer_sizes, bool is_circular, bool force_default_page_size,
    bool force_batch_size, bool is_ddr)
{
    const uint32_t MAX_DESCS_COUNT = (buffer_type == vdma::VdmaBuffer::Type::SCATTER_GATHER) ?
        MAX_SG_DESCS_COUNT : MAX_CCB_DESCS_COUNT;
    const uint32_t MIN_DESCS_COUNT = (buffer_type == vdma::VdmaBuffer::Type::SCATTER_GATHER) ?
        MIN_SG_DESCS_COUNT : MIN_CCB_DESCS_COUNT;
    const uint16_t MAX_PAGE_SIZE = (buffer_type == vdma::VdmaBuffer::Type::SCATTER_GATHER) ?
        MAX_SG_PAGE_SIZE : MAX_CCB_PAGE_SIZE;
    const uint16_t MIN_PAGE_SIZE = (buffer_type == vdma::VdmaBuffer::Type::SCATTER_GATHER) ?
        MIN_SG_PAGE_SIZE : MIN_CCB_PAGE_SIZE;

    const uint16_t initial_desc_page_size = find_initial_desc_page_size(buffer_type, transfer_sizes, max_desc_page_size,
        force_default_page_size, MIN_PAGE_SIZE);

    CHECK_AS_EXPECTED(max_desc_page_size <= MAX_PAGE_SIZE, HAILO_INTERNAL_FAILURE,
        "max_desc_page_size given {} is bigger than hw max desc page size {}",
            max_desc_page_size, MAX_PAGE_SIZE);
    CHECK_AS_EXPECTED(MIN_PAGE_SIZE <= max_desc_page_size, HAILO_INTERNAL_FAILURE,
        "max_desc_page_size given {} is lower that hw min desc page size {}",
            max_desc_page_size, MIN_PAGE_SIZE);

    CHECK_AS_EXPECTED(initial_desc_page_size <= max_desc_page_size, HAILO_INTERNAL_FAILURE,
        "Initial descriptor page size ({}) is larger than maximum descriptor page size ({})",
        initial_desc_page_size, max_desc_page_size);
    CHECK_AS_EXPECTED(initial_desc_page_size >= MIN_PAGE_SIZE, HAILO_INTERNAL_FAILURE,
        "Initial descriptor page size ({}) is smaller than minimum descriptor page size ({})",
        initial_desc_page_size, MIN_PAGE_SIZE);
    if (get_required_descriptor_count(transfer_sizes, max_desc_page_size, is_ddr) > MAX_DESCS_COUNT) {
        return make_unexpected(HAILO_CANT_MEET_BUFFER_REQUIREMENTS);
    }

    // Defined as uint32_t to prevent overflow (as we multiply it by two in each iteration of the while loop bellow)
    auto local_desc_page_size = static_cast<uint32_t>(initial_desc_page_size);

    auto descs_count = get_required_descriptor_count(transfer_sizes, initial_desc_page_size, is_ddr);
    // Too many descriptors; try a larger desc_page_size which will lead to less descriptors used
    while ((descs_count * batch_size) > (MAX_DESCS_COUNT - 1)) {
        CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(local_desc_page_size << 1), HAILO_INTERNAL_FAILURE,
            "Descriptor page size needs to fit in 16B");
        local_desc_page_size = static_cast<uint16_t>(local_desc_page_size << 1);

        if (local_desc_page_size > max_desc_page_size) {
            if (force_batch_size) {
                return make_unexpected(HAILO_CANT_MEET_BUFFER_REQUIREMENTS);
            } else {
                // If not forcing minimum batch (It's acceptable to run infer on lower batch instead of returning error)
                // once reached over the max page size, stop
                local_desc_page_size = max_desc_page_size;
                descs_count = get_required_descriptor_count(transfer_sizes, static_cast<uint16_t>(local_desc_page_size),
                    is_ddr);
                break;
            }
        }

        descs_count = get_required_descriptor_count(transfer_sizes, static_cast<uint16_t>(local_desc_page_size),
            is_ddr);
    }

    // Found desc_page_size and descs_count
    const auto desc_page_size = static_cast<uint16_t>(local_desc_page_size);
    if ((buffer_type == vdma::VdmaBuffer::Type::SCATTER_GATHER) && (initial_desc_page_size != desc_page_size)) {
        LOGGER__WARNING("Desc page size value ({}) is not optimal for performance.", desc_page_size);
    }

    if (is_circular) {
        // The length of a descriptor list is always a power of 2. Therefore, on circular buffers the hw will have to
        // access all descriptors.
        descs_count = get_nearest_powerof_2(descs_count, MIN_DESCS_COUNT);
        CHECK_AS_EXPECTED(descs_count <= MAX_DESCS_COUNT, HAILO_CANT_MEET_BUFFER_REQUIREMENTS);
    }

    return BufferSizesRequirements{descs_count, desc_page_size};
}

Expected<BufferSizesRequirements> BufferSizesRequirements::get_buffer_requirements_single_transfer(
    vdma::VdmaBuffer::Type buffer_type, uint16_t max_desc_page_size, uint16_t min_batch_size, uint16_t max_batch_size,
    uint32_t transfer_size, bool is_circular, bool force_default_page_size, bool force_batch_size, bool is_vdma_aligned_buffer,
    bool is_ddr)
{
    const uint32_t MAX_DESCS_COUNT = (buffer_type == vdma::VdmaBuffer::Type::SCATTER_GATHER) ?
        MAX_SG_DESCS_COUNT : MAX_CCB_DESCS_COUNT;
    const uint32_t MIN_DESCS_COUNT = (buffer_type == vdma::VdmaBuffer::Type::SCATTER_GATHER) ?
        MIN_SG_DESCS_COUNT : MIN_CCB_DESCS_COUNT;

    // First, get the result for the min size
    auto results = get_buffer_requirements_multiple_transfers(buffer_type, max_desc_page_size,
        min_batch_size, {transfer_size}, is_circular, force_default_page_size, force_batch_size, is_ddr);
    if (HAILO_CANT_MEET_BUFFER_REQUIREMENTS == results.status()) {
        // In case of failure to meet requirements, return without error printed to the prompt.
        return make_unexpected(HAILO_CANT_MEET_BUFFER_REQUIREMENTS);
    }
    CHECK_EXPECTED(results);

    uint32_t descs_per_transfer = DIV_ROUND_UP(transfer_size, results->desc_page_size());
    if (!is_vdma_aligned_buffer) {
        // Add desc for boundary channel because might need extra descriptor for user non aligned buffer async API
        descs_per_transfer++;
    }

    // In order to fetch all descriptors, the amount of active descs is lower by one that the amount
    // of descs given  (Otherwise we won't be able to determine if the buffer is empty or full).
    // Therefore we add 1 in order to compensate.
    uint32_t descs_count = std::min((descs_per_transfer * max_batch_size) + 1, static_cast<uint32_t>(MAX_DESCS_COUNT));
    descs_count = std::max(descs_count, MIN_DESCS_COUNT);
    if (is_circular) {
        descs_count = get_nearest_powerof_2(descs_count, MIN_DESCS_COUNT);
    }

    return BufferSizesRequirements{ descs_count, results->desc_page_size() };
}

uint16_t BufferSizesRequirements::find_initial_desc_page_size(
    vdma::VdmaBuffer::Type buffer_type, const std::vector<uint32_t> &transfer_sizes,
    uint16_t max_desc_page_size, bool force_default_page_size, uint16_t min_page_size)
{
    static const uint16_t DEFAULT_PAGE_SIZE = (buffer_type == vdma::VdmaBuffer::Type::SCATTER_GATHER) ?
        DEFAULT_SG_PAGE_SIZE : DEFAULT_CCB_PAGE_SIZE;
    const uint16_t channel_max_page_size = std::min(DEFAULT_PAGE_SIZE, max_desc_page_size);
    const auto max_transfer_size = *std::max_element(transfer_sizes.begin(), transfer_sizes.end());
    // Note: If the pages pointed to by the descriptors are copied in their entirety, then DEFAULT_PAGE_SIZE
    //       is the optimal value. For transfer_sizes smaller than DEFAULT_PAGE_SIZE using smaller descriptor page
    //       sizes will save memory consumption without harming performance. In the case of nms for example, only one bbox
    //       is copied from each page. Hence, we'll use min_page_size for nms.
    const auto optimize_low_page_size = ((channel_max_page_size > max_transfer_size) && !force_default_page_size);
    const uint16_t initial_desc_page_size = optimize_low_page_size ?
        static_cast<uint16_t>(get_nearest_powerof_2(max_transfer_size, min_page_size)) :
        channel_max_page_size;
    if (channel_max_page_size != initial_desc_page_size) {
        LOGGER__INFO("Using non-default initial_desc_page_size of {}, due to a small transfer size ({})",
            initial_desc_page_size, max_transfer_size);
    }
    return initial_desc_page_size;
}

uint32_t BufferSizesRequirements::get_required_descriptor_count(const std::vector<uint32_t> &transfer_sizes,
    uint16_t desc_page_size, bool is_ddr_layer)
{
    uint32_t desc_count = 0;
    for (auto &transfer_size : transfer_sizes) {
        desc_count += DIV_ROUND_UP(transfer_size, desc_page_size);
    }
    // In case of is_circular don't add extra descriptor - because amount of descs needs to be power of 2 and then
    // will round up to next power of 2 (and we anyways make sure we are smaller than MAX_DESCS which is 64K - 1)
    // Otherwise - one extra descriptor is "required", because the amount of available descriptors is (desc_count - 1)
    // TODO HRT-15875: check if we need this other descriptor in non circular cases
    return is_ddr_layer ? desc_count : (desc_count + 1);
}

} /* namespace vdma */
} /* namespace hailort */
