/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file event_os_specific.cpp
 * @brief Event/semaphore OS specific implementation for linux using eventfd
 **/

#include "common/event_internal.hpp"
#include "common/utils.hpp"

namespace hailort
{

Expected<size_t> WaitableGroup::wait_any(std::chrono::milliseconds timeout)
{
    int poll_ret = -1;
    do {
        poll_ret = poll(m_waitable_handles.data(), m_waitable_handles.size(), static_cast<int>(timeout.count()));
    } while ((0 > poll_ret) && (EINTR == poll_ret));

    if (0 == poll_ret) {
        LOGGER__TRACE("Timeout");
        return make_unexpected(HAILO_TIMEOUT);
    }
    CHECK_AS_EXPECTED(poll_ret > 0, HAILO_INTERNAL_FAILURE, "poll failed with errno={}", errno);

    for (size_t index = 0; index < m_waitable_handles.size(); index++) {
        if (m_waitable_handles[index].revents & POLLIN) {
            auto status = m_waitables[index].get().post_wait();
            CHECK_SUCCESS_AS_EXPECTED(status);

            return index;
        }
    }

    LOGGER__ERROR("None of the pollfd are in read state");
    return make_unexpected(HAILO_INTERNAL_FAILURE);
}

} /* namespace hailort */
