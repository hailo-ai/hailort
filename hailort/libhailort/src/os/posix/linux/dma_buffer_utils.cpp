/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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
#include <fcntl.h>

namespace hailort
{

const size_t DmaBufferUtils::MAX_DMABUF_SIZE = 450 * 1024 * 1024;

Expected<MemoryView> DmaBufferUtils::mmap_dma_buffer(hailo_dma_buffer_t dma_buffer, BufferProtection dma_buffer_protection)
{
    int prot = 0;
    uint64_t dma_buf_sync_flags = 0;
    if (BufferProtection::READ == dma_buffer_protection) {
        prot = PROT_READ;
        dma_buf_sync_flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ;
    } else if (BufferProtection::WRITE == dma_buffer_protection) {
        prot = PROT_WRITE;
        dma_buf_sync_flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE;
    } else {
        LOGGER__ERROR("Invalid buffer protection: {}", static_cast<int>(dma_buffer_protection));
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }

    void* dma_buf_ptr = mmap(NULL, dma_buffer.size, prot, MAP_SHARED, dma_buffer.fd, 0);
    CHECK_AS_EXPECTED(MAP_FAILED != dma_buf_ptr, HAILO_INTERNAL_FAILURE, "Failed to run mmap on DMA buffer");

    struct dma_buf_sync sync = {
        .flags = dma_buf_sync_flags,
    };
    auto err = ioctl(dma_buffer.fd, DMA_BUF_IOCTL_SYNC, &sync);
    CHECK_AS_EXPECTED(0 == err, HAILO_INTERNAL_FAILURE, "Failed to run DMA_BUF_IOCTL_SYNC on FD, size: {}, fd: {}, address: {}, errno {}", dma_buffer.size,
        dma_buffer.fd, static_cast<void*>(dma_buf_ptr), err);

    return MemoryView(dma_buf_ptr, dma_buffer.size);
}

hailo_status DmaBufferUtils::munmap_dma_buffer(hailo_dma_buffer_t dma_buffer, MemoryView dma_buffer_memview, BufferProtection dma_buffer_protection)
{
    uint64_t dma_buf_sync_flags = 0;
    if (BufferProtection::READ == dma_buffer_protection) {
        dma_buf_sync_flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ;
    } else if (BufferProtection::WRITE == dma_buffer_protection) {
        dma_buf_sync_flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE;
    } else {
        LOGGER__ERROR("Invalid buffer protection: {}", static_cast<int>(dma_buffer_protection));
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }

    struct dma_buf_sync sync = {
        .flags = dma_buf_sync_flags,
    };

    auto err = ioctl(dma_buffer.fd, DMA_BUF_IOCTL_SYNC, &sync);
    CHECK(0 == err, HAILO_INTERNAL_FAILURE, "Failed to run DMA_BUF_IOCTL_SYNC ioctl, errno {}", err);

    err = munmap(static_cast<void*>(dma_buffer_memview.data()), dma_buffer.size);
    CHECK(0 == err, HAILO_INTERNAL_FAILURE, "Failed to munmap dma buffer, size: {}, fd: {}, address: {}, errno {}", dma_buffer.size, dma_buffer.fd,
        static_cast<void*>(dma_buffer_memview.data()), err);

    return HAILO_SUCCESS;
}

Expected<FileDescriptor> DmaBufferUtils::create_dma_buffer(size_t size, const char *name, dma_heap_allocation_data *heap_data)
{
    CHECK(size <= MAX_DMABUF_SIZE, HAILO_INVALID_ARGUMENT, "DMA buffer size {} exceeds maximum size {}", size, MAX_DMABUF_SIZE);
    
    FileDescriptor heap_fd(open(name, O_RDWR));
    CHECK(heap_fd >= 0, HAILO_FILE_OPERATION_FAILURE, "Failed to open DMA heap device '{}'. errno: {}", name, errno);

    CHECK(0 == ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, heap_data), HAILO_INTERNAL_FAILURE, 
        "Failed to allocate DMA buffer of size {}. errno: {}", size, errno);

    return FileDescriptor(static_cast<int>(heap_data->fd));
}

Expected<FileDescriptor> DmaBufferUtils::create_dma_buffer(const char *name, size_t size)
{
    struct dma_heap_allocation_data heap_data_output = {};
    heap_data_output.len = size;
    heap_data_output.fd_flags = O_RDWR | O_CLOEXEC;
    heap_data_output.fd = 0;
    heap_data_output.heap_flags = 0;

    return create_dma_buffer(size, name, &heap_data_output);
}

Expected<std::string> DmaBufferUtils::get_dma_heap_path()
{
    static const std::vector<std::string> VALID_DMA_HEAPS = {
        "/dev/dma_heap/hailo_media_buf,cma",   // vpu
        "/dev/dma_heap/linux,cma",   // accelerator
    };

    for (const auto &path : VALID_DMA_HEAPS) {
        if (access(path.c_str(), F_OK) == 0) {
            return Expected<std::string>(path);
        }
    }
    LOGGER__ERROR("No valid DMA heap found");
    return make_unexpected(HAILO_INTERNAL_FAILURE);    
}
} /* namespace hailort */
