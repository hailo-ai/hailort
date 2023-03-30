/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file mapped_buffer_impl.hpp
 * @brief Vdma mapped buffer pimpl class defintion
 **/
#ifndef _HAILO_VDMA_MAPPED_BUFFER_IMPL_HPP_
#define _HAILO_VDMA_MAPPED_BUFFER_IMPL_HPP_

#include "hailo/dma_mapped_buffer.hpp"
#include "os/mmap_buffer.hpp"
#include "os/hailort_driver.hpp"
#include "hailo/expected.hpp"

namespace hailort {

#if defined(__linux__) || defined(_MSC_VER)

class DmaMappedBuffer::Impl final {
public:
    // If user_address is nullptr, a buffer of size 'size' will be allocated and mapped to dma in 'data_direction'
    // Otherwise, the buffer pointed to by user_address will be mapped to dma in 'data_direction'
    static Expected<Impl> create(HailoRTDriver &driver, HailoRTDriver::DmaDirection data_direction,
        size_t size, void *user_address = nullptr);

    Impl(Impl &&other) noexcept;
    Impl(const Impl &other) = delete;
    Impl &operator=(const Impl &other) = delete;
    Impl &operator=(Impl &&other) = delete;
    ~Impl();

    void* user_address();
    size_t size() const;
    HailoRTDriver::VdmaBufferHandle handle();
    // TODO: validate that offset is cache aligned (HRT-9811)
    hailo_status synchronize(size_t offset, size_t count);

private:
    Impl(vdma_mapped_buffer_driver_identifier driver_allocated_buffer_id, size_t size,
         HailoRTDriver::DmaDirection data_direction, void *user_address, MmapBuffer<void> &&mapped_buffer,
         HailoRTDriver &driver, hailo_status &status);
    Impl(vdma_mapped_buffer_driver_identifier driver_allocated_buffer_id, size_t size,
         HailoRTDriver::DmaDirection data_direction, MmapBuffer<void> &&mapped_buffer, HailoRTDriver &driver,
         hailo_status &status);

    HailoRTDriver &m_driver;
    vdma_mapped_buffer_driver_identifier m_driver_allocated_buffer_id;
    HailoRTDriver::VdmaBufferHandle m_mapping_handle;
    MmapBuffer<void> m_mapped_buffer;
    const size_t m_size;
    const HailoRTDriver::DmaDirection m_data_direction;
    void *const m_user_address;
};

#elif defined(__QNX__)

// TODO: merge qnx and non-qnx impls (HRT-9508)
class DmaMappedBuffer::Impl final {
public:
    static Expected<Impl> create(HailoRTDriver &driver, HailoRTDriver::DmaDirection data_direction,
        size_t size, void *user_address = nullptr);

    Impl(const Impl &other) = delete;
    Impl &operator=(const Impl &other) = delete;
    Impl &operator=(Impl &&other) = delete;
    Impl(Impl &&other) noexcept;
    ~Impl();

    void* user_address();
    size_t size() const;
    HailoRTDriver::VdmaBufferHandle handle();
    hailo_status synchronize(size_t offset, size_t count);

private:
    Impl(void *addr, size_t size, HailoRTDriver::DmaDirection data_direction,
        shm_handle_t shm_handle, int shm_fd, HailoRTDriver &driver, hailo_status &status);

    static const int INVALID_FD;
    static const shm_handle_t INVALID_HANDLE;
    static const char* VDMA_BUFFER_TYPE_MEMORY_NAME;

    HailoRTDriver &m_driver;
    void *m_address;
    const size_t m_size;
    const HailoRTDriver::DmaDirection m_data_direction;
    vdma_mapped_buffer_driver_identifier m_driver_allocated_buffer_id;
    HailoRTDriver::VdmaBufferHandle m_mapping_handle;
};

#else
#error "unsupported platform!"
#endif // defined(__linux__) || defined(_MSC_VER)

} /* namespace hailort */

#endif /* _HAILO_VDMA_MAPPED_BUFFER_IMPL_HPP_ */