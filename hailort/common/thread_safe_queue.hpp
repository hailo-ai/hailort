/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file thread_safe_queue.hpp
 * @brief Thread safe queue taken from https://stackoverflow.com/a/16075550
 **/

#ifndef HAILO_THREAD_SAFE_QUEUE_HPP_
#define HAILO_THREAD_SAFE_QUEUE_HPP_

#include "hailo/expected.hpp"
#include "hailo/event.hpp"

#include "common/utils.hpp"
#include "common/logger_macros.hpp"
#include "common/event_internal.hpp"

// Define __unix__ for inclusion of readerwriterqueue.h because readerwriterqueue is implemented over POSIX standards 
// but checks __unix__ - otherwise QNX returns unsupported platform (need HAILO_UNDEF_UNIX_FLAG in order to undefine
// __unix__ only in case of defining it here)
#if defined(__QNX__) && !defined(__unix__)
#define __unix__
#define HAILO_UNDEF_UNIX_FLAG 
#endif

#include "readerwriterqueue.h"

#if defined(HAILO_UNDEF_UNIX_FLAG)
#undef __unix__
#undef HAILO_UNDEF_UNIX_FLAG
#endif

#include <queue>
#include <mutex>
#include <memory>
#include <condition_variable>
#include <chrono>


namespace hailort
{

#define DEFAULT_TIMEOUT_MS (1000)

template <class T>
class SafeQueue final {
public:
    static constexpr size_t UNLIMITED_QUEUE_SIZE = std::numeric_limits<size_t>::max();

    SafeQueue(size_t max_size) :
        m_max_size(max_size)
    {}

    SafeQueue() :
        SafeQueue(UNLIMITED_QUEUE_SIZE)
    {}


    ~SafeQueue() = default;

    hailo_status enqueue(T &&t)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if ((m_max_size != UNLIMITED_QUEUE_SIZE) && (m_queue.size() >= m_max_size)) {
            return HAILO_QUEUE_IS_FULL;
        }
        m_queue.push(std::move(t));
        return HAILO_SUCCESS;
    }

    Expected<T> dequeue()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        CHECK_AS_EXPECTED(!m_queue.empty(), HAILO_INTERNAL_FAILURE, "Can't dequeue if queue is empty");
        T val = m_queue.front();
        m_queue.pop();
        return val;
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        while (!m_queue.empty()) {
            m_queue.pop();
        }
    }

    bool empty() const { return m_queue.empty(); }
    size_t size() const { return m_queue.size(); }
    size_t max_size() const { return m_max_size; }

protected:
    const size_t m_max_size;
    std::queue<T> m_queue;
    mutable std::mutex m_mutex;
};

// Single-Producer Single-Consumer Queue
// The queue's size is limited
template<typename T, size_t MAX_BLOCK_SIZE = 512>
class SpscQueue
{
private:
    typedef moodycamel::ReaderWriterQueue<T, MAX_BLOCK_SIZE> ReaderWriterQueue;

public:
    static constexpr auto INIFINITE_TIMEOUT() { return std::chrono::milliseconds(HAILO_INFINITE); }

    SpscQueue(size_t max_size, SemaphorePtr items_enqueued_sema, SemaphorePtr items_dequeued_sema,
              EventPtr shutdown_event, std::chrono::milliseconds default_timeout) :
        m_inner(max_size),
        m_items_enqueued_sema_or_shutdown(items_enqueued_sema, shutdown_event),
        m_items_enqueued_sema(items_enqueued_sema),
        m_items_dequeued_sema_or_shutdown(items_dequeued_sema, shutdown_event),
        m_items_dequeued_sema(items_dequeued_sema),
        m_default_timeout(default_timeout)
    {}

    virtual ~SpscQueue() = default;
    SpscQueue(SpscQueue &&other) = default;

    static Expected<SpscQueue> create(size_t max_size, const EventPtr& shutdown_event,
        std::chrono::milliseconds default_timeout = std::chrono::milliseconds(1000))
    {
        if (0 == max_size) {
            LOGGER__ERROR("Invalid queue max_size (must be greater than zero)");
            return make_unexpected(HAILO_INVALID_ARGUMENT);
        }

        // * items_enqueued_sema:
        //   +1 for each enqueued item
        //   -1 for each dequeued item
        //   Blocks when there are no items in the queue (hence when the queue is built it starts at zero)
        // * items_dequeued_sema:
        //   +1 for each dequeued item
        //   -1 for each enqueued item
        //   Blocks when the queue is full (which happens when it's value reaches zero, hence it starts at queue size)
        auto items_enqueued_sema = Semaphore::create_shared(0);
        CHECK_EXPECTED(items_enqueued_sema, "Failed creating items_enqueued_sema semaphore");

        auto items_dequeued_sema = Semaphore::create_shared(static_cast<uint32_t>(max_size));
        CHECK_EXPECTED(items_dequeued_sema, "Failed creating items_dequeued_sema semaphore");

        return SpscQueue(max_size, items_enqueued_sema.release(), items_dequeued_sema.release(), shutdown_event, default_timeout);
    }

    static std::shared_ptr<SpscQueue> create_shared(size_t max_size, const EventPtr& shutdown_event,
        std::chrono::milliseconds default_timeout = std::chrono::milliseconds(1000))
    {
        auto queue = create(max_size, shutdown_event, default_timeout);
        if (!queue) {
            LOGGER__ERROR("Failed creating queue. status={}", queue.status());
            return nullptr;
        }

        return make_shared_nothrow<SpscQueue>(queue.release());
    }

