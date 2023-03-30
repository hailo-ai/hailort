/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file mapped_buffer_impl.cpp
 * @brief Dma mapped buffer pimpl class implementation
 **/
#include "mapped_buffer_impl.hpp"

namespace hailort {

#if defined(__linux__) || defined(_MSC_VER)

Expected<DmaMappedBuffer::Impl> DmaMappedBuffer::Impl::create(HailoRTDriver &driver,
    HailoRTDriver::DmaDirection data_direction, size_t size, void *user_address)
{
    if (nullptr != user_address) {
        // User allocated buffer - create an empty MmapBuffer<void> (it doesn't hold the buffer)
        auto status = HAILO_UNINITIALIZED;
        auto result = DmaMappedBuffer::Impl(HailoRTDriver::INVALID_DRIVER_BUFFER_HANDLE_VALUE, size,
            data_direction, user_address, MmapBuffer<void>(), driver, status);
        CHECK_SUCCESS_AS_EXPECTED(status);

        return result;
    } else if (driver.allocate_driver_buffer()) {
        // Allocate buffer via driver
        auto driver_buffer_handle = driver.vdma_low_memory_buffer_alloc(size);
        CHECK_EXPECTED(driver_buffer_handle);

        uintptr_t driver_buff_handle = driver_buffer_handle.release();

        auto mapped_buffer = MmapBuffer<void>::create_file_map(size, driver.fd(), driver_buff_handle);
        CHECK_EXPECTED(mapped_buffer);

        auto status = HAILO_UNINITIALIZED;
        auto result = DmaMappedBuffer::Impl(driver_buff_handle, size, data_direction, mapped_buffer.release(),
            driver, status);
        CHECK_SUCCESS_AS_EXPECTED(status);

        return result;
    } else {
        // Standard userspace allocation
        auto mapped_buffer = MmapBuffer<void>::create_shared_memory(size);
        CHECK_EXPECTED(mapped_buffer);

        auto status = HAILO_UNINITIALIZED;
        auto result = DmaMappedBuffer::Impl(HailoRTDriver::INVALID_DRIVER_BUFFER_HANDLE_VALUE, size,
            data_direction, mapped_buffer.release(), driver, status);
        CHECK_SUCCESS_AS_EXPECTED(status);

        return result;
    }
}

DmaMappedBuffer::Impl::Impl(vdma_mapped_buffer_driver_identifier driver_allocated_buffer_id,
                            size_t size, HailoRTDriver::DmaDirection data_direction, void *user_address,
                            MmapBuffer<void> &&mapped_buffer, HailoRTDriver &driver, hailo_status &status) :
    m_driver(driver),
    m_driver_allocated_buffer_id(driver_allocated_buffer_id),
    m_mapping_handle(HailoRTDriver::INVALID_DRIVER_VDMA_MAPPING_HANDLE_VALUE),
    m_mapped_buffer(std::move(mapped_buffer)),
    m_size(size),
    m_data_direction(data_direction),
    m_user_address(user_address)
{
    if (m_mapped_buffer.is_mapped() && (m_user_address != m_mapped_buffer.address())) {
        status = HAILO_INVALID_ARGUMENT;
        return;
    }

    auto expected_handle = driver.vdma_buffer_map(m_user_address, m_size, m_data_direction,
        m_driver_allocated_buffer_id);
    if (!expected_handle) {
        status = expected_handle.status();
        return;
    }

    m_mapping_handle = expected_handle.release();
    status = HAILO_SUCCESS;
}

DmaMappedBuffer::Impl::Impl(vdma_mapped_buffer_driver_identifier driver_allocated_buffer_id,
                            size_t size, HailoRTDriver::DmaDirection data_direction,
                            MmapBuffer<void> &&mapped_buffer, HailoRTDriver &driver, hailo_status &status) :
    Impl(driver_allocated_buffer_id, size, data_direction, mapped_buffer.address(), std::move(mapped_buffer), driver, status)
{}

DmaMappedBuffer::Impl::Impl(Impl &&other) noexcept :
    m_driver(other.m_driver),
    m_driver_allocated_buffer_id(std::exchange(other.m_driver_allocated_buffer_id, HailoRTDriver::INVALID_DRIVER_BUFFER_HANDLE_VALUE)),
    m_mapping_handle(std::exchange(other.m_mapping_handle, HailoRTDriver::INVALID_DRIVER_VDMA_MAPPING_HANDLE_VALUE)),
    m_mapped_buffer(std::move(other.m_mapped_buffer)),
    m_size(std::move(other.m_size)),
    m_data_direction(std::move(other.m_data_direction)),
    m_user_address(std::move(other.m_user_address))
{}

DmaMappedBuffer::Impl::~Impl()
{
    if (HailoRTDriver::INVALID_DRIVER_VDMA_MAPPING_HANDLE_VALUE != m_mapping_handle) {
        m_driver.vdma_buffer_unmap(m_mapping_handle);
        m_mapping_handle = HailoRTDriver::INVALID_DRIVER_VDMA_MAPPING_HANDLE_VALUE;
    }

    if (HailoRTDriver::INVALID_DRIVER_BUFFER_HANDLE_VALUE != m_driver_allocated_buffer_id) {
        m_driver.vdma_low_memory_buffer_free(m_driver_allocated_buffer_id);
        m_driver_allocated_buffer_id = HailoRTDriver::INVALID_DRIVER_BUFFER_HANDLE_VALUE;
    }
}

void* DmaMappedBuffer::Impl::user_address()
{
    return m_user_address;
}

size_t DmaMappedBuffer::Impl::size() const
{
    return m_size;
}

HailoRTDriver::VdmaBufferHandle DmaMappedBuffer::Impl::handle()
{
    return m_mapping_handle;
}

hailo_status DmaMappedBuffer::Impl::synchronize(size_t offset, size_t count)
{
    CHECK(offset + count <= size(), HAILO_INVALID_ARGUMENT,
        "Synchronizing {} bytes starting at offset {} will overflow (buffer size {})",
        offset, count, size());
    return m_driver.vdma_buffer_sync(m_mapping_handle, m_data_direction, offset, count);
}

#elif defined(__QNX__)

#include <fcntl.h>

const int DmaMappedBuffer::Impl::INVALID_FD = -1;
const shm_handle_t DmaMappedBuffer::Impl::INVALID_HANDLE = (shm_handle_t)-1;
const char* DmaMappedBuffer::Impl::VDMA_BUFFER_TYPE_MEMORY_NAME = "/memory/below4G/ram/below1G";

Expected<DmaMappedBuffer::Impl> DmaMappedBuffer::Impl::create(HailoRTDriver &driver,
    HailoRTDriver::DmaDirection data_direction, size_t size, void *user_address)
{
    // TODO: HRT-9508
    CHECK_AS_EXPECTED(user_address == nullptr, HAILO_NOT_IMPLEMENTED, "User allocated buffers not supported on qnx");

    // Destructor of type_mem_fd will close fd
    FileDescriptor type_mem_fd(posix_typed_mem_open(VDMA_BUFFER_TYPE_MEMORY_NAME, O_RDWR, POSIX_TYPED_MEM_ALLOCATE));
    if (INVALID_FD == type_mem_fd) {
        LOGGER__ERROR("Error getting fd from typed memory of type {}, errno {}\n", VDMA_BUFFER_TYPE_MEMORY_NAME,
            errno);
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }

    vdma_mapped_buffer_driver_identifier driver_buff_handle;
    driver_buff_handle.shm_fd = shm_open(SHM_ANON, O_RDWR | O_CREAT, 0777);
    CHECK_AS_EXPECTED(INVALID_FD != driver_buff_handle.shm_fd, HAILO_INTERNAL_FAILURE,
        "Error creating shm object, errno is: {}", errno);

    // backs the shared memory object with physical memory
    int err = shm_ctl(driver_buff_handle.shm_fd, SHMCTL_ANON | SHMCTL_TYMEM, (uint64_t)type_mem_fd,
        size);
    if (-1 == err) {
        LOGGER__ERROR("Error backing shm object in physical memory, errno is: {}", errno);
        close(driver_buff_handle.shm_fd);
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }

    // Create shared memory handle to send to driver
    err = shm_create_handle(driver_buff_handle.shm_fd, driver.resource_manager_pid(), O_RDWR,
        &driver_buff_handle.shm_handle, 0);
    if (0 != err) {
        LOGGER__ERROR("Error creating shm object handle, errno is: {}", errno);
        close(driver_buff_handle.shm_fd);
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }

    void *address = mmap(0, size, PROT_WRITE | PROT_READ | PROT_NOCACHE, MAP_SHARED, driver_buff_handle.shm_fd, 0);
    if (MAP_FAILED == address) {
        LOGGER__ERROR("Failed to mmap buffer with errno:{}", errno);
        shm_delete_handle(driver_buff_handle.shm_handle);
        close(driver_buff_handle.shm_fd);
        return make_unexpected(HAILO_OUT_OF_HOST_MEMORY);
    }

    hailo_status status = HAILO_UNINITIALIZED;
    auto result = DmaMappedBuffer::Impl(address, size, data_direction, driver_buff_handle.shm_handle,
        driver_buff_handle.shm_fd, driver, status);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to map buffer to vdma");
        munmap(address, size);
        shm_delete_handle(driver_buff_handle.shm_handle);
        close(driver_buff_handle.shm_fd);
        return make_unexpected(status);
    }

