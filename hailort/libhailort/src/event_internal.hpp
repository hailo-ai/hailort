/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file event.hpp
 * @brief Event and Semaphore wrapper objects used for multithreading
 **/

#ifndef _EVENT_INTERNAL_HPP_
#define _EVENT_INTERNAL_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"

#include <memory>
#include <vector>
#include <array>
#include <chrono>
#if defined(__GNUC__)
#include <poll.h>
#endif

namespace hailort
{

// TODO: Replace with a static wait_multiple func belonging to Waitable (SDK-16567).
//       Will get a vector of pointers as an argument. Can also use variadic
//       template args for cases with fixed number Waitables
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

private:
    // Note: We want to guarantee that if the shutdown event is signaled, HAILO_SHUTDOWN_EVENT_SIGNALED will be
    //       returned.
    //       * In Unix, using poll this isn't a problem since we'll get all the readable fds in a single call.
    //       * In Windows, using WaitForMultipleObjects, this works differently (from msdn):
    //         If bWaitAll is FALSE, the return value minus WAIT_OBJECT_0 indicates the lpHandles array index
    //         of the object that satisfied the wait. If more than one object became signaled during the call,
    //         this is the array index of the signaled object with the smallest index value of all the signaled
    //         objects.
    //         (https://docs.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitformultipleobjects)
    //       * Hence, SHUTDOWN_INDEX must come before WAITABLE_INDEX!
    static const size_t SHUTDOWN_INDEX = 0;
    static const size_t WAITABLE_INDEX = 1;
    #if defined(_MSC_VER) || defined(__QNX__)
    using WaitHandleArray = std::array<underlying_waitable_handle_t, 2>;
    #else
    using WaitHandleArray = std::array<struct pollfd, 2>;
    #endif

    const WaitablePtr m_waitable;
    const EventPtr m_shutdown_event;
    WaitHandleArray m_wait_handle_array;

    static WaitHandleArray create_wait_handle_array(WaitablePtr waitable, EventPtr shutdown_event);
};

} /* namespace hailort */

#endif /* _EVENT_INTERNAL_HPP_ */
