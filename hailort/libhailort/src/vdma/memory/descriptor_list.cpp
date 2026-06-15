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
    CHECK(desc_page_size <= driver.desc_max_page_size(), HAILO_INVALID_ARGUMENT, "Desc-page size is larger than max");

    CHECK(desc_count <= MAX_SG_DESCS_COUNT, HAILO_INVALID_ARGUMENT, "descs_count is larger than max");

    CHECK(!is_circular || is_powerof2(desc_count), HAILO_INVALID_ARGUMENT,
          "Desc-count for circular lists must be a power of 2");

    TRY(auto handle, driver.descriptors_list_create(desc_count, desc_page_size, is_circular));

    return DescriptorList(handle, desc_count, desc_page_size, driver);
}

DescriptorList::~DescriptorList()
{
    if (m_handle != INVALID_DESC_LIST_HANDLE) {
        auto status = m_driver.descriptors_list_release(m_handle);
        if(HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to release descriptor list {} with status {}", m_handle, status);
        }
    }
}

DescriptorList::DescriptorList(DescriptorList &&other) noexcept :
    m_desc_count(other.m_desc_count),
    m_driver(other.m_driver),
    m_desc_page_size(other.m_desc_page_size)
{
    m_handle = std::exchange(other.m_handle, INVALID_DESC_LIST_HANDLE);
}

hailo_status DescriptorList::program(MappedBuffer& buffer, size_t buffer_size,
    size_t buffer_offset, ChannelId channel_id, uint32_t starting_desc, uint32_t batch_size /* = 1 */,
    bool should_bind /* = true */, InterruptsDomain last_desc_interrupts /* = InterruptsDomain::NONE */, uint32_t stride /* = 0 */)
{
    const auto desc_list_capacity = m_desc_page_size * count();
    CHECK(buffer_size <= desc_list_capacity, HAILO_INVALID_ARGUMENT,
        "Can't bind a buffer larger than the descriptor list's capacity. Buffer size {}, descriptor list capacity {}",
        buffer_size, desc_list_capacity);

    return m_driver.descriptors_list_program(m_handle, buffer.handle(), buffer_size,
        buffer_offset, channel_id.channel_index, starting_desc, batch_size, should_bind, last_desc_interrupts, stride);
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
