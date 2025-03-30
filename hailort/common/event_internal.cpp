/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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


WaitOrShutdown::WaitOrShutdown(WaitablePtr waitable, EventPtr shutdown_event) :
    m_waitable(waitable),
    m_shutdown_event(shutdown_event),
    m_waitable_group(create_waitable_group(m_waitable, m_shutdown_event))
{}

hailo_status WaitOrShutdown::wait(std::chrono::milliseconds timeout)
{
    TRY_WITH_ACCEPTABLE_STATUS(HAILO_TIMEOUT, const auto index, m_waitable_group.wait_any(timeout));
    assert(index <= WAITABLE_INDEX);
    return (index == SHUTDOWN_INDEX) ? HAILO_SHUTDOWN_EVENT_SIGNALED : HAILO_SUCCESS;
}

hailo_status WaitOrShutdown::signal()
{
    // Cannot signal a WaitOrShutdown which has only shutdown event
    CHECK_NOT_NULL(m_waitable, HAILO_INVALID_OPERATION);
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

    if (nullptr != waitable) {
        waitables.emplace_back(std::ref(*waitable));
    }

    return waitables;
}

} /* namespace hailort */
