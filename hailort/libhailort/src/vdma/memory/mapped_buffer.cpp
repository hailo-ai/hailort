/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file mapped_buffer.cpp
 * @brief Vdma mapped buffer implementation
 **/

#include "mapped_buffer.hpp"


namespace hailort {
namespace vdma {

Expected<MappedBufferPtr> MappedBuffer::create_shared(DmaAbleBufferPtr buffer, HailoRTDriver &driver,
    HailoRTDriver::DmaDirection data_direction)
{
    TRY(auto buffer_handle, driver.vdma_buffer_map(reinterpret_cast<uintptr_t>(buffer->user_address()), buffer->size(), data_direction,
        buffer->buffer_identifier(), HailoRTDriver::DmaBufferType::USER_PTR_BUFFER));

    auto result = make_shared_nothrow<MappedBuffer>(driver, buffer, data_direction, buffer_handle, buffer->size());
    CHECK_NOT_NULL_AS_EXPECTED(result, HAILO_OUT_OF_HOST_MEMORY);

    return result;
}

Expected<MappedBufferPtr> MappedBuffer::create_shared_by_allocation(size_t size, HailoRTDriver &driver,
    HailoRTDriver::DmaDirection data_direction)
{
    auto buffer = DmaAbleBuffer::create_by_allocation(size, driver);
    CHECK_EXPECTED(buffer);

    return create_shared(buffer.release(), driver, data_direction);
}

Expected<MappedBufferPtr> MappedBuffer::create_shared_from_dmabuf(int dmabuf_fd, size_t size, HailoRTDriver &driver,
    HailoRTDriver::DmaDirection data_direction)
{
    TRY(auto buffer_handle, driver.vdma_buffer_map_dmabuf(dmabuf_fd, size, data_direction,
        HailoRTDriver::DmaBufferType::DMABUF_BUFFER));

    // TODO: if need user address for dmabuf use DmaBufDmaAbleBuffer
    auto result = make_shared_nothrow<MappedBuffer>(driver, nullptr, data_direction, buffer_handle, size, dmabuf_fd);
    CHECK_NOT_NULL_AS_EXPECTED(result, HAILO_OUT_OF_HOST_MEMORY);

    return result;
}

MappedBuffer::MappedBuffer(HailoRTDriver &driver, DmaAbleBufferPtr buffer, HailoRTDriver::DmaDirection data_direction,
    HailoRTDriver::VdmaBufferHandle vdma_buffer_handle, size_t size, int fd) :
    m_driver(driver),
    m_buffer(buffer),
    m_mapping_handle(vdma_buffer_handle),
    m_data_direction(data_direction),
    m_size(size),
    m_fd(fd)
{}

MappedBuffer::~MappedBuffer()
{
    if (HailoRTDriver::INVALID_DRIVER_VDMA_MAPPING_HANDLE_VALUE != m_mapping_handle) {
        auto address = INVALID_FD != m_fd ? static_cast<uintptr_t>(m_fd) : reinterpret_cast<uintptr_t>(user_address());
        m_driver.vdma_buffer_unmap(address, size(), m_data_direction);
        m_mapping_handle = HailoRTDriver::INVALID_DRIVER_VDMA_MAPPING_HANDLE_VALUE;
    }
}

MappedBuffer::MappedBuffer(MappedBuffer &&other) noexcept :
    m_driver(other.m_driver),
    m_buffer(std::move(other.m_buffer)),
    m_mapping_handle(std::exchange(other.m_mapping_handle, HailoRTDriver::INVALID_DRIVER_VDMA_MAPPING_HANDLE_VALUE)),
    m_data_direction(other.m_data_direction),
    m_size(other.m_size)
{}

void* MappedBuffer::user_address()
{
    assert (m_buffer); // On dmabuf, m_buffer does not exist
    return m_buffer->user_address();
}

size_t MappedBuffer::size() const
{
    return m_size;
}

HailoRTDriver::DmaDirection MappedBuffer::direction() const
{
    return m_data_direction;
}

Expected<int> MappedBuffer::fd()
{
    CHECK(INVALID_FD != m_fd, HAILO_INTERNAL_FAILURE, "fd is only supported for DMABUF type MappedBuffer");

    return Expected<int>(m_fd);
}

HailoRTDriver::VdmaBufferHandle MappedBuffer::handle()
{
    return m_mapping_handle;
}

hailo_status MappedBuffer::synchronize(HailoRTDriver::DmaSyncDirection sync_direction)
{
    static constexpr auto BUFFER_START = 0;
    return synchronize(BUFFER_START, size(), sync_direction);
}

hailo_status MappedBuffer::synchronize(size_t offset, size_t count, HailoRTDriver::DmaSyncDirection sync_direction)
{
    CHECK(offset + count <= size(), HAILO_INVALID_ARGUMENT,
        "Synchronizing {} bytes starting at offset {} will overflow (buffer size {})",
        offset, count, size());
    return m_driver.vdma_buffer_sync(m_mapping_handle, sync_direction, offset, count);
}

hailo_status MappedBuffer::write(const void *buf_src, size_t count, size_t offset, bool should_sync)
{
    if ((count + offset) > size()) {
        LOGGER__ERROR("Requested size {} from offset {} is more than the buffer size {}", count, offset, size());
        return HAILO_INSUFFICIENT_BUFFER;
    }

    if (count > 0) {
        auto dst_addr = static_cast<uint8_t*>(user_address()) + offset;
        memcpy(dst_addr, buf_src, count);

        if (should_sync) {
            auto status = synchronize(offset, count, HailoRTDriver::DmaSyncDirection::TO_DEVICE);
            CHECK_SUCCESS(status, "Failed synching vdma buffer on write");
        }
    }

    return HAILO_SUCCESS;
}

hailo_status MappedBuffer::read(void *buf_dst, size_t count, size_t offset, bool should_sync)
{
    if ((count + offset) > size()) {
        LOGGER__ERROR("Requested size {} from offset {} is more than the buffer size {}", count, offset, size());
        return HAILO_INSUFFICIENT_BUFFER;
    }

    if (count > 0) {
        const auto src_addr = static_cast<uint8_t*>(user_address()) + offset;
        if (should_sync) {
            const auto status = synchronize(offset, count, HailoRTDriver::DmaSyncDirection::TO_HOST);
            CHECK_SUCCESS(status, "Failed synching vdma buffer on read");
        }

        memcpy(buf_dst, src_addr, count);
    }

    return HAILO_SUCCESS;
}

hailo_status MappedBuffer::write_cyclic(const void *buf_src, size_t count, size_t offset, bool should_sync)
{
    if (count > size()) {
        LOGGER__ERROR("Requested size({}) is more than the buffer size {}", count, size());
        return HAILO_INSUFFICIENT_BUFFER;
    }

    auto size_to_end = size() - offset;
    auto copy_size = std::min(size_to_end, count);
    auto status = write(buf_src, copy_size, offset, should_sync);
    if (HAILO_SUCCESS != status) {
        return status;
    }

    auto remaining_size = count - copy_size;
    if (remaining_size > 0) {
        status = write((uint8_t*)buf_src + copy_size, remaining_size, 0);
        if (HAILO_SUCCESS != status) {
            return status;
        }
    }

    return HAILO_SUCCESS;
}

hailo_status MappedBuffer::read_cyclic(void *buf_dst, size_t count, size_t offset, bool should_sync)
{
    if (count > size()) {
        LOGGER__ERROR("Requested size({}) is more than the buffer size {}", count, size());
        return HAILO_INSUFFICIENT_BUFFER;
    }

    auto size_to_end = size() - offset;
    auto copy_size = std::min(size_to_end, count);
    auto status = read(buf_dst, copy_size, offset, should_sync);
    if (HAILO_SUCCESS != status) {
        return status;
    }

    auto remaining_size = count - copy_size;
    if (remaining_size > 0) {
        status = read((uint8_t*)buf_dst + copy_size, remaining_size, 0, should_sync);
        if (HAILO_SUCCESS != status) {
            return status;
        }
    }

    return HAILO_SUCCESS;
}

} /* namespace vdma */
} /* namespace hailort */
