/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file fork_support.hpp
 * @brief Utilities/classes uses to support fork in the process.
 *        In general, fork SHOULD NOT be supported, but we still have some places that uses fork.
 *        Hopefully this file will be delete as soon as possible.
 **/

#ifndef _HAILO_FORK_SUPPORT_HPP_
#define _HAILO_FORK_SUPPORT_HPP_

#include <mutex>
#include <functional>
#include <map>
#include <assert.h>

#ifndef _MSC_VER
#include <sys/mman.h>
#endif

#ifndef _MSC_VER
// Windows did the right choice - not supporting fork() at all, so we don't support it either.
#define HAILO_IS_FORK_SUPPORTED
#endif


namespace hailort
{


#ifdef HAILO_IS_FORK_SUPPORTED

// Replacement for std::recursive_mutex
class RecursiveSharedMutex final {
public:
    RecursiveSharedMutex();
    ~RecursiveSharedMutex();

    RecursiveSharedMutex(const RecursiveSharedMutex &) = delete;
    RecursiveSharedMutex &operator=(const RecursiveSharedMutex &) = delete;
    RecursiveSharedMutex(RecursiveSharedMutex &&) = delete;
    RecursiveSharedMutex &operator=(RecursiveSharedMutex &&) = delete;

    void lock();
    void unlock();

    pthread_mutex_t *native_handle()
    {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
};

// Replacement for std::condition_variable, can work only with RecursiveSharedMutex 
class SharedConditionVariable final {
public:

    SharedConditionVariable();
    ~SharedConditionVariable();

    SharedConditionVariable(const SharedConditionVariable &) = delete;
    SharedConditionVariable &operator=(const SharedConditionVariable &) = delete;
    SharedConditionVariable(SharedConditionVariable &&) = delete;
    SharedConditionVariable &operator=(SharedConditionVariable &&) = delete;

    bool wait_for(std::unique_lock<RecursiveSharedMutex> &lock, std::chrono::milliseconds timeout,
        std::function<bool()> condition);
    void notify_one();
    void notify_all();

private:
    pthread_cond_t m_cond;
};


// Objects that inherit from this class, will automatically reside in memory region shared
// between forked processed.
// virtual dtor is not implemented for this class since it shouldn't be used for polymorphism (=
// delete shouldn't be called on the SharedAllocatedObject).
class SharedAllocatedObject {
public:
    void* operator new(std::size_t size) = delete;
    void* operator new(std::size_t size, const std::nothrow_t&) throw()
    {
        // Map a shared memory region into the virtual memory of the process
        void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if (ptr == MAP_FAILED) {
            return nullptr;
        }
        return ptr;
    }

    // Custom operator delete function that unmaps the shared memory region
    void operator delete(void* ptr, std::size_t size)
    {
        munmap(ptr, size);
    }
};


// pthread_atfork api has 2 problems:
//      1. The callbacks doesn't accept context.
//      2. Callbacks cannot be unregistered.
// In order to solve this issue, the AtForkRegistry singleton exists and manages some registry
// of atfork callbacks.
// pthread_atfork is called only once, and on the provided callbacks, the registered user callbacks
// are called.
class AtForkRegistry final {
public:

    static AtForkRegistry &get_instance()
    {
        static AtForkRegistry at_fork;
        return at_fork;
    }

    AtForkRegistry(const AtForkRegistry &) = delete;
    AtForkRegistry &operator=(const AtForkRegistry &) = delete;

    // Special key used to identify the registered callbacks. One can use `this` as
    // a unique identifier.
    using Key = void*;

    struct AtForkCallbacks {
        std::function<void()> before_fork;
        std::function<void()> after_fork_in_parent;
        std::function<void()> after_fork_in_child;
    };

    // Init this guard with AtForkCallbacks, and the callbacks will be registered until destructed.
    struct AtForkGuard {
        AtForkGuard(Key key, const AtForkCallbacks &callbacks) :
            m_key(key)
        {
            AtForkRegistry::get_instance().register_atfork(key, callbacks);
        }

        ~AtForkGuard()
        {
            AtForkRegistry::get_instance().unregister_atfork(m_key);
        }

        AtForkGuard(const AtForkGuard&) = delete;
        AtForkGuard &operator=(const AtForkGuard &) = delete;

        const Key m_key;
    };


private:

    AtForkRegistry()
    {
        pthread_atfork(
            []() { get_instance().before_fork(); },
            []() { get_instance().after_fork_in_parent(); },
            []() { get_instance().after_fork_in_child(); }
        );
    }

    void register_atfork(Key key, const AtForkCallbacks &callbacks)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        assert(m_callbacks.end() == m_callbacks.find(key));
        m_callbacks[key] = callbacks;
    }

    void unregister_atfork(Key key)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        assert(m_callbacks.end() != m_callbacks.find(key));
        m_callbacks.erase(key);
    }

    void before_fork()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto &callback : m_callbacks) {
            callback.second.before_fork();
        }
    }

    void after_fork_in_parent()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto &callback : m_callbacks) {
            callback.second.after_fork_in_parent();
        }
    }

    void after_fork_in_child()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto &callback : m_callbacks) {
            callback.second.after_fork_in_child();
        }
    }

    std::mutex m_mutex;
    std::map<Key, AtForkCallbacks> m_callbacks;
};

#else /* HAILO_IS_FORK_SUPPORTED */
using RecursiveSharedMutex = std::recursive_mutex;
using SharedConditionVariable = std::condition_variable_any;


class SharedAllocatedObject {};
#endif


} /* namespace hailort */

#endif /* _HAILO_FORK_SUPPORT_HPP_ */
