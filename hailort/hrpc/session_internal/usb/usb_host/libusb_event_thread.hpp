/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file libusb_event_thread.hpp
 * @brief Background thread that polls libusb for async transfer completions
 **/

#ifndef _LIBUSB_EVENT_THREAD_HPP_
#define _LIBUSB_EVENT_THREAD_HPP_

#include "hailo/expected.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

struct libusb_context;

namespace hailort
{

// Background thread that polls libusb_handle_events to drive async bulk transfer completions.
// Shared via weak_ptr — automatically starts when the first session acquires it and stops when the last session releases it.
class LibusbEventThread final
{
public:
    static Expected<std::shared_ptr<LibusbEventThread>> create_shared(libusb_context *ctx);
    ~LibusbEventThread();

    LibusbEventThread(const LibusbEventThread &other) = delete;
    LibusbEventThread &operator=(const LibusbEventThread &other) = delete;
    LibusbEventThread(LibusbEventThread &&other) = delete;
    LibusbEventThread &operator=(LibusbEventThread &&other) = delete;

private:
    explicit LibusbEventThread(libusb_context *ctx);
    void start();
    void run(libusb_context *context);

    std::thread m_thread;
    std::atomic_bool m_is_running;
    libusb_context *m_context;

    // Static for sharing, weak_ptr to prevent zombie threads on shutdown
    static std::mutex m_mutex;
    static std::weak_ptr<LibusbEventThread> m_ptr;
};

} // namespace hailort

#endif // _LIBUSB_EVENT_THREAD_HPP_
