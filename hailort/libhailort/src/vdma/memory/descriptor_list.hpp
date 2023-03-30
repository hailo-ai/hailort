/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file descriptor_list.hpp
 * @brief Allocates a list of buffer descriptors used for VDMA
 **/

#ifndef _HAILO_VDMA_DESCRIPTOR_LIST_HPP_
#define _HAILO_VDMA_DESCRIPTOR_LIST_HPP_

#include "hailo/expected.hpp"
#include "hailo/dma_mapped_buffer.hpp"

#include "common/utils.hpp"

#include "vdma/channel/channel_id.hpp"
#include "os/hailort_driver.hpp"
#include "os/mmap_buffer.hpp"


namespace hailort {
namespace vdma {


#define MAX_DESCS_COUNT (64 * 1024u)
#define MIN_DESCS_COUNT (2u)
#define DEFAULT_DESC_COUNT (64 * 1024u)

static_assert(is_powerof2(MAX_DESCS_COUNT), "MAX_DESCS_COUNT must be a power of 2");
static_assert(is_powerof2(MIN_DESCS_COUNT), "MIN_DESCS_COUNT must be a power of 2");
static_assert(is_powerof2(DEFAULT_DESC_COUNT), "DEFAULT_DESC_COUNT must be a power of 2");
static_assert(DEFAULT_DESC_COUNT <= MAX_DESCS_COUNT && DEFAULT_DESC_COUNT >= MIN_DESCS_COUNT,
    "DEFAULT_DESC_COUNT not in range");

// From PLDA's vDMA controller reference:
// - Addresses of pages pointed to by vDMA descriptors need to be on a 64B boundry.
//   Hence, we require a minimum page size of 64B.
// - G_PAGE_SIZE_MAX dictates the maximum desc page size:
//     max_page_size = 2 ^ (G_PAGE_SIZE_MAX - 1)
//   In our case max_page_size = 2 ^ (13 - 1) = 4096
#define MIN_DESC_PAGE_SIZE (64u)
// TODO: Calculate from G_PAGE_SIZE_MAX (I.e. read the reg etc.)
#define MAX_DESC_PAGE_SIZE (4096u)
static constexpr uint16_t DEFAULT_DESC_PAGE_SIZE = 512;

static_assert(is_powerof2(MIN_DESC_PAGE_SIZE), "MIN_DESC_PAGE_SIZE must be a power of 2");
static_assert(MIN_DESC_PAGE_SIZE > 0, "MIN_DESC_PAGE_SIZE must be larger then 0");
static_assert(is_powerof2(MAX_DESC_PAGE_SIZE), "MAX_DESC_PAGE_SIZE must be a power of 2");
static_assert(MAX_DESC_PAGE_SIZE > 0, "MAX_DESC_PAGE_SIZE must be larger then 0");
static_assert(is_powerof2(DEFAULT_DESC_PAGE_SIZE), "DEFAULT_DESC_PAGE_SIZE must be a power of 2");
static_assert(DEFAULT_DESC_PAGE_SIZE > 0, "DEFAULT_DESC_PAGE_SIZE must be larger then 0");


struct VdmaDescriptor 
{
    uint32_t PageSize_DescControl;
    uint32_t AddrL_rsvd_DataID;
    uint32_t AddrH;
    uint32_t RemainingPageSize_Status;
};

enum class InterruptsDomain 
{
    NONE    = 0,
    DEVICE  = 1 << 0,
    HOST    = 1 << 1,
    BOTH    = DEVICE | HOST
};

inline bool host_interuptes_enabled(InterruptsDomain interrupts_domain)
{
    return 0 != (static_cast<uint32_t>(interrupts_domain) & static_cast<uint32_t>(InterruptsDomain::HOST));
}

inline bool device_interuptes_enabled(InterruptsDomain interrupts_domain)
{
    return 0 != (static_cast<uint32_t>(interrupts_domain) & static_cast<uint32_t>(InterruptsDomain::DEVICE));
}

class DescriptorList
{
public:
    static Expected<DescriptorList> create(uint32_t desc_count, uint16_t requested_desc_page_size,
        HailoRTDriver &driver);

    ~DescriptorList();

    DescriptorList(const DescriptorList &other) = delete;
    DescriptorList &operator=(const DescriptorList &other) = delete;
    DescriptorList(DescriptorList &&other) noexcept;
    DescriptorList &operator=(DescriptorList &&other) = delete;

