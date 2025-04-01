/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file transfer_common.cpp
 **/

#include "transfer_common.hpp"

namespace hailort
{


TransferBuffer::TransferBuffer() :
    m_base_buffer(MemoryView{}),
    m_size(0),
    m_offset(0),
    m_type(TransferBufferType::MEMORYVIEW)
{}

TransferBuffer::TransferBuffer(hailo_dma_buffer_t dmabuf) :
    m_dmabuf(dmabuf),
    m_size(dmabuf.size),
    m_offset(0),
    m_type(TransferBufferType::DMABUF)
{}

TransferBuffer::TransferBuffer(MemoryView base_buffer, size_t size, size_t offset) :
    m_base_buffer(base_buffer),
    m_size(size),
    m_offset(offset),
    m_type(TransferBufferType::MEMORYVIEW)
{
    assert(m_size <= base_buffer.size());
    assert(m_offset < base_buffer.size());
}

TransferBuffer::TransferBuffer(MemoryView base_buffer)
    : TransferBuffer(base_buffer, base_buffer.size(), 0)
{}

Expected<MemoryView> TransferBuffer::base_buffer()
{
    CHECK(TransferBufferType::DMABUF != m_type, HAILO_INTERNAL_FAILURE,
        "base_buffer is not supported for DMABUF type TransferBuffer");

    return Expected<MemoryView>(m_base_buffer);
}

Expected<int> TransferBuffer::dmabuf_fd()
{
    CHECK(TransferBufferType::DMABUF == m_type, HAILO_INTERNAL_FAILURE,
        "dmabuf_fd is only supported for DMABUF type TransferBuffer");

    return Expected<int>(m_dmabuf.fd);
}

Expected<vdma::MappedBufferPtr> TransferBuffer::map_buffer(HailoRTDriver &driver, HailoRTDriver::DmaDirection direction)
{
    if (m_mappings) {
        return Expected<vdma::MappedBufferPtr>{m_mappings};
    }
    if (TransferBufferType::DMABUF == m_type) {
        TRY(m_mappings, vdma::MappedBuffer::create_shared_from_dmabuf(m_dmabuf.fd, m_dmabuf.size, driver, direction));
    } else {
        if (is_aligned_for_dma()) {
            TRY(auto dma_able_buffer, vdma::DmaAbleBuffer::create_from_user_address(m_base_buffer.data(), m_base_buffer.size()));
            TRY(m_mappings, vdma::MappedBuffer::create_shared(std::move(dma_able_buffer), driver, direction));
        } else {
            // Allocate a new bounce buffer for the mapping.
            // On H2D dir, copy the data on the map
            // On D2H dir, copy the data on the unmap
            TRY(m_mappings, vdma::MappedBuffer::create_shared_by_allocation(m_base_buffer.size(), driver, direction));
            if (HailoRTDriver::DmaDirection::H2D == direction) {
                (void)copy_to(MemoryView(m_mappings->user_address(), m_mappings->size()));
            }
        }
    }

    return Expected<vdma::MappedBufferPtr>{m_mappings};
}

void TransferBuffer::unmap_buffer()
{
    const bool is_bounce_buffer = !is_aligned_for_dma();
    if (is_bounce_buffer && m_mappings && (HailoRTDriver::DmaDirection::D2H == m_mappings->direction())) {
        (void)copy_from(MemoryView(m_mappings->user_address(), m_mappings->size()));
    }
    m_mappings.reset();
}

Expected<std::vector<HailoRTDriver::TransferBuffer>> TransferBuffer::to_driver_buffers()
{
    CHECK(m_mappings, HAILO_INTERNAL_FAILURE, "transfer-buffer must be mapped before launch-transfer");

    std::vector<HailoRTDriver::TransferBuffer> res;
    HailoRTDriver::TransferBuffer buf;

    if (TransferBufferType::DMABUF == m_type) {
        CHECK(0 == m_offset, HAILO_INTERNAL_FAILURE, "no support for non-zero offset for dmabuf");
        buf.is_dma_buf = true;
        buf.size = m_size;
        buf.addr_or_fd = static_cast<uintptr_t>(m_dmabuf.fd);
        res.emplace_back(buf);

        return Expected<std::vector<HailoRTDriver::TransferBuffer>>{res};
    } else {
        auto parts = get_continuous_parts();

        buf.is_dma_buf = false;
        buf.size = parts.first.size();
        buf.addr_or_fd = reinterpret_cast<uintptr_t>(m_mappings->user_address()) + static_cast<uintptr_t>(m_offset);
        res.emplace_back(buf);

        if (!parts.second.empty()) {
            buf.size = parts.second.size();
            buf.addr_or_fd = reinterpret_cast<uintptr_t>(m_mappings->user_address());
            res.emplace_back(buf);
        }

        return Expected<std::vector<HailoRTDriver::TransferBuffer>>{res};
    }

}

hailo_status TransferBuffer::copy_to(MemoryView buffer)
{
    CHECK(buffer.size() == m_size, HAILO_INTERNAL_FAILURE, "buffer size {} must be {}", buffer.size(), m_size);
    CHECK(TransferBufferType::MEMORYVIEW == m_type, HAILO_INTERNAL_FAILURE,
        "copy_to function is only supported in MEMORYVIEW type TransferBuffer");

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
    CHECK(TransferBufferType::MEMORYVIEW == m_type, HAILO_INTERNAL_FAILURE,
        "copy_from function is only supported in MEMORYVIEW type TransferBuffer");

    auto continuous_parts = get_continuous_parts();
    memcpy(continuous_parts.first.data(), buffer.data(), continuous_parts.first.size());
    if (!continuous_parts.second.empty()) {
        const size_t src_offset = continuous_parts.first.size();
        memcpy(continuous_parts.second.data(), buffer.data() + src_offset, continuous_parts.second.size());
    }

    return HAILO_SUCCESS;
}

bool TransferBuffer::is_aligned_for_dma() const
{
    if (TransferBufferType::DMABUF == m_type) {
        return true;
    }

    const auto dma_able_alignment = OsUtils::get_dma_able_alignment();
    return (0 == reinterpret_cast<uintptr_t>(m_base_buffer.data()) % dma_able_alignment);
}

bool TransferBuffer::is_wrap_around() const
{
    return (m_offset + m_size) > m_base_buffer.size();
}

std::pair<MemoryView, MemoryView> TransferBuffer::get_continuous_parts()
{
    if (is_wrap_around()) {
        const auto size_to_end = m_base_buffer.size() - m_offset;
        assert(size_to_end < m_size);
        return std::make_pair(
            MemoryView(m_base_buffer.data() + m_offset, size_to_end),
            MemoryView(m_base_buffer.data(), m_size - size_to_end)
        );

    } else {
        return std::make_pair(
            MemoryView(m_base_buffer.data() + m_offset, m_size),
            MemoryView()
        );
    }
}

} /* namespace hailort */
