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
#include "hailo/hailort_common.hpp"

#include "common/utils.hpp"

#include "vdma/channel/channel_id.hpp"
#include "vdma/memory/mapped_buffer.hpp"

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
// - Addresses of pages pointed to by vDMA descriptors need to be on a 64B boundary.
//   Hence, we require a minimum page size of 64B.
// - G_PAGE_SIZE_MAX dictates the maximum desc page size:
//     max_page_size = 2 ^ (G_PAGE_SIZE_MAX - 1)
//   In our case max_page_size = 2 ^ (13 - 1) = 4096
static constexpr uint16_t MIN_DESC_PAGE_SIZE = 64;
static constexpr uint16_t MAX_DESC_PAGE_SIZE = 4096;
static constexpr uint16_t DEFAULT_DESC_PAGE_SIZE = 512;

static_assert(is_powerof2(MIN_DESC_PAGE_SIZE), "MIN_DESC_PAGE_SIZE must be a power of 2");
static_assert(MIN_DESC_PAGE_SIZE > 0, "MIN_DESC_PAGE_SIZE must be larger then 0");
static_assert(is_powerof2(MAX_DESC_PAGE_SIZE), "MAX_DESC_PAGE_SIZE must be a power of 2");
static_assert(MAX_DESC_PAGE_SIZE > 0, "MAX_DESC_PAGE_SIZE must be larger then 0");
static_assert(is_powerof2(DEFAULT_DESC_PAGE_SIZE), "DEFAULT_DESC_PAGE_SIZE must be a power of 2");
static_assert(DEFAULT_DESC_PAGE_SIZE > 0, "DEFAULT_DESC_PAGE_SIZE must be larger then 0");


static constexpr auto DESCRIPTOR_STATUS_MASK = 0xFF;
static constexpr auto DESCRIPTOR_STATUS_DONE_BIT = 0;
static constexpr auto DESCRIPTOR_STATUS_ERROR_BIT = 1;

struct VdmaDescriptor
{
    // Struct layout is taken from PLDA spec for vDMA, and cannot be changed.
    uint32_t PageSize_DescControl;
    uint32_t AddrL_rsvd_DataID;
    uint32_t AddrH;
    uint32_t RemainingPageSize_Status;

#ifndef NDEBUG
    // Easy accessors (only on debug since we mark DESC_STATUS_REQ and DESC_STATUS_REQ_ERR are set only on debug).
    uint8_t status() const
    {
        return RemainingPageSize_Status & DESCRIPTOR_STATUS_MASK;
    }

    bool is_done() const
    {
        return is_bit_set(status(), DESCRIPTOR_STATUS_DONE_BIT);
    }

    bool is_error() const
    {
        return is_bit_set(status(), DESCRIPTOR_STATUS_ERROR_BIT);
    }
#endif /* NDEBUG */
};

static_assert(SIZE_OF_SINGLE_DESCRIPTOR == sizeof(VdmaDescriptor), "Invalid size of descriptor");

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
    static Expected<DescriptorList> create(uint32_t desc_count, uint16_t desc_page_size, bool is_circular,
        HailoRTDriver &driver);

    ~DescriptorList();

    DescriptorList(const DescriptorList &other) = delete;
    DescriptorList &operator=(const DescriptorList &other) = delete;
    DescriptorList(DescriptorList &&other) noexcept;
    DescriptorList &operator=(DescriptorList &&other) = delete;

    uint32_t count() const
    {
        assert(m_desc_list_info.desc_count <= std::numeric_limits<uint32_t>::max());
        return static_cast<uint32_t>(m_desc_list_info.desc_count);
    }

    uint64_t dma_address() const
    {
        return m_desc_list_info.dma_address;
    }

    VdmaDescriptor& operator[](size_t i)
    {
        assert(i < count());
        return desc_list()[i];
    }

    uint16_t desc_page_size() const
    {
        return m_desc_page_size;
    }

    uintptr_t handle() const
    {
        return m_desc_list_info.handle;
    }

    uint16_t max_transfers(uint32_t transfer_size)
    {
        // We need to keep at least 1 free desc at all time.
        return static_cast<uint16_t>((count() - 1) / descriptors_in_buffer(transfer_size));
    }

    // Map descriptors starting at offset to the start of buffer, wrapping around the descriptor list as needed
    // On hailo8, we allow configuring buffer without specific channel index (default is INVALID_VDMA_CHANNEL_INDEX).
    hailo_status configure_to_use_buffer(MappedBuffer& buffer, ChannelId channel_id, uint32_t starting_desc = 0);
    // All descritors are initialized to have size of m_desc_page_size - so all we do is set the last descritor for the
    // Interrupt - and then after transfer has finished clear the previously used first and last decsriptors.
    // This saves us write/ reads to the desscriptor list which is DMA memory.
    Expected<uint16_t> program_last_descriptor(size_t transfer_size, InterruptsDomain last_desc_interrupts_domain,
        size_t desc_offset);
    void program_single_descriptor(VdmaDescriptor &descriptor, uint16_t page_size, InterruptsDomain interrupts_domain);
    hailo_status reprogram_descriptor_interrupts_domain(size_t desc_index, InterruptsDomain interrupts_domain);
    void clear_descriptor(const size_t desc_index);

    uint32_t descriptors_in_buffer(size_t buffer_size) const;
    static uint32_t descriptors_in_buffer(size_t buffer_size, uint16_t desc_page_size);
    static uint32_t calculate_descriptors_count(uint32_t buffer_size, uint16_t batch_size, uint16_t desc_page_size);

private:
    DescriptorList(uint32_t desc_count, uint16_t desc_page_size, bool is_circular, HailoRTDriver &driver,
        hailo_status &status);

    VdmaDescriptor *desc_list() { return reinterpret_cast<VdmaDescriptor*>(m_desc_list_info.user_address); }

    uint32_t get_interrupts_bitmask(InterruptsDomain interrupts_domain);
    void reprogram_single_descriptor_interrupts_domain(VdmaDescriptor &descriptor, InterruptsDomain interrupts_domain);


    DescriptorsListInfo m_desc_list_info;
    const bool m_is_circular;
    HailoRTDriver &m_driver;
    const uint16_t m_desc_page_size;
};

} /* namespace vdma */
} /* namespace hailort */

#endif //_HAILO_VDMA_DESCRIPTOR_LIST_HPP_