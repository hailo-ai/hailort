/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file descriptor_list.hpp
 * @brief Allocates a list of buffer descriptors used for VDMA
 **/

#ifndef _HAILO_VDMA_DESCRIPTOR_LIST_HPP_
#define _HAILO_VDMA_DESCRIPTOR_LIST_HPP_

#include "hailo/expected.hpp"
#include "hailo/hailort_common.hpp"

#include "common/utils.hpp"

#include "vdma/channel/channel_id.hpp"
#include "vdma/memory/mapped_buffer.hpp"
#include "vdma/driver/hailort_driver.hpp"


namespace hailort {
namespace vdma {


#define MAX_SG_DESCS_COUNT (64 * 1024u)
#define MIN_SG_DESCS_COUNT (2u)
#define DEFAULT_DESC_COUNT (64 * 1024u)

static_assert(is_powerof2(MAX_SG_DESCS_COUNT), "MAX_SG_DESCS_COUNT must be a power of 2");
static_assert(is_powerof2(MIN_SG_DESCS_COUNT), "MIN_SG_DESCS_COUNT must be a power of 2");
static_assert(is_powerof2(DEFAULT_DESC_COUNT), "DEFAULT_DESC_COUNT must be a power of 2");
static_assert(DEFAULT_DESC_COUNT <= MAX_SG_DESCS_COUNT && DEFAULT_DESC_COUNT >= MIN_SG_DESCS_COUNT,
    "DEFAULT_DESC_COUNT not in range");

// From PLDA's vDMA controller reference:
// - Addresses of pages pointed to by vDMA descriptors need to be on a 64B boundary.
//   Hence, we require a minimum page size of 64B.
// - G_PAGE_SIZE_MAX dictates the maximum desc page size:
//     max_page_size = 2 ^ (G_PAGE_SIZE_MAX - 1)
//   In our case max_page_size = 2 ^ (13 - 1) = 4096
static constexpr uint16_t MIN_SG_PAGE_SIZE = 64;
static constexpr uint16_t MAX_SG_PAGE_SIZE = 4096;
static constexpr uint16_t DEFAULT_SG_PAGE_SIZE = 512;

static_assert(is_powerof2(MIN_SG_PAGE_SIZE), "MIN_SG_PAGE_SIZE must be a power of 2");
static_assert(MIN_SG_PAGE_SIZE > 0, "MIN_SG_PAGE_SIZE must be larger then 0");
static_assert(is_powerof2(MAX_SG_PAGE_SIZE), "MAX_SG_PAGE_SIZE must be a power of 2");
static_assert(MAX_SG_PAGE_SIZE > 0, "MAX_SG_PAGE_SIZE must be larger then 0");
static_assert(is_powerof2(DEFAULT_SG_PAGE_SIZE), "DEFAULT_SG_PAGE_SIZE must be a power of 2");
static_assert(DEFAULT_SG_PAGE_SIZE > 0, "DEFAULT_SG_PAGE_SIZE must be larger then 0");

static constexpr size_t SINGLE_DESCRIPTOR_SIZE = 0x10;
static constexpr size_t DESCRIPTOR_LIST_ALIGN = 1 << 16;


class DescriptorList
{
public:
    static Expected<DescriptorList> create(uint32_t desc_count, uint16_t desc_page_size, bool is_circular,
        HailoRTDriver &driver);

    ~DescriptorList();

    DescriptorList(const DescriptorList &other) = delete;
    DescriptorList &operator=(const DescriptorList &other) = delete;
    DescriptorList(DescriptorList &&other) noexcept;
    DescriptorList &operator=(DescriptorList &&other) = delete;

    uint32_t count() const
    {
        return m_desc_count;
    }

    uint64_t dma_address() const
    {
        return m_desc_list_info.dma_address;
    }

    uint16_t desc_page_size() const
    {
        return m_desc_page_size;
    }

    uintptr_t handle() const
    {
        return m_desc_list_info.handle;
    }

    uint16_t max_transfers(uint32_t transfer_size, bool include_bounce_buffer = false) const
    {
        const auto descs_needed = descriptors_in_buffer(transfer_size) + (include_bounce_buffer ? 1 : 0);
        // We need to keep at least 1 free desc at all time.
        return static_cast<uint16_t>((count() - 1) / descs_needed);
    }

    // Map descriptors starting at offset to the start of buffer, wrapping around the descriptor list as needed
    // On hailo8, we allow configuring buffer without specific channel index (default is INVALID_VDMA_CHANNEL_INDEX).
    hailo_status program(MappedBuffer& buffer, size_t buffer_size, size_t buffer_offset,
        ChannelId channel_id, uint32_t starting_desc = 0,
        uint32_t batch_size = 1,
        bool should_bind = true,
        InterruptsDomain last_desc_interrupts = InterruptsDomain::NONE,
        uint32_t stride = 0);

    uint32_t descriptors_in_buffer(size_t buffer_size) const;
    static uint32_t descriptors_in_buffer(size_t buffer_size, uint16_t desc_page_size);
    static uint32_t calculate_descriptors_count(uint32_t buffer_size, uint16_t batch_size, uint16_t desc_page_size);

    /**
     * Returns the size of the buffer needed to allocate the descriptors list.
     */
    static size_t descriptors_buffer_allocation_size(uint32_t desc_count);

private:
    DescriptorList(uint32_t desc_count, uint16_t desc_page_size, bool is_circular, HailoRTDriver &driver,
        hailo_status &status);

    DescriptorsListInfo m_desc_list_info;
    const uint32_t m_desc_count;
    const bool m_is_circular;
    HailoRTDriver &m_driver;
    const uint16_t m_desc_page_size;
};

} /* namespace vdma */
} /* namespace hailort */

#endif //_HAILO_VDMA_DESCRIPTOR_LIST_HPP_