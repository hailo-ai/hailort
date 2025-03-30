/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file event_os_specific.cpp
 * @brief Event/semaphore OS specific implementation for qnx using pevents
 **/

#include "common/event_internal.hpp"
#include "common/utils.hpp"

#define WFMO
#include "pevents.h"
#undef WFMO

namespace hailort
{

Expected<size_t> WaitableGroup::wait_any(std::chrono::milliseconds timeout)
{
    int wait_index = -1;
    const uint64_t timeout_ms = (timeout.count() > INT_MAX) ? INT_MAX : static_cast<uint64_t>(timeout.count());
    const bool WAIT_FOR_ANY = false;
    const auto wait_result = neosmart::WaitForMultipleEvents(m_waitable_handles.data(),
        static_cast<int>(m_waitable_handles.size()), WAIT_FOR_ANY, timeout_ms, wait_index);
    if (0 != wait_result) {
        if (ETIMEDOUT == wait_result) {
            return make_unexpected(HAILO_TIMEOUT);
        } else {
            LOGGER__ERROR("WaitForMultipleEvents Failed, error: {}", wait_result);
            return make_unexpected(HAILO_INTERNAL_FAILURE);
        }
    }

    auto status = m_waitables[wait_index].get().post_wait();
    CHECK_SUCCESS_AS_EXPECTED(status);

    return wait_index;
}

} /* namespace hailort */
