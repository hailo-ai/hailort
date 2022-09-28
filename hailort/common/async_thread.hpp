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
            m_result = func();
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
        return std::move(m_result);
    }

private:
    T m_result;
    std::thread m_thread;
};


template<typename T>
using AsyncThreadPtr = std::unique_ptr<AsyncThread<T>>;

} /* namespace hailort */

#endif /* _ASYNC_THREAD_HPP_ */
