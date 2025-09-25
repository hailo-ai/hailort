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
#include "utils/buffer_storage.hpp"
#include "net_flow/pipeline/pipeline.hpp"

/** hailort namespace */
namespace hailort
{

class HAILORTAPI DmaBufferUtils
{
public:
    static Expected<MemoryView> mmap_dma_buffer(hailo_dma_buffer_t dma_buffer, BufferProtection dma_buffer_protection);
    static hailo_status munmap_dma_buffer(hailo_dma_buffer_t dma_buffer, MemoryView dma_buffer_memview, BufferProtection dma_buffer_protection);
};

} /* namespace hailort */

#endif /* _HAILO_DMA_BUFFER_UTILS_HPP_ */
