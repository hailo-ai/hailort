/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file mmap_buffer.cpp
 * @brief Wrapper around windows memory mapping (mmap). Not implemented yet
 **/

#include "os/mmap_buffer.hpp"

namespace hailort
{

void * const MmapBufferImpl::INVALID_ADDR = NULL;

Expected<MmapBufferImpl> MmapBufferImpl::create_shared_memory(size_t length)
{
    void *address = VirtualAlloc(NULL, length, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    CHECK_AS_EXPECTED(INVALID_ADDR != address, HAILO_OUT_OF_HOST_MEMORY, "Failed to mmap buffer with error:{}", GetLastError());
    return MmapBufferImpl(address, length, true);
}

hailo_status MmapBufferImpl::unmap()
{
    if (m_unmappable) {
        VirtualFree(m_address, m_length, MEM_RELEASE);
    }
    return HAILO_SUCCESS;
}

} /* namespace hailort */
