/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file status_event.hpp
 * @brief Bundles an Event with an atomic outcome status so an async producer
 *        can signal both "ready" and "ready-but-failed" to the waiter.
 **/

#ifndef _HAILO_STATUS_EVENT_HPP_
#define _HAILO_STATUS_EVENT_HPP_

#include "hailo/event.hpp"
#include "hailo/expected.hpp"
#include "hailo/hailort.h"

#include "common/utils.hpp"

#include <atomic>
#include <chrono>
#include <memory>

namespace hailort
{

class StatusEvent final
{
public:
    static Expected<std::shared_ptr<StatusEvent>> create_shared()
    {
        TRY(auto event, Event::create(Event::State::not_signalled));
        auto signal = make_shared_nothrow<StatusEvent>(std::move(event));
        CHECK_NOT_NULL(signal, HAILO_OUT_OF_HOST_MEMORY);
        return signal;
    }

    explicit StatusEvent(Event event) : m_event(std::move(event)) {}

    StatusEvent(const StatusEvent &) = delete;
    StatusEvent &operator=(const StatusEvent &) = delete;
    StatusEvent(StatusEvent &&) = delete;
    StatusEvent &operator=(StatusEvent &&) = delete;

    hailo_status signal_success()
    {
        m_status.store(HAILO_SUCCESS, std::memory_order_release);
        return m_event.signal();
    }

    hailo_status signal_failure(hailo_status status)
    {
        m_status.store(status, std::memory_order_release);
        return m_event.signal();
    }

    hailo_status wait(std::chrono::milliseconds timeout)
    {
        auto wait_status = m_event.wait(timeout);
        if (HAILO_SUCCESS != wait_status) {
            return wait_status;
        }
        return m_status.load(std::memory_order_acquire);
    }

private:
    Event m_event;
    std::atomic<hailo_status> m_status{HAILO_UNINITIALIZED};
};

using StatusEventPtr = std::shared_ptr<StatusEvent>;

} /* namespace hailort */

#endif /* _HAILO_STATUS_EVENT_HPP_ */
