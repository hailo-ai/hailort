/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file usb_event_loop.cpp
 * @brief USB Event Loop Implementation
 **/

#include "hrpc/session_internal/usb/usb_device/usb_event_loop.hpp"
#include "common/logger_macros.hpp"
#include "byte_order.h"
#include "common/utils.hpp"
#include "hailo/hailort.h"

#include <linux/usb/functionfs.h>

namespace hailort
{

static constexpr auto REBOOT_FLUSH_DELAY = std::chrono::seconds(1);

hailo_status UsbEventLoop::set_event_handler(uint32_t port, std::shared_ptr<UsbEventHandler> listener)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    CHECK(m_listeners.find(port) == m_listeners.end(), HAILO_INVALID_OPERATION, "Handler already exists for port {}", port);
    m_listeners[port] = listener;
    return HAILO_SUCCESS;
}

hailo_status UsbEventLoop::remove_event_handler(uint32_t port)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    CHECK(m_listeners.find(port) != m_listeners.end(), HAILO_INVALID_OPERATION, "Handler does not exist for port {}", port);
    m_listeners.erase(port);
    return HAILO_SUCCESS;
}

std::shared_ptr<UsbEventHandler> UsbEventLoop::find_handler(uint32_t port)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_listeners.find(port) != m_listeners.end()) {
        return m_listeners[port];
    }
    return nullptr;
}

hailo_status UsbEventLoop::read_request(uint8_t request_code_raw, size_t request_length, Buffer &event_buffer)
{
    // Zero-length SETUP OUT transfers have no data stage to drain
    if (request_length > 0) {
        ssize_t ret = ::read(*m_control_ep_fd, event_buffer.data(), request_length);
        CHECK(ret > 0, HAILO_INTERNAL_FAILURE, "EP0 read failed: {} (errno={})", strerror(errno), errno);
    }

    // If a client closed before requesting the response, drop the pending request
    if (m_pending_opcode != UsbControlProtocolOpcode::INVALID) {
        auto status = drop_pending_request();
        CHECK_SUCCESS(status);
    }

    UsbControlProtocolOpcode request_code = static_cast<UsbControlProtocolOpcode>(request_code_raw);
    switch (request_code) {
    case UsbControlProtocolOpcode::CONNECT:
    {
        UsbConnectRequest *request = reinterpret_cast<UsbConnectRequest*>(event_buffer.data());
        auto handler = find_handler(request->port);
        if (!handler) {
            m_response.connect_response.status = static_cast<uint32_t>(HAILO_CONNECTION_REFUSED);
            break;
        }

        auto status = open_interface(request->interface, request->session_group_id, handler);
        if (HAILO_SUCCESS != status) {
            m_response.connect_response.status = static_cast<uint32_t>(status);
            break;
        }

        m_request.connect_request = *request;
        m_response.connect_response.status = static_cast<uint32_t>(HAILO_SUCCESS);
        break;
    }
    case UsbControlProtocolOpcode::CLOSE:
    {
        UsbCloseRequest *request = reinterpret_cast<UsbCloseRequest*>(event_buffer.data());
        auto handler = find_handler(request->port);
        if (!handler) {
            m_response.close_response.status = static_cast<uint32_t>(HAILO_CONNECTION_REFUSED);
            break;
        }

        auto status = close_interface(request->interface, handler);
        if (HAILO_SUCCESS != status) {
            m_response.close_response.status = static_cast<uint32_t>(status);
            break;
        }

        m_request.close_request = *request;
        m_response.close_response.status = static_cast<uint32_t>(HAILO_SUCCESS);
        break;
    }
    case UsbControlProtocolOpcode::SYS_REBOOT:
    {
        LOGGER__INFO("SYS_REBOOT request received (req=0x{:02x}), rebooting after flush delay", request_code_raw);
        std::this_thread::sleep_for(REBOOT_FLUSH_DELAY);
        int reboot_ret = std::system("reboot");
        if (reboot_ret != 0) {
            LOGGER__ERROR("std::system(\"reboot\") returned non-zero status: {}", reboot_ret);
        }
        break;
    }
    default:
        LOGGER__ERROR("Got unknown request: {}, length: {}", request_code_raw, request_length);
        return HAILO_INTERNAL_FAILURE;
    }

    m_pending_opcode = request_code;
    return HAILO_SUCCESS;
}

