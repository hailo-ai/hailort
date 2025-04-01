/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file shared_memory_buffer.cpp
 * @brief Shared memory implementaion in Windows. 
 * Based on Windows docs: https://learn.microsoft.com/en-us/windows/win32/memory/creating-named-shared-memory
 **/

#include "common/shared_memory_buffer.hpp"
#include "common/utils.hpp"
#include "hailo/hailort.h"

#include <windows.h>

namespace hailort
{

Expected<SharedMemoryBufferPtr> SharedMemoryBuffer::create(size_t size, const std::string &shm_name)
{
    HANDLE handle_map_file = CreateFileMapping(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
            static_cast<DWORD>(size), static_cast<LPCSTR>(shm_name.c_str()));
    CHECK_AS_EXPECTED((handle_map_file != nullptr), HAILO_INTERNAL_FAILURE, "Failed to create shared memory object, error = {}", GetLastError());

    auto shm_fd = FileDescriptor(handle_map_file);
    TRY(auto mmapped_buffer, MmapBuffer<void>::create_file_map(size, shm_fd, 0));

    auto result = make_shared_nothrow<SharedMemoryBuffer>(shm_name, std::move(mmapped_buffer), true);
    CHECK_NOT_NULL_AS_EXPECTED(result, HAILO_OUT_OF_HOST_MEMORY);

    return result;
}

Expected<SharedMemoryBufferPtr> SharedMemoryBuffer::open(size_t size, const std::string &shm_name)
{
    HANDLE handle_map_file = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE,  static_cast<LPCSTR>(shm_name.c_str()));
    CHECK_AS_EXPECTED((handle_map_file != nullptr), HAILO_INTERNAL_FAILURE, "Failed to open file mapping object, error = {}", GetLastError());

    auto shm_fd = FileDescriptor(handle_map_file);
    TRY(auto mmapped_buffer, MmapBuffer<void>::create_file_map(size, shm_fd, 0));

    auto result = make_shared_nothrow<SharedMemoryBuffer>(shm_name, std::move(mmapped_buffer), false);
    CHECK_NOT_NULL_AS_EXPECTED(result, HAILO_OUT_OF_HOST_MEMORY);

    return result;
}

SharedMemoryBuffer::~SharedMemoryBuffer()
{}

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

} /* namespace hailort */
