/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file event.cpp
 * @brief Event & Semaphore wrapper for QNX
 *
 * TODO: doc
 **/
#include "hailo/event.hpp"
#include "hailo/hailort.h"
#include "common/utils.hpp"
#include "event_internal.hpp"

#include <poll.h>
#include <utility>

#define INVALID_FD (-1)

namespace hailort
{

Waitable::Waitable(underlying_handle_t handle) :
    m_handle(handle)
{}

Waitable::~Waitable()
{
    if (-1 != m_handle) {
        (void) close(m_handle);
    }
}

Waitable::Waitable(Waitable&& other) :
    m_handle(std::exchange(other.m_handle, INVALID_FD))
{}

underlying_handle_t Waitable::get_underlying_handle()
{
    return m_handle;
}

hailo_status Waitable::eventfd_poll(underlying_handle_t fd, std::chrono::milliseconds timeout)
{
    (void) fd;
    (void) timeout;
    return HAILO_NOT_IMPLEMENTED;
}

hailo_status Waitable::eventfd_read(underlying_handle_t fd)
{
    (void) fd;
    return HAILO_NOT_IMPLEMENTED;
}

hailo_status Waitable::eventfd_write(underlying_handle_t fd)
{
    (void) fd;
    return HAILO_NOT_IMPLEMENTED;
}

Expected<Event> Event::create(const State& initial_state)
{
    (void) initial_state;
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

EventPtr Event::create_shared(const State& initial_state)
{
    (void) initial_state;
    return make_shared_nothrow<Event>(INVALID_FD);
}

hailo_status Event::wait(std::chrono::milliseconds timeout)
{
    (void) timeout;
    return HAILO_NOT_IMPLEMENTED;
}

hailo_status Event::signal()
{
    return HAILO_NOT_IMPLEMENTED;
}

bool Event::is_auto_reset()
{
    return false;
}

hailo_status Event::reset()
{
    return HAILO_NOT_IMPLEMENTED;
}

underlying_handle_t Event::open_event_handle(const State& initial_state)
{
    (void) initial_state;
    return INVALID_FD;
}

Expected<Semaphore> Semaphore::create(uint32_t initial_count)
{
    (void) initial_count;
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

SemaphorePtr Semaphore::create_shared(uint32_t initial_count)
{
    (void) initial_count;
    return make_shared_nothrow<Semaphore>(INVALID_FD);
}

hailo_status Semaphore::wait(std::chrono::milliseconds timeout)
{
    (void) timeout;
    return HAILO_NOT_IMPLEMENTED;
}

hailo_status Semaphore::signal()
{
    return HAILO_NOT_IMPLEMENTED;
}

bool Semaphore::is_auto_reset()
{
    return true;
}

underlying_handle_t Semaphore::open_semaphore_handle(uint32_t initial_count)
{
    (void) initial_count;
    return INVALID_FD;
}

WaitOrShutdown::WaitOrShutdown(WaitablePtr waitable, EventPtr shutdown_event)
{
    (void) waitable;
    (void) shutdown_event;
}

hailo_status WaitOrShutdown::wait(std::chrono::milliseconds timeout)
{
    (void) timeout;
    return HAILO_NOT_IMPLEMENTED;
}

hailo_status WaitOrShutdown::signal()
{
    return HAILO_NOT_IMPLEMENTED;
}

WaitOrShutdown::WaitHandleArray WaitOrShutdown::create_wait_handle_array(WaitablePtr waitable, EventPtr shutdown_event)
{
    (void) waitable;
    (void) shutdown_event;
    // Note the order!
    WaitHandleArray pfds{{
        {INVALID_FD, POLLIN, 0},
        {INVALID_FD, POLLIN, 0}
    }};
    return pfds;
}

} /* namespace hailort */
