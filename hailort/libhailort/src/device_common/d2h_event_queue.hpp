/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file d2h_event_queue.hpp
 * @brief TODO: brief
 *
 * TODO: doc
 **/

#ifndef HAILO_D2H_EVENT_QUEUE_HPP_
#define HAILO_D2H_EVENT_QUEUE_HPP_

#include "utils/thread_safe_queue.hpp"

#include "d2h_events.h"


namespace hailort
{

class D2hEventQueue : public SafeQueue<D2H_EVENT_MESSAGE_t> {
public:
    void clear() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_queue = std::queue<D2H_EVENT_MESSAGE_t>();
    }
};

} /* namespace hailort */

#endif // HAILO_D2H_EVENT_QUEUE_HPP_
