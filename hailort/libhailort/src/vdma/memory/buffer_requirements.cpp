/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file buffer_requirements.cpp
 **/

#include "buffer_requirements.hpp"
#include "vdma/memory/descriptor_list.hpp"
#include "utils.h"

#include <numeric>

namespace hailort {
namespace vdma {

// Minimum size of ccb buffers in descriptors, taken from the CCB spec.
static constexpr uint32_t MIN_CCB_DESCRIPTORS_COUNT = 16;

Expected<BufferSizesRequirements> BufferSizesRequirements::get_sg_buffer_requirements_single_transfer(
    uint16_t max_desc_page_size, uint16_t min_batch_size, uint16_t max_batch_size, uint32_t transfer_size,
    bool is_circular, const bool force_default_page_size, const bool force_batch_size, const bool is_vdma_aligned_buffer)
{
    // First, get the result for the min size
    auto results = get_sg_buffer_requirements_multiple_transfers(max_desc_page_size, min_batch_size,
        {transfer_size}, is_circular, force_default_page_size, force_batch_size);
    CHECK_EXPECTED(results);

    // In order to fetch all descriptors, the amount of active descs is lower by one that the amount
    // of descs given  (Otherwise we won't be able to determine if the buffer is empty or full).
    // Therefore we add 1 in order to compensate.
    uint32_t descs_per_transfer = DIV_ROUND_UP(transfer_size, results->desc_page_size());
    if (!is_vdma_aligned_buffer) {
        // Add desc for boundary channel because might need extra descriptor for user non aligned buffer async API
        descs_per_transfer++;
    }
    uint32_t descs_count = std::min((descs_per_transfer * max_batch_size) + 1, MAX_DESCS_COUNT);
    if (is_circular) {
        descs_count = get_nearest_powerof_2(descs_count, MIN_DESCS_COUNT);
    }

    return BufferSizesRequirements{ descs_count, results->desc_page_size() };
}

Expected<BufferSizesRequirements> BufferSizesRequirements::get_sg_buffer_requirements_multiple_transfers(
    uint16_t max_desc_page_size, uint16_t batch_size, const std::vector<uint32_t> &transfer_sizes,
    bool is_circular, const bool force_default_page_size, const bool force_batch_size)
{
    const uint16_t initial_desc_page_size = find_initial_desc_page_size(transfer_sizes, max_desc_page_size, force_default_page_size);

    CHECK_AS_EXPECTED(max_desc_page_size <= MAX_DESC_PAGE_SIZE, HAILO_INTERNAL_FAILURE,
        "max_desc_page_size given {} is bigger than hw max desc page size {}",
            max_desc_page_size, MAX_DESC_PAGE_SIZE);
    CHECK_AS_EXPECTED(MIN_DESC_PAGE_SIZE <= max_desc_page_size, HAILO_INTERNAL_FAILURE,
        "max_desc_page_size given {} is lower that hw min desc page size {}",
            max_desc_page_size, MIN_DESC_PAGE_SIZE);

    const uint16_t min_desc_page_size = MIN_DESC_PAGE_SIZE;
    CHECK_AS_EXPECTED(initial_desc_page_size <= max_desc_page_size, HAILO_INTERNAL_FAILURE,
        "Initial descriptor page size ({}) is larger than maximum descriptor page size ({})",
        initial_desc_page_size, max_desc_page_size);
    CHECK_AS_EXPECTED(initial_desc_page_size >= min_desc_page_size, HAILO_INTERNAL_FAILURE,
        "Initial descriptor page size ({}) is smaller than minimum descriptor page size ({})",
        initial_desc_page_size, min_desc_page_size);
    CHECK_AS_EXPECTED(MAX_DESCS_COUNT >= get_required_descriptor_count(transfer_sizes, max_desc_page_size),
        HAILO_OUT_OF_DESCRIPTORS,
        "Network shapes exceeds driver descriptors capabilities."
        "Minimal descriptors count: {}, max allowed on the driver: {}."
        "(A common cause for this error could be the large transfer size - which is {}).",
        get_required_descriptor_count(transfer_sizes, max_desc_page_size), (MAX_DESCS_COUNT - 1),
        std::accumulate(transfer_sizes.begin(), transfer_sizes.end(), 0));

    // Defined as uint32_t to prevent overflow (as we multiply it by two in each iteration of the while loop bellow)
    uint32_t local_desc_page_size = initial_desc_page_size;

    uint32_t descs_count = get_required_descriptor_count(transfer_sizes, initial_desc_page_size);
    // Too many descriptors; try a larger desc_page_size which will lead to less descriptors used
    while ((descs_count * batch_size) > (MAX_DESCS_COUNT - 1)) {
        CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(local_desc_page_size << 1), HAILO_INTERNAL_FAILURE,
            "Descriptor page size needs to fit in 16B");
        local_desc_page_size = static_cast<uint16_t>(local_desc_page_size << 1);

        if (local_desc_page_size > max_desc_page_size) {
            if (force_batch_size) {
                LOGGER__ERROR("Network shapes and batch size exceeds driver descriptors capabilities. "
                "Required descriptors count: {}, max allowed on the driver: {}. "
                "(A common cause for this error could be the batch size - which is {}).",
                (batch_size * descs_count), (MAX_DESCS_COUNT - 1), batch_size);
                return make_unexpected(HAILO_OUT_OF_DESCRIPTORS);
            } else {
                // If not forcing minimum batch (It's acceptable to run infer on lower batch instead of returning error)
                // once reached over the max page size, stop
                local_desc_page_size = max_desc_page_size;
                descs_count = get_required_descriptor_count(transfer_sizes, static_cast<uint16_t>(local_desc_page_size));
                break;
            }
        }

        descs_count = get_required_descriptor_count(transfer_sizes, static_cast<uint16_t>(local_desc_page_size));
    }

