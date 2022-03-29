/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file event.cpp
 * @brief Event & Semaphore wrapper for Unix
 *
 * TODO: doc
 **/
#include "hailo/event.hpp"
#include "hailo/hailort.h"
#include "common/utils.hpp"
#include "event_internal.hpp"

#include <sys/eventfd.h>
#include <poll.h>
#include <utility>

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
    m_handle(std::exchange(other.m_handle, -1))
{}

underlying_handle_t Waitable::get_underlying_handle()
{
    return m_handle;
}

hailo_status Waitable::eventfd_poll(underlying_handle_t fd, std::chrono::milliseconds timeout)
{
    hailo_status status = HAILO_UNINITIALIZED;
    struct pollfd pfd{};
    int poll_ret = -1;

    assert(-1 != fd);

    if (UINT32_MAX < timeout.count()) {
        status = HAILO_INVALID_ARGUMENT;
        LOGGER__ERROR("Invalid timeout value: {}", timeout.count());
        goto l_exit;
    }
    if (INT_MAX < timeout.count()) {
        timeout = std::chrono::milliseconds(INT_MAX);
    }

    pfd.fd = fd;
    pfd.events = POLLIN;
    do {
        poll_ret = poll(&pfd, 1, static_cast<int>(timeout.count()));
    } while ((0 > poll_ret) && (EINTR == poll_ret));

    if (0 == poll_ret) {
        LOGGER__TRACE("Timeout");
        status = HAILO_TIMEOUT;
        goto l_exit;
    }
    if (0 > poll_ret) {
        LOGGER__ERROR("poll failed with errno={}", errno);
        status = HAILO_INTERNAL_FAILURE;
        goto l_exit;
    }
    if (0 == (pfd.revents & POLLIN)) {
        LOGGER__ERROR("pfd not in read state. revents={}", pfd.revents);
        status = HAILO_INTERNAL_FAILURE;
        goto l_exit;
    }

    status = HAILO_SUCCESS;
l_exit:
    return status;
}

hailo_status Waitable::eventfd_read(underlying_handle_t fd)
{
    hailo_status status = HAILO_UNINITIALIZED;
    ssize_t read_ret = -1;
    uint64_t dummy;

    assert(-1 != fd);

    read_ret = read(fd, &dummy, sizeof(dummy));
    if (sizeof(dummy) != read_ret) {
        LOGGER__ERROR("read failed. bytes_read={}, expected={}, errno={}", read_ret, sizeof(dummy), errno);
        status = HAILO_INTERNAL_FAILURE;
        goto l_exit;    
    }

    status = HAILO_SUCCESS;
l_exit:
    return status;
}

hailo_status Waitable::eventfd_write(underlying_handle_t fd)
{
    hailo_status status = HAILO_UNINITIALIZED;
    ssize_t write_ret = -1;
    uint64_t buffer = 1;

    assert(-1 != fd);
    
    write_ret = write(fd, &buffer, sizeof(buffer));
    if (sizeof(buffer) != write_ret) {
        LOGGER__ERROR("write failed. bytes_written={}, expected={}, errno={}", write_ret, sizeof(buffer), errno);
        status = HAILO_INTERNAL_FAILURE;
        goto l_exit;    
    }

    status = HAILO_SUCCESS;
l_exit:
    return status;
}

Expected<Event> Event::create(const State& initial_state)
{
    const auto handle = open_event_handle(initial_state);
    if (-1 == handle) {
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }
    return Event(handle);
}

EventPtr Event::create_shared(const State& initial_state)
{
    const auto handle = open_event_handle(initial_state);
    if (-1 == handle) {
        return nullptr;
    }

    return make_shared_nothrow<Event>(handle);
}

hailo_status Event::wait(std::chrono::milliseconds timeout)
{
    return eventfd_poll(m_handle, timeout);
}

hailo_status Event::signal()
{
    return eventfd_write(m_handle);
}

bool Event::is_auto_reset()
{
    return false;
}

hailo_status Event::reset()
{
    if (HAILO_TIMEOUT == wait(std::chrono::seconds(0))) {
        // Event is not set nothing to do, otherwise `eventfd_read` would block forever
        return HAILO_SUCCESS;
    }
    return eventfd_read(m_handle);
}

