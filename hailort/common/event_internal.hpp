/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file event.hpp
 * @brief Event and Semaphore wrapper objects used for multithreading
 **/

#ifndef _EVENT_INTERNAL_HPP_
#define _EVENT_INTERNAL_HPP_

#include "hailo/event.hpp"

#include <memory>
#include <vector>
#include <array>
#include <chrono>
#if defined(__GNUC__)
#include <poll.h>
#endif

namespace hailort
{

// Group of Waitable objects that can be waited for together
class WaitableGroup final
{
public:
    WaitableGroup(std::vector<std::reference_wrapper<Waitable>> &&waitables) :
        m_waitables(std::move(waitables)),
        m_waitable_handles(create_waitable_handle_vector(m_waitables))
    {}

    /**
     * Waits until any of the given waitables are signaled. Returns the index in the waitables vector
     * of the signaled waitable with the smallest index value.
     */
    Expected<size_t> wait_any(std::chrono::milliseconds timeout);

private:

#if defined(__linux__)
    using WaitableHandle = pollfd;
#else
    using WaitableHandle = underlying_waitable_handle_t;
#endif

    static std::vector<WaitableHandle> create_waitable_handle_vector(
        const std::vector<std::reference_wrapper<Waitable>> &waitables)
    {
        std::vector<WaitableHandle> waitable_handles;
        waitable_handles.reserve(waitables.size());
        for (auto &waitable : waitables) {
#if defined(__linux__)
            waitable_handles.emplace_back(pollfd{waitable.get().get_underlying_handle(), POLLIN, 0});
#else
            waitable_handles.emplace_back(waitable.get().get_underlying_handle());
#endif
        }
        return waitable_handles;
    }

    // Initialization dependency
    std::vector<std::reference_wrapper<Waitable>> m_waitables;
    // Store this vector here to avoid runtime allocations.
    std::vector<WaitableHandle> m_waitable_handles;
};

class WaitOrShutdown final
{
public:
    WaitOrShutdown(WaitablePtr waitable, EventPtr shutdown_event);
    ~WaitOrShutdown() = default;

    WaitOrShutdown(const WaitOrShutdown &other) = delete;
    WaitOrShutdown &operator=(const WaitOrShutdown &other) = delete;
    WaitOrShutdown(WaitOrShutdown &&other) noexcept = default;
    WaitOrShutdown &operator=(WaitOrShutdown &&other) = delete;

    // Waits on waitable or shutdown_event to be signaled:
    // * If shutdown_event is signaled:
    //   - shutdown_event is not reset
    //   - HAILO_SHUTDOWN_EVENT_SIGNALED is returned
    // * If waitable is signaled:
    //   - waitable is reset if waitable->is_auto_reset()
    //   - HAILO_SUCCESS is returned
    // * If both waitable and shutdown_event are signaled:
    //   - shutdown_event is not reset
    //   - waitable is not reset
    //   - HAILO_SHUTDOWN_EVENT_SIGNALED is returned
    // * If neither are signaled, then HAILO_TIMEOUT is returned
    // * On any failure an appropriate status shall be returned
    hailo_status wait(std::chrono::milliseconds timeout);
    hailo_status signal();
    hailo_status shutdown();

private:
    static WaitableGroup create_waitable_group(WaitablePtr waitable, EventPtr shutdown_event);

    // Note: We want to guarantee that if the shutdown event is signaled, HAILO_SHUTDOWN_EVENT_SIGNALED will be
    //       returned.
    //       Waitable::wait_any returns the smallest index value of all the signaled objects.
    //       Hence, SHUTDOWN_INDEX must come before WAITABLE_INDEX!
    static const size_t SHUTDOWN_INDEX = 0;
    static const size_t WAITABLE_INDEX = 1;

    const WaitablePtr m_waitable;
    const EventPtr m_shutdown_event;

    WaitableGroup m_waitable_group;
};

} /* namespace hailort */

#endif /* _EVENT_INTERNAL_HPP_ */
