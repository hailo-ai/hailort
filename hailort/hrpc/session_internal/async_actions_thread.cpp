/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file async_actions_thread.cpp
 * @brief Async Actions Thread - implementation
 **/

#include "hrpc/session_internal/async_actions_thread.hpp"
#include "common/logger_macros.hpp"
#include "common/utils.hpp"
#include "common/os_utils.hpp"

namespace hailort
{

Expected<std::shared_ptr<AsyncActionsThread>> AsyncActionsThread::create(size_t queue_size)
{
    TRY(auto shutdown_event, Event::create_shared(Event::State::not_signalled));
    TRY(auto write_queue, SpscQueue<AsyncAction>::create(queue_size, shutdown_event));

    auto ptr = make_shared_nothrow<AsyncActionsThread>(std::move(write_queue), shutdown_event);
    CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);

    return ptr;
}

AsyncActionsThread::AsyncActionsThread(SpscQueue<AsyncAction> &&queue, EventPtr shutdown_event) :
    m_queue(std::move(queue)), m_shutdown_event(shutdown_event), m_current_queue_size(0)
{
    m_thread = std::thread([this] () { thread_loop(); });
}

hailo_status AsyncActionsThread::abort()
{
    auto status = m_shutdown_event->signal();
    if (HAILO_SUCCESS != status) {
        LOGGER__CRITICAL("Failed to signal shutdown event, status = {}", status);
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }

    const bool IGNORE_SHUTDOWN_EVENT = true;
    while (true) {
        auto action = m_queue.dequeue(std::chrono::milliseconds(0), IGNORE_SHUTDOWN_EVENT);
        if (HAILO_TIMEOUT == action.status()) {
            break;
        }
        if (!action) {
            status = action.status();
            LOGGER__ERROR("Failed to dequeue action, status = {}", status);
            continue;
        }

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_current_queue_size--;
        }
        m_cv.notify_one();

        action->on_finish_callback(action->action(true));
    }
    return status;
}

AsyncActionsThread::~AsyncActionsThread()
{
    abort();
}

hailo_status AsyncActionsThread::thread_loop()
{
    while (true) {
        TRY_WITH_ACCEPTABLE_STATUS(HAILO_SHUTDOWN_EVENT_SIGNALED, auto action,
            m_queue.dequeue(std::chrono::milliseconds(HAILO_INFINITE)));
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_current_queue_size--;
        }
        m_cv.notify_one();
        action.on_finish_callback(action.action(false));
    }
    return HAILO_SUCCESS;
}

hailo_status AsyncActionsThread::wait_for_enqueue_ready(std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    CHECK(m_cv.wait_for(lock, timeout, [this] () {
        return m_current_queue_size < m_queue.max_capacity();
    }), HAILO_TIMEOUT, "Timeout waiting for enqueue ready");
    return HAILO_SUCCESS;
}

hailo_status AsyncActionsThread::enqueue_nonblocking(AsyncAction action)
{
    auto status = m_queue.enqueue(action, std::chrono::milliseconds(0));
    CHECK(status != HAILO_TIMEOUT, HAILO_QUEUE_IS_FULL, "Queue is full, queue size = {}",
        m_queue.size_approx());// Should call wait_for_enqueue_ready() before enqueue_nonblocking()
    CHECK_SUCCESS(status);

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_current_queue_size++;
    }
    m_cv.notify_one();

    return HAILO_SUCCESS;
}

} // namespace hailort
