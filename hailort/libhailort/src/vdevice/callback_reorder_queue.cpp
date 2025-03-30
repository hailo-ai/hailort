/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file callback_reorder_queue.cpp
 **/

#include "callback_reorder_queue.hpp"

namespace hailort
{

TransferDoneCallback CallbackReorderQueue::wrap_callback(const TransferDoneCallback &original)
{
    std::lock_guard<std::mutex> lock_guard(m_queue_mutex);
    const uint64_t current_callback_index = m_registered_callbacks++;

    return [this, original, current_callback_index](hailo_status status) {
        // Push callback without calling it yet.
        push_callback(std::make_pair(current_callback_index, [original, status]() {
            return original(status);
        }));

        // Then, call the queued callbacks in order (if there is ready callback).
        call_queued_callbacks_in_order();
    };
}

void CallbackReorderQueue::cancel_last_callback()
{
    std::lock_guard<std::mutex> lock_guard(m_queue_mutex);
    assert(m_called_callbacks < m_registered_callbacks);
    m_registered_callbacks--;
}

void CallbackReorderQueue::push_callback(const Callback &callback)
{
    std::lock_guard<std::mutex> lock_guard(m_queue_mutex);
    assert(m_callbacks_queue.size() < m_max_size);
    m_callbacks_queue.push(callback);
}

void CallbackReorderQueue::call_queued_callbacks_in_order()
{
    // Allow only one thread to execute the callbacks.
    std::lock_guard<std::mutex> callbacks_lock(m_callbacks_mutex);

    while (auto callback = pop_ready_callback()) {
        callback->second();
    }
}

Expected<CallbackReorderQueue::Callback> CallbackReorderQueue::pop_ready_callback()
{
    std::lock_guard<std::mutex> lock_guard(m_queue_mutex);

    if (m_callbacks_queue.empty()) {
        return make_unexpected(HAILO_NOT_AVAILABLE);
    }

    if (m_callbacks_queue.top().first != m_called_callbacks) {
        // We need to wait until top() contains callback with index - m_called_callbacks.
        return make_unexpected(HAILO_NOT_AVAILABLE);
    }

    auto next_callback = m_callbacks_queue.top();
    m_callbacks_queue.pop();

    m_called_callbacks++;
    return next_callback;
}

} /* namespace hailort */