underlying_handle_t Event::open_event_handle(const State& initial_state)
{
    static const int NO_FLAGS = 0;
    const int state = initial_state == State::signalled ? 1 : 0;
    const auto handle = eventfd(state, NO_FLAGS);
    if (-1 == handle) {
        LOGGER__ERROR("Call to eventfd failed with errno={}", errno);
    }
    return handle;
}

Expected<Semaphore> Semaphore::create(uint32_t initial_count)
{
    const auto handle = open_semaphore_handle(initial_count);
    if (-1 == handle) {
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }
    return Semaphore(handle);
}

SemaphorePtr Semaphore::create_shared(uint32_t initial_count)
{
    const auto handle = open_semaphore_handle(initial_count);
    if (-1 == handle) {
        return nullptr;
    }

    return make_shared_nothrow<Semaphore>(handle);
}

hailo_status Semaphore::wait(std::chrono::milliseconds timeout)
{
    // TODO: See SDK-16568 (might be necessary in the future)
    hailo_status status = eventfd_poll(m_handle, timeout);
    if (HAILO_TIMEOUT == status) {
        LOGGER__INFO("eventfd_poll failed, status = {}", status);
        return status;
    }
    CHECK_SUCCESS(status);

    status = eventfd_read(m_handle);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Semaphore::signal()
{
    return eventfd_write(m_handle);
}

bool Semaphore::is_auto_reset()
{
    return true;
}

underlying_handle_t Semaphore::open_semaphore_handle(uint32_t initial_count)
{
    static const int SEMAPHORE = EFD_SEMAPHORE;
    const auto handle = eventfd(initial_count, SEMAPHORE);
    if (-1 == handle) {
        LOGGER__ERROR("Call to eventfd failed with errno={}", errno);
    }
    return handle;
}

WaitOrShutdown::WaitOrShutdown(WaitablePtr waitable, EventPtr shutdown_event) :
    m_waitable(waitable),
    m_shutdown_event(shutdown_event),
    m_wait_handle_array(create_wait_handle_array(waitable, shutdown_event))
{}

hailo_status WaitOrShutdown::wait(std::chrono::milliseconds timeout)
{
    int poll_ret = -1;
    do {
        poll_ret = poll(m_wait_handle_array.data(), m_wait_handle_array.size(), static_cast<int>(timeout.count()));
    } while ((0 > poll_ret) && (EINTR == poll_ret));

    if (0 == poll_ret) {
        LOGGER__TRACE("Timeout");
        return HAILO_TIMEOUT;
    }
    if (0 > poll_ret) {
        LOGGER__ERROR("poll failed with errno={}", errno);
        return HAILO_INTERNAL_FAILURE;
    }
    if ((0 == (m_wait_handle_array[WAITABLE_INDEX].revents & POLLIN)) &&
        (0 == (m_wait_handle_array[SHUTDOWN_INDEX].revents & POLLIN))) {
        LOGGER__ERROR("Both pfds not in read state: waitable.revents={}, shutdown.revents={}",
            m_wait_handle_array[WAITABLE_INDEX].revents, m_wait_handle_array[SHUTDOWN_INDEX].revents);
        return HAILO_INTERNAL_FAILURE;
    }

    if (m_wait_handle_array[SHUTDOWN_INDEX].revents & POLLIN) {
        return HAILO_SHUTDOWN_EVENT_SIGNALED;
    }

    if (m_waitable->is_auto_reset() && (m_wait_handle_array[WAITABLE_INDEX].revents & POLLIN)) {
        uint64_t dummy;
        ssize_t read_ret = read(m_wait_handle_array[WAITABLE_INDEX].fd, &dummy, sizeof(dummy));
        if (sizeof(dummy) != read_ret) {
            LOGGER__ERROR("read failed. bytes_read={}, expected={}, errno={}", read_ret, sizeof(dummy), errno);
            return HAILO_INTERNAL_FAILURE;
        }
    }

    return HAILO_SUCCESS;
}

hailo_status WaitOrShutdown::signal()
{
    return m_waitable->signal();
}

WaitOrShutdown::WaitHandleArray WaitOrShutdown::create_wait_handle_array(WaitablePtr waitable, EventPtr shutdown_event)
{
    // Note the order!
    WaitHandleArray pfds{{
        {shutdown_event->get_underlying_handle(), POLLIN, 0},
        {waitable->get_underlying_handle(), POLLIN, 0}
    }};
    return pfds;
}

} /* namespace hailort */
