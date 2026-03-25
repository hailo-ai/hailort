/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file usb_session.hpp
 * @brief Hailo Session Header for USB based comunication
 **/

#ifndef _USB_SESSION_HPP_
#define _USB_SESSION_HPP_

#include "hailo/expected.hpp"
#include "hailo/event.hpp"
#include "hailo/hailo_session.hpp"
#include "hrpc/connection_context.hpp"
#include "hrpc/session_internal/usb/usb_control_protocol.hpp"
#include "common/thread_safe_queue.hpp"

#include <mutex>
#include <condition_variable>
#include <chrono>

// Usb transfers allocate a buffer in the kernel, so we limit the size of the buffer to 1MB to avoid allocation errors.
// We limit to 1MB, because the default max size is 16MB (defined in the file /sys/module/usbcore/parameters/usbfs_memory_mb on Linux),
// which is for ALL transfers on the device. Meaning between interfaces and clients.
static constexpr size_t USB_MAX_BUFFER_SIZE = 1024 * 1024; // 1MB

namespace hailort
{

class UsbControlListener;
class UsbConnectionContext : public ConnectionContext
{
public:
    static Expected<std::shared_ptr<ConnectionContext>> create_client_shared(const std::string &device_id);
    static Expected<std::shared_ptr<ConnectionContext>> create_server_shared();

    UsbConnectionContext(hailo_usb_device_info_t usb_info) 
        : ConnectionContext(false), m_usb_info(usb_info) {}
    UsbConnectionContext() 
        : ConnectionContext(true), m_usb_info({}) {}
    virtual ~UsbConnectionContext() = default;

    virtual Device::Type device_type() override { return Device::Type::USB; }
    const hailo_usb_device_info_t &usb_info() const { return m_usb_info; }
    static UsbControlListener &usb_control_listener() { return m_usb_control_listener; }

private:
    const hailo_usb_device_info_t m_usb_info;
    static UsbControlListener m_usb_control_listener;
};

class UsbSession : public Session
{
public:
    static Expected<std::shared_ptr<UsbSession>> connect(std::shared_ptr<UsbConnectionContext> context, uint16_t port);

    virtual ~UsbSession() = default;

    usb_interface_t interface() const { return m_interface; }

    virtual hailo_status write(const uint8_t *buffer, size_t size,
        std::chrono::milliseconds timeout = DEFAULT_WRITE_TIMEOUT) override;
    virtual hailo_status read(uint8_t *buffer, size_t size,
        std::chrono::milliseconds timeout = DEFAULT_READ_TIMEOUT) override;

    virtual Expected<int> read_fd() override;
    virtual Expected<Buffer> allocate_buffer(size_t size, hailo_dma_buffer_direction_t direction) override;

protected:
    UsbSession(uint16_t port, usb_interface_t interface);

    const usb_interface_t m_interface;

private:
    std::mutex m_write_mutex;
    std::condition_variable m_write_cv;
    std::mutex m_read_mutex;
    std::condition_variable m_read_cv;
};

class UsbListener : public SessionListener
{
public:
    static Expected<std::shared_ptr<UsbListener>> create_shared(std::shared_ptr<UsbConnectionContext> context, uint16_t port);

    UsbListener(std::shared_ptr<UsbConnectionContext> context, uint16_t port,
        std::shared_ptr<SpscQueue<std::shared_ptr<UsbSession>>> connections_queue, EventPtr shutdown_event)
        : SessionListener(port), m_context(context), m_connections_queue(connections_queue), m_shutdown_event(shutdown_event) {}
    virtual ~UsbListener();

    virtual Expected<std::shared_ptr<Session>> accept() override;

private:
    std::shared_ptr<UsbConnectionContext> m_context;
    std::shared_ptr<SpscQueue<std::shared_ptr<UsbSession>>> m_connections_queue;
    EventPtr m_shutdown_event;
};

} // namespace hailort

#endif // _USB_SESSION_HPP_
