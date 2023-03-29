/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file descriptor_list.cpp
 * @brief Implements vdma descriptor list class
 **/

#include "vdma/memory/descriptor_list.hpp"
#include "vdma/memory/mapped_buffer_impl.hpp"

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


Expected<DescriptorList> DescriptorList::create(uint32_t desc_count, uint16_t requested_desc_page_size,
    HailoRTDriver &driver)
{
    hailo_status status = HAILO_UNINITIALIZED;
    auto desc_page_size_value = driver.calc_desc_page_size(requested_desc_page_size);
    DescriptorList object(desc_count, driver, desc_page_size_value, status);
    if (HAILO_SUCCESS != status) {
        return make_unexpected(status);
    }

    // No need to initialize descripotrs here because they are initialized in driver in hailo_vdma_program_descriptor()

    return object;
}

DescriptorList::DescriptorList(uint32_t desc_count, HailoRTDriver &driver, uint16_t desc_page_size,
                                       hailo_status &status) :
    m_mapped_list(),
    m_count(desc_count),
    m_depth(0),
    m_desc_handle(0),
    m_dma_address(0),
    m_driver(driver),
    m_desc_page_size(desc_page_size)
{
    if (!is_powerof2(desc_count)) {
        LOGGER__ERROR("Descriptor count ({}) must be power of 2", desc_count);
        status = HAILO_INVALID_ARGUMENT;
        return;
    }

    auto depth = calculate_desc_list_depth(desc_count);
    if (!depth) {
        status = depth.status();
        return;
    }
    m_depth = depth.value();

    auto desc_handle_phys_addr_pair = m_driver.descriptors_list_create(desc_count);
    if (!desc_handle_phys_addr_pair) {
        status = desc_handle_phys_addr_pair.status();
        return;
    }

    m_desc_handle = desc_handle_phys_addr_pair->first;
    m_dma_address = desc_handle_phys_addr_pair->second;
    
    auto mapped_list = MmapBuffer<VdmaDescriptor>::create_file_map(desc_count * sizeof(VdmaDescriptor), m_driver.fd(), m_desc_handle);
    if (!mapped_list) {
        LOGGER__ERROR("Failed to memory map descriptors. desc handle: {:X}", m_desc_handle);
        status = mapped_list.status();
        return;
    }

    m_mapped_list = mapped_list.release();
    status = HAILO_SUCCESS;
}

DescriptorList::~DescriptorList()
{
    if (HAILO_SUCCESS != m_mapped_list.unmap()) {
        LOGGER__ERROR("Failed to release descriptors mapping");
    }

    // Note: The descriptors_list is freed by the desc_handle (no need to use the phys_address to free)
    if (0 != m_desc_handle) {
        if(HAILO_SUCCESS != m_driver.descriptors_list_release(m_desc_handle)) {
            LOGGER__ERROR("Failed to release descriptor list {}", m_desc_handle);
        }
    }
}

DescriptorList::DescriptorList(DescriptorList &&other) noexcept : 
    m_mapped_list(std::move(other.m_mapped_list)),
    m_count(std::move(other.m_count)),
    m_depth(std::move(other.m_depth)),
    m_desc_handle(std::exchange(other.m_desc_handle, 0)),
    m_dma_address(std::exchange(other.m_dma_address, 0)),
    m_driver(other.m_driver),
    m_desc_page_size(other.m_desc_page_size)  {}

Expected<uint8_t> DescriptorList::calculate_desc_list_depth(size_t count)
{
    // Calculate log2 of m_count (by finding the offset of the MSB)
    uint32_t depth = 0;
    while (count >>= 1) {
        ++depth;
    }
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(depth), HAILO_INTERNAL_FAILURE, "Calculated desc_list_depth is too big: {}", depth);
    return static_cast<uint8_t>(depth);
}

hailo_status DescriptorList::configure_to_use_buffer(DmaMappedBuffer& buffer, ChannelId channel_id, uint32_t starting_desc)
{
    const auto desc_list_capacity = m_desc_page_size * m_count;
    CHECK(buffer.size() <= desc_list_capacity, HAILO_INVALID_ARGUMENT,
        "Can't bind a buffer larger than the descriptor list's capacity. Buffer size {}, descriptor list capacity {}",
        buffer.size(), desc_list_capacity);

    return m_driver.descriptors_list_bind_vdma_buffer(m_desc_handle, buffer.pimpl->handle(), m_desc_page_size,
        channel_id.channel_index, starting_desc);
}

