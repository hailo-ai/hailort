/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file usb_control_listener.hpp
 * @brief USB Control Listener Header for USB control communication
 **/

#ifndef _USB_CONTROL_LISTENER_HPP_
#define _USB_CONTROL_LISTENER_HPP_

#include "hailort_server/usb/usb_functionfs_configure.hpp"
#include "hrpc/session_internal/usb/usb_device/usb_event_loop.hpp"
  
namespace hailort
{

class UsbControlListener final
{
public:
    UsbControlListener();
    ~UsbControlListener();

    hailo_status set_event_handler(uint32_t port, std::shared_ptr<UsbEventHandler> handler);
    hailo_status remove_event_handler(uint32_t port);
    hailo_status start_control_listener();

    static std::string get_receive_fd_path(usb_interface_t interface);
    static std::string get_send_fd_path(usb_interface_t interface);

private:
    hailo_status listen_ep0();
    hailo_status stop_control_listener();

    std::thread m_control_thread;
    std::shared_ptr<FileDescriptor> m_control_fd;
    bool m_is_running;
    std::mutex m_mutex;
    std::unique_ptr<UsbFunctionFsConfiguration> m_ffs_config;
    std::unique_ptr<UsbEventLoop> m_event_loop;
};

} // namespace hailort

#endif // _USB_CONTROL_LISTENER_HPP_
