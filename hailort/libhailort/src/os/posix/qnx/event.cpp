/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file event.cpp
 * @brief Event & Semaphore wrapper for QNX
 *
 * This class implements our Events API over the neosmart pevents events. It also implement the Semaphore behavior and API
 * Using the pevents events. For more information check out the implementation of pevents https://github.com/neosmart/pevents
 **/

#include "hailo/hailort.h"
#include "hailo/event.hpp"

#include "common/utils.hpp"
#include "common/event_internal.hpp"

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

hailo_status Waitable::wait(std::chrono::milliseconds timeout)
{
    auto status = wait_for_single_object(m_handle, timeout);
    if (HAILO_TIMEOUT == status) {
        LOGGER__TRACE("wait_for_single_object failed with timeout (timeout={}ms)", timeout.count());
        return status;
    }
    CHECK_SUCCESS(status);

    status = post_wait();
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

underlying_waitable_handle_t Waitable::get_underlying_handle()
{
    return m_handle;
}

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
        return make_unexpected(HAILO_EVENT_CREATE_FAIL);
    }
    return std::move(Event(handle));
}

Expected<EventPtr> Event::create_shared(const State& initial_state)
{
    const auto handle = open_event_handle(initial_state);
    CHECK_AS_EXPECTED(INVALID_EVENT_HANDLE != handle, HAILO_EVENT_CREATE_FAIL);

    auto res = make_shared_nothrow<Event>(handle);
    CHECK_NOT_NULL_AS_EXPECTED(res, HAILO_OUT_OF_HOST_MEMORY);

    return res;
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
        return make_unexpected(HAILO_EVENT_CREATE_FAIL);
    }
    return std::move(Semaphore(handle, initial_count));
}

Expected<SemaphorePtr> Semaphore::create_shared(uint32_t initial_count)
{
    const auto handle = open_semaphore_handle(initial_count);
    CHECK(INVALID_EVENT_HANDLE != handle, HAILO_EVENT_CREATE_FAIL);

    auto res = make_shared_nothrow<Semaphore>(handle, initial_count);
    CHECK_NOT_NULL(res, HAILO_OUT_OF_HOST_MEMORY);

    return res;
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

hailo_status Semaphore::post_wait()
{
    std::unique_lock<std::mutex> lock(m_sem_mutex);
    CHECK(m_count.load() > 0, HAILO_INTERNAL_FAILURE, "Wait returned on semaphore with 0 value");

    m_count--;

    // After decrementing the value of the semaphore - check if the new value is bigger than 0 and if it is signal the event
    if (m_count > 0) {
        neosmart::SetEvent(m_handle);
    }

    return HAILO_SUCCESS;
}


} /* namespace hailort */
