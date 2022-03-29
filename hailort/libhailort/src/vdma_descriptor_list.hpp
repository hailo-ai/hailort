/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_descriptor_list.hpp
 * @brief Allocates a list of buffer descriptors used for VDMA
 *
 **/

#ifndef _HAILO_VDMA_DESCRIPTOR_LIST_HPP_
#define _HAILO_VDMA_DESCRIPTOR_LIST_HPP_

#include "os/hailort_driver.hpp"
#include "hailo/expected.hpp"
#include "os/mmap_buffer.hpp"
#include "vdma_buffer.hpp"
#include "common/utils.hpp"

namespace hailort
{

// HW doens't support more than 32 channels
#define MAX_HOST_CHANNELS_COUNT (32)

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
#define DEFAULT_DESC_PAGE_SIZE (512u)

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

enum class VdmaInterruptsDomain 
{
    NONE    = 0,
    DEVICE  = 1 << 0,
    HOST    = 1 << 1,
    BOTH    = DEVICE | HOST
};

inline bool host_interuptes_enabled(VdmaInterruptsDomain interrupts_domain)
{
    return 0 != (static_cast<uint32_t>(interrupts_domain) & static_cast<uint32_t>(VdmaInterruptsDomain::HOST));
}

inline bool device_interuptes_enabled(VdmaInterruptsDomain interrupts_domain)
{
    return 0 != (static_cast<uint32_t>(interrupts_domain) & static_cast<uint32_t>(VdmaInterruptsDomain::DEVICE));
}

class VdmaDescriptorList
{
public:
    static Expected<VdmaDescriptorList> create(size_t desc_count, uint16_t requested_desc_page_size,
        HailoRTDriver &driver);

    ~VdmaDescriptorList();

    VdmaDescriptorList(const VdmaDescriptorList &other) = delete;
    VdmaDescriptorList &operator=(const VdmaDescriptorList &other) = delete;
    VdmaDescriptorList(VdmaDescriptorList &&other) noexcept;
    VdmaDescriptorList &operator=(VdmaDescriptorList &&other) = delete;

    uint8_t depth() const
    {
        return m_depth;
    }

    size_t count() const
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

    hailo_status configure_to_use_buffer(VdmaBuffer& buffer, uint8_t channel_index);
    // On hailo8, we allow configuring buffer without specific channel index.
    hailo_status configure_to_use_buffer(VdmaBuffer& buffer);

    Expected<uint16_t> program_descriptors(size_t transfer_size, VdmaInterruptsDomain first_desc_interrupts_domain,
        VdmaInterruptsDomain last_desc_interrupts_domain, size_t desc_offset, bool is_circular);

    Expected<uint16_t> program_descs_for_ddr_transfers(uint32_t row_size, bool should_raise_interrupt, 
        uint32_t number_of_rows_per_intrpt, uint32_t buffered_rows, uint16_t initial_descs_offset, bool is_circular);

    uint32_t descriptors_in_buffer(size_t buffer_size) const;
    static uint32_t descriptors_in_buffer(size_t buffer_size, uint16_t desc_page_size);
    static uint32_t calculate_descriptors_count(uint32_t buffer_size, uint16_t batch_size, uint16_t desc_page_size);
    static Expected<std::pair<uint16_t, uint32_t>> get_desc_buffer_sizes_for_single_transfer(const HailoRTDriver &driver,
        uint16_t min_batch_size, uint16_t max_batch_size, uint32_t transfer_size);
    static Expected<std::pair<uint16_t, uint32_t>> get_desc_buffer_sizes_for_multiple_transfers(const HailoRTDriver &driver,
        uint16_t batch_size, const std::vector<uint32_t> &transfer_sizes);

private:
    VdmaDescriptorList(size_t desc_count, HailoRTDriver &driver, uint16_t desc_page_size, hailo_status &status);
    uint32_t get_interrupts_bitmask(VdmaInterruptsDomain interrupts_domain);
    void program_single_descriptor(VdmaDescriptor &descriptor, uint16_t page_size,
        VdmaInterruptsDomain interrupts_domain);
    static Expected<uint8_t> calculate_desc_list_depth(size_t count);
    // Note: initial_desc_page_size should be the optimal descriptor page size.
    static Expected<std::pair<uint16_t, uint32_t>> get_desc_buffer_sizes_for_single_transfer_impl(
        const HailoRTDriver &driver, uint16_t min_batch_size, uint16_t max_batch_size, uint32_t transfer_size,
        uint16_t initial_desc_page_size);
    static Expected<std::pair<uint16_t, uint32_t>> get_desc_buffer_sizes_for_multiple_transfers_impl(
        const HailoRTDriver &driver, uint16_t batch_size, const std::vector<uint32_t> &transfer_sizes,
        uint16_t initial_desc_page_size);

    MmapBuffer<VdmaDescriptor> m_mapped_list;
    size_t m_count;
    uint8_t m_depth;
    uintptr_t m_desc_handle;
    uint64_t m_dma_address;
    HailoRTDriver &m_driver;
    const uint16_t m_desc_page_size;
};

} /* namespace hailort */

#endif //_HAILO_VDMA_DESCRIPTOR_LIST_HPP_