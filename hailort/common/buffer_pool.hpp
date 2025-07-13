/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file buffer_pool.hpp
 * @brief Buffer pool
 **/

#ifndef _HAILO_BUFFER_POOL_HPP_
#define _HAILO_BUFFER_POOL_HPP_

#include "hailo/hailort.h"
#include "hailo/hailort_common.hpp"
#include "hailo/buffer.hpp"
#include "hailo/vdevice.hpp"
#include "hailo/dma_mapped_buffer.hpp"
#include "common/thread_safe_queue.hpp"

#include <mutex>

namespace hailort
{

using AllocateFunc = std::function<Expected<Buffer>(size_t)>;

class BufferPool;
using BufferPoolPtr = std::shared_ptr<BufferPool>;

// TODO: HRT-12690 - Make other buffer pools to use this as base class
class BufferPool
{
public:
    BufferPool(size_t buffer_size, std::vector<BufferPtr> &&buffers,
        SpscQueue<BufferPtr> &&m_free_buffers_queue, size_t buffers_count);
    static Expected<BufferPoolPtr> create_shared(size_t buffer_size, size_t buffer_count, AllocateFunc allocate_func);

    BufferPool(BufferPool &&) = delete;
    BufferPool(const BufferPool &) = delete;
    BufferPool &operator=(BufferPool &&) = delete;
    BufferPool &operator=(const BufferPool &) = delete;
    virtual ~BufferPool() = default;

    Expected<BufferPtr> acquire_buffer();
    size_t current_size();
    hailo_status return_to_pool(BufferPtr buffer);
    size_t buffers_count();
    size_t buffer_size();

private:
    size_t m_buffer_size;
    size_t m_buffers_count;
    std::vector<BufferPtr> m_buffers;
    SpscQueue<BufferPtr> m_free_buffers_queue;
    std::mutex m_mutex;
};

class FastBufferPool;
using FastBufferPoolPtr = std::shared_ptr<FastBufferPool>;

class FastBufferPool
{
public:
    FastBufferPool(std::vector<BufferPtr> &&buffers);
    static Expected<FastBufferPoolPtr> create_shared(size_t buffer_size, size_t buffer_count, AllocateFunc allocate_func);

    ~FastBufferPool() = default;

    FastBufferPool(BufferPool &&) = delete;
    FastBufferPool(const BufferPool &) = delete;
    FastBufferPool &operator=(BufferPool &&) = delete;
    FastBufferPool &operator=(const BufferPool &) = delete;

    Expected<BufferPtr> acquire();
    hailo_status release(BufferPtr &&buffer);

private:
    moodycamel::BlockingReaderWriterQueue<BufferPtr> m_free_buffers_queue;
    std::mutex m_acquire_mutex;
    std::mutex m_release_mutex;
};

} /* namespace hailort */

#endif /* _HAILO_BUFFER_POOL_HPP_ */
