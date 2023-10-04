/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file event_internal.cpp
 * @brief Internal implementation for events, shared between all os.
 **/

#include "common/event_internal.hpp"
#include "common/logger_macros.hpp"
#include "common/utils.hpp"

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

WaitOrShutdown::WaitOrShutdown(WaitablePtr waitable, EventPtr shutdown_event) :
    m_waitable(waitable),
    m_shutdown_event(shutdown_event),
    m_waitable_group(create_waitable_group(m_waitable, m_shutdown_event))
{}

hailo_status WaitOrShutdown::wait(std::chrono::milliseconds timeout)
{
    auto index = m_waitable_group.wait_any(timeout);
    if (index.status() == HAILO_TIMEOUT) {
        return index.status();
    }
    CHECK_EXPECTED_AS_STATUS(index);

    assert(index.value() <= WAITABLE_INDEX);
    return (index.value() == SHUTDOWN_INDEX) ? HAILO_SHUTDOWN_EVENT_SIGNALED : HAILO_SUCCESS;
}

hailo_status WaitOrShutdown::signal()
{
    return m_waitable->signal();
}

hailo_status WaitOrShutdown::shutdown()
{
    return m_shutdown_event->signal();
}

WaitableGroup WaitOrShutdown::create_waitable_group(WaitablePtr waitable, EventPtr shutdown_event)
{
    // Note the order - consistent with SHUTDOWN_INDEX, WAITABLE_INDEX.
    std::vector<std::reference_wrapper<Waitable>> waitables;
    waitables.emplace_back(std::ref(*shutdown_event));
    waitables.emplace_back(std::ref(*waitable));
    return waitables;
}

} /* namespace hailort */
