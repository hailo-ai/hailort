/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file named_mutex_guard.hpp
 * @brief Named mutex guard implementation
 **/

#include "named_mutex_guard.hpp"
#include "hailo/hailort.h"
#include "common/logger_macros.hpp"
#include "common/utils.hpp"

namespace hailort
{

Expected<std::unique_ptr<NamedMutexGuard>> NamedMutexGuard::create(const std::string &named_mutex)
{
    // Create a named mutex
    HANDLE mutex_handle = CreateMutex(NULL, FALSE, named_mutex.c_str());
    CHECK_AS_EXPECTED(mutex_handle != NULL, HAILO_INTERNAL_FAILURE, "Failed to create named mutex, error = {}", GetLastError());

    // Check if the mutex is already acquired by another instance
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        LOGGER__ERROR("Another instance of {} is already running", named_mutex);
        CloseHandle(mutex_handle);
        return make_unexpected(HAILO_INVALID_OPERATION);
    }

    auto guarded_named_mutex = make_unique_nothrow<NamedMutexGuard>(mutex_handle);
    CHECK_NOT_NULL_AS_EXPECTED(guarded_named_mutex, HAILO_OUT_OF_HOST_MEMORY);

    return guarded_named_mutex;
}

NamedMutexGuard::NamedMutexGuard(HANDLE mutex_handle) : m_mutex_handle(mutex_handle)
{}

NamedMutexGuard::~NamedMutexGuard()
{
    CloseHandle(m_mutex_handle);
}

} /* namespace hailort */
