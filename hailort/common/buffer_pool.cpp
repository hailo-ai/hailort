/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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

BufferPool::BufferPool(size_t buffer_size, std::vector<BufferPtr> &&buffers,
        SpscQueue<BufferPtr> &&free_buffers_queue, size_t buffers_count) :
    m_buffer_size(buffer_size),
    m_buffers_count(buffers_count),
    m_buffers(std::move(buffers)),
    m_free_buffers_queue(std::move(free_buffers_queue))
{}

Expected<BufferPoolPtr> BufferPool::create_shared(size_t buffer_size, size_t buffer_count,
    std::function<Expected<Buffer>(size_t)> allocate_func)
{
    TRY(auto shutdown_event, Event::create_shared(Event::State::not_signalled));
    TRY(auto free_buffers_queue, SpscQueue<BufferPtr>::create(buffer_count, shutdown_event, DEFAULT_TRANSFER_TIMEOUT));

    std::vector<BufferPtr> buffers;
    buffers.reserve(buffer_count);

    for (size_t i = 0; i < buffer_count; i++) {
        TRY(auto buffer, allocate_func(buffer_size));
        
        auto buffer_ptr = make_shared_nothrow<Buffer>(std::move(buffer));
        CHECK_NOT_NULL(buffer_ptr, HAILO_OUT_OF_HOST_MEMORY);

        auto status = free_buffers_queue.enqueue(buffer_ptr);
        CHECK_SUCCESS(status);

        buffers.emplace_back(buffer_ptr);
    }

    auto buffer_pool = make_shared_nothrow<BufferPool>(buffer_size, std::move(buffers), std::move(free_buffers_queue), buffer_count);
    CHECK_NOT_NULL(buffer_pool, HAILO_OUT_OF_HOST_MEMORY);

    return buffer_pool;
}

Expected<BufferPtr> BufferPool::acquire_buffer()
{
    TRY_WITH_ACCEPTABLE_STATUS(HAILO_SHUTDOWN_EVENT_SIGNALED, auto buffer,
        m_free_buffers_queue.dequeue(DEFAULT_TRANSFER_TIMEOUT));
    return buffer;
}

size_t BufferPool::current_size()
{
    return m_free_buffers_queue.size_approx();
}

hailo_status BufferPool::return_to_pool(BufferPtr buffer)
{
    CHECK(buffer->size() == m_buffer_size, HAILO_INTERNAL_FAILURE,
        "Buffer size is not the same as expected for pool! ({} != {})", buffer->size(), m_buffer_size);

    std::unique_lock<std::mutex> lock(m_mutex);
    auto status = m_free_buffers_queue.enqueue(buffer);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

size_t BufferPool::buffers_count()
{
    return m_buffers_count;
}

size_t BufferPool::buffer_size()
{
    return m_buffer_size;
}

FastBufferPool::FastBufferPool(std::vector<BufferPtr> &&buffers) :
    m_free_buffers_queue(buffers.size())
{
    for (auto &buffer : buffers) {
        (void)m_free_buffers_queue.enqueue(buffer);
    }
}

Expected<FastBufferPoolPtr> FastBufferPool::create_shared(size_t buffer_size, size_t buffer_count, AllocateFunc allocate_func)
{
    std::vector<BufferPtr> buffers;
    buffers.reserve(buffer_count);

    for (size_t i = 0; i < buffer_count; i++) {
        TRY(auto buffer, allocate_func(buffer_size));
        
        auto buffer_ptr = make_shared_nothrow<Buffer>(std::move(buffer));
        CHECK_NOT_NULL(buffer_ptr, HAILO_OUT_OF_HOST_MEMORY);

        buffers.emplace_back(buffer_ptr);
    }

    auto buffer_pool = make_shared_nothrow<FastBufferPool>(std::move(buffers));
    CHECK_NOT_NULL(buffer_pool, HAILO_OUT_OF_HOST_MEMORY);

    return buffer_pool;
}

Expected<BufferPtr> FastBufferPool::acquire()
{
    BufferPtr result{};
    std::lock_guard<std::mutex> lock(m_acquire_mutex);

    CHECK(m_free_buffers_queue.wait_dequeue_timed(result, DEFAULT_TRANSFER_TIMEOUT), HAILO_TIMEOUT);

    return result;
}

hailo_status FastBufferPool::release(BufferPtr &&buffer)
{
    std::lock_guard<std::mutex> lock(m_release_mutex);

    CHECK(m_free_buffers_queue.try_enqueue(std::move(buffer)), HAILO_QUEUE_IS_FULL);

    return HAILO_SUCCESS;
}

} /* namespace hailort */
