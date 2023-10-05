/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file transfer_common.cpp
 **/

#include "transfer_common.hpp"
#include "vdma/memory/mapped_buffer.hpp"

namespace hailort
{


TransferBuffer::TransferBuffer() :
    m_base_buffer(nullptr),
    m_size(0),
    m_offset(0)
{}

TransferBuffer::TransferBuffer(BufferPtr base_buffer, size_t size, size_t offset) :
    m_base_buffer(std::move(base_buffer)),
    m_size(size),
    m_offset(offset)
{
    assert(m_size <= m_base_buffer->size());
    assert(m_offset < m_base_buffer->size());
}

TransferBuffer::TransferBuffer(BufferPtr base_buffer)
    : TransferBuffer(base_buffer, base_buffer->size(), 0)
{}

Expected<vdma::MappedBufferPtr> TransferBuffer::map_buffer(HailoRTDriver &driver, HailoRTDriver::DmaDirection direction)
{
    CHECK_AS_EXPECTED(m_base_buffer->storage().type() == BufferStorage::Type::DMA, HAILO_INVALID_ARGUMENT,
        "Buffer must be dma-able (provided buffer type {})", static_cast<int>(m_base_buffer->storage().type()));

    // Map if not already mapped
    auto is_new_mapping_exp = m_base_buffer->storage().dma_map(driver, to_hailo_dma_direction(direction));
    CHECK_EXPECTED(is_new_mapping_exp);

    return m_base_buffer->storage().get_dma_mapped_buffer(driver.device_id());
}

hailo_status TransferBuffer::copy_to(MemoryView buffer)
{
    CHECK(buffer.size() == m_size, HAILO_INTERNAL_FAILURE, "buffer size {} must be {}", buffer.size(), m_size);

    auto continuous_parts = get_continuous_parts();
    memcpy(buffer.data(), continuous_parts.first.data(), continuous_parts.first.size());
    if (!continuous_parts.second.empty()) {
        const size_t dest_offset = continuous_parts.first.size();
        memcpy(buffer.data() + dest_offset, continuous_parts.second.data(), continuous_parts.second.size());
    }
    return HAILO_SUCCESS;
}

hailo_status TransferBuffer::copy_from(const MemoryView buffer)
{
    CHECK(buffer.size() == m_size, HAILO_INTERNAL_FAILURE, "buffer size {} must be {}", buffer.size(), m_size);

    auto continuous_parts = get_continuous_parts();
    memcpy(continuous_parts.first.data(), buffer.data(), continuous_parts.first.size());
    if (!continuous_parts.second.empty()) {
        const size_t src_offset = continuous_parts.first.size();
        memcpy(continuous_parts.second.data(), buffer.data() + src_offset, continuous_parts.second.size());
    }

    return HAILO_SUCCESS;
}

hailo_status TransferBuffer::synchronize(HailoRTDriver &driver, HailoRTDriver::DmaSyncDirection sync_direction)
{
    auto mapped_buffer = m_base_buffer->storage().get_dma_mapped_buffer(driver.device_id());
    CHECK_EXPECTED_AS_STATUS(mapped_buffer);

    auto continuous_parts = get_continuous_parts();

    auto status = synchronize_part(*mapped_buffer, continuous_parts.first, sync_direction);
    CHECK_SUCCESS(status);

    if (!continuous_parts.second.empty()) {
        status = synchronize_part(*mapped_buffer, continuous_parts.second, sync_direction);
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status TransferBuffer::synchronize_part(vdma::MappedBufferPtr &mapped_buffer, MemoryView continuous_part,
    HailoRTDriver::DmaSyncDirection sync_direction)
{
    assert(!continuous_part.empty());
    assert(continuous_part.data() >= m_base_buffer->data());

    return mapped_buffer->synchronize(continuous_part.data() - m_base_buffer->data(), continuous_part.size(),
        sync_direction);
}

bool TransferBuffer::is_wrap_around() const
{
    return (m_offset + m_size) > m_base_buffer->size();
}

std::pair<MemoryView, MemoryView> TransferBuffer::get_continuous_parts()
{
    if (is_wrap_around()) {
        const auto size_to_end = m_base_buffer->size() - m_offset;
        assert(size_to_end < m_size);
        return std::make_pair(
            MemoryView(m_base_buffer->data() + m_offset, size_to_end),
            MemoryView(m_base_buffer->data(), m_size - size_to_end)
        );

    } else {
        return std::make_pair(
            MemoryView(m_base_buffer->data() + m_offset, m_size),
            MemoryView()
        );
    }
}

} /* namespace hailort */
