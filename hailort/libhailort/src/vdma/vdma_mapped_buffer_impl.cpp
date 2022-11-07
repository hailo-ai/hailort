#include "vdma_mapped_buffer_impl.hpp"

namespace hailort {
namespace vdma {

#if defined(__linux__) || defined(_MSC_VER)

Expected<VdmaMappedBufferImpl> VdmaMappedBufferImpl::allocate_vdma_buffer(HailoRTDriver &driver, size_t required_size)
{
    // Check if driver should be allocated from driver or from user
    if (driver.allocate_driver_buffer()) {
        auto driver_buffer_handle = driver.vdma_low_memory_buffer_alloc(required_size);
        CHECK_EXPECTED(driver_buffer_handle);

        uintptr_t driver_buff_handle = driver_buffer_handle.release();

        auto mapped_buffer = MmapBuffer<void>::create_file_map(required_size, driver.fd(), driver_buff_handle);
        CHECK_EXPECTED(mapped_buffer);

        return VdmaMappedBufferImpl(mapped_buffer.release(), driver_buff_handle, driver);
    }
    else {
        auto mapped_buffer = MmapBuffer<void>::create_shared_memory(required_size);
        CHECK_EXPECTED(mapped_buffer);
        return VdmaMappedBufferImpl(mapped_buffer.release(), HailoRTDriver::INVALID_DRIVER_BUFFER_HANDLE_VALUE, driver);
    }
}

VdmaMappedBufferImpl::~VdmaMappedBufferImpl()
{
    if (m_mapped_buffer) {
        if (HailoRTDriver::INVALID_DRIVER_BUFFER_HANDLE_VALUE != m_driver_mapped_buffer_identifier) {
            m_driver.vdma_low_memory_buffer_free(m_driver_mapped_buffer_identifier);
        }
    }
}

#elif defined(__QNX__)

#include <fcntl.h> 

const int VdmaMappedBufferImpl::INVALID_FD = -1;
const shm_handle_t VdmaMappedBufferImpl::INVALID_HANDLE = (shm_handle_t)-1;
const char* VdmaMappedBufferImpl::VDMA_BUFFER_TYPE_MEMORY_NAME = "/memory/below4G/ram/below1G";

Expected<VdmaMappedBufferImpl> VdmaMappedBufferImpl::allocate_vdma_buffer(HailoRTDriver &driver, size_t required_size)
{
    // Desctructor of type_mem_fd will close fd
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
        required_size);
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

    void *address = mmap(0, required_size, PROT_WRITE | PROT_READ | PROT_NOCACHE, MAP_SHARED, driver_buff_handle.shm_fd, 0);
    if (MAP_FAILED == address) {
        LOGGER__ERROR("Failed to mmap buffer with errno:{}", errno);
        shm_delete_handle(driver_buff_handle.shm_handle);
        close(driver_buff_handle.shm_fd);
        return make_unexpected(HAILO_OUT_OF_HOST_MEMORY);
    }

    return VdmaMappedBufferImpl(address, required_size, driver_buff_handle.shm_handle, driver_buff_handle.shm_fd, driver);
}

VdmaMappedBufferImpl::~VdmaMappedBufferImpl()
{
    if (nullptr != m_address) {
        if (0 != munmap(m_address, m_length)) {
            LOGGER__ERROR("Error unmapping memory at address {}, Errno: {}", m_address, errno);
        }

        if (INVALID_FD != m_driver_mapped_buffer_identifier.shm_fd) {
            if (0 != close(m_driver_mapped_buffer_identifier.shm_fd)) {
                LOGGER__ERROR("Error closing shared memory fd, Errno: {}", errno);
            }
        }
    }
}

#else
#error "unsupported platform!"
#endif // defined(__linux__) || defined(_MSC_VER)

} /* namespace vdma */
} /* namespace hailort */