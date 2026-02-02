/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file usb_device_side.cpp
 * @brief USB Device Side Session
 **/

#include "hrpc/session_internal/usb/usb_device_side.hpp"

#include <cstring>
#include <mutex>
#include <memory>
#include <fcntl.h> 
#include <errno.h>
#include <chrono>
#include <linux/usb/functionfs.h>

namespace hailort
{
 
static constexpr const char *CONTROL_ENDPOINT_PATH = "/dev/ffs-hailo/ep0";
static constexpr std::chrono::milliseconds EP0_WAIT_TIMEOUT{2000};
static constexpr std::chrono::milliseconds EP0_POLL_INTERVAL{50};

UsbControlListener UsbConnectionContext::m_usb_control_listener;

Expected<std::shared_ptr<ConnectionContext>> UsbConnectionContext::create_server_shared()
{
    auto status = m_usb_control_listener.start_control_listener();
    CHECK_SUCCESS(status, "Failed to start control listener. status: {}", status);

    auto ptr = make_shared_nothrow<UsbConnectionContext>();
    CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::dynamic_pointer_cast<ConnectionContext>(ptr);
}

UsbControlListener::UsbControlListener()
    : m_control_fd(nullptr), m_is_running(false), m_configurator(nullptr), m_event_loop(nullptr)
{
}

static Expected<std::shared_ptr<FileDescriptor>> open_ep0_with_retries()
{
    const auto start = std::chrono::steady_clock::now();
    while (true) {
        int fd = ::open(CONTROL_ENDPOINT_PATH, O_RDWR);
        if (fd >= 0) {
            auto ptr = make_shared_nothrow<FileDescriptor>(fd);
            CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);
            return ptr;
        }

        CHECK(errno != ENOENT, HAILO_DEVICE_NOT_CONNECTED,
            "Failed to open EP0: {} (errno={})", strerror(errno), errno);

        CHECK(std::chrono::steady_clock::now() - start >= EP0_WAIT_TIMEOUT, HAILO_TIMEOUT,
            "Timeout waiting for EP0 to become available: {}", CONTROL_ENDPOINT_PATH);

        std::this_thread::sleep_for(EP0_POLL_INTERVAL);
    }
}

hailo_status UsbControlListener::listen_ep0()
{
    auto status = m_event_loop->loop();
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status UsbControlListener::start_control_listener()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_is_running) {
        return HAILO_SUCCESS;
    }

    TRY(m_control_fd, open_ep0_with_retries());
    m_configurator = make_unique_nothrow<UsbFunctionFsConfiguration>(m_control_fd);
    CHECK_NOT_NULL(m_configurator, HAILO_OUT_OF_HOST_MEMORY);

    m_event_loop = make_unique_nothrow<UsbEventLoop>(m_control_fd);
    CHECK_NOT_NULL(m_event_loop, HAILO_OUT_OF_HOST_MEMORY);

    auto status = m_configurator->write_descriptors_and_strings();
    CHECK_SUCCESS(status, "Failed to write USB descriptors");

    status = m_configurator->enable_udc();
    CHECK_SUCCESS(status, "Failed to enable UDC");

    m_is_running = true;
    m_control_thread = std::thread([this]() {
        auto status = listen_ep0();
        if (status != HAILO_SUCCESS) {
            LOGGER__ERROR("Failed to listen to EP0: {}", status);
            m_is_running = false;
        }
    });
    return HAILO_SUCCESS;
}

hailo_status UsbControlListener::stop_control_listener()
{
    m_is_running = false;

    if (m_configurator) {
        auto status = m_configurator->disable_udc(); 
        if (status != HAILO_SUCCESS) {
            LOGGER__ERROR("Failed to disable UDC: {}", status);
        }
        m_configurator.reset();
    }

    if (m_control_thread.joinable()) {
        m_control_thread.join();
    }
    
    m_control_fd.reset();
    m_event_loop.reset();

    return HAILO_SUCCESS;
}

UsbControlListener::~UsbControlListener()
{
    auto status = stop_control_listener();
    if (status != HAILO_SUCCESS) {
        LOGGER__ERROR("Failed to stop control listener. status: {}", status);
    }
}