Expected<uint16_t> DescriptorList::program_last_descriptor(size_t transfer_size,
    InterruptsDomain last_desc_interrupts_domain, size_t desc_offset, bool is_circular)
{
    assert(transfer_size > 0);
    const auto required_descriptors = descriptors_in_buffer(transfer_size);
    // Required_descriptors + desc_offset can't reach m_count.
    if ((!is_circular) && ((required_descriptors + desc_offset) > m_count)){
        LOGGER__ERROR("Requested transfer size ({}) result in more descriptors than available ({})", transfer_size, m_count);
        return make_unexpected(HAILO_OUT_OF_DESCRIPTORS);
    }

    // Program last descriptor of the transfer size
    /* write residue page with the remaining buffer size*/
    auto resuide = transfer_size - (required_descriptors - 1) * m_desc_page_size;
    assert(IS_FIT_IN_UINT16(resuide));
    size_t last_desc = (desc_offset + required_descriptors - 1) & (m_count - 1);
    program_single_descriptor((*this)[last_desc], static_cast<uint16_t>(resuide), last_desc_interrupts_domain);

    return std::move(static_cast<uint16_t>(required_descriptors));
}

hailo_status DescriptorList::reprogram_descriptor_interrupts_domain(size_t desc_index,
    InterruptsDomain interrupts_domain)
{
    if (desc_index >= m_count){
        LOGGER__ERROR("Requested desc (index={}) exceeds the number of descriptors in the list ({})", desc_index, m_count);
        return HAILO_OUT_OF_DESCRIPTORS;
    }
    reprogram_single_descriptor_interrupts_domain((*this)[desc_index], interrupts_domain);
    return HAILO_SUCCESS;
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
        MAX_DESCS_COUNT);

    return get_nearest_powerof_2(descs_count, MIN_DESCS_COUNT);
}

Expected<std::pair<uint16_t, uint32_t>> DescriptorList::get_desc_buffer_sizes_for_single_transfer(
    const HailoRTDriver &driver, uint16_t min_batch_size, uint16_t max_batch_size, uint32_t transfer_size)
{
    // Note: If the pages pointed to by the descriptors are copied in their entirety, then DEFAULT_DESC_PAGE_SIZE
    //       is the optimal value. For transfer_sizes smaller than DEFAULT_DESC_PAGE_SIZE using smaller descriptor page
    //       sizes will save memory consuption without harming performance. In the case of nms for example, only one bbox
    //       is copied from each page. Hence, we'll use MIN_DESC_PAGE_SIZE for nms.
    const uint32_t initial_desc_page_size = (DEFAULT_DESC_PAGE_SIZE > transfer_size) ? 
        get_nearest_powerof_2(transfer_size, MIN_DESC_PAGE_SIZE) : DEFAULT_DESC_PAGE_SIZE;
    if (DEFAULT_DESC_PAGE_SIZE != initial_desc_page_size) {
        LOGGER__INFO("Using non-default initial_desc_page_size of {}, due to a small transfer size ({})",
            initial_desc_page_size, transfer_size);
    }
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(initial_desc_page_size), HAILO_INTERNAL_FAILURE,
        "Descriptor page size needs to fit in 16B");
    
    return get_desc_buffer_sizes_for_single_transfer_impl(driver, min_batch_size, max_batch_size, transfer_size,
        static_cast<uint16_t>(initial_desc_page_size));
}

Expected<std::pair<uint16_t, uint32_t>> DescriptorList::get_desc_buffer_sizes_for_multiple_transfers(
    const HailoRTDriver &driver, uint16_t batch_size, const std::vector<uint32_t> &transfer_sizes)
{
    return get_desc_buffer_sizes_for_multiple_transfers_impl(driver, batch_size, transfer_sizes,
        DEFAULT_DESC_PAGE_SIZE);
}

Expected<std::pair<uint16_t, uint32_t>> DescriptorList::get_desc_buffer_sizes_for_single_transfer_impl(
    const HailoRTDriver &driver, uint16_t min_batch_size, uint16_t max_batch_size, uint32_t transfer_size,
    uint16_t initial_desc_page_size)
{
    auto results =  DescriptorList::get_desc_buffer_sizes_for_multiple_transfers_impl(driver, min_batch_size,
        {transfer_size}, initial_desc_page_size);
    CHECK_EXPECTED(results);

    auto page_size = results->first;

    auto desc_count = std::min(MAX_DESCS_COUNT,
            DescriptorList::calculate_descriptors_count(transfer_size, max_batch_size, page_size));

    return std::make_pair(page_size, desc_count);
}