    // Found desc_page_size and descs_count
    const auto desc_page_size = static_cast<uint16_t>(local_desc_page_size);
    if (initial_desc_page_size != desc_page_size) {
        LOGGER__WARNING("Desc page size value ({}) is not optimal for performance.", desc_page_size);
    }

    if (is_circular) {
        // The length of a descriptor list is always a power of 2. Therefore, on circular buffers the hw will have to
        // access all descriptors.
        descs_count = get_nearest_powerof_2(descs_count, MIN_DESCS_COUNT);
        CHECK_AS_EXPECTED(descs_count <= MAX_DESCS_COUNT, HAILO_OUT_OF_DESCRIPTORS);
    }

    return BufferSizesRequirements{descs_count, desc_page_size};
}

Expected<BufferSizesRequirements> BufferSizesRequirements::get_ccb_buffer_requirements_single_transfer(uint16_t batch_size,
    uint32_t transfer_size, bool is_circular)
{
    const uint16_t desc_page_size = DEFAULT_DESC_PAGE_SIZE;
    const auto desc_per_transfer = DIV_ROUND_UP(transfer_size, desc_page_size);
    auto descs_count = desc_per_transfer * batch_size;
    descs_count = std::max(descs_count, MIN_CCB_DESCRIPTORS_COUNT);
    if (is_circular) {
        // The first 12 channels in D2H CCB ("regular channels") requires that the amount of descriptors will be a power
        // of 2.
        // We can optimize it by checking that channel index is one of the last 4 channels ("enhanced channels"), or
        // even allocate those indexes.
        // Meanwhile however, we always use power of 2
        descs_count = get_nearest_powerof_2(descs_count, MIN_CCB_DESCRIPTORS_COUNT);
    }

    return BufferSizesRequirements{descs_count, desc_page_size};
}


uint16_t BufferSizesRequirements::find_initial_desc_page_size(const std::vector<uint32_t> &transfer_sizes,
    const uint16_t max_desc_page_size, const bool force_default_page_size)
{
    const uint16_t channel_max_page_size = std::min(DEFAULT_DESC_PAGE_SIZE, max_desc_page_size);
    const auto max_transfer_size = *std::max_element(transfer_sizes.begin(), transfer_sizes.end());
    // Note: If the pages pointed to by the descriptors are copied in their entirety, then DEFAULT_DESC_PAGE_SIZE
    //       is the optimal value. For transfer_sizes smaller than DEFAULT_DESC_PAGE_SIZE using smaller descriptor page
    //       sizes will save memory consuption without harming performance. In the case of nms for example, only one bbox
    //       is copied from each page. Hence, we'll use MIN_DESC_PAGE_SIZE for nms.
    const auto optimize_low_page_size = ((channel_max_page_size > max_transfer_size) && !force_default_page_size);
    const uint16_t initial_desc_page_size = optimize_low_page_size ?
        static_cast<uint16_t>(get_nearest_powerof_2(max_transfer_size, MIN_DESC_PAGE_SIZE)) :
        channel_max_page_size;
    if (channel_max_page_size != initial_desc_page_size) {
        LOGGER__INFO("Using non-default initial_desc_page_size of {}, due to a small transfer size ({})",
            initial_desc_page_size, max_transfer_size);
    }
    return initial_desc_page_size;
}

uint32_t BufferSizesRequirements::get_required_descriptor_count(const std::vector<uint32_t> &transfer_sizes,
    uint16_t desc_page_size)
{
    uint32_t desc_count = 0;
    for (auto &transfer_size : transfer_sizes) {
        desc_count += DIV_ROUND_UP(transfer_size, desc_page_size);
    }
    // One extra descriptor is needed, because the amount of available descriptors is (desc_count - 1)
    return desc_count + 1;
}

} /* namespace vdma */
} /* namespace hailort */
