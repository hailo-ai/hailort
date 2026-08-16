/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file named_mutex.cpp
 * @brief Named mutex guard Windows implementation using Windows named mutex
 **/

#include "common/named_mutex.hpp"
#include "hailo/hailort.h"
#include "common/utils.hpp"

#include <algorithm>
#include <mutex>
#include <unordered_map>

// Windows named mutex names have a maximum length and certain character restrictions.
// We prefix with "Global\" to make the mutex system-wide across all sessions.
static const std::string HAILO_MUTEX_PREFIX = "Global\\hailo_";

namespace hailort
{

static DWORD to_win32_timeout_ms(std::chrono::milliseconds timeout)
{
    if (timeout.count() < 0) {
        return 0;
    }
    if (timeout.count() >= static_cast<int64_t>(INFINITE)) {
        return INFINITE;
    }
    return static_cast<DWORD>(timeout.count());
}

std::unordered_map<std::string, std::shared_ptr<std::timed_mutex>> NamedMutex::m_shared_mutexes;
std::mutex NamedMutex::m_map_mutex;

Expected<std::shared_ptr<NamedMutex>> NamedMutex::create(const std::string &path)
{
    CHECK(!path.empty(), HAILO_INVALID_ARGUMENT, "Invalid named mutex path: {}", path);
    const auto full_path = HAILO_MUTEX_PREFIX + path;

    std::shared_ptr<std::timed_mutex> in_process_mutex = nullptr;
    {
        std::unique_lock<std::mutex> lock(m_map_mutex);
        if (contains(m_shared_mutexes, full_path)) {
            in_process_mutex = m_shared_mutexes[full_path];
        } else {
            in_process_mutex = make_shared_nothrow<std::timed_mutex>();
            CHECK_NOT_NULL(in_process_mutex, HAILO_OUT_OF_HOST_MEMORY);
            m_shared_mutexes[full_path] = in_process_mutex;
        }
    }

    HANDLE mutex_handle = CreateMutex(nullptr, FALSE, full_path.c_str());
    CHECK(nullptr != mutex_handle, HAILO_INTERNAL_FAILURE,
        "Failed to create named mutex '{}' with error: {}", full_path, GetLastError());

    auto ptr = make_shared_nothrow<NamedMutex>(path, mutex_handle, std::move(in_process_mutex));
    CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);

    return ptr;
}

hailo_status NamedMutex::lock(std::chrono::milliseconds timeout)
{
    const auto start_time = std::chrono::steady_clock::now();
    const bool is_infinite = (timeout.count() >= static_cast<int64_t>(INFINITE));

    bool got_in_process = false;
    if (is_infinite) {
        m_in_process_mutex->lock();
        got_in_process = true;
    } else {
        got_in_process = m_in_process_mutex->try_lock_for(timeout);
    }
    if (!got_in_process) {
        return HAILO_TIMEOUT;
    }

    DWORD wait_time = INFINITE;
    if (!is_infinite) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);
        const auto remaining = (elapsed >= timeout) ? std::chrono::milliseconds(0) : (timeout - elapsed);
        wait_time = to_win32_timeout_ms(remaining);
    }

    DWORD result = WaitForSingleObject(m_mutex_handle, wait_time);
    switch (result) {
    case WAIT_OBJECT_0:
        return HAILO_SUCCESS;
    case WAIT_ABANDONED:
        LOGGER__INFO("Named mutex '{}' was abandoned by another process", m_path);
        return HAILO_SUCCESS;
    case WAIT_TIMEOUT:
        m_in_process_mutex->unlock();
        return HAILO_TIMEOUT;
    case WAIT_FAILED:
    default:
        const DWORD last_error = GetLastError();
        LOGGER__ERROR("Failed to lock named mutex '{}' with error: {}", m_path, last_error);
        m_in_process_mutex->unlock();
        return HAILO_INTERNAL_FAILURE;
    }
}

hailo_status NamedMutex::unlock()
{
    BOOL result = ReleaseMutex(m_mutex_handle);
    const DWORD last_error = result ? 0 : GetLastError();
    m_in_process_mutex->unlock();
    CHECK(result, HAILO_INTERNAL_FAILURE,
        "Failed to unlock named mutex '{}' with error: {}", m_path, last_error);
    return HAILO_SUCCESS;
}

NamedMutex::~NamedMutex()
{
    if (nullptr != m_mutex_handle) {
        CloseHandle(m_mutex_handle);
    }
}

} /* namespace hailort */
