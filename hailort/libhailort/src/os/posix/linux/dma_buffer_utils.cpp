/**
 * Copyright (c) 2020-2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file dma_buffer_utils.cpp
 * @brief A module for managing DMA buffers on Linux
 **/
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/dma-buf.h>


#include "hailo/hailort.h"
#include "hailo/event.hpp"
#include "common/utils.hpp"
#include "utils/dma_buffer_utils.hpp"

namespace hailort
{

Expected<MemoryView> DmaBufferUtils::mmap_dma_buffer_write(hailo_dma_buffer_t dma_buffer)
{
    void* dma_buf_ptr = mmap(NULL, dma_buffer.size, PROT_WRITE, MAP_SHARED, dma_buffer.fd, 0);
    CHECK_AS_EXPECTED(MAP_FAILED != dma_buf_ptr, HAILO_INTERNAL_FAILURE, "Failed to run mmap on DMA buffer for writing");

    struct dma_buf_sync sync = {
        .flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE,
    };
    auto err = ioctl(dma_buffer.fd, DMA_BUF_IOCTL_SYNC, &sync);
    CHECK_AS_EXPECTED(0 == err, HAILO_INTERNAL_FAILURE, "Failed to run DMA_BUF_IOCTL_SYNC ioctl, errno {}", err);

    return MemoryView(dma_buf_ptr, dma_buffer.size);
}

hailo_status DmaBufferUtils::munmap_dma_buffer_write(hailo_dma_buffer_t dma_buffer, MemoryView dma_buffer_memview)
{
    struct dma_buf_sync sync = {
        .flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE,
    };

    auto err = ioctl(dma_buffer.fd, DMA_BUF_IOCTL_SYNC, &sync);
    CHECK(0 == err, HAILO_INTERNAL_FAILURE, "Failed to run DMA_BUF_IOCTL_SYNC ioctl, errno {}", err);

    err = munmap(dma_buffer_memview.data(), dma_buffer.size);
    CHECK(0 == err, HAILO_INTERNAL_FAILURE, "Failed to munmap dma buffer, errno {}", err);

    return HAILO_SUCCESS;
}

Expected<MemoryView> DmaBufferUtils::mmap_dma_buffer_read(hailo_dma_buffer_t dma_buffer)
{
    void* dma_buf_ptr = mmap(NULL, dma_buffer.size, PROT_READ, MAP_SHARED, dma_buffer.fd, 0);
    CHECK_AS_EXPECTED(MAP_FAILED != dma_buf_ptr, HAILO_INTERNAL_FAILURE, "Failed to run mmap on DMA buffer for reading");

    struct dma_buf_sync sync = {
        .flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ,
    };
    auto err = ioctl(dma_buffer.fd, DMA_BUF_IOCTL_SYNC, &sync);
    CHECK_AS_EXPECTED(0 == err, HAILO_INTERNAL_FAILURE, "Failed to run DMA_BUF_IOCTL_SYNC ioctl, errno {}", err);

    return MemoryView(dma_buf_ptr, dma_buffer.size);
}

hailo_status DmaBufferUtils::munmap_dma_buffer_read(hailo_dma_buffer_t dma_buffer, MemoryView dma_buffer_memview)
{
    struct dma_buf_sync sync = {
        .flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ,
    };
    auto err = ioctl(dma_buffer.fd, DMA_BUF_IOCTL_SYNC, &sync);
    CHECK(0 == err, HAILO_INTERNAL_FAILURE, "Failed to run DMA_BUF_IOCTL_SYNC ioctl, errno {}", err);

    err = munmap(dma_buffer_memview.data(), dma_buffer.size);
    CHECK(0 == err, HAILO_INTERNAL_FAILURE, "Failed to unmap dma buffer, errno {}", err);

    return HAILO_SUCCESS;
}

} /* namespace hailort */