hailo_status UsbControlListener::set_event_handler(uint32_t port, std::shared_ptr<UsbEventHandler> handler)
{
    return m_event_loop->set_event_handler(port, handler);
}

hailo_status UsbControlListener::remove_event_handler(uint32_t port)
{
    return m_event_loop->remove_event_handler(port);
}

UsbSessionDeviceSide::UsbSessionDeviceSide(uint16_t port, usb_interface_t interface)
    : UsbSession(port, interface)
{
}

UsbSessionDeviceSide::~UsbSessionDeviceSide()
{
    close();
}

hailo_status UsbSessionDeviceSide::close()
{
    // TODO: currently UsbSessionDeviceSide is not getting destructed because the server is able to read from it without getting an abort error
    return HAILO_SUCCESS;
}

hailo_status UsbSessionsMap::add_session(std::shared_ptr<UsbSession> session)
{
    CHECK(m_sessions.find(session->interface()) == m_sessions.end(), HAILO_INVALID_OPERATION,
        "Session for interface {} already exists", session->interface());
    m_sessions.emplace(session->interface(), session);
    return HAILO_SUCCESS;
}
    
hailo_status UsbSessionsMap::close_session(usb_interface_t interface)
{
    auto session = m_sessions.at(interface);
    auto status = session->close();
    CHECK_SUCCESS(status);

    m_sessions.erase(interface);
    return HAILO_SUCCESS;
}

hailo_status UsbEventHandlerImpl::handle_connect(usb_interface_t interface)
{
    auto session = make_shared_nothrow<UsbSessionDeviceSide>(m_port, interface);
    CHECK_NOT_NULL(session, HAILO_OUT_OF_HOST_MEMORY);

    auto status = m_sessions_map->add_session(session);
    CHECK_SUCCESS(status, "Failed to add session for interface {}", interface);

    status = m_connections_queue->enqueue(session);
    CHECK_SUCCESS(status, "Failed to enqueue interface {}: {}", interface);

    return HAILO_SUCCESS;
}

hailo_status UsbEventHandlerImpl::handle_close(usb_interface_t interface)
{
    auto status = m_sessions_map->close_session(interface);
    CHECK_SUCCESS(status, "Failed to close session for interface {}: {}", interface);

    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<UsbListener>> UsbListener::create_shared(std::shared_ptr<UsbConnectionContext> context, uint16_t port)
{
    TRY(auto shutdown_event, Event::create_shared(Event::State::not_signalled));
    auto connections_queue = SpscQueue<std::shared_ptr<UsbSession>>::create_shared(MAX_USB_INTERFACES, shutdown_event);
    CHECK_NOT_NULL(connections_queue, HAILO_OUT_OF_HOST_MEMORY);

    auto ptr = make_shared_nothrow<UsbListener>(context, port, connections_queue, shutdown_event);
    CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);

    auto sessions_map = make_shared_nothrow<UsbSessionsMap>();
    CHECK_NOT_NULL(sessions_map, HAILO_OUT_OF_HOST_MEMORY);

    auto event_handler = make_shared_nothrow<UsbEventHandlerImpl>(port, connections_queue, sessions_map);
    CHECK_NOT_NULL(event_handler, HAILO_OUT_OF_HOST_MEMORY);

    auto status = context->usb_control_listener().set_event_handler(port, event_handler);
    CHECK_SUCCESS(status);

    return ptr;
}

Expected<std::shared_ptr<Session>> UsbListener::accept()
{
    TRY_WITH_ACCEPTABLE_STATUS(HAILO_SHUTDOWN_EVENT_SIGNALED,
        auto session, m_connections_queue->dequeue(std::chrono::milliseconds(HAILO_INFINITE)));
    return std::dynamic_pointer_cast<Session>(session);
}

UsbListener::~UsbListener()
{
    auto status = m_shutdown_event->signal();
    if (status != HAILO_SUCCESS) {
        LOGGER__ERROR("Failed to signal shutdown event. status: {}", status);
    }

    status = m_context->usb_control_listener().remove_event_handler(m_port);
    if (status != HAILO_SUCCESS) {
        LOGGER__ERROR("Failed to remove event handler for port {}: {}", m_port, status);
    }
}

} // namespace hailort