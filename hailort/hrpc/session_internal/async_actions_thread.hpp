/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file async_actions_thread.hpp
 * @brief Async Actions Thread - processes async actions from a queue in a dedicated thread
 **/

#ifndef _ASYNC_ACTIONS_THREAD_HPP_
#define _ASYNC_ACTIONS_THREAD_HPP_

#include "hailo/expected.hpp"
#include "hailo/event.hpp"
#include "common/thread_safe_queue.hpp"
#include <memory>

namespace hailort
{

inline constexpr uint64_t MAX_ONGOING_TRANSFERS = 128;

struct AsyncAction
{
    std::function<hailo_status(bool)> action;
    std::function<void(hailo_status)> on_finish_callback;
};

class AsyncActionsThread final
{
public:
    static Expected<std::shared_ptr<AsyncActionsThread>> create(size_t queue_size);
    AsyncActionsThread(SpscQueue<AsyncAction> &&queue, EventPtr shutdown_event);
    ~AsyncActionsThread();

    hailo_status wait_for_enqueue_ready(std::chrono::milliseconds timeout);
    hailo_status enqueue_nonblocking(AsyncAction action);
    hailo_status abort();
private:
    hailo_status thread_loop();

    std::mutex m_mutex;
    std::condition_variable m_cv;
    SpscQueue<AsyncAction> m_queue;
    EventPtr m_shutdown_event;
    std::thread m_thread;
    std::atomic<uint32_t> m_current_queue_size;
};

} // namespace hailort

#endif // _ASYNC_ACTIONS_THREAD_HPP_
