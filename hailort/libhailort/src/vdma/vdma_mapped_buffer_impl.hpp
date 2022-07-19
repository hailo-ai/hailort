/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/

#ifndef _HAILO_VDMA_MAPPED_BUFFER_IMPL_HPP_
#define _HAILO_VDMA_MAPPED_BUFFER_IMPL_HPP_

#include "os/mmap_buffer.hpp"
#include "os/hailort_driver.hpp"
#include "hailo/expected.hpp"

namespace hailort {
namespace vdma {

#if defined(__linux__) || defined(_MSC_VER)

class VdmaMappedBufferImpl final {
public:
    VdmaMappedBufferImpl(HailoRTDriver &driver) : m_mapped_buffer(),
        m_driver_mapped_buffer_identifier(HailoRTDriver::INVALID_DRIVER_BUFFER_HANDLE_VALUE), m_driver(driver) {}

    ~VdmaMappedBufferImpl();

    VdmaMappedBufferImpl(VdmaMappedBufferImpl &&other) noexcept : 
        m_mapped_buffer(std::move(other.m_mapped_buffer)),
        m_driver_mapped_buffer_identifier(std::exchange(other.m_driver_mapped_buffer_identifier, HailoRTDriver::INVALID_DRIVER_BUFFER_HANDLE_VALUE)),
        m_driver(other.m_driver)
    {}

    VdmaMappedBufferImpl(const VdmaMappedBufferImpl &other) = delete;
    VdmaMappedBufferImpl &operator=(const VdmaMappedBufferImpl &other) = delete;
    VdmaMappedBufferImpl &operator=(VdmaMappedBufferImpl &&other) = delete;

    void* get() { return m_mapped_buffer.get(); }

    vdma_mapped_buffer_driver_identifier& get_mapped_buffer_identifier() { return m_driver_mapped_buffer_identifier; }

    explicit operator bool()
    {
        if (m_mapped_buffer)
            return true;
        return false;
    }

    static Expected<VdmaMappedBufferImpl> allocate_vdma_buffer(HailoRTDriver &driver, size_t required_size);

private:
    VdmaMappedBufferImpl(MmapBuffer<void>&& mapped_buffer, vdma_mapped_buffer_driver_identifier driver_handle, HailoRTDriver &driver) :
        m_mapped_buffer(std::move(mapped_buffer)), m_driver_mapped_buffer_identifier(driver_handle), m_driver(driver) {}

    MmapBuffer<void> m_mapped_buffer;
    vdma_mapped_buffer_driver_identifier m_driver_mapped_buffer_identifier;
    HailoRTDriver &m_driver;
};

#elif defined(__QNX__)

class VdmaMappedBufferImpl final {
public:
    VdmaMappedBufferImpl(HailoRTDriver &driver): m_address(nullptr), m_length(0), m_driver(driver) {
        m_driver_mapped_buffer_identifier.shm_handle = INVALID_HANDLE;
        m_driver_mapped_buffer_identifier.shm_fd = INVALID_FD;
    }

    ~VdmaMappedBufferImpl();

    VdmaMappedBufferImpl(VdmaMappedBufferImpl &&other) noexcept : m_address(std::exchange(other.m_address, nullptr)),
        m_length(std::exchange(other.m_length, 0)), m_driver(other.m_driver)
    {
        m_driver_mapped_buffer_identifier.shm_handle = std::exchange(other.m_driver_mapped_buffer_identifier.shm_handle, INVALID_HANDLE);
        m_driver_mapped_buffer_identifier.shm_fd = std::exchange(other.m_driver_mapped_buffer_identifier.shm_fd, INVALID_FD);

    }

    VdmaMappedBufferImpl(const VdmaMappedBufferImpl &other) = delete;
    VdmaMappedBufferImpl &operator=(const VdmaMappedBufferImpl &other) = delete;
    VdmaMappedBufferImpl &operator=(VdmaMappedBufferImpl &&other) = delete;

    void* get() { return m_address; }

    vdma_mapped_buffer_driver_identifier& get_mapped_buffer_identifier() { return m_driver_mapped_buffer_identifier; }

    explicit operator bool()
    {
        return (nullptr != m_address);
    }

    static Expected<VdmaMappedBufferImpl> allocate_vdma_buffer(HailoRTDriver &driver, size_t required_size);

private:
    VdmaMappedBufferImpl(void *addr, size_t length, shm_handle_t shm_handle, int shm_fd, HailoRTDriver &driver) :
        m_address(addr), m_length(length), m_driver(driver)
    {
        m_driver_mapped_buffer_identifier.shm_handle = shm_handle;
        m_driver_mapped_buffer_identifier.shm_fd = shm_fd;
    }

    static const int INVALID_FD;
    static const shm_handle_t INVALID_HANDLE;

    void *m_address;
    size_t m_length;
    vdma_mapped_buffer_driver_identifier m_driver_mapped_buffer_identifier;
    HailoRTDriver &m_driver;
};

#else
#error "unsupported platform!"
#endif // defined(__linux__) || defined(_MSC_VER)

} /* namespace vdma */
} /* namespace hailort */

#endif /* _HAILO_VDMA_MAPPED_BUFFER_IMPL_HPP_ */