/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file event.cpp
 * @brief Event & Semaphore wrapper for Unix
 **/

#include "hailo/hailort.h"
#include "hailo/event.hpp"

#include "common/utils.hpp"
#include "common/event_internal.hpp"
#include "common/internal_env_vars.hpp"

#include <sys/eventfd.h>
#include <fcntl.h>
#include <poll.h>
#include <utility>


namespace hailort
{

#define HIGH_FD_OFFSET (1024)

bool should_use_high_fd()
{
    return is_env_variable_on(HAILO_USE_HIGH_FD_ENV_VAR);
}

int move_fd_to_higher(int handle)
{
    int new_handle = fcntl(handle, F_DUPFD_CLOEXEC, HIGH_FD_OFFSET);
    if (-1 == new_handle) {
        LOGGER__ERROR("failed to duplicate event FD. errno={}", errno);
    }
    close(handle);
    return new_handle;
}


Waitable::Waitable(underlying_waitable_handle_t handle) :
    m_handle(handle)
{}

hailo_status Waitable::wait(std::chrono::milliseconds timeout)
{
    auto status = wait_for_single_object(m_handle, timeout);
    if (HAILO_TIMEOUT == status) {
        LOGGER__TRACE("wait_for_single_object failed with timeout (timeout={}ms)", timeout.count());
        return status;
    }
    CHECK_SUCCESS(status);

    status = post_wait();
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

underlying_waitable_handle_t Waitable::get_underlying_handle()
{
    return m_handle;
}

Waitable::~Waitable()
{
    if (-1 != m_handle) {
        (void) close(m_handle);
    }
}

Waitable::Waitable(Waitable&& other) :
    m_handle(std::exchange(other.m_handle, -1))
{}

hailo_status Waitable::wait_for_single_object(underlying_waitable_handle_t handle, std::chrono::milliseconds timeout)
{
    return eventfd_poll(handle, timeout);
}

hailo_status Waitable::eventfd_poll(underlying_waitable_handle_t fd, std::chrono::milliseconds timeout)
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

hailo_status Waitable::eventfd_read(underlying_waitable_handle_t fd)
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

hailo_status Waitable::eventfd_write(underlying_waitable_handle_t fd)
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
        return make_unexpected(HAILO_EVENT_CREATE_FAIL);
    }
    return Event(handle);
}

Expected<EventPtr> Event::create_shared(const State& initial_state)
{
    const auto handle = open_event_handle(initial_state);
    CHECK_AS_EXPECTED(-1 != handle, HAILO_EVENT_CREATE_FAIL);

    auto res = make_shared_nothrow<Event>(handle);
    CHECK_NOT_NULL_AS_EXPECTED(res, HAILO_OUT_OF_HOST_MEMORY);

    return res;
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

underlying_waitable_handle_t Event::open_event_handle(const State& initial_state)
{
    static const int NO_FLAGS = 0;
    const int state = initial_state == State::signalled ? 1 : 0;
    const auto handle = eventfd(state, NO_FLAGS);
    if (-1 == handle) {
        LOGGER__ERROR("Call to eventfd failed with errno={}", errno);
        return handle;
    }

    if (should_use_high_fd()) {
        return move_fd_to_higher(handle);
    }

    return handle;
}

Expected<Semaphore> Semaphore::create(uint32_t initial_count)
{
    const auto handle = open_semaphore_handle(initial_count);
    if (-1 == handle) {
        return make_unexpected(HAILO_EVENT_CREATE_FAIL);
    }
    return Semaphore(handle);
}

Expected<SemaphorePtr> Semaphore::create_shared(uint32_t initial_count)
{
    const auto handle = open_semaphore_handle(initial_count);
    CHECK_AS_EXPECTED(-1 != handle, HAILO_EVENT_CREATE_FAIL);

    auto res = make_shared_nothrow<Semaphore>(handle);
    CHECK_NOT_NULL_AS_EXPECTED(res, HAILO_OUT_OF_HOST_MEMORY);

    return res;
}

hailo_status Semaphore::signal()
{
    return eventfd_write(m_handle);
}

bool Semaphore::is_auto_reset()
{
    return true;
}

hailo_status Semaphore::post_wait()
{
    return eventfd_read(m_handle);
}

underlying_waitable_handle_t Semaphore::open_semaphore_handle(uint32_t initial_count)
{
    static const int SEMAPHORE = EFD_SEMAPHORE;
    const auto handle = eventfd(initial_count, SEMAPHORE);
    if (-1 == handle) {
        LOGGER__ERROR("Call to eventfd failed with errno={}", errno);
        return handle;
    }

    if (should_use_high_fd()) {
        return move_fd_to_higher(handle);
    }

    return handle;
}

} /* namespace hailort */
