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
    ssize_t ret = ::read(*m_control_ep_fd, event_buffer.data(), request_length);
    CHECK(ret > 0, HAILO_INTERNAL_FAILURE, "EP0 read failed: {} (errno={})", strerror(errno), errno);

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

        auto status = open_interface(request->interface, handler);
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
    default:
        LOGGER__ERROR("Got unknown request: {}, length: {}", request_code_raw, request_length);
        return HAILO_INTERNAL_FAILURE;
    }

    m_pending_opcode = request_code;
    return HAILO_SUCCESS;
}

hailo_status UsbEventLoop::open_interface(usb_interface_t interface, std::shared_ptr<UsbEventHandler> handler)
{
    CHECK(interface < MAX_USB_INTERFACES, HAILO_INVALID_ARGUMENT, "Invalid interface: {}", interface);

    if (!contains(m_available_interfaces, interface)) {
        auto status = close_interface(interface, handler);
        CHECK_SUCCESS(status);
    }

    m_available_interfaces.erase(interface);

    auto status = handler->handle_connect(interface);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status UsbEventLoop::close_interface_impl(usb_interface_t interface)
{
    CHECK(!contains(m_available_interfaces, interface) && (interface < MAX_USB_INTERFACES), HAILO_NOT_FOUND);
    m_available_interfaces.insert(interface);
    return HAILO_SUCCESS;
}

hailo_status UsbEventLoop::close_interface(usb_interface_t interface, std::shared_ptr<UsbEventHandler> handler)
{
    auto status = close_interface_impl(interface);
    CHECK_SUCCESS(status);

    status = handler->handle_close(interface);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status UsbEventLoop::drop_pending_request()
{
    if (UsbControlProtocolOpcode::CONNECT == m_pending_opcode) {
        auto status = close_interface_impl(m_request.connect_request.interface);
        CHECK_SUCCESS(status);
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