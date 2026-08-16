/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file usb_event_loop.hpp
 * @brief USB Event Loop Header
 **/

#ifndef _USB_EVENT_LOOP_HPP_
#define _USB_EVENT_LOOP_HPP_

#include "hailo/hailort.h"
#include "hailo/buffer.hpp"
#include "hailort_server/usb/usb_functionfs_configure.hpp"
#include "hrpc/session_internal/usb/usb_control_protocol.hpp"

#include <mutex>
#include <unordered_map>

namespace hailort
{

class UsbEventHandler
{
public:
    virtual ~UsbEventHandler() = default;
    virtual hailo_status handle_connect(usb_interface_t interface) = 0;
    virtual hailo_status handle_close(usb_interface_t interface) = 0;
};

class UsbEventLoop
{
public:
    UsbEventLoop(std::shared_ptr<FileDescriptor> control_ep_fd) : m_control_ep_fd(control_ep_fd), m_pending_opcode(UsbControlProtocolOpcode::INVALID)
    {}
    virtual ~UsbEventLoop() = default;

    hailo_status loop();
    hailo_status set_event_handler(uint32_t port, std::shared_ptr<UsbEventHandler> handler);
    hailo_status remove_event_handler(uint32_t port);

private:
    static constexpr size_t EVENT_BUFFER_SIZE = 64;

    hailo_status read_request(uint8_t request_code, size_t request_length, Buffer &event_buffer);
    hailo_status write_response(uint8_t request_code, size_t request_length);

    std::shared_ptr<UsbEventHandler> find_handler(uint32_t port);

    // These functions are not thread safe and should be called only from the event loop thread
    hailo_status open_interface(usb_interface_t interface, uint32_t session_group_id,
        std::shared_ptr<UsbEventHandler> handler);
    hailo_status close_interface(usb_interface_t interface, std::shared_ptr<UsbEventHandler> handler);
    hailo_status close_interfaces_by_session_group_id(uint32_t session_group_id);
    hailo_status drop_pending_request();

    std::shared_ptr<FileDescriptor> m_control_ep_fd;
    UsbControlProtocolOpcode m_pending_opcode;
    union {
        UsbConnectRequest connect_request;
        UsbCloseRequest close_request;
    } m_request;
    union {
        UsbConnectResponse connect_response;
        UsbCloseResponse close_response;
    } m_response;
    struct InterfaceInfo {
        uint32_t session_group_id;
        std::shared_ptr<UsbEventHandler> handler;
    };

    std::unordered_map<usb_interface_t, InterfaceInfo> m_active_interfaces;
    std::unordered_map<uint32_t, std::shared_ptr<UsbEventHandler>> m_listeners;
    std::mutex m_mutex;
};

} // namespace hailort

#endif // _USB_EVENT_LOOP_HPP_