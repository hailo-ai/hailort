/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file callback_reorder_queue.hpp
 * @brief When using multiple devices with async API, we may get interrupt for some input/output stream out of order
 *        (For example - the second device may be faster than the first).
 *        To ensure the order of the callbacks, we put the callbacks in queue and call them in the same order inserted.
 **/

#ifndef _HAILO_CALLBACK_REORDER_QUEUE_HPP_
#define _HAILO_CALLBACK_REORDER_QUEUE_HPP_

#include "vdma/channel/transfer_common.hpp"

#include <mutex>
#include <queue>

namespace hailort
{

class CallbackReorderQueue final {
public:
    CallbackReorderQueue(size_t max_size) :
        m_max_size(max_size),
        m_callbacks_queue(compare_callbacks{}, make_queue_storage(m_max_size))
    {}

    // Wraps the given original callback so it will be called in the same wrap_callback order.
    TransferDoneCallback wrap_callback(const TransferDoneCallback &original);

    // If some wrapped callback wasn't registered to some async API (for example because the queue is full), we need to
    // remove the counters we added in `wrap_callback` (otherwise, next callback will wait forever).
    // Note!
    //   * Call this function only after a `wrap_callback` was called.
    //   * Make sure the wrapped callback will never be called! (Otherwise counters will loss syncronization).
    void cancel_last_callback();

private:
    // must be called with m_lock held
    void call_queued_callbacks_in_order();

    // Each callback has a function pointer and its index
    using Callback = std::pair<uint64_t, std::function<void()>>;

    void push_callback(const Callback &callback);

    // Pop next callback ready to be called. Can return HAILO_NOT_AVAILABLE if there is no callback ready.
    Expected<Callback> pop_ready_callback();

    // We don't want to have any memory allocations in runtime, so we init the priority queue with a reserved vector.
    static std::vector<Callback> make_queue_storage(size_t max_size)
    {
        std::vector<Callback> storage;
        storage.reserve(max_size);
        return storage;
    }

    const size_t m_max_size;

    // Guards access to m_callbacks_queue and the counters.
    std::mutex m_queue_mutex;

    // Increasing counter for the index on next register callback. We don't worry about overflow (Even if we assume
    // extreme value of 1,000,000 per second)
    uint64_t m_registered_callbacks = 0;

    // Amount of callback that have called. Because the callbacks are called in order, this counter contains the index
    // of the next callback expected to be executed.
    uint64_t m_called_callbacks = 0;

    struct compare_callbacks {
        bool operator()(const Callback &a, const Callback &b)
        {
            // We want to pop the lower index first
            return a.first > b.first;
        }
    };

    // Callbacks are stored inside a priority_queue data-structure.
    // The queue is sorted by the callbacks index (so we pop the callbacks with the smallest index first).
    std::priority_queue<Callback, std::vector<Callback>, compare_callbacks> m_callbacks_queue;

    // This lock guarantee that only one thread is executing the callbacks.
    std::mutex m_callbacks_mutex;
};

} /* namespace hailort */

#endif /* _HAILO_CALLBACK_REORDER_QUEUE_HPP_ */
