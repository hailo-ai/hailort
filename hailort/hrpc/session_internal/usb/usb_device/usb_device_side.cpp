/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file usb_device_side.cpp
 * @brief USB Device Side Session
 **/

#include "hrpc/session_internal/usb/usb_device/usb_device_side.hpp"
#include "common/utils.hpp"
#include "vdma/transfer_common.hpp"

#include <fcntl.h> 

namespace hailort
{

UsbControlListener UsbConnectionContext::m_usb_control_listener;
Expected<std::shared_ptr<ConnectionContext>> UsbConnectionContext::create_server_shared()
{
    auto status = m_usb_control_listener.start_control_listener();
    CHECK_SUCCESS(status, "Failed to start control listener. status: {}", status);

    auto ptr = make_shared_nothrow<UsbConnectionContext>();
    CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::dynamic_pointer_cast<ConnectionContext>(ptr);
}

Expected<std::shared_ptr<UsbSessionDeviceSide>> UsbSessionDeviceSide::create(uint16_t port, usb_interface_t interface)
{
    auto session = make_shared_nothrow<UsbSessionDeviceSide>(port, interface);
    CHECK_NOT_NULL(session, HAILO_OUT_OF_HOST_MEMORY);
    
    auto status = session->init_io_threads();
    CHECK_SUCCESS(status, "Failed to create IO threads");
    
    status = session->open_fds(interface);
    CHECK_SUCCESS(status, "Failed to open fds for interface {}", interface);
    
    return session;
}

UsbSessionDeviceSide::UsbSessionDeviceSide(uint16_t port, usb_interface_t interface)
    : UsbSession(port, interface), m_receive_fd(nullptr), m_send_fd(nullptr), m_write_actions_thread(nullptr),
      m_read_actions_thread(nullptr), m_read_aio_context(nullptr), m_write_aio_context(nullptr), m_is_closed(false)
{}

hailo_status UsbSessionDeviceSide::init_io_threads()
{
    TRY(m_write_actions_thread, AsyncActionsThread::create(MAX_ONGOING_TRANSFERS));
    TRY(m_read_actions_thread, AsyncActionsThread::create(MAX_ONGOING_TRANSFERS));
    return HAILO_SUCCESS;
}

hailo_status UsbSessionDeviceSide::abort_transfers()
{
    auto status = HAILO_SUCCESS;

    if (m_write_actions_thread) {
        auto write_status = m_write_actions_thread->abort();
        if (HAILO_SUCCESS != write_status) {
            LOGGER__ERROR("Failed to abort device write actions thread: {}", write_status);
            if (HAILO_SUCCESS == status) {
                status = write_status;
            }
        }
    }

    if (m_read_actions_thread) {
        auto read_status = m_read_actions_thread->abort();
        if (HAILO_SUCCESS != read_status) {
            LOGGER__ERROR("Failed to abort device read actions thread: {}", read_status);
            if (HAILO_SUCCESS == status) {
                status = read_status;
            }
        }
    }

    return status;
}

hailo_status UsbSessionDeviceSide::wait_for_write_async_ready(size_t /*transfer_size*/, std::chrono::milliseconds timeout)
{
    return m_write_actions_thread->wait_for_enqueue_ready(timeout);
}

hailo_status UsbSessionDeviceSide::wait_for_read_async_ready(size_t /*transfer_size*/, std::chrono::milliseconds timeout)
{
    return m_read_actions_thread->wait_for_enqueue_ready(timeout);
}

hailo_status UsbSessionDeviceSide::write_async(TransferRequest &&request)
{
    CHECK_NOT_NULL(m_write_actions_thread, HAILO_NOT_SUPPORTED);
    return m_write_actions_thread->enqueue_nonblocking({[this, buffers=std::move(request.transfer_buffers)] (bool is_aborted) -> hailo_status {
        if (is_aborted) {
            return HAILO_COMMUNICATION_CLOSED;
        }

        for (const auto &transfer_buffer : buffers) {
            TRY(const auto &buffer, transfer_buffer.base_buffer());
            auto status = write_async_impl(buffer.data(), buffer.size());
            if (HAILO_SUCCESS != status) {
                return status;
            }
        }
        return HAILO_SUCCESS;
    }, request.callback});
}

hailo_status UsbSessionDeviceSide::read_async(TransferRequest &&request)
{
    CHECK_NOT_NULL(m_read_actions_thread, HAILO_NOT_SUPPORTED);
    return m_read_actions_thread->enqueue_nonblocking({[this, buffers=std::move(request.transfer_buffers)] (bool is_aborted) -> hailo_status {
        if (is_aborted) {
            return HAILO_COMMUNICATION_CLOSED;
        }
        for (const auto &transfer_buffer : buffers) {
            TRY(auto buffer, transfer_buffer.base_buffer());
            auto status = read_async_impl(buffer.data(), buffer.size());
            if (HAILO_SUCCESS != status) {
                return status;
            }
        }
        return HAILO_SUCCESS;
    }, request.callback});
}

hailo_status UsbSessionDeviceSide::open_fds(usb_interface_t interface)
{
    const auto &receive_fd_path = UsbControlListener::get_receive_fd_path(interface);
    const int receive_fd = ::open(receive_fd_path.c_str(), O_RDWR);
    CHECK(receive_fd >= 0, HAILO_OPEN_FILE_FAILURE, "Failed to open receive fd: {}", strerror(errno));
    m_receive_fd = make_shared_nothrow<FileDescriptor>(receive_fd);
    CHECK_NOT_NULL(m_receive_fd, HAILO_OUT_OF_HOST_MEMORY);

    const auto &send_fd_path = UsbControlListener::get_send_fd_path(interface);
    const int send_fd = ::open(send_fd_path.c_str(), O_RDWR);
    CHECK(send_fd >= 0, HAILO_OPEN_FILE_FAILURE, "Failed to open send fd: {}", strerror(errno));
    m_send_fd = make_shared_nothrow<FileDescriptor>(send_fd);
    CHECK_NOT_NULL(m_send_fd, HAILO_OUT_OF_HOST_MEMORY);

    TRY(m_read_aio_context, AioContext::create(MAX_ONGOING_TRANSFERS));
    TRY(m_write_aio_context, AioContext::create(MAX_ONGOING_TRANSFERS));

    return HAILO_SUCCESS;
}

hailo_status UsbSessionDeviceSide::read_async_impl(uint8_t *data, size_t size)
{
    const int read_fd = static_cast<int>(*m_receive_fd);
    size_t offset = 0;
    while (offset < size) {
        if (m_is_closed.load()) {
            return HAILO_COMMUNICATION_CLOSED;
        }

        const size_t remaining = std::min(USB_MAX_BUFFER_SIZE, size - offset);
        auto status = m_read_aio_context->submit_read(read_fd, const_cast<uint8_t*>(data) + offset, remaining, 0);
        CHECK_SUCCESS(status);

        TRY_WITH_ACCEPTABLE_STATUS(HAILO_COMMUNICATION_CLOSED,
            auto bytes_transferred, m_read_aio_context->wait_for_completion(read_fd, m_is_closed));
        offset += bytes_transferred;
    }
    return HAILO_SUCCESS;
}

hailo_status UsbSessionDeviceSide::write_async_impl(const uint8_t *data, size_t size)
{
    const int write_fd = static_cast<int>(*m_send_fd);
    size_t offset = 0;
    while (offset < size) {
        if (m_is_closed.load()) {
            return HAILO_COMMUNICATION_CLOSED;
        }

        const size_t remaining = std::min(USB_MAX_BUFFER_SIZE, size - offset);
        auto status = m_write_aio_context->submit_write(write_fd, const_cast<uint8_t*>(data) + offset, remaining, 0);
        CHECK_SUCCESS(status);

        TRY_WITH_ACCEPTABLE_STATUS(HAILO_COMMUNICATION_CLOSED,
            auto bytes_transferred, m_write_aio_context->wait_for_completion(write_fd, m_is_closed));
        offset += bytes_transferred;
    }
    return HAILO_SUCCESS;
}

UsbSessionDeviceSide::~UsbSessionDeviceSide()
{
    close();
}

hailo_status UsbSessionDeviceSide::close()
{
    if (m_is_closed.exchange(true)) {
        return HAILO_SUCCESS; 
    }    

    if (m_read_aio_context) {
        m_read_aio_context->wake();
    }
    if (m_write_aio_context) {
        m_write_aio_context->wake();
    }

    auto status = abort_transfers();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to abort transfers: {}", status);
    }

    m_read_aio_context.reset();
    m_write_aio_context.reset();

    m_receive_fd.reset();
    m_send_fd.reset();

    return status;
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
    TRY(auto session, UsbSessionDeviceSide::create(m_port, interface));

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