/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file libusb_event_thread.cpp
 * @brief Background thread that polls libusb for async transfer completions
 **/

#include "hrpc/session_internal/usb/usb_host/libusb_event_thread.hpp"
#include "common/logger_macros.hpp"
#include "common/utils.hpp"

#include <libusb-1.0/libusb.h>
#include <chrono>

namespace hailort
{

static constexpr suseconds_t USB_EVENT_LOOP_TIMEOUT_US = 100 * 1000; // 100ms

std::mutex LibusbEventThread::m_mutex;
std::weak_ptr<LibusbEventThread> LibusbEventThread::m_ptr;

Expected<std::shared_ptr<LibusbEventThread>> LibusbEventThread::create_shared(libusb_context *ctx)
{
    CHECK_NOT_NULL(ctx, HAILO_INVALID_ARGUMENT);

    std::lock_guard<std::mutex> lock(m_mutex);
    auto ptr = m_ptr.lock();
    if (ptr) {
        CHECK(ptr->m_context == ctx, HAILO_INVALID_OPERATION,
            "LibusbEventThread already running with a different libusb_context");
        return Expected<std::shared_ptr<LibusbEventThread>>(ptr);
    }

    ptr = std::shared_ptr<LibusbEventThread>(new (std::nothrow) LibusbEventThread(ctx));
    CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);
    ptr->start();
    m_ptr = ptr;

    return Expected<std::shared_ptr<LibusbEventThread>>(ptr);
}

LibusbEventThread::LibusbEventThread(libusb_context *ctx) : m_is_running(true), m_context(ctx)
{}

void LibusbEventThread::start()
{
    m_thread = std::thread([this]() { run(m_context); });
}

LibusbEventThread::~LibusbEventThread()
{
    // Sanity check: by the time the destructor runs, no shared_ptr should reference this object.
    const auto remaining = m_ptr.use_count();
    if (0 != remaining) {
        LOGGER__CRITICAL("LibusbEventThread destroyed with {} remaining shared_ptr reference(s)", remaining);
    }

    m_is_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void LibusbEventThread::run(libusb_context *context)
{
    auto last_error_log = std::chrono::steady_clock::time_point{};
    while (m_is_running.load()) {
        struct timeval timeout = {};
        timeout.tv_sec = 0;
        timeout.tv_usec = USB_EVENT_LOOP_TIMEOUT_US;

        // The 'completed' argument is intended for the libusb external-event-handling
        // pattern (libusb_lock_events / libusb_unlock_events).  We use the simple
        // polling pattern, so we always pass 0 (not completed).
        int completed = 0;
        const int rc = libusb_handle_events_timeout_completed(context, &timeout, &completed);
        if ((0 == rc) || (LIBUSB_ERROR_INTERRUPTED == rc)) {
            continue;
        }

        const auto now = std::chrono::steady_clock::now();
        if ((last_error_log.time_since_epoch().count() == 0) ||
            ((now - last_error_log) >= std::chrono::seconds(1))) {
            LOGGER__ERROR("libusb event thread failed: {}", libusb_error_name(rc));
            last_error_log = now;
        }
    }
}

} // namespace hailort
