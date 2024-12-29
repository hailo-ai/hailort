/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
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

// TODO: HRT-12690 - Make other buffer pools to use this as base class
class BasicBufferPool
{
public:
    BasicBufferPool(size_t buffer_size, std::vector<BufferPtr> &&buffers,
        SpscQueue<BufferPtr> &&m_free_buffers_queue, size_t buffers_count);

    BasicBufferPool(BasicBufferPool &&) = delete;
    BasicBufferPool(const BasicBufferPool &) = delete;
    BasicBufferPool &operator=(BasicBufferPool &&) = delete;
    BasicBufferPool &operator=(const BasicBufferPool &) = delete;
    virtual ~BasicBufferPool() = default;

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
using BasicBufferPoolPtr = std::shared_ptr<BasicBufferPool>;

// TODO: HRT-12690 - DMA buffer pool is also used in the service - code duplication
class DmaAbleBufferPool : public BasicBufferPool
{
public:
    static Expected<std::shared_ptr<DmaAbleBufferPool>> create_shared(size_t buffer_size, size_t buffer_count,
        EventPtr shutdown_event);

    DmaAbleBufferPool(size_t buffer_size, std::vector<BufferPtr> &&buffers,
        SpscQueue<BufferPtr> &&m_free_buffers_queue, size_t buffers_count);
    DmaAbleBufferPool(DmaAbleBufferPool &&) = delete;
    DmaAbleBufferPool(const DmaAbleBufferPool &) = delete;
    DmaAbleBufferPool &operator=(DmaAbleBufferPool &&) = delete;
    DmaAbleBufferPool &operator=(const DmaAbleBufferPool &) = delete;
    virtual ~DmaAbleBufferPool() = default;
};
using DmaAbleBufferPoolPtr = std::shared_ptr<DmaAbleBufferPool>;

} /* namespace hailort */

#endif /* _HAILO_BUFFER_POOL_HPP_ */
