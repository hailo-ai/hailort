#include "mapped_buffer.hpp"
#include "microprofile.h"

namespace hailort {
namespace vdma {

Expected<MappedBuffer> MappedBuffer::create(size_t required_size, HailoRTDriver::DmaDirection data_direction,
    HailoRTDriver &driver)
{
    hailo_status status = HAILO_UNINITIALIZED;
    MappedBuffer object(required_size, data_direction, driver, status);
    if (HAILO_SUCCESS != status) {
        return make_unexpected(status);
    }

    return object;
}

MappedBuffer::MappedBuffer(
    size_t required_size, HailoRTDriver::DmaDirection data_direction, HailoRTDriver &driver, hailo_status &status)
    : m_user_address(), m_size(required_size), m_driver(driver),
      m_driver_buff_handle(HailoRTDriver::INVALID_DRIVER_BUFFER_HANDLE_VALUE)
{
    auto buffer = allocate_vdma_buffer(driver, required_size, m_driver_buff_handle);
    if (! buffer) {
        status = buffer.status();
        return;
    }

    auto expected_handle = m_driver.vdma_buffer_map(buffer->get(), required_size, data_direction, m_driver_buff_handle);
    if (!expected_handle) {
        status = expected_handle.status();
        return;
    }
    
    m_handle = expected_handle.release();
    m_user_address = buffer.release();
    status = HAILO_SUCCESS;
}

MappedBuffer::~MappedBuffer()
{
    if (m_user_address) {
        m_driver.vdma_buffer_unmap(m_handle);

        if (HailoRTDriver::INVALID_DRIVER_BUFFER_HANDLE_VALUE != m_driver_buff_handle) {
            m_driver.vdma_low_memory_buffer_free(m_driver_buff_handle);
        }
    }
}

hailo_status MappedBuffer::write(const void *buf_src, size_t count, size_t offset)
{
    if ((count + offset) > m_size) {
        LOGGER__ERROR("Requested size {} from offset {} is more than the MappedBuffer size {}", count, offset, m_size);
        return HAILO_INSUFFICIENT_BUFFER;
    }

    if (count > 0) {
        auto dst_vdma_address = (uint8_t*)m_user_address.get() + offset;
        memcpy(dst_vdma_address, buf_src, count);

        auto status = m_driver.vdma_buffer_sync(m_handle, HailoRTDriver::DmaDirection::H2D, dst_vdma_address, count);
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed synching vdma buffer on write");
            return status;
        }
    }
    
    return HAILO_SUCCESS;
}

hailo_status MappedBuffer::read(void *buf_dst, size_t count, size_t offset)
{
    if ((count + offset) > m_size) {
        LOGGER__ERROR("Requested size {} from offset {} is more than the MappedBuffer size {}", count, offset, m_size);
        return HAILO_INSUFFICIENT_BUFFER;
    }

    if (count > 0) {
        auto dst_vdma_address = (uint8_t*)m_user_address.get() + offset;
        auto status = m_driver.vdma_buffer_sync(m_handle, HailoRTDriver::DmaDirection::D2H, dst_vdma_address, count);
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed synching vdma buffer on read");
            return status;
        }

        memcpy(buf_dst, dst_vdma_address, count);
    }
    
    return HAILO_SUCCESS;
}

hailo_status MappedBuffer::write_cyclic(const void *buf_src, size_t count, size_t offset)
{
    MICROPROFILE_SCOPEI("vDMA", "Write buffer", 0);
    if (count > m_size) {
        LOGGER__ERROR("Requested size({}) is more than the MappedBuffer size {}", count, m_size);
        return HAILO_INSUFFICIENT_BUFFER;
    }

    auto size_to_end = m_size - offset;
    auto copy_size = std::min(size_to_end, count);
    auto status = write(buf_src, copy_size, offset);
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

hailo_status MappedBuffer::read_cyclic(void *buf_dst, size_t count, size_t offset)
{
    MICROPROFILE_SCOPEI("vDMA", "Read buffer", 0);
    if (count > m_size) {
        LOGGER__ERROR("Requested size({}) is more than the MappedBuffer size {}", count, m_size);
        return HAILO_INSUFFICIENT_BUFFER;
    }

    auto size_to_end = m_size - offset;
    auto copy_size = std::min(size_to_end, count);
    auto status = read(buf_dst, copy_size, offset);
    if (HAILO_SUCCESS != status) {
        return status;
    }

    auto remaining_size = count - copy_size;
    if (remaining_size > 0) {
        status = read((uint8_t*)buf_dst + copy_size, remaining_size, 0);
        if (HAILO_SUCCESS != status) {
            return status;
        }
    }

    return HAILO_SUCCESS;
}

Expected<MmapBuffer<void>> MappedBuffer::allocate_vdma_buffer(HailoRTDriver &driver, size_t required_size,
    uintptr_t &driver_buff_handle)
{
    // Check if driver should be allocated from driver or from user
    if (driver.allocate_driver_buffer()) {
        auto driver_buffer_handle = driver.vdma_low_memory_buffer_alloc(required_size);
        CHECK_EXPECTED(driver_buffer_handle);

        driver_buff_handle = driver_buffer_handle.release();

        return MmapBuffer<void>::create_file_map(required_size, driver.fd(), driver_buff_handle);
    }
    else {
        return MmapBuffer<void>::create_shared_memory(required_size);
    }
}

} /* namespace vdma */
} /* namespace hailort */
