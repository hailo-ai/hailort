/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file buffer_pool.cpp
 * @brief Implementation of vdma buffer pool
 **/

#include "buffer_pool.hpp"
#include "hailo/hailort.h"
#include "hailo/expected.hpp"

Expected<BufferPoolPtr> BufferPool::create(size_t num_buffers, size_t buffer_size,
    hailo_vdma_buffer_direction_flags_t data_direction_flags, Device &device)
{
    std::queue<std::shared_ptr<DmaMappedBuffer>> queue;
    for (auto i = 0; i < num_buffers; i++) {
        auto mapped_buffer = DmaMappedBuffer::create(buffer_size, data_direction_flags, device);
        if (!mapped_buffer) {
            return make_unexpected(mapped_buffer.status());
        }

        auto mapped_buffer_ptr = std::make_shared<DmaMappedBuffer>(mapped_buffer.release());
        if (nullptr == mapped_buffer_ptr) {
            return make_unexpected(HAILO_OUT_OF_HOST_MEMORY);
        }

        queue.push(mapped_buffer_ptr);
    }
    
    auto result = std::make_shared<BufferPool>(num_buffers, std::move(queue));
    if (nullptr == result) {
        return make_unexpected(HAILO_OUT_OF_HOST_MEMORY);
    }

    return result;
}

BufferPool::BufferPool(size_t max_size, std::queue<std::shared_ptr<DmaMappedBuffer>> &&queue) :
    m_max_size(max_size),
    m_mutex(),
    m_cv(),
    m_queue(queue)
{}

BufferPool::~BufferPool()
{
    m_cv.notify_all();
}

std::shared_ptr<DmaMappedBuffer> BufferPool::dequeue()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this] { return m_queue.size() > 0; });
    auto buffer = m_queue.front();
    m_queue.pop();

    return buffer;
}
void BufferPool::enqueue(std::shared_ptr<DmaMappedBuffer> buffer)
{
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return m_max_size > m_queue.size(); });
        m_queue.push(buffer);
    }

    m_cv.notify_one();
}

void BufferPool::wait_for_pending_buffers()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this] { return m_max_size == m_queue.size(); });
}
