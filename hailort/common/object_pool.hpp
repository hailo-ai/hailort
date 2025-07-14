/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file object_pool.hpp
 * @brief ObjectPool is a generic, thread-safe, zero-allocation (after-initialization) pool.
 *        Acquiring from an ObjectPool<T> returns a Pooled<T>; which behaves almost exactly like a std::shared_ptr<T>,
 *        only that it returns the object to the pool when no references remain (if the pool still exists).
 *        ObjectPool promises zero allocations during runtime, i.e. after the pool is created.
 *
 *        TODO: HRT-17399
 *          ObjectPool uses a SpscQueue to manage objects which has performance issues due to underlying EventFD.
 *          Should be changed to use moodycamel::ReaderWriterQueue.
 *
 *        TODO: HRT-17405
 *          This class is missing unit-tests!
 *
 *        TODO: ALL POOLS SHOULD USE THIS POOL!
 **/

#ifndef _HAILO_OBJECT_POOL_HPP_
#define _HAILO_OBJECT_POOL_HPP_

#include "hailo/hailort.h"
#include "hailo/hailort_common.hpp"
#include "common/thread_safe_queue.hpp"

namespace hailort
{


template<typename T>
class ObjectPool;

template<typename T>
using ObjectPoolPtr = std::shared_ptr<ObjectPool<T>>;

template<typename T>
class Pooled
{
public:
    Pooled() : m_object(nullptr) {}
    Pooled(Pooled &&other) noexcept = default;

    Pooled &operator=(Pooled other)
    {
        other.swap(*this);
        return *this;
    }

    Pooled(const Pooled &other) 
    {
        m_object = other.m_object;
        m_ref_count = other.m_ref_count;
        m_pool = other.m_pool;
        ++(*m_ref_count);
    }

    virtual ~Pooled()
    {
        release();
    }

    T *operator->() const
    {
        return m_object.get();
    }

    T& operator*() const
    {
        return *m_object;
    }

    void release()
    {
        if (nullptr == m_object) {
            return;
        }
        auto use_count = --(*m_ref_count);
        auto pool = m_pool.lock();
        if (use_count == 0 && pool) {
            auto status = pool->return_to_pool({std::move(m_object), std::move(m_ref_count), std::move(m_pool)});
            if (HAILO_SUCCESS != status) {
                LOGGER__ERROR("Failed to return object to pool with status: {}", status);
            }
        }
        m_object.reset();
        m_ref_count.reset();
    }

    friend class ObjectPool<T>;

private:
    Pooled(std::shared_ptr<T> object, std::shared_ptr<std::atomic_uint32_t> ref_count, std::weak_ptr<ObjectPool<T>> pool)
        : m_object(std::move(object)), m_ref_count(std::move(ref_count)), m_pool(std::move(pool))
    {}

    void swap(Pooled &other)
    {
        std::swap(m_object, other.m_object);
        std::swap(m_ref_count, other.m_ref_count);
        std::swap(m_pool, other.m_pool);
    }

    std::shared_ptr<T> m_object;
    std::shared_ptr<std::atomic_uint32_t> m_ref_count;
    std::weak_ptr<ObjectPool<T>> m_pool;
};

template<typename T>
class ObjectPool
{
public:
    static Expected<ObjectPoolPtr<T>> create_shared(size_t count, std::function<Expected<T>()> create_object_func, EventPtr shutdown_event = nullptr)
    {
        if (!shutdown_event) {
            TRY(shutdown_event, Event::create_shared(Event::State::not_signalled));
        }
        TRY(auto queue, SpscQueue<Pooled<T>>::create(count, shutdown_event, DEFAULT_TRANSFER_TIMEOUT));

        auto object_pool = make_shared_nothrow<ObjectPool<T>>(std::move(queue));
        CHECK_NOT_NULL(object_pool, HAILO_OUT_OF_HOST_MEMORY);

        for (size_t i = 0; i < count; i++) {
            TRY(auto object, create_object_func());

            auto object_ptr = make_shared_nothrow<T>(std::move(object));
            CHECK_NOT_NULL(object_ptr, HAILO_OUT_OF_HOST_MEMORY);

            auto ref_count = make_shared_nothrow<std::atomic_uint32_t>(1);
            CHECK_NOT_NULL(ref_count, HAILO_OUT_OF_HOST_MEMORY);

            Pooled<T> pooled_object(object_ptr, ref_count, object_pool);
            pooled_object.release();
        }

        return object_pool;
    }

    ObjectPool(SpscQueue<Pooled<T>> &&queue) :
        m_objects_count(queue.max_capacity()),
        m_queue(std::move(queue))
    {}

    ObjectPool(ObjectPool &&) = delete;
    ObjectPool(const ObjectPool &) = delete;
    ObjectPool &operator=(ObjectPool &&) = delete;
    ObjectPool &operator=(const ObjectPool &) = delete;

    virtual ~ObjectPool() = default;

    Expected<Pooled<T>> acquire()
    {
        std::unique_lock<std::mutex> lock(m_acquire_mutex);
        TRY_WITH_ACCEPTABLE_STATUS(HAILO_SHUTDOWN_EVENT_SIGNALED, auto object,
            m_queue.dequeue(DEFAULT_TRANSFER_TIMEOUT));
        *(object.m_ref_count) = 1;
        return object;
    }

    size_t capacity() const
    {
        return m_objects_count;
    }

    friend class Pooled<T>;

private:
    hailo_status return_to_pool(Pooled<T> object)
    {
        std::unique_lock<std::mutex> lock(m_return_mutex);
        auto status = m_queue.enqueue(std::move(object));
        CHECK_SUCCESS(status);

        return HAILO_SUCCESS;
    }

    const size_t m_objects_count;
    std::vector<std::shared_ptr<T>> m_objects;
    SpscQueue<Pooled<T>> m_queue;
    std::mutex m_acquire_mutex;
    std::mutex m_return_mutex;
};

} /* namespace hailort */

#endif /* _HAILO_OBJECT_POOL_HPP_ */
