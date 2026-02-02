/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file usb_device_side.hpp
 * @brief USB Device Side Header for USB device side communication
 **/

#ifndef _USB_DEVICE_SIDE_HPP_
#define _USB_DEVICE_SIDE_HPP_

#include "hrpc/session_internal/usb/usb_session.hpp"
#include "hailort_server/usb/usb_functionfs_configure.hpp"
#include "hrpc/session_internal/usb/usb_event_loop.hpp"
  
namespace hailort
{

class UsbControlListener
{
public:
    UsbControlListener();
    UsbControlListener(const UsbControlListener&) = default;
    UsbControlListener &operator=(const UsbControlListener&) = default;
    ~UsbControlListener();

    hailo_status set_event_handler(uint32_t port, std::shared_ptr<UsbEventHandler> handler);
    hailo_status remove_event_handler(uint32_t port);
    hailo_status start_control_listener();

private:
    hailo_status listen_ep0();
    hailo_status stop_control_listener();

    std::thread m_control_thread;
    std::shared_ptr<FileDescriptor> m_control_fd;
    bool m_is_running;
    std::mutex m_mutex;
    std::unique_ptr<UsbFunctionFsConfiguration> m_configurator;
    std::unique_ptr<UsbEventLoop> m_event_loop;
};

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
    UsbSessionDeviceSide(uint16_t port, usb_interface_t interface);
    virtual ~UsbSessionDeviceSide();

    virtual hailo_status close() override;
};

} // namespace hailort

#endif // _USB_DEVICE_SIDE_HPP_
