/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file dma_buffer.hpp
 * @brief A module for managing DMA buffers
 **/

#ifndef _HAILO_DMA_BUFFER_UTILS_HPP_
#define _HAILO_DMA_BUFFER_UTILS_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/hailort_dma-heap.h"
#include "utils/buffer_storage.hpp"
#include "net_flow/pipeline/pipeline.hpp"

/** hailort namespace */
namespace hailort
{

class HAILORTAPI DmaBufferUtils
{
public:
    static const size_t MAX_DMABUF_SIZE;
    static Expected<MemoryView> mmap_dma_buffer(hailo_dma_buffer_t dma_buffer, BufferProtection dma_buffer_protection);
    static hailo_status munmap_dma_buffer(hailo_dma_buffer_t dma_buffer, MemoryView dma_buffer_memview, BufferProtection dma_buffer_protection);
    static Expected<FileDescriptor> create_dma_buffer(const char *name, size_t size);
#ifdef __linux__
    static Expected<FileDescriptor> create_dma_buffer(size_t size, const char *name, dma_heap_allocation_data *heap_data = nullptr);
    static Expected<std::string> get_dma_heap_path();
#endif
};

} /* namespace hailort */

#endif /* _HAILO_DMA_BUFFER_UTILS_HPP_ */
