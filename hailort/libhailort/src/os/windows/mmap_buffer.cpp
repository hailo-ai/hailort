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

Expected<MmapBufferImpl> MmapBufferImpl::create_shared_memory(size_t)
{
    LOGGER__ERROR("Creating shared memory is not implemented on windows");
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

Expected<MmapBufferImpl> MmapBufferImpl::create_file_map(size_t, FileDescriptor &, uintptr_t )
{
    LOGGER__ERROR("Creating file mapping is not implemented on windows");
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

hailo_status MmapBufferImpl::unmap()
{
    LOGGER__ERROR("Unmapping is not implemented on windows");
    return HAILO_NOT_IMPLEMENTED;
}

} /* namespace hailort */
