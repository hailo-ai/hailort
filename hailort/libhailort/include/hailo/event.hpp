/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file event.hpp
 * @brief Event and Semaphore wrapper objects used for multithreading
 **/

#ifndef _HAILO_EVENT_HPP_
#define _HAILO_EVENT_HPP_

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

class Waitable;
using WaitablePtr = std::shared_ptr<Waitable>;
using WaitablePtrList = std::vector<WaitablePtr>;

class HAILORTAPI Waitable
{
public:    
    explicit Waitable(underlying_handle_t handle);
    virtual ~Waitable();
    Waitable(Waitable&& other);

    Waitable(const Waitable&) = delete;
    Waitable& operator=(const Waitable&) = delete;
    Waitable& operator=(Waitable&&) = delete;

    // Blocks the current thread until the waitable is signaled
    // * If this->is_auto_reset(), then the Waitable is reset after wait returns with HAILO_SUCCESS 
    // * Otherwise, the Waitable is not reset
    virtual hailo_status wait(std::chrono::milliseconds timeout) = 0;
    virtual hailo_status signal() = 0;
    virtual bool is_auto_reset() = 0;
    underlying_handle_t get_underlying_handle();

    static constexpr auto INIFINITE_TIMEOUT() { return std::chrono::milliseconds(HAILO_INFINITE); }

protected:
    #if defined(_MSC_VER)
    static hailo_status wait_for_single_object(underlying_handle_t handle, std::chrono::milliseconds timeout);
    #else
    // Waits on the fd until the waitable is signaled
    static hailo_status eventfd_poll(underlying_handle_t fd, std::chrono::milliseconds timeout);
    // Expected to be called after eventfd_poll returns HAILO_SUCCESS
    static hailo_status eventfd_read(underlying_handle_t fd);
    static hailo_status eventfd_write(underlying_handle_t fd);
    #endif

    underlying_handle_t m_handle;
};

class Event;
using EventPtr = std::shared_ptr<Event>;
using EventPtrList = std::vector<EventPtr>;

// Manual reset event
class HAILORTAPI Event : public Waitable
{
public:
    enum class State
    {
        signalled,
        not_signalled
    };

    using Waitable::Waitable;

    static Expected<Event> create(const State& initial_state);
    static EventPtr create_shared(const State& initial_state);

    virtual hailo_status wait(std::chrono::milliseconds timeout) override;
    virtual hailo_status signal() override;
    virtual bool is_auto_reset() override;
    hailo_status reset();

private:
    static underlying_handle_t open_event_handle(const State& initial_state);
};

class Semaphore;
using SemaphorePtr = std::shared_ptr<Semaphore>;
using SemaphorePtrList = std::vector<SemaphorePtr>;

class HAILORTAPI Semaphore : public Waitable
{
public:
    using Waitable::Waitable;

    static Expected<Semaphore> create(uint32_t initial_count);
    static SemaphorePtr create_shared(uint32_t initial_count);

    virtual hailo_status wait(std::chrono::milliseconds timeout) override;
    virtual hailo_status signal() override;
    virtual bool is_auto_reset() override;

private:
    static underlying_handle_t open_semaphore_handle(uint32_t initial_count);
};

} /* namespace hailort */

#endif /* _HAILO_EVENT_HPP_ */
