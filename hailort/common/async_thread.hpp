/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file async_thread.hpp
 **/

#ifndef _ASYNC_THREAD_HPP_
#define _ASYNC_THREAD_HPP_

#include <functional>
#include <thread>
#include <memory>
#include <atomic>
#include <condition_variable>

namespace hailort
{

/**
 * Basic implementation of an async result of a function on some new thread. We use this class instead of `std::async`
 * because std::async uses future object that store/throw exceptions, and we can't compile it to armv7l platform.
 */
template<typename T>
class AsyncThread final {
public:
    explicit AsyncThread(std::function<T(void)> func) :
        m_result(),
        m_thread([this, func]() {
            m_result.store(func());
        })
    {}

    /**
     * NOTE! this object is not moveable by purpose, on creation we create a lambda that take `this`, if we
     * move the object `this` will change and the callback will be wrong. Use exeternal storage like std::unique_ptr
     * to move the object (or to put it inside a container)
     */
    AsyncThread(const AsyncThread<T> &) = delete;
    AsyncThread(AsyncThread<T> &&other) = delete;
    AsyncThread<T>& operator=(const AsyncThread<T>&) = delete;
    AsyncThread<T>& operator=(AsyncThread<T> &&) = delete;

    T get()
    {
        if (m_thread.joinable()) {
            m_thread.join();
        }
        return m_result.load();
    }

private:
    std::atomic<T> m_result;
    std::thread m_thread;
};


class ReusableThread final
{
public:
    ReusableThread(std::function<void()> lambda) : m_is_active(true), m_is_running(true)
    {
        m_thread = std::thread([this, lambda] () {
            while (m_is_active) {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [&] { return ((m_is_running.load()) || !(m_is_active.load())); });
                if (m_is_active.load()) {
                    lambda();
                    m_is_running = false;
                }
            }
        });
    };

    ~ReusableThread()
    {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_is_running = false;
            m_is_active = false;
        }
        m_cv.notify_one();
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    ReusableThread(const ReusableThread &other) = delete;
    ReusableThread &operator=(const ReusableThread &other) = delete;
    ReusableThread &operator=(ReusableThread &&other) = delete;
    ReusableThread(ReusableThread &&other) noexcept = delete;

    void restart()
    {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_is_running = true;
        }
        m_cv.notify_one();

    }

private:
    std::thread m_thread;
    std::atomic_bool m_is_active;
    std::atomic_bool m_is_running;
    std::mutex m_mutex;
    std::condition_variable m_cv;
};


template<typename T>
using AsyncThreadPtr = std::unique_ptr<AsyncThread<T>>;

} /* namespace hailort */

#endif /* _ASYNC_THREAD_HPP_ */
