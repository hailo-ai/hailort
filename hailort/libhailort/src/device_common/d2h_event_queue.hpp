/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file d2h_event_queue.hpp
 * @brief Queue for d2h events
 **/

#ifndef HAILO_D2H_EVENT_QUEUE_HPP_
#define HAILO_D2H_EVENT_QUEUE_HPP_

#include "common/thread_safe_queue.hpp"

#include "d2h_events.h"


namespace hailort
{


class D2hEventQueue final {
public:
    D2hEventQueue() = default;

    // Add an element to the queue.
    void push(D2H_EVENT_MESSAGE_t t);

    // Get the "front"-element.
    // If the queue is empty, wait till a element is available.
    D2H_EVENT_MESSAGE_t pop();

    void clear();

protected:
    std::queue<D2H_EVENT_MESSAGE_t> m_queue;
    mutable std::mutex m_mutex;
    std::condition_variable m_queue_not_empty;
};


} /* namespace hailort */

#endif // HAILO_D2H_EVENT_QUEUE_HPP_
