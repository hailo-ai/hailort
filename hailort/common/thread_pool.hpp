/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file thread_pool.hpp
 * @brief Implementation of thread pool that uses async threads
 **/

#ifndef _THREAD_POOL_HPP_
#define _THREAD_POOL_HPP_

#include "async_thread.hpp"

namespace hailort {

class HailoThreadPool {
public:
    HailoThreadPool(size_t num_worker_threads) : m_num_threads(num_worker_threads), m_kill_threads(false) {
        auto shutdown_event = Event::create_shared(Event::State::not_signalled).release();

        for (size_t i = 0; i < num_worker_threads; i++) {
            m_threads.emplace_back(std::make_unique<AsyncThread<hailo_status>>(
            [this]() -> hailo_status {
                while(true) {
                    std::function<hailo_status()> func;
                    {
                        std::unique_lock<std::mutex> lock(m_mutex);
                        m_cv.wait(lock, [this](){ return (m_kill_threads || !m_queue.empty()); });
                        if (m_kill_threads && m_queue.empty()) {
                            return HAILO_SUCCESS;
                        }
                        func = std::move(m_queue.front());
                        m_queue.pop();
                    }

                    hailo_status status = func();
                    if (HAILO_SUCCESS != status) {
                        LOGGER__ERROR("thread failed with status {}");
                    }
               }
            }
        ));
        }
    }

    HailoThreadPool(const HailoThreadPool &) = delete;
    HailoThreadPool(HailoThreadPool &&other) = delete;
    HailoThreadPool& operator=(const HailoThreadPool&) = delete;
    HailoThreadPool& operator=(HailoThreadPool &&) = delete;

    template<class F, class... Args>
    void add_job(F&& func, Args&&... args) {
        auto job = std::bind(std::forward<F>(func), std::forward<Args>(args)...);
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_kill_threads) {
                LOGGER__ERROR("Cannot add jobs after threadpool has been terminated");
                return;
            }
            m_queue.emplace(job);
        }
        m_cv.notify_one();
    }

    ~HailoThreadPool() {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_kill_threads = true;
        }
        m_cv.notify_all();
        for (size_t i = 0; i < m_num_threads; i++) {
            AsyncThreadPtr<hailo_status> thread = std::move(m_threads[i]);
            thread->get();
        }
    }

private:
    size_t m_num_threads;
    std::vector<AsyncThreadPtr<hailo_status>> m_threads;
    std::queue<std::function<hailo_status()>> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_kill_threads;
    
};

} /* namespace hailort*/

#endif // _THREAD_POOL_HPP_