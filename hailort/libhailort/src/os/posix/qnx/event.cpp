/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file event.cpp
 * @brief Event & Semaphore wrapper for QNX
 *
 * This class implements our Events API over the neosmart pevents events. It also implement the Semaphore behavior and API
 * Using the pevents events. For more information check out the implementation of pevents https://github.com/neosmart/pevents
 **/
#include "hailo/event.hpp"
#include "hailo/hailort.h"
#include "common/utils.hpp"
#include "event_internal.hpp"

#include <poll.h>
#include <utility>

#define WFMO
#include "pevents.h"
#undef WFMO

#define INVALID_EVENT_HANDLE    (nullptr)
#define WAIT_OBJECT_0           (0)

namespace hailort
{

Waitable::Waitable(underlying_waitable_handle_t handle) :
    m_handle(handle)
{}

Waitable::~Waitable()
{
    if (INVALID_EVENT_HANDLE != m_handle) {
        int err = 0;
        if (0 != (err = neosmart::DestroyEvent(m_handle))) {
            LOGGER__ERROR("Error destroying event handle, returned error={}", err);
        }
    }
}

Waitable::Waitable(Waitable&& other) :
    m_handle(std::exchange(other.m_handle, INVALID_EVENT_HANDLE))
{}

underlying_waitable_handle_t Waitable::get_underlying_handle()
{
    return m_handle;
}

hailo_status Waitable::wait_for_single_object(underlying_waitable_handle_t handle, std::chrono::milliseconds timeout)
{
    const size_t timeout_ms = (timeout.count() > INT_MAX) ? INT_MAX : static_cast<size_t>(timeout.count());
    const auto wait_result = neosmart::WaitForEvent(handle, timeout_ms);
    switch (wait_result) {
        case WAIT_OBJECT_0:
            return HAILO_SUCCESS;
        case ETIMEDOUT:
            return HAILO_TIMEOUT;
        default:
            LOGGER__ERROR("WaitForEvent failed, returned error={}", wait_result);
            return HAILO_INTERNAL_FAILURE;
    }
}

Expected<Event> Event::create(const State& initial_state)
{
    const auto handle = open_event_handle(initial_state);
    if (INVALID_EVENT_HANDLE == handle) {
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }
    return std::move(Event(handle));
}

EventPtr Event::create_shared(const State& initial_state)
{
    const auto handle = open_event_handle(initial_state);
    if (INVALID_EVENT_HANDLE == handle) {
        return nullptr;
    }

    return make_shared_nothrow<Event>(handle);
}

hailo_status Event::wait(std::chrono::milliseconds timeout)
{
    return wait_for_single_object(m_handle, timeout);
}

hailo_status Event::signal()
{
    const auto result = neosmart::SetEvent(m_handle);
    CHECK(0 == result, HAILO_INTERNAL_FAILURE, "SetEvent failed with error {}" , result);

    return HAILO_SUCCESS;
}

bool Event::is_auto_reset()
{
    return false;
}

hailo_status Event::reset()
{
    const auto result = neosmart::ResetEvent(m_handle);
    CHECK(0 == result, HAILO_INTERNAL_FAILURE, "ResetEvent failed with error {}", result);
    
    return HAILO_SUCCESS;
}

underlying_waitable_handle_t Event::open_event_handle(const State& initial_state)
{
    const bool manual_reset = true;
    const bool state = (initial_state == State::signalled ? true : false);
    auto event = neosmart::CreateEvent(manual_reset, state);
    if (INVALID_EVENT_HANDLE == event) {
        LOGGER__ERROR("Call to CreateEvent failed");
    }
    return event;
}

Expected<Semaphore> Semaphore::create(uint32_t initial_count)
{
    const auto handle = open_semaphore_handle(initial_count);
    if (INVALID_EVENT_HANDLE == handle) {
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }
    return std::move(Semaphore(handle, initial_count));
}

SemaphorePtr Semaphore::create_shared(uint32_t initial_count)
{
    const auto handle = open_semaphore_handle(initial_count);
    if (INVALID_EVENT_HANDLE == handle) {
        return nullptr;
    }

    return make_shared_nothrow<Semaphore>(handle, initial_count);
}

hailo_status Semaphore::wait(std::chrono::milliseconds timeout)
{
    auto wait_result = wait_for_single_object(m_handle, timeout);
    if (HAILO_SUCCESS == wait_result) {
        m_sem_mutex.lock();
        if (0 == m_count.load()) {
            LOGGER__ERROR("Waiting on semaphore with 0 value");
        }
        if (m_count > 0) {
            m_count--;
        }
        // After decrementing the value of the semaphore - check if the new value is bigger than 0 and if it is signal the event
        if (m_count > 0) {
            neosmart::SetEvent(m_handle);
        }
        m_sem_mutex.unlock();
    }
    
    return wait_result;
}

hailo_status Semaphore::signal()
{
    m_sem_mutex.lock();
    const auto result = neosmart::SetEvent(m_handle);
    if (0 != result) {
        LOGGER__ERROR("SetEvent failed with error {}", result);
        m_sem_mutex.unlock();
        return HAILO_INTERNAL_FAILURE;
    }
    m_count++;
    m_sem_mutex.unlock();
    
    return HAILO_SUCCESS;
}

bool Semaphore::is_auto_reset()
{
    return true;
}

underlying_waitable_handle_t Semaphore::open_semaphore_handle(uint32_t initial_count)
{
    const bool manual_reset = false;
    const bool state = (initial_count > 0 ? true : false);
    auto event = neosmart::CreateEvent(manual_reset, state);
    if (INVALID_EVENT_HANDLE == event) {
        LOGGER__ERROR("Call to CreateEvent failed");
    }
    return event;
}

Semaphore::Semaphore(underlying_waitable_handle_t handle, uint32_t initial_count) :
    Waitable(handle), m_count(initial_count)
{}

Semaphore::Semaphore(Semaphore&& other) :
    Waitable(std::move(other))
{
    other.m_sem_mutex.lock();
    m_count.store(other.m_count.load());
    other.m_sem_mutex.unlock();
}

WaitOrShutdown::WaitOrShutdown(WaitablePtr waitable, EventPtr shutdown_event) :
    m_waitable(waitable),
    m_shutdown_event(shutdown_event),
    m_wait_handle_array(create_wait_handle_array(waitable, shutdown_event))
{}

void Event::post_wait()
{}

void Semaphore::post_wait(){
    m_sem_mutex.lock();
    if (0 == m_count.load()) {
        LOGGER__ERROR("Wait Returned on semaphore with 0 value");
    }
    if (m_count > 0) {
        m_count--;
    }
    // After decrementing the value of the semaphore - check if the new value is bigger than 0 and if it is signal the event
    if (m_count > 0) {
        neosmart::SetEvent(m_handle);
    }
    m_sem_mutex.unlock();
}

hailo_status WaitOrShutdown::wait(std::chrono::milliseconds timeout)
{
    int wait_index = -1;
    const uint64_t timeout_ms = (timeout.count() > INT_MAX) ? INT_MAX : static_cast<uint64_t>(timeout.count());
    const auto wait_result = neosmart::WaitForMultipleEvents(m_wait_handle_array.data(), static_cast<int>(m_wait_handle_array.size()),
        false, timeout_ms, wait_index);
    // If semaphore need to subtract from counter
    if (0 != wait_result) {
        if (ETIMEDOUT == wait_result) {
            return HAILO_TIMEOUT;
        } else {
            LOGGER__ERROR("WaitForMultipleEvents Failed, error: {}", wait_result);
            return HAILO_INTERNAL_FAILURE;
        }
    }
    
    if (WAITABLE_INDEX == wait_index) {
        // Meaning it can be a semaphore object
        m_waitable->post_wait();
        return HAILO_SUCCESS;
    } else if (SHUTDOWN_INDEX == wait_index) {
        return HAILO_SHUTDOWN_EVENT_SIGNALED;
    } else {
        LOGGER__ERROR("Invalid event index signalled in WaitForMultipleEventsFailed, index: {}", wait_index);
        return HAILO_INTERNAL_FAILURE;
    }
}

hailo_status WaitOrShutdown::signal()
{
    return m_waitable->signal();
}

WaitOrShutdown::WaitHandleArray WaitOrShutdown::create_wait_handle_array(WaitablePtr waitable, EventPtr shutdown_event)
{
    // Note the order!
    WaitHandleArray handles{
        shutdown_event->get_underlying_handle(),
        waitable->get_underlying_handle()
    };
    return handles;
}

} /* namespace hailort */
