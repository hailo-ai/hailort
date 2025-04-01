/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file event.cpp
 * @brief Event & Semaphore wrapper for Windows
 **/

#include "hailo/hailort.h"
#include "hailo/event.hpp"

#include "common/utils.hpp"
#include "common/event_internal.hpp"

#include <utility>
#include <limits>


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
    if (nullptr != m_handle) {
        (void) CloseHandle(m_handle);
    }
}

Waitable::Waitable(Waitable&& other) :
    m_handle(std::exchange(other.m_handle, nullptr))
{}

static DWORD timeout_millies(long long value)
{
    DWORD millies = static_cast<DWORD>(value);
    if (UINT_MAX < value) {
        millies = INFINITE;
    }
    return millies;
}

hailo_status Waitable::wait_for_single_object(underlying_waitable_handle_t handle, std::chrono::milliseconds timeout)
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
        return make_unexpected(HAILO_EVENT_CREATE_FAIL);
    }
    return std::move(Event(handle));
}

Expected<EventPtr> Event::create_shared(const State& initial_state)
{
    const auto handle = open_event_handle(initial_state);
    CHECK_AS_EXPECTED(nullptr != handle, HAILO_EVENT_CREATE_FAIL);

    auto res = make_shared_nothrow<Event>(handle);
    CHECK_NOT_NULL_AS_EXPECTED(res, HAILO_OUT_OF_HOST_MEMORY);

    return res;
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

underlying_waitable_handle_t Event::open_event_handle(const State& initial_state)
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
        return make_unexpected(HAILO_EVENT_CREATE_FAIL);
    }
    return std::move(Semaphore(handle));
}

Expected<SemaphorePtr> Semaphore::create_shared(uint32_t initial_count)
{
    const auto handle = open_semaphore_handle(initial_count);
    CHECK_AS_EXPECTED(nullptr != handle, HAILO_EVENT_CREATE_FAIL);

    auto res = make_shared_nothrow<Semaphore>(handle);
    CHECK_NOT_NULL_AS_EXPECTED(res, HAILO_OUT_OF_HOST_MEMORY);

    return res;
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

hailo_status Semaphore::post_wait()
{
    // On windows, after wait on semaphore the counters decrease automatically.
    return HAILO_SUCCESS;
}

underlying_waitable_handle_t Semaphore::open_semaphore_handle(uint32_t initial_count)
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

} /* namespace hailort */
