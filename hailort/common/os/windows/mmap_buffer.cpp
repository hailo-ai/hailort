/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file mmap_buffer.cpp
 * @brief Wrapper around windows memory mapping (mmap). Not implemented yet
 **/

#include "common/mmap_buffer.hpp"

#include <windows.h>
#include <stdio.h>
#include <conio.h>
#include <tchar.h>

namespace hailort
{

void * const MmapBufferImpl::INVALID_ADDR = NULL;

Expected<MmapBufferImpl> MmapBufferImpl::create_shared_memory(size_t)
{
    LOGGER__ERROR("Creating shared memory is not implemented on windows");
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

Expected<MmapBufferImpl> MmapBufferImpl::create_file_map(size_t size, FileDescriptor &fd, uintptr_t offset)
{
    DWORD offset_high = static_cast<DWORD>(offset >> 32); // High 32 bits
    DWORD offset_low = static_cast<DWORD>(offset & 0xFFFFFFFF); // Low 32 bits

    auto file_view_ptr = MapViewOfFile(fd, FILE_MAP_ALL_ACCESS, offset_high, offset_low, size);
    CHECK_AS_EXPECTED((file_view_ptr != nullptr), HAILO_INTERNAL_FAILURE, "Failed to map view of file, error = {}", GetLastError());
    
    return MmapBufferImpl(file_view_ptr, size);
}

hailo_status MmapBufferImpl::unmap()
{
    if (m_address != nullptr) {
        UnmapViewOfFile(m_address);
        m_address = nullptr;
    }
    return HAILO_SUCCESS;
}

} /* namespace hailort */