    return result;
}

DmaMappedBuffer::Impl::Impl(void *addr, size_t size, HailoRTDriver::DmaDirection data_direction,
                         shm_handle_t shm_handle, int shm_fd, HailoRTDriver &driver, hailo_status &status) :
    m_driver(driver),
    m_address(addr),
    m_size(size),
    m_data_direction(data_direction)
{
    m_driver_allocated_buffer_id.shm_handle = shm_handle;
    m_driver_allocated_buffer_id.shm_fd = shm_fd;

    auto expected_handle = driver.vdma_buffer_map(addr, size, data_direction, m_driver_allocated_buffer_id);
    if (!expected_handle) {
        status = expected_handle.status();
        return;
    }

    m_mapping_handle = expected_handle.release();
    status = HAILO_SUCCESS;
}

DmaMappedBuffer::Impl::Impl(Impl &&other) noexcept :
    m_driver(other.m_driver),
    m_address(std::exchange(other.m_address, nullptr)),
    m_size(std::move(other.m_size)),
    m_data_direction(std::move(other.m_data_direction)),
    m_mapping_handle(std::exchange(other.m_mapping_handle, HailoRTDriver::INVALID_DRIVER_VDMA_MAPPING_HANDLE_VALUE))
{
    m_driver_allocated_buffer_id.shm_handle = std::exchange(other.m_driver_allocated_buffer_id.shm_handle, INVALID_HANDLE);
    m_driver_allocated_buffer_id.shm_fd = std::exchange(other.m_driver_allocated_buffer_id.shm_fd, INVALID_FD);
}

