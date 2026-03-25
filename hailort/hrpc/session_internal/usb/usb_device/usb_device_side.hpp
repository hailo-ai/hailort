/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file usb_device_side.hpp
 * @brief USB Device Side Header for USB device side communication
 **/

#ifndef _USB_DEVICE_SIDE_HPP_
#define _USB_DEVICE_SIDE_HPP_

#include "hrpc/session_internal/usb/usb_session.hpp"
#include "hrpc/session_internal/usb/usb_device/usb_control_listener.hpp"
#include "hrpc/session_internal/usb/usb_device/aio_context.hpp"
#include "hrpc/session_internal/async_actions_thread.hpp"
  
namespace hailort
{

class UsbSessionsMap final
{
public:
    UsbSessionsMap() = default;
    ~UsbSessionsMap() = default;

    hailo_status add_session(std::shared_ptr<UsbSession> session);
    hailo_status close_session(usb_interface_t interface);

private:
    std::unordered_map<usb_interface_t, std::shared_ptr<UsbSession>> m_sessions;
};

class UsbEventHandlerImpl : public UsbEventHandler
{
public:
    UsbEventHandlerImpl(uint16_t port,
        std::shared_ptr<SpscQueue<std::shared_ptr<UsbSession>>> connections_queue,
        std::shared_ptr<UsbSessionsMap> sessions_map) :
            m_port(port), m_connections_queue(connections_queue), m_sessions_map(sessions_map) {}
    virtual ~UsbEventHandlerImpl() = default;

    virtual hailo_status handle_connect(usb_interface_t interface) override;
    virtual hailo_status handle_close(usb_interface_t interface) override;

private:
    const uint16_t m_port;
    std::shared_ptr<SpscQueue<std::shared_ptr<UsbSession>>> m_connections_queue;
    std::shared_ptr<UsbSessionsMap> m_sessions_map;
};

class UsbSessionDeviceSide : public UsbSession
{
public:
    static Expected<std::shared_ptr<UsbSessionDeviceSide>> create(uint16_t port, usb_interface_t interface);
    UsbSessionDeviceSide(uint16_t port, usb_interface_t interface);
    virtual hailo_status close() override;
    virtual hailo_status wait_for_write_async_ready(size_t transfer_size, std::chrono::milliseconds timeout) override;
    virtual hailo_status write_async(TransferRequest &&request) override;
    virtual hailo_status wait_for_read_async_ready(size_t transfer_size, std::chrono::milliseconds timeout) override;
    virtual hailo_status read_async(TransferRequest &&request) override;
    virtual ~UsbSessionDeviceSide();

private:
    hailo_status init_io_threads();
    hailo_status abort_transfers();
    hailo_status open_fds(usb_interface_t interface);
    hailo_status read_async_impl(uint8_t *data, size_t size);
    hailo_status write_async_impl(const uint8_t *data, size_t size);

    std::shared_ptr<FileDescriptor> m_receive_fd;
    std::shared_ptr<FileDescriptor> m_send_fd;
    std::shared_ptr<AsyncActionsThread> m_write_actions_thread;
    std::shared_ptr<AsyncActionsThread> m_read_actions_thread;
    std::unique_ptr<AioContext> m_read_aio_context;
    std::unique_ptr<AioContext> m_write_aio_context;
    std::atomic_bool m_is_closed;
};

} // namespace hailort

#endif // _USB_DEVICE_SIDE_HPP_