Expected<std::pair<uint16_t, uint32_t>> DescriptorList::get_desc_buffer_sizes_for_multiple_transfers_impl(
    const HailoRTDriver &driver, uint16_t batch_size, const std::vector<uint32_t> &transfer_sizes,
    uint16_t initial_desc_page_size)
{
    const uint16_t min_desc_page_size = driver.calc_desc_page_size(MIN_DESC_PAGE_SIZE);
    const uint16_t max_desc_page_size = driver.calc_desc_page_size(MAX_DESC_PAGE_SIZE);
    // Defined as uint32_t to prevent overflow (as we multiply it by two in each iteration of the while loop bellow)
    uint32_t local_desc_page_size = driver.calc_desc_page_size(initial_desc_page_size);
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(local_desc_page_size), HAILO_INTERNAL_FAILURE,
        "Descriptor page size needs to fit in 16B");
    CHECK_AS_EXPECTED(local_desc_page_size <= max_desc_page_size, HAILO_INTERNAL_FAILURE,
        "Initial descriptor page size ({}) is larger than maximum descriptor page size ({})",
        local_desc_page_size, max_desc_page_size);
    CHECK_AS_EXPECTED(local_desc_page_size >= min_desc_page_size, HAILO_INTERNAL_FAILURE,
        "Initial descriptor page size ({}) is smaller than minimum descriptor page size ({})",
        local_desc_page_size, min_desc_page_size);

    uint32_t acc_desc_count = get_descriptors_count_needed(transfer_sizes, static_cast<uint16_t>(local_desc_page_size));

    // Too many descriptors; try a larger desc_page_size which will lead to less descriptors used
    while ((acc_desc_count * batch_size) > (MAX_DESCS_COUNT - 1)) {
        local_desc_page_size <<= 1;

        CHECK_AS_EXPECTED(local_desc_page_size <= max_desc_page_size, HAILO_OUT_OF_DESCRIPTORS,
            "Network shapes and batch size exceeds driver descriptors capabilities. "
            "Required descriptors count: {}, max allowed on the driver: {}. "
            "(A common cause for this error could be the batch size - which is {}).",
            (batch_size * acc_desc_count), (MAX_DESCS_COUNT - 1), batch_size);

        CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(local_desc_page_size), HAILO_INTERNAL_FAILURE,
            "Descriptor page size needs to fit in 16B");

        acc_desc_count = get_descriptors_count_needed(transfer_sizes, static_cast<uint16_t>(local_desc_page_size));
    }

    // Found desc_page_size and acc_desc_count
    const auto desc_page_size = static_cast<uint16_t>(local_desc_page_size);

    // Find descs_count
    const auto descs_count = get_nearest_powerof_2(acc_desc_count, MIN_DESCS_COUNT);
    CHECK_AS_EXPECTED(descs_count <= MAX_DESCS_COUNT, HAILO_OUT_OF_DESCRIPTORS);

    if (initial_desc_page_size != desc_page_size) {
        LOGGER__WARNING("Desc page size value ({}) is not optimal for performance.", desc_page_size);
    }

    return std::make_pair(desc_page_size, descs_count);
}

uint32_t DescriptorList::get_descriptors_count_needed(const std::vector<uint32_t> &transfer_sizes,
    uint16_t desc_page_size)
{
    uint32_t desc_count = 0;
    for (auto &transfer_size : transfer_sizes) {
        desc_count += descriptors_in_buffer(transfer_size, desc_page_size);
    }

    // One extra descriptor is needed, because the amount of available descriptors is (desc_count - 1)
    desc_count += 1;
    return desc_count;
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

void DescriptorList::program_single_descriptor(VdmaDescriptor &descriptor, uint16_t page_size,
    InterruptsDomain interrupts_domain)
{
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

void DescriptorList::reprogram_single_descriptor_interrupts_domain(VdmaDescriptor &descriptor,
    InterruptsDomain interrupts_domain)
{
    // Set the IRQ control bits to zero
    // Make all edits to the local variable local_pagesize_desc_ctrl that is on the stack to save read/writes to DDR
    auto local_pagesize_desc_ctrl = (descriptor.PageSize_DescControl & ~DESC_IRQ_MASK);
    
    if (InterruptsDomain::NONE == interrupts_domain) {
        // Nothing else to do
        descriptor.PageSize_DescControl = local_pagesize_desc_ctrl;
        return;
    }

    local_pagesize_desc_ctrl |= (DESC_REQUREST_IRQ_PROCESSED | DESC_REQUREST_IRQ_ERR |
        get_interrupts_bitmask(interrupts_domain));

    descriptor.PageSize_DescControl = local_pagesize_desc_ctrl;
}

void DescriptorList::clear_descriptor(const size_t desc_index)
{
    // Clear previous descriptor properties
    program_single_descriptor((*this)[desc_index], m_desc_page_size, InterruptsDomain::NONE);
}

} /* namespace vdma */
} /* namespace hailort */