DmaMappedBuffer::Impl::~Impl()
{
    if (HailoRTDriver::INVALID_DRIVER_VDMA_MAPPING_HANDLE_VALUE != m_mapping_handle) {
        m_driver.vdma_buffer_unmap(m_mapping_handle);
        m_mapping_handle = HailoRTDriver::INVALID_DRIVER_VDMA_MAPPING_HANDLE_VALUE;
    }

    if (nullptr != m_address) {
        if (0 != munmap(m_address, m_size)) {
            LOGGER__ERROR("Error unmapping memory at address {}, Errno: {}", m_address, errno);
        }
    }

    if (INVALID_FD != m_driver_allocated_buffer_id.shm_fd) {
        if (0 != close(m_driver_allocated_buffer_id.shm_fd)) {
            LOGGER__ERROR("Error closing shared memory fd, Errno: {}", errno);
        }
    }
}

void* DmaMappedBuffer::Impl::user_address()
{
    return m_address;
}
size_t DmaMappedBuffer::Impl::size() const
{
    return m_size;
}

HailoRTDriver::VdmaBufferHandle DmaMappedBuffer::Impl::handle()
{
    return m_mapping_handle;
}

hailo_status DmaMappedBuffer::Impl::synchronize(size_t offset, size_t count)
{
    CHECK(offset + count <= size(), HAILO_INVALID_ARGUMENT,
        "Synchronizing {} bytes starting at offset {} will overflow (buffer size {})",
        offset, count, size());
    return m_driver.vdma_buffer_sync(m_mapping_handle, m_data_direction, offset, count);
}

#else
#error "unsupported platform!"
#endif // defined(__linux__) || defined(_MSC_VER)

} /* namespace hailort */