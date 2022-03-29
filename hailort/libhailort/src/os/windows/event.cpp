/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file event.cpp
 * @brief Event & Semaphore wrapper for Windows
 **/
#include "hailo/event.hpp"
#include "hailo/hailort.h"
#include "common/utils.hpp"
#include "event_internal.hpp"

#include <utility>
#include <limits>

namespace hailort
{

Waitable::Waitable(underlying_handle_t handle) :
    m_handle(handle)
{}

Waitable::~Waitable()
{
    if (nullptr != m_handle) {
        (void) CloseHandle(m_handle);
    }
}

Waitable::Waitable(Waitable&& other) :
    m_handle(std::exchange(other.m_handle, nullptr))
{}

underlying_handle_t Waitable::get_underlying_handle()
{
    return m_handle;
}

static DWORD timeout_millies(long long value)
{
    DWORD millies = static_cast<DWORD>(value);
    if (UINT_MAX < value) {
        millies = INFINITE;
    }
    return millies;
}

hailo_status Waitable::wait_for_single_object(underlying_handle_t handle, std::chrono::milliseconds timeout)
{
    DWORD wait_millies = timeout_millies(timeout.count());
    assert(nullptr != handle);

    const auto wait_result = WaitForSingleObject(handle, wait_millies);
    switch (wait_result) {
        case WAIT_OBJECT_0:
            return HAILO_SUCCESS;
        case WAIT_TIMEOUT:
            return HAILO_TIMEOUT;
        case WAIT_ABANDONED:
            LOGGER__ERROR("WaitForSingleObject on handle={:X} returned WAIT_ABANDONED", handle);
            return HAILO_INTERNAL_FAILURE;
        default:
            LOGGER__ERROR("WaitForSingleObject on handle={:X} returned WAIT_FAILED, last_error={}", handle, GetLastError());
            return HAILO_INTERNAL_FAILURE;
    }
}

Expected<Event> Event::create(const State& initial_state)
{
    const auto handle = open_event_handle(initial_state);
    if (nullptr == handle) {
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }
    return std::move(Event(handle));
}

EventPtr Event::create_shared(const State& initial_state)
{
    const auto handle = open_event_handle(initial_state);
    if (nullptr == handle) {
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
    const auto result = SetEvent(m_handle);
    if (0 == result) {
        LOGGER__ERROR("SetEvent on handle={:X} failed with last_error={}", m_handle, GetLastError());
        return HAILO_INTERNAL_FAILURE;
    }

    return HAILO_SUCCESS;
}

bool Event::is_auto_reset()
{
    return false;
}

hailo_status Event::reset()
{
    const auto result = ResetEvent(m_handle);
    if (0 == result) {
        LOGGER__ERROR("ResetEvent on handle={:X} failed with last_error={}", m_handle, GetLastError());
        return HAILO_INTERNAL_FAILURE;
    }
    
    return HAILO_SUCCESS;
}

underlying_handle_t Event::open_event_handle(const State& initial_state)
{
    static const LPSECURITY_ATTRIBUTES NO_INHERITANCE = nullptr;
    static const BOOL                  MANUAL_RESET = true;
    static const LPCSTR                NO_NAME = nullptr;
    const BOOL state = initial_state == State::signalled ? true : false;
    const auto handle = CreateEventA(NO_INHERITANCE, MANUAL_RESET, state, NO_NAME);
    if (nullptr == handle) {
        LOGGER__ERROR("Call to CreateEventA failed with last_error={}", GetLastError());
    }
    return handle;
}

Expected<Semaphore> Semaphore::create(uint32_t initial_count)
{
    const auto handle = open_semaphore_handle(initial_count);
    if (nullptr == handle) {
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }
    return std::move(Semaphore(handle));
}

SemaphorePtr Semaphore::create_shared(uint32_t initial_count)
{
    const auto handle = open_semaphore_handle(initial_count);
    if (nullptr == handle) {
        return nullptr;
    }

    return make_shared_nothrow<Semaphore>(handle);
}

hailo_status Semaphore::wait(std::chrono::milliseconds timeout)
{
    return wait_for_single_object(m_handle, timeout);
}

hailo_status Semaphore::signal()
{
    static const LONG INCREMENT_BY_ONE = 1;
    static const PLONG IGNORE_PREVIOUS_COUNT = nullptr;
    const auto result = ReleaseSemaphore(m_handle, INCREMENT_BY_ONE, IGNORE_PREVIOUS_COUNT);
    if (0 == result) {
        LOGGER__ERROR("ReleaseSemaphore on handle={:X} failed with last_error={}", m_handle, GetLastError());
        return HAILO_INTERNAL_FAILURE;
    }
    
    return HAILO_SUCCESS;
}

bool Semaphore::is_auto_reset()
{
    return true;
}

underlying_handle_t Semaphore::open_semaphore_handle(uint32_t initial_count)
{
    static const LPSECURITY_ATTRIBUTES NO_INHERITANCE = nullptr;
    static const LONG                  MAX_SIZE = std::numeric_limits<long>::max();
    static const LPCSTR                NO_NAME = nullptr;
    const auto handle = CreateSemaphoreA(NO_INHERITANCE, initial_count, MAX_SIZE, NO_NAME);
    if (nullptr == handle) {
        LOGGER__ERROR("Call to CreateSemaphoreA failed with last_error={}", GetLastError());
    }
    return handle;
}

WaitOrShutdown::WaitOrShutdown(WaitablePtr waitable, EventPtr shutdown_event) :
    m_waitable(waitable),
    m_shutdown_event(shutdown_event),
    m_wait_handle_array(create_wait_handle_array(waitable, shutdown_event))
{}

hailo_status WaitOrShutdown::wait(std::chrono::milliseconds timeout)
{
    DWORD wait_millies = timeout_millies(timeout.count());

    static const BOOL WAIT_FOR_ANY = false;
    const auto wait_result = WaitForMultipleObjects(static_cast<DWORD>(m_wait_handle_array.size()),
        m_wait_handle_array.data(), WAIT_FOR_ANY, wait_millies);
    switch (wait_result) {
        case WAIT_OBJECT_0 + WAITABLE_INDEX:
            return HAILO_SUCCESS;
        case WAIT_OBJECT_0 + SHUTDOWN_INDEX:
            return HAILO_SHUTDOWN_EVENT_SIGNALED;
        case WAIT_TIMEOUT:
            return HAILO_TIMEOUT;
        default:
            LOGGER__ERROR("WaitForMultipleObjects returned {}, last_error={}", wait_result, GetLastError());
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