    static std::unique_ptr<SpscQueue> create_unique(size_t max_size, const EventPtr& shutdown_event,
        std::chrono::milliseconds default_timeout = std::chrono::milliseconds(1000))
    {
        auto queue = create(max_size, shutdown_event, default_timeout);
        if (!queue) {
            LOGGER__ERROR("Failed creating queue. status={}", queue.status());
            return nullptr;
        }

        return make_unique_nothrow<SpscQueue>(queue.release());
    }
    
    Expected<T> dequeue(std::chrono::milliseconds timeout, bool ignore_shutdown_event = false) AE_NO_TSAN
    {
        hailo_status wait_result = HAILO_UNINITIALIZED;
        if (ignore_shutdown_event) {
            wait_result = m_items_enqueued_sema->wait(timeout);
        } else {
            wait_result = m_items_enqueued_sema_or_shutdown.wait(timeout);
        }

        if (HAILO_SHUTDOWN_EVENT_SIGNALED == wait_result) {
            LOGGER__TRACE("Shutdown event has been signaled");
            return make_unexpected(wait_result);
        }
        if (HAILO_TIMEOUT == wait_result) {
            LOGGER__TRACE("Timeout, the queue is empty");
            return make_unexpected(wait_result);
        }
        if (HAILO_SUCCESS != wait_result) {
            LOGGER__WARNING("m_items_enqueued_sema received an unexpected failure");
            return make_unexpected(wait_result);
        }
        
        // The queue isn't empty
        T result{};
        const bool success = m_inner.try_dequeue(result);
        assert(success);
        AE_UNUSED(success);

        const auto signal_result = m_items_dequeued_sema_or_shutdown.signal();
        if (HAILO_SUCCESS != signal_result) {
            return make_unexpected(signal_result);
        }
        return result;
    }

    Expected<T> dequeue() AE_NO_TSAN
    {
        return dequeue(m_default_timeout);
    }

    hailo_status enqueue(const T& result, std::chrono::milliseconds timeout, bool ignore_shutdown_event = false) AE_NO_TSAN
    {
        hailo_status wait_result = HAILO_UNINITIALIZED;
        if (ignore_shutdown_event) {
            wait_result = m_items_dequeued_sema->wait(timeout);
        } else {
            wait_result = m_items_dequeued_sema_or_shutdown.wait(timeout);
        }

        if (HAILO_SHUTDOWN_EVENT_SIGNALED == wait_result) {
            LOGGER__TRACE("Shutdown event has been signaled");
            return wait_result;
        }
        if (HAILO_TIMEOUT == wait_result) {
            LOGGER__TRACE("Timeout, the queue is full");
            return wait_result;
        }
        if (HAILO_SUCCESS != wait_result) {
            LOGGER__WARNING("m_items_dequeued_sema received an unexpected failure");
            return wait_result;
        }

        // The queue isn't full
        const bool success = m_inner.try_enqueue(result);
        assert(success);
        AE_UNUSED(success);

        return m_items_enqueued_sema_or_shutdown.signal();
    }

    inline hailo_status enqueue(const T& result, bool ignore_shutdown_event = false) AE_NO_TSAN
    {
        return enqueue(result, m_default_timeout, ignore_shutdown_event);
    }

    // TODO: Do away with two copies of this function? (SDK-16481)
    hailo_status enqueue(T&& result, std::chrono::milliseconds timeout, bool ignore_shutdown_event = false) AE_NO_TSAN
    {
        hailo_status wait_result = HAILO_UNINITIALIZED;
        if (ignore_shutdown_event) {
            wait_result = m_items_dequeued_sema->wait(timeout);
        } else {
            wait_result = m_items_dequeued_sema_or_shutdown.wait(timeout);
        }

        if (HAILO_SHUTDOWN_EVENT_SIGNALED == wait_result) {
            LOGGER__TRACE("Shutdown event has been signaled");
            return wait_result;
        }
        if (HAILO_TIMEOUT == wait_result) {
            LOGGER__TRACE("Timeout, the queue is full");
            return wait_result;
        }
        if (HAILO_SUCCESS != wait_result) {
            LOGGER__WARNING("m_items_dequeued_sema received an unexpected failure");
            return wait_result;
        }

        // The queue isn't full
        const bool success = m_inner.try_enqueue(std::move(result));
        assert(success);
        AE_UNUSED(success);

        return m_items_enqueued_sema_or_shutdown.signal();
    }

    // TODO: HRT-3810, remove hacky argument ignore_shutdown_event
    inline hailo_status enqueue(T&& result, bool ignore_shutdown_event = false) AE_NO_TSAN
    {
        return enqueue(std::move(result), m_default_timeout, ignore_shutdown_event);
    }

    size_t size_approx()
    {
        return m_inner.size_approx();
    }

    size_t max_capacity()
    {
        return m_inner.max_capacity();
    }

    bool is_queue_full()
    {
        return (m_inner.size_approx() == m_inner.max_capacity());
    }

    hailo_status clear() AE_NO_TSAN
    {
        auto status = HAILO_SUCCESS;
        while (HAILO_SUCCESS == status) {
            auto output = dequeue(std::chrono::milliseconds(0), true);
            status = output.status();
        }

        if (HAILO_TIMEOUT == status) {
            return HAILO_SUCCESS;
        }
        return status;
    }

private:
    ReaderWriterQueue m_inner;
    WaitOrShutdown m_items_enqueued_sema_or_shutdown;
    SemaphorePtr m_items_enqueued_sema;
    WaitOrShutdown m_items_dequeued_sema_or_shutdown;
    SemaphorePtr m_items_dequeued_sema;
    std::chrono::milliseconds m_default_timeout;
};

} /* namespace hailort */

#endif // HAILO_THREAD_SAFE_QUEUE_HPP_
