/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file descriptor_list.cpp
 * @brief Implements vdma descriptor list class
 **/

#include "vdma/memory/descriptor_list.hpp"

#include "utils.h"


#define DESC_STATUS_REQ                       (1 << 0)
#define DESC_STATUS_REQ_ERR                   (1 << 1)
#define DESC_REQUREST_IRQ_PROCESSED           (1 << 2)
#define DESC_REQUREST_IRQ_ERR                 (1 << 3)

#define PCIE_DMA_HOST_INTERRUPTS_BITMASK      (1 << 5)
#define PCIE_DMA_DEVICE_INTERRUPTS_BITMASK    (1 << 4)

#define DRAM_DMA_HOST_INTERRUPTS_BITMASK      (1 << 4)
#define DRAM_DMA_DEVICE_INTERRUPTS_BITMASK    (1 << 5)

#define DESC_PAGE_SIZE_SHIFT                  (8)
#define DESC_PAGE_SIZE_MASK                   (0xFFFFFF00)
#define DESC_IRQ_MASK                         (0x0000003C)

namespace hailort {
namespace vdma {


Expected<DescriptorList> DescriptorList::create(uint32_t desc_count, uint16_t desc_page_size, bool is_circular,
    HailoRTDriver &driver)
{
    hailo_status status = HAILO_UNINITIALIZED;
    assert(desc_page_size <= driver.desc_max_page_size());

    CHECK_AS_EXPECTED(desc_count <= MAX_SG_DESCS_COUNT, HAILO_INVALID_ARGUMENT,
        "descs_count {} must be smaller/equal to {}", desc_count, MAX_SG_DESCS_COUNT);

    DescriptorList object(desc_count, desc_page_size, is_circular, driver, status);
    if (HAILO_SUCCESS != status) {
        return make_unexpected(status);
    }

    // No need to initialize descriptors here because they are initialized in driver in hailo_vdma_program_descriptor()

    return object;
}

DescriptorList::DescriptorList(uint32_t desc_count, uint16_t desc_page_size, bool is_circular, HailoRTDriver &driver,
                               hailo_status &status) :
    m_desc_list_info(),
    m_is_circular(is_circular),
    m_driver(driver),
    m_desc_page_size(desc_page_size)
{
    if (m_is_circular && !is_powerof2(desc_count)) {
        LOGGER__ERROR("Descriptor count ({}) for circular descriptor list must be power of 2", desc_count);
        status = HAILO_INVALID_ARGUMENT;
        return;
    }

    auto desc_list_info = m_driver.descriptors_list_create(desc_count, m_desc_page_size, m_is_circular);
    if (!desc_list_info) {
        status = desc_list_info.status();
        return;
    }

    m_desc_list_info = desc_list_info.release();

    status = HAILO_SUCCESS;
}

DescriptorList::~DescriptorList()
{
    if (0 != m_desc_list_info.handle) {
        auto status = m_driver.descriptors_list_release(m_desc_list_info);
        if(HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to release descriptor list {} with status {}", m_desc_list_info.handle, status);
        }
    }
}

DescriptorList::DescriptorList(DescriptorList &&other) noexcept :
    m_desc_list_info(),
    m_is_circular(std::move(other.m_is_circular)),
    m_driver(other.m_driver),
    m_desc_page_size(other.m_desc_page_size)
{
    m_desc_list_info.handle = std::exchange(other.m_desc_list_info.handle, 0);
    m_desc_list_info.dma_address = std::exchange(other.m_desc_list_info.dma_address, 0);
    m_desc_list_info.desc_count = std::move(other.m_desc_list_info.desc_count);
    m_desc_list_info.user_address = std::exchange(other.m_desc_list_info.user_address, nullptr);
}

hailo_status DescriptorList::configure_to_use_buffer(MappedBuffer& buffer, size_t buffer_size,
    size_t buffer_offset, ChannelId channel_id, uint32_t starting_desc)
{
    const auto desc_list_capacity = m_desc_page_size * count();
    CHECK(buffer_size <= desc_list_capacity, HAILO_INVALID_ARGUMENT,
        "Can't bind a buffer larger than the descriptor list's capacity. Buffer size {}, descriptor list capacity {}",
        buffer_size, desc_list_capacity);

    return m_driver.descriptors_list_bind_vdma_buffer(m_desc_list_info.handle, buffer.handle(), buffer_size, 
        buffer_offset, channel_id.channel_index, starting_desc);
}

Expected<uint16_t> DescriptorList::program_last_descriptor(size_t transfer_size,
    InterruptsDomain last_desc_interrupts_domain, size_t desc_offset)
{
    assert(transfer_size > 0);
    const auto required_descriptors = descriptors_in_buffer(transfer_size);
    // Required_descriptors + desc_offset can't reach m_count.
    if ((!m_is_circular) && ((required_descriptors + desc_offset) > count())){
        LOGGER__ERROR("Requested transfer size ({}) result in more descriptors than available ({})", transfer_size, count());
        return make_unexpected(HAILO_OUT_OF_DESCRIPTORS);
    }

    // Program last descriptor of the transfer size
    /* write residue page with the remaining buffer size*/
    auto resuide = transfer_size - (required_descriptors - 1) * m_desc_page_size;
    assert(IS_FIT_IN_UINT16(resuide));
    size_t last_desc = (desc_offset + required_descriptors - 1) % count();
    program_single_descriptor(last_desc, static_cast<uint16_t>(resuide), last_desc_interrupts_domain);

    return std::move(static_cast<uint16_t>(required_descriptors));
}

uint32_t DescriptorList::descriptors_in_buffer(size_t buffer_size) const
{
    return descriptors_in_buffer(buffer_size, m_desc_page_size);
}

uint32_t DescriptorList::descriptors_in_buffer(size_t buffer_size, uint16_t desc_page_size)
{
    assert(buffer_size < std::numeric_limits<uint32_t>::max());
    return static_cast<uint32_t>(DIV_ROUND_UP(buffer_size, desc_page_size));
}

uint32_t DescriptorList::calculate_descriptors_count(uint32_t buffer_size, uint16_t batch_size, uint16_t desc_page_size)
{
    // Because we use cyclic buffer, the amount of active descs is lower by one that the amount
    // of descs given  (Otherwise we won't be able to determine if the buffer is empty or full).
    // Therefore we add 1 in order to compensate.
    uint32_t descs_count = std::min(((descriptors_in_buffer(buffer_size, desc_page_size) * batch_size) + 1),
        MAX_SG_DESCS_COUNT);

    return get_nearest_powerof_2(descs_count, MIN_SG_DESCS_COUNT);
}

uint32_t DescriptorList::get_interrupts_bitmask(InterruptsDomain interrupts_domain)
{
    uint32_t host_bitmask = 0;
    uint32_t device_bitmask = 0;

    switch (m_driver.dma_type()) {
    case HailoRTDriver::DmaType::PCIE:
        host_bitmask = PCIE_DMA_HOST_INTERRUPTS_BITMASK;
        device_bitmask = PCIE_DMA_DEVICE_INTERRUPTS_BITMASK;
        break;
    case HailoRTDriver::DmaType::DRAM:
        host_bitmask = DRAM_DMA_HOST_INTERRUPTS_BITMASK;
        device_bitmask = DRAM_DMA_DEVICE_INTERRUPTS_BITMASK;
        break;
    default:
        assert(false);
    }

    uint32_t bitmask = 0;
    if (host_interuptes_enabled(interrupts_domain)) {
        bitmask |= host_bitmask;
    }
    if (device_interuptes_enabled(interrupts_domain)) {
        bitmask |= device_bitmask;
    }

    return bitmask;
}

void DescriptorList::program_single_descriptor(size_t desc_index, uint16_t page_size,
    InterruptsDomain interrupts_domain)
{
    auto &descriptor = (*this)[desc_index];

    // Update the descriptor's PAGE_SIZE field in the control register with the maximum size of the DMA page.
    // Make all edits to the local variable local_pagesize_desc_ctrl that is on the stack to save read/writes to DDR
    auto local_pagesize_desc_ctrl = static_cast<uint32_t>(page_size << DESC_PAGE_SIZE_SHIFT) & DESC_PAGE_SIZE_MASK;

    if (InterruptsDomain::NONE != interrupts_domain) {
        // Update the desc_control
        local_pagesize_desc_ctrl |= (DESC_REQUREST_IRQ_PROCESSED | DESC_REQUREST_IRQ_ERR |
            get_interrupts_bitmask(interrupts_domain));
#ifndef NDEBUG
        local_pagesize_desc_ctrl |= (DESC_STATUS_REQ | DESC_STATUS_REQ_ERR);
#endif
    }

    descriptor.PageSize_DescControl = local_pagesize_desc_ctrl;

#ifndef NDEBUG
    // Clear status
    descriptor.RemainingPageSize_Status = 0;
#endif
}

} /* namespace vdma */
} /* namespace hailort */