    uint8_t depth() const
    {
        return m_depth;
    }

    uint32_t count() const
    {
        return m_count;
    }

    uint64_t dma_address() const
    {
        return m_dma_address;
    }

    VdmaDescriptor& operator[](size_t i)
    {
        assert(i < m_count);
        return m_mapped_list[i];
    }

    uint16_t desc_page_size() const
    {
        return m_desc_page_size;
    }

    uintptr_t handle() const
    {
        return m_desc_handle;
    }

    uint16_t max_transfers(uint32_t transfer_size)
    {
        // We need to keep at least 1 free desc at all time.
        return static_cast<uint16_t>((m_count - 1) / descriptors_in_buffer(transfer_size));
    }

    // Map descriptors starting at offset to the start of buffer, wrapping around the descriptor list as needed
    // On hailo8, we allow configuring buffer without specific channel index (default is INVALID_VDMA_CHANNEL_INDEX).
    hailo_status configure_to_use_buffer(DmaMappedBuffer& buffer, ChannelId channel_id, uint32_t starting_desc = 0);
    // All descritors are initialized to have size of m_desc_page_size - so all we do is set the last descritor for the
    // Interrupt - and then after transfer has finished clear the previously used first and last decsriptors.
    // This saves us write/ reads to the desscriptor list which is DMA memory.
    Expected<uint16_t> program_last_descriptor(size_t transfer_size, InterruptsDomain last_desc_interrupts_domain,
        size_t desc_offset, bool is_circular);
    void program_single_descriptor(VdmaDescriptor &descriptor, uint16_t page_size, InterruptsDomain interrupts_domain);
    hailo_status reprogram_descriptor_interrupts_domain(size_t desc_index, InterruptsDomain interrupts_domain);
    void clear_descriptor(const size_t desc_index);

    uint32_t descriptors_in_buffer(size_t buffer_size) const;
    static uint32_t descriptors_in_buffer(size_t buffer_size, uint16_t desc_page_size);
    static uint32_t calculate_descriptors_count(uint32_t buffer_size, uint16_t batch_size, uint16_t desc_page_size);
    static Expected<std::pair<uint16_t, uint32_t>> get_desc_buffer_sizes_for_single_transfer(const HailoRTDriver &driver,
        uint16_t min_batch_size, uint16_t max_batch_size, uint32_t transfer_size);
    static Expected<std::pair<uint16_t, uint32_t>> get_desc_buffer_sizes_for_multiple_transfers(const HailoRTDriver &driver,
        uint16_t batch_size, const std::vector<uint32_t> &transfer_sizes);

private:
    DescriptorList(uint32_t desc_count, HailoRTDriver &driver, uint16_t desc_page_size, hailo_status &status);
    uint32_t get_interrupts_bitmask(InterruptsDomain interrupts_domain);
    void reprogram_single_descriptor_interrupts_domain(VdmaDescriptor &descriptor, InterruptsDomain interrupts_domain);
    static Expected<uint8_t> calculate_desc_list_depth(size_t count);
    // Note: initial_desc_page_size should be the optimal descriptor page size.
    static Expected<std::pair<uint16_t, uint32_t>> get_desc_buffer_sizes_for_single_transfer_impl(
        const HailoRTDriver &driver, uint16_t min_batch_size, uint16_t max_batch_size, uint32_t transfer_size,
        uint16_t initial_desc_page_size);
    static Expected<std::pair<uint16_t, uint32_t>> get_desc_buffer_sizes_for_multiple_transfers_impl(
        const HailoRTDriver &driver, uint16_t batch_size, const std::vector<uint32_t> &transfer_sizes,
        uint16_t initial_desc_page_size);
    static uint32_t get_descriptors_count_needed(const std::vector<uint32_t> &transfer_sizes,
        uint16_t desc_page_size);

    MmapBuffer<VdmaDescriptor> m_mapped_list;
    uint32_t m_count;
    uint8_t m_depth;
    uintptr_t m_desc_handle;
    uint64_t m_dma_address;
    HailoRTDriver &m_driver;
    const uint16_t m_desc_page_size;
};

} /* namespace vdma */
} /* namespace hailort */

#endif //_HAILO_VDMA_DESCRIPTOR_LIST_HPP_