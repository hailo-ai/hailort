/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file shared_memory_buffer.cpp
 * @brief Posix Shared memory implementation
 **/

#include "common/shared_memory_buffer.hpp"
#include "common/utils.hpp"

#include "hailo/hailort.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

namespace hailort
{

#ifndef __ANDROID__

Expected<SharedMemoryBufferPtr> SharedMemoryBuffer::create(size_t size, const std::string &shm_name)
{
    auto shm_segment_fd = shm_open(shm_name.c_str(), (O_CREAT | O_RDWR), (S_IRWXU | S_IRWXG | S_IRWXO)); // mode 0777
    CHECK_AS_EXPECTED((shm_segment_fd != -1), HAILO_INTERNAL_FAILURE, "Failed to create shared memory object, errno = {}", errno);
    auto shm_fd = FileDescriptor(shm_segment_fd);

    auto res = ftruncate(shm_fd, size);
    CHECK_AS_EXPECTED(res != -1, HAILO_INTERNAL_FAILURE, "Failed to set size of shared memory object, errno = {}", errno);

    TRY(auto mmapped_buffer, MmapBuffer<void>::create_file_map(size, shm_fd, 0));
    auto result = make_shared_nothrow<SharedMemoryBuffer>(shm_name, std::move(mmapped_buffer), true);
    CHECK_NOT_NULL_AS_EXPECTED(result, HAILO_OUT_OF_HOST_MEMORY);

    return result;
}

Expected<SharedMemoryBufferPtr> SharedMemoryBuffer::open(size_t size, const std::string &shm_name)
{
    auto shm_segment_fd = shm_open(shm_name.c_str(), O_RDWR, (S_IRWXU | S_IRWXG | S_IRWXO)); // mode 0777
    CHECK_AS_EXPECTED((shm_segment_fd != -1), HAILO_INTERNAL_FAILURE, "Failed to open shared memory object, errno = {}", errno);
    auto shm_fd = FileDescriptor(shm_segment_fd);

    TRY(auto mmapped_buffer, MmapBuffer<void>::create_file_map(size, shm_fd, 0));
    auto result = make_shared_nothrow<SharedMemoryBuffer>(shm_name, std::move(mmapped_buffer), false);
    CHECK_NOT_NULL_AS_EXPECTED(result, HAILO_OUT_OF_HOST_MEMORY);

    return result;
}

SharedMemoryBuffer::~SharedMemoryBuffer()
{
    if (m_memory_owner) {
        shm_unlink(m_shm_name.c_str());
    }
}

size_t SharedMemoryBuffer::size() const
{
    return m_shm_mmap_buffer.size();
}

void *SharedMemoryBuffer::user_address()
{
    return m_shm_mmap_buffer.address();
}

std::string SharedMemoryBuffer::shm_name()
{
    return m_shm_name;
}

#else

// TODO: HRT-14770 support android shared memory
Expected<SharedMemoryBufferPtr> SharedMemoryBuffer::create(size_t, const std::string &)
{
    LOGGER__ERROR("SharedMemoryBuffer::create is not implemented for Android");
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

Expected<SharedMemoryBufferPtr> SharedMemoryBuffer::open(size_t, const std::string &)
{
    LOGGER__ERROR("SharedMemoryBuffer::open is not implemented for Android");
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

size_t SharedMemoryBuffer::size() const
{
    LOGGER__ERROR("SharedMemoryBuffer::size is not implemented for Android");
    return 0;
}

void *SharedMemoryBuffer::user_address()
{
    LOGGER__ERROR("SharedMemoryBuffer::user_address is not implemented for Android");
    return nullptr;
}

std::string SharedMemoryBuffer::shm_name()
{
    LOGGER__ERROR("SharedMemoryBuffer::shm_name is not implemented for Android");
    return "";
}

#endif

} /* namespace hailort */
