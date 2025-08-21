/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file dma_buffer_utils.cpp
 * @brief A module for managing DMA buffers on Windows (not supported)
 **/

#include "utils/dma_buffer_utils.hpp"


namespace hailort
{

const size_t DmaBufferUtils::MAX_DMABUF_SIZE = 0; // DMA buffers not supported on Windows

Expected<MemoryView> DmaBufferUtils::mmap_dma_buffer(hailo_dma_buffer_t /*dma_buffer*/, BufferProtection /*dma_buffer_protection*/)
{
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

hailo_status DmaBufferUtils::munmap_dma_buffer(hailo_dma_buffer_t /*dma_buffer*/, MemoryView /*dma_buffer_memview*/,
    BufferProtection /*dma_buffer_protection*/)
{
    return HAILO_NOT_IMPLEMENTED;
}

Expected<FileDescriptor> DmaBufferUtils::create_dma_buffer(const char */*name*/, size_t /*size*/)
{
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

} /* namespace hailort */
