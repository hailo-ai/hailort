/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file mmap_buffer.cpp
 * @brief Wrapper around unix memory mapping (mmap)
 **/

#include "common/mmap_buffer.hpp"
#include <sys/ioctl.h>

#include <sys/mman.h>
#include <errno.h>

#if defined(__linux__)
#include <linux/mman.h>
#endif

// If MAP_UNINITIALIZED isn't defined (MAP_UNINITIALIZED isn't POSIX standard) then it has no impact in mmap function
#ifndef MAP_UNINITIALIZED
#define MAP_UNINITIALIZED (0)
#endif

namespace hailort
{

#define INVALID_FD (-1)


void * const MmapBufferImpl::INVALID_ADDR = MAP_FAILED;

Expected<MmapBufferImpl> MmapBufferImpl::create_shared_memory(size_t length)
{
    void *address = mmap(nullptr, length, PROT_WRITE | PROT_READ,
        MAP_ANONYMOUS | MAP_SHARED | MAP_UNINITIALIZED,
        INVALID_FD, /*offset=*/ 0);

    CHECK_AS_EXPECTED(INVALID_ADDR != address, HAILO_OUT_OF_HOST_MEMORY, "Failed to mmap buffer with errno:{}", errno);
    return MmapBufferImpl(address, length);
}

Expected<MmapBufferImpl> MmapBufferImpl::create_file_map(size_t length, FileDescriptor &file, uintptr_t offset)
{
    void *address = mmap(nullptr, length, PROT_WRITE | PROT_READ, MAP_SHARED, file, (off_t)offset);
    CHECK_AS_EXPECTED(INVALID_ADDR != address, HAILO_INTERNAL_FAILURE, "Failed to mmap buffer fd with errno:{}", errno);
    return MmapBufferImpl(address, length);
}

#if defined(__QNX__)
Expected<MmapBufferImpl> MmapBufferImpl::create_file_map_nocache(size_t length, FileDescriptor &file, uintptr_t offset)
{
    void *address = mmap(nullptr, length, PROT_WRITE | PROT_READ | PROT_NOCACHE, MAP_SHARED, file, (off_t)offset);
    CHECK_AS_EXPECTED(INVALID_ADDR != address, HAILO_INTERNAL_FAILURE, "Failed to mmap buffer fd with errno:{}", errno);
    return MmapBufferImpl(address, length);
}
#endif /* defined(__QNX__) */

hailo_status MmapBufferImpl::unmap()
{
    if (!is_mapped()) {
        return HAILO_SUCCESS;
    }

    if (0 != munmap(m_address, m_length)) {
        LOGGER__ERROR("munmap of address {}, length: {} failed with errno {}", (void*)m_address, m_length, errno);
        return HAILO_INTERNAL_FAILURE;
    }

    m_address = INVALID_ADDR;
    m_length = 0;
    return HAILO_SUCCESS;
}

} /* namespace hailort */
