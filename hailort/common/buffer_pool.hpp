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
class BasicBufferPool;
using BasicBufferPoolPtr = std::shared_ptr<BasicBufferPool>;

// TODO: HRT-12690 - Make other buffer pools to use this as base class
class BasicBufferPool
{
public:
    BasicBufferPool(size_t buffer_size, std::vector<BufferPtr> &&buffers,
        SpscQueue<BufferPtr> &&m_free_buffers_queue, size_t buffers_count);
    static Expected<BasicBufferPoolPtr> create_shared(size_t buffer_size, size_t buffer_count,
        std::function<Expected<Buffer>(size_t)> allocate_func);

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

template<typename T>
class ObjectPool
{
public:
    static Expected<std::shared_ptr<ObjectPool<T>>> create_shared(size_t count, std::function<Expected<T>()> create_object_func)
    {
        TRY(auto shutdown_event, Event::create_shared(Event::State::not_signalled));
        TRY(auto free_objects_queue, SpscQueue<std::shared_ptr<T>>::create(count, shutdown_event, DEFAULT_TRANSFER_TIMEOUT));

        std::vector<std::shared_ptr<T>> objects;
        objects.reserve(count);

        for (size_t i = 0; i < count; i++) {
            TRY(auto object, create_object_func());

            auto object_ptr = make_shared_nothrow<T>(std::move(object));
            CHECK_NOT_NULL(object_ptr, HAILO_OUT_OF_HOST_MEMORY);

            auto status = free_objects_queue.enqueue(object_ptr);
            CHECK_SUCCESS(status);

            objects.emplace_back(object_ptr);
        }

        auto object_pool = make_shared_nothrow<ObjectPool<T>>(std::move(objects), std::move(free_objects_queue), count);
        CHECK_NOT_NULL(object_pool, HAILO_OUT_OF_HOST_MEMORY);

        return object_pool;
    }

    ObjectPool(std::vector<std::shared_ptr<T>> &&objects, SpscQueue<std::shared_ptr<T>> &&free_objects_queue,
        size_t objects_count) :
        m_objects_count(objects_count),
        m_objects(std::move(objects)),
        m_free_objects_queue(std::move(free_objects_queue))
    {}

    ObjectPool(ObjectPool &&) = delete;
    ObjectPool(const ObjectPool &) = delete;
    ObjectPool &operator=(ObjectPool &&) = delete;
    ObjectPool &operator=(const ObjectPool &) = delete;
    virtual ~ObjectPool() = default;

    Expected<std::shared_ptr<T>> acquire()
    {
        TRY_WITH_ACCEPTABLE_STATUS(HAILO_SHUTDOWN_EVENT_SIGNALED, auto object,
            m_free_objects_queue.dequeue(DEFAULT_TRANSFER_TIMEOUT));
        return object;
    }

    hailo_status return_to_pool(std::shared_ptr<T> object)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        auto status = m_free_objects_queue.enqueue(object);
        CHECK_SUCCESS(status);

        return HAILO_SUCCESS;
    }

    size_t count() const
    {
        return m_objects_count;
    }

    size_t current_count() const
    {
        return m_free_objects_queue.size_approx();
    }

private:
    const size_t m_objects_count;
    std::vector<std::shared_ptr<T>> m_objects;
    SpscQueue<std::shared_ptr<T>> m_free_objects_queue;
    std::mutex m_mutex;
};

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
