/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file descriptor_list.cpp
 * @brief Implements vdma descriptor list class
 **/

#include "vdma/memory/descriptor_list.hpp"

#include "utils.h"

namespace hailort {
namespace vdma {


Expected<DescriptorList> DescriptorList::create(uint32_t desc_count, uint16_t desc_page_size, bool is_circular,
    HailoRTDriver &driver)
{
    hailo_status status = HAILO_UNINITIALIZED;
    const auto desc_params = driver.get_sg_desc_params();
    assert(desc_page_size <= desc_params.max_page_size);

    CHECK_AS_EXPECTED(desc_count <= desc_params.max_descs_count, HAILO_INVALID_ARGUMENT,
        "descs_count {} must be smaller/equal to {}", desc_count, desc_params.max_descs_count);

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
    m_desc_count(desc_count),
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
    m_desc_count(other.m_desc_count),
    m_is_circular(std::move(other.m_is_circular)),
    m_driver(other.m_driver),
    m_desc_page_size(other.m_desc_page_size)
{
    m_desc_list_info.handle = std::exchange(other.m_desc_list_info.handle, 0);
    m_desc_list_info.dma_address = std::exchange(other.m_desc_list_info.dma_address, 0);
}

hailo_status DescriptorList::program(MappedBuffer& buffer, size_t buffer_size,
    size_t buffer_offset, ChannelId channel_id, uint32_t starting_desc, uint32_t batch_size /* = 1 */,
    InterruptsDomain last_desc_interrupts /* = InterruptsDomain::NONE */)
{
    const auto desc_list_capacity = m_desc_page_size * count();
    CHECK(buffer_size <= desc_list_capacity, HAILO_INVALID_ARGUMENT,
        "Can't bind a buffer larger than the descriptor list's capacity. Buffer size {}, descriptor list capacity {}",
        buffer_size, desc_list_capacity);

    return m_driver.descriptors_list_program(m_desc_list_info.handle, buffer.handle(), buffer_offset, buffer_size,
        batch_size, channel_id.channel_index, starting_desc, last_desc_interrupts);
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

size_t DescriptorList::descriptors_buffer_allocation_size(uint32_t desc_count)
{
    // based on hailo_desc_list_create from linux driver
    auto ALIGN = [](size_t size, size_t alignment) {
        const auto mask = alignment - 1;
        return (size + mask) & ~mask;
    };

    const auto total_size = vdma::SINGLE_DESCRIPTOR_SIZE * desc_count;
    return ALIGN(total_size, vdma::DESCRIPTOR_LIST_ALIGN);
}

} /* namespace vdma */
} /* namespace hailort */
