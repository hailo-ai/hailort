/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file d2h_event_queue.cpp
 **/

#include "d2h_event_queue.hpp"

namespace hailort
{

void D2hEventQueue::push(D2H_EVENT_MESSAGE_t t)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push(t);
    }
    m_queue_not_empty.notify_one();
}

D2H_EVENT_MESSAGE_t D2hEventQueue::pop()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_queue_not_empty.wait(lock, [this](){ return !m_queue.empty(); });
    D2H_EVENT_MESSAGE_t val = m_queue.front();
    m_queue.pop();
    return val;
}

void D2hEventQueue::clear()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_queue = std::queue<D2H_EVENT_MESSAGE_t>();
}

} /* namespace hailort */
