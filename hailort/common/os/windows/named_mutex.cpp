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

// Windows named mutex names have a maximum length and certain character restrictions.
// We prefix with "Global\" to make the mutex system-wide across all sessions.
static const std::string HAILO_MUTEX_PREFIX = "Global\\hailo\\";

namespace hailort
{

Expected<std::shared_ptr<NamedMutex>> NamedMutex::create(const std::string &path)
{
    CHECK(!path.empty(), HAILO_INVALID_ARGUMENT, "Invalid named mutex path: {}", path);
    const auto full_path = HAILO_MUTEX_PREFIX + path;

    HANDLE mutex_handle = CreateMutex(nullptr, FALSE, full_path.c_str());
    CHECK(nullptr != mutex_handle, HAILO_INTERNAL_FAILURE,
        "Failed to create named mutex '{}' with error: {}", full_path, GetLastError());

    auto ptr = make_shared_nothrow<NamedMutex>(path, mutex_handle);
    CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);

    return ptr;
}

hailo_status NamedMutex::lock(std::chrono::milliseconds timeout)
{
    DWORD wait_time = (std::chrono::milliseconds::max() == timeout) ? INFINITE : static_cast<DWORD>(timeout.count());
    DWORD result = WaitForSingleObject(m_mutex_handle, wait_time);
    switch (result) {
    case WAIT_OBJECT_0:
        return HAILO_SUCCESS;
    case WAIT_ABANDONED:
        LOGGER__INFO("Named mutex '{}' was abandoned by another process", m_path);
        return HAILO_SUCCESS;
    case WAIT_TIMEOUT:
        return HAILO_TIMEOUT;
    case WAIT_FAILED:
    default:
        LOGGER__ERROR("Failed to lock named mutex '{}' with error: {}", m_path, GetLastError());
        return HAILO_INTERNAL_FAILURE;
    }
}

hailo_status NamedMutex::unlock()
{
    BOOL result = ReleaseMutex(m_mutex_handle);
    CHECK(result, HAILO_INTERNAL_FAILURE,
        "Failed to unlock named mutex '{}' with error: {}", m_path, GetLastError());
    return HAILO_SUCCESS;
}

NamedMutex::~NamedMutex()
{
    if (nullptr != m_mutex_handle) {
        CloseHandle(m_mutex_handle);
    }
}

} /* namespace hailort */
