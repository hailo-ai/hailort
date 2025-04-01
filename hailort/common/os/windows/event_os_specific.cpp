/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file event_os_specific.cpp
 * @brief Event/semaphore OS specific implementation for windows using event/semaphore HANDLE
 **/

#include "common/event_internal.hpp"
#include "common/utils.hpp"

namespace hailort
{

static DWORD timeout_millies(long long value)
{
    DWORD millies = static_cast<DWORD>(value);
    if (UINT_MAX < value) {
        millies = INFINITE;
    }
    return millies;
}

Expected<size_t> WaitableGroup::wait_any(std::chrono::milliseconds timeout)
{
    DWORD wait_millies = timeout_millies(timeout.count());

    const auto WAIT_OBJECT_N = WAIT_OBJECT_0 + m_waitable_handles.size();
    const bool WAIT_FOR_ANY = false;
    const auto wait_result = WaitForMultipleObjects(static_cast<DWORD>(m_waitable_handles.size()),
        m_waitable_handles.data(), WAIT_FOR_ANY, wait_millies);
    if (wait_result == WAIT_TIMEOUT) {
        return make_unexpected(HAILO_TIMEOUT);
    } else if ((wait_result >= WAIT_OBJECT_0) && (wait_result < WAIT_OBJECT_N)) {
        // Object is signaled.
        // Note! On windows there is no need to call post_wait() because it is done automatically.
        return wait_result - WAIT_OBJECT_0;
    } else {
        LOGGER__ERROR("WaitForMultipleObjects returned {}, last_error={}", wait_result, GetLastError());
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }
}

} /* namespace hailort */
