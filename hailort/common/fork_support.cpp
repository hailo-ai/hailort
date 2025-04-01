/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file fork_support.cpp
 **/

#include "fork_support.hpp"
#include "common/logger_macros.hpp"


namespace hailort
{

#ifdef HAILO_IS_FORK_SUPPORTED
RecursiveSharedMutex::RecursiveSharedMutex()
{
    // Make sharable mutex
    pthread_mutexattr_t mutex_attrs{};
    int err = pthread_mutexattr_init(&mutex_attrs);
    if (0 != err) {
        LOGGER__CRITICAL("Failed init mutex attr, aborting");
        std::abort();
    }

    err = pthread_mutexattr_setpshared(&mutex_attrs, PTHREAD_PROCESS_SHARED);
    if (0 != err) {
        LOGGER__CRITICAL("pthread_mutexattr_setpshared failed");
        std::abort();
    }

    err = pthread_mutexattr_settype(&mutex_attrs, PTHREAD_MUTEX_RECURSIVE);
    if (0 != err) {
        LOGGER__CRITICAL("pthread_mutexattr_settype failed");
        std::abort();
    }

    err = pthread_mutex_init(&m_mutex, &mutex_attrs);
    if (0 != pthread_mutexattr_destroy(&mutex_attrs)) {
        LOGGER__CRITICAL("Failed destroy mutexattr");
        // continue
    }
    if (0 != err) {
        LOGGER__CRITICAL("Failed init mutex, aborting");
        std::abort();
    }
}

RecursiveSharedMutex::~RecursiveSharedMutex()
{
    int err = pthread_mutex_destroy(&m_mutex);
    if (0 != err) {
        LOGGER__ERROR("Failed destroy shared mutex, errno {}", err);
    }
}

void RecursiveSharedMutex::lock()
{
    int err = pthread_mutex_lock(&m_mutex);
    if (0 != err) {
        LOGGER__ERROR("Failed lock shared mutex, errno {}", err);
        std::abort();
    }
}

void RecursiveSharedMutex::unlock()
{
    int err = pthread_mutex_unlock(&m_mutex);
    if (0 != err) {
        LOGGER__ERROR("Failed unlock shared mutex, errno {}", err);
        std::abort();
    }
}

SharedConditionVariable::SharedConditionVariable()
{
    // Make sharable condvar
    pthread_condattr_t cond_attrs{};
    int err = pthread_condattr_init(&cond_attrs);
    if (0 != err) {
        LOGGER__CRITICAL("Failed init condition variable attr, aborting");
        std::abort();
    }

    err = pthread_condattr_setpshared(&cond_attrs, PTHREAD_PROCESS_SHARED);
    if (0 != err) {
        LOGGER__CRITICAL("pthread_condattr_setpshared failed");
        std::abort();
    }

    err = pthread_condattr_setclock(&cond_attrs, CLOCK_MONOTONIC);
    if (0 != err) {
        LOGGER__CRITICAL("pthread_condattr_setclock failed");
        std::abort();
    }

    err = pthread_cond_init(&m_cond, &cond_attrs);
    if (0 != pthread_condattr_destroy(&cond_attrs)) {
        LOGGER__CRITICAL("Failed destroy condattr");
        // continue
    }
    if (0 != err) {
        LOGGER__CRITICAL("Failed init mutex, aborting");
        std::abort();
    }
}

SharedConditionVariable::~SharedConditionVariable()
{
    int err = pthread_cond_destroy(&m_cond);
    if (0 != err) {
        LOGGER__ERROR("Failed destory vdma channel condition varialbe, errno {}", err);
    }
}

// Get the absolute time for the given timeout - calculate now() + timeout_ns
// using system CLOCK_MONOTONIC (Used for pthread condition variable wait)
static struct timespec get_absolute_time(std::chrono::nanoseconds timeout_ns)
{
    // Using chrono with timespec types to avoid casts
    using ts_seconds = std::chrono::duration<decltype(timespec::tv_sec)>;
    using ts_nanoseconds = std::chrono::duration<decltype(timespec::tv_nsec), std::nano>;

    struct timespec current_ts{};
    clock_gettime(CLOCK_MONOTONIC, &current_ts);

    assert((current_ts.tv_sec + std::chrono::duration_cast<ts_seconds>(timeout_ns).count()) <
        std::numeric_limits<decltype(timespec::tv_sec)>::max());
    auto absolute_sec = ts_seconds(current_ts.tv_sec) + std::chrono::duration_cast<ts_seconds>(timeout_ns);
    assert(current_ts.tv_nsec <= std::nano::den);
    auto absolute_nsec = ts_nanoseconds(current_ts.tv_nsec) +
        std::chrono::duration_cast<ts_nanoseconds>(timeout_ns % std::chrono::seconds(1));

    // Nanos overflow
    if (absolute_nsec.count() >= std::nano::den) {
        absolute_sec += ts_seconds(1);
        absolute_nsec = absolute_nsec % ts_seconds(1);
    }

    return timespec {
        .tv_sec = absolute_sec.count(),
        .tv_nsec = absolute_nsec.count()
    };
}

bool SharedConditionVariable::wait_for(std::unique_lock<RecursiveSharedMutex> &lock, std::chrono::milliseconds timeout, std::function<bool()> condition)
{
    if (UINT32_MAX == timeout.count()) {
        // Infinity wait
        int err = 0;
        while (!condition() && err == 0) {
            err = pthread_cond_wait(&m_cond, lock.mutex()->native_handle());
        }
        if (err != 0) {
            LOGGER__CRITICAL("Error waiting for shared condition variable: {}", err);
            std::abort();
        }
        return true;
    }
    else if (0 == timeout.count()) {
        // Special case for 0 timeout - we don't want to mess with absolute time
        return condition();
    } else {
        // Timed wait
        auto ts = get_absolute_time(timeout);

        int err = 0;
        while (!condition() && err == 0) {
            err = pthread_cond_timedwait(&m_cond, lock.mutex()->native_handle(), &ts);
        }
        if ((err != 0) && (err != ETIMEDOUT)) {
            LOGGER__CRITICAL("Error waiting for shared condition variable: {}", err);
            std::abort();
        }
        return err == 0;
    }
}

void SharedConditionVariable::notify_one()
{
    pthread_cond_signal(&m_cond);
}

void SharedConditionVariable::notify_all()
{
    pthread_cond_broadcast(&m_cond);
}

#endif /* HAILO_IS_FORK_SUPPORTED */


} /* namespace hailort */