hailo_status UsbEventLoop::open_interface(usb_interface_t interface, uint32_t session_group_id,
    std::shared_ptr<UsbEventHandler> handler)
{
    CHECK(interface < MAX_USB_INTERFACES, HAILO_INVALID_ARGUMENT, "Invalid interface: {}", interface);

    auto it = m_active_interfaces.find(interface);
    if (it != m_active_interfaces.end()) {
        const uint32_t old_session_group_id = it->second.session_group_id;
        LOGGER__WARNING("Stale connection on interface {} (old session_group_id={}, new={}), "
            "closing all interfaces from old session group", interface, old_session_group_id, session_group_id);
        auto status = close_interfaces_by_session_group_id(old_session_group_id);
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to close stale session group {}: {}", old_session_group_id, status);
        }
    }

    m_active_interfaces[interface] = {session_group_id, handler};

    auto status = handler->handle_connect(interface);
    if (HAILO_SUCCESS != status) {
        m_active_interfaces.erase(interface);
        return status;
    }

    return HAILO_SUCCESS;
}

hailo_status UsbEventLoop::close_interface(usb_interface_t interface, std::shared_ptr<UsbEventHandler> handler)
{
    CHECK(m_active_interfaces.count(interface), HAILO_NOT_FOUND, "Interface {} is not active", interface);
    m_active_interfaces.erase(interface);

    auto status = handler->handle_close(interface);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status UsbEventLoop::close_interfaces_by_session_group_id(uint32_t session_group_id)
{
    // Collect interfaces to close before iterating, since close_interface modifies m_active_interfaces
    std::vector<std::pair<usb_interface_t, std::shared_ptr<UsbEventHandler>>> to_close;
    for (auto &[iface, info] : m_active_interfaces) {
        if (info.session_group_id == session_group_id) {
            to_close.emplace_back(iface, info.handler);
        }
    }

    hailo_status first_failure = HAILO_SUCCESS;
    for (auto &[iface, handler] : to_close) {
        auto status = close_interface(iface, handler);
        if (HAILO_SUCCESS != status) {
            LOGGER__WARNING("Failed to close interface {}: {}", iface, status);
            if (HAILO_SUCCESS == first_failure) {
                first_failure = status;
            }
        }
    }
    return first_failure;
}

hailo_status UsbEventLoop::drop_pending_request()
{
    if (UsbControlProtocolOpcode::CONNECT == m_pending_opcode) {
        m_active_interfaces.erase(m_request.connect_request.interface);
    }

    m_pending_opcode = UsbControlProtocolOpcode::INVALID;
    return HAILO_SUCCESS;
}

hailo_status UsbEventLoop::write_response(uint8_t request_code_raw, size_t request_length)
{
    UsbControlProtocolOpcode request_code = static_cast<UsbControlProtocolOpcode>(request_code_raw);
    CHECK(m_pending_opcode == request_code, HAILO_INVALID_OPERATION,
        "Received response-request for an different operation than the pending one, pending opcode: {}, requested opcode: {}",
        static_cast<uint8_t>(m_pending_opcode), request_code_raw);

    switch (request_code) {
    case UsbControlProtocolOpcode::CONNECT:
    {
        ssize_t ret = ::write(*m_control_ep_fd, &m_response.connect_response, sizeof(m_response.connect_response));
        CHECK(ret > 0, HAILO_INTERNAL_FAILURE, "EP0 write failed: {} (errno={})", strerror(errno), errno);
        break;
    }
    case UsbControlProtocolOpcode::CLOSE:
    {
        ssize_t ret = ::write(*m_control_ep_fd, &m_response.close_response, sizeof(m_response.close_response));
        CHECK(ret > 0, HAILO_INTERNAL_FAILURE, "EP0 write failed: {} (errno={})", strerror(errno), errno);
        break;
    }
    default:
        LOGGER__ERROR("Got unknown request: {}, length: {}", request_code_raw, request_length);
        return HAILO_INTERNAL_FAILURE;
    }

    m_pending_opcode = UsbControlProtocolOpcode::INVALID;
    return HAILO_SUCCESS;
}

hailo_status UsbEventLoop::loop()
{
    usb_functionfs_event event = {};
    TRY(auto event_buffer, Buffer::create(EVENT_BUFFER_SIZE));

    while (::read(*m_control_ep_fd, &event, sizeof(event)) > 0) {
        switch (event.type) {
        case FUNCTIONFS_SETUP:
        {
            auto request_code = event.u.setup.bRequest;
            auto request_length = BYTE_ORDER__le16toh(event.u.setup.wLength);
            bool is_host_reading_response = event.u.setup.bRequestType & USB_DIR_IN;

            hailo_status status = HAILO_UNINITIALIZED;
            if (is_host_reading_response) {
                status = write_response(request_code, request_length);
            } else {
                status = read_request(request_code, request_length, event_buffer);
            }
            CHECK_SUCCESS(status);
            break;
        }
        default:
            LOGGER__DEBUG("Got unhandled USB event: {}", event.type);
            break;
        }
    }

    return HAILO_SUCCESS;
}

} // namespace hailort