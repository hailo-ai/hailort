/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file buffer_pool.cpp
 * @brief Buffer pool implementation
 **/

#include "buffer_pool.hpp"
#include "hailo/hailort.h"

namespace hailort
{

BasicBufferPool::BasicBufferPool(size_t buffer_size, std::vector<BufferPtr> &&buffers,
        SpscQueue<BufferPtr> &&free_buffers_queue, size_t buffers_count) :
    m_buffer_size(buffer_size),
    m_buffers_count(buffers_count),
    m_buffers(std::move(buffers)),
    m_free_buffers_queue(std::move(free_buffers_queue))
{}

Expected<BufferPtr> BasicBufferPool::acquire_buffer()
{
    TRY_WITH_ACCEPTABLE_STATUS(HAILO_SHUTDOWN_EVENT_SIGNALED, auto buffer,
        m_free_buffers_queue.dequeue(DEFAULT_TRANSFER_TIMEOUT));
    return buffer;
}

size_t BasicBufferPool::current_size()
{
    return m_free_buffers_queue.size_approx();
}

hailo_status BasicBufferPool::return_to_pool(BufferPtr buffer)
{
    CHECK(buffer->size() == m_buffer_size, HAILO_INTERNAL_FAILURE,
        "Buffer size is not the same as expected for pool! ({} != {})", buffer->size(), m_buffer_size);

    std::unique_lock<std::mutex> lock(m_mutex);
    auto status = m_free_buffers_queue.enqueue(buffer);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

size_t BasicBufferPool::buffers_count()
{
    return m_buffers_count;
}

size_t BasicBufferPool::buffer_size()
{
    return m_buffer_size;
}

} /* namespace hailort */
