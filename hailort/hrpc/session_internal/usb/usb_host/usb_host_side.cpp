/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file usb_host_side.cpp
 * @brief USB Host Side Session
 **/

#include "hrpc/session_internal/usb/usb_host/usb_host_side.hpp"
#include "hrpc/session_internal/usb/usb_host/libusb_event_thread.hpp"
#include "hrpc/session_internal/usb/usb_host/usb_transfer_queue.hpp"
#include "common/named_mutex.hpp"
#include "device_common/usb/usb_utils.hpp"
#include "hailo/hailort.h"
#include "common/utils.hpp"

namespace hailort
{

#ifdef HAILO_EMULATOR
static constexpr size_t MAX_CONNECT_RETRIES = 1000;
#else
static constexpr size_t MAX_CONNECT_RETRIES = 10;
#endif
  
Expected<std::shared_ptr<ConnectionContext>> UsbConnectionContext::create_client_shared(const std::string &device_id)
{
    TRY(auto usb_info, UsbUtils::parse_usb_device_info(device_id));
    auto ptr = make_shared_nothrow<UsbConnectionContext>(usb_info);
    CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);
    return std::dynamic_pointer_cast<ConnectionContext>(ptr);
}

Expected<std::shared_ptr<UsbSession>> UsbSession::connect(std::shared_ptr<UsbConnectionContext> context, uint16_t port)
{
    return UsbSessionHostSide::connect(context, port);
}

static std::string usb_ep_mutex_name(const hailo_usb_device_info_t &usb_info, uint8_t ep)
{
    return "usb_ep" + std::to_string(ep) + "_mutex_" + std::to_string(usb_info.bus) + "_" + std::to_string(usb_info.device_address);
}

Expected<std::shared_ptr<UsbInterface>> UsbInterface::acquire(const hailo_usb_device_info_t &usb_info)
{
    TRY(const auto &device_id, UsbUtils::usb_device_info_to_string(usb_info));
    for (size_t i = 0; i < MAX_USB_INTERFACES; i++) {
        TRY(auto mutex, NamedMutex::create(usb_ep_mutex_name(usb_info, static_cast<usb_interface_t>(i + 1))));
        auto expected_guard = NamedMutex::LockGuard::create(mutex, std::chrono::milliseconds(0));
        if (HAILO_TIMEOUT == expected_guard.status()) {
            continue;
        }
        CHECK_EXPECTED(expected_guard);

        auto ptr = make_shared_nothrow<UsbInterface>(usb_interface_t(i), expected_guard.release());
        CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);
        return ptr;
    }

    return make_unexpected(HAILO_DEVICE_TEMPORARILY_UNAVAILABLE);
}

Expected<std::shared_ptr<UsbControlCommunication>> UsbControlCommunication::create(libusb_device_handle *handle)
{
    libusb_device *dev = libusb_get_device(handle);
    hailo_usb_device_info_t usb_info = {libusb_get_bus_number(dev), libusb_get_device_address(dev)};
    TRY(auto mutex, NamedMutex::create(usb_ep_mutex_name(usb_info, 0)));

    auto ptr = make_shared_nothrow<UsbControlCommunication>(handle, mutex);
    CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);
    return ptr;
}

hailo_status UsbControlCommunication::write(const uint8_t *buffer, uint16_t size, uint8_t request_id)
{
    int bytes_written = libusb_control_transfer(m_handle,
        LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE,
        request_id, CONTROL_TRANSFER_VALUE, CONTROL_TRANSFER_INDEX,
        const_cast<uint8_t*>(buffer), size, CONTROL_TRANSFER_TIMEOUT_MS
    );

    if (LIBUSB_ERROR_NO_DEVICE == bytes_written) {
        return HAILO_DEVICE_NOT_CONNECTED;
    }
    CHECK((bytes_written >= 0) && (static_cast<size_t>(bytes_written) == size),
        HAILO_LIBUSB_FAILURE, "Control transfer failed or partial {}: expected {} bytes, got {} (id: 0x{:02X})",
        libusb_error_name(bytes_written), size, bytes_written, request_id);
    return HAILO_SUCCESS;
}

hailo_status UsbControlCommunication::read(uint8_t *buffer, uint16_t size, uint8_t request_id)
{
    int bytes_read = libusb_control_transfer(m_handle,
        LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE,
        request_id, CONTROL_TRANSFER_VALUE, CONTROL_TRANSFER_INDEX, buffer, size, CONTROL_TRANSFER_TIMEOUT_MS
    );
    if (LIBUSB_ERROR_NO_DEVICE == bytes_read) {
        return HAILO_DEVICE_NOT_CONNECTED;
    }
    CHECK((bytes_read >= 0) && (static_cast<size_t>(bytes_read) == size),
        HAILO_LIBUSB_FAILURE, "Control transfer failed or partial {}: expected {} bytes, got {} (id: 0x{:02X})",
        libusb_error_name(bytes_read), size, bytes_read, request_id);

    return HAILO_SUCCESS;
}

hailo_status UsbControlCommunication::transfer(const uint8_t *write_buffer, uint16_t write_size,
    uint8_t *read_buffer, uint16_t read_size, uint8_t request_id)
{
    static constexpr std::chrono::milliseconds GUARD_TIMEOUT = std::chrono::seconds(10);
    TRY(auto guard, NamedMutex::LockGuard::create(m_mutex, GUARD_TIMEOUT));

    auto status = write(write_buffer, write_size, request_id);
    CHECK_SUCCESS_ACCEPT_DISCONNECT(status);

    status = read(read_buffer, read_size, request_id);
    CHECK_SUCCESS_ACCEPT_DISCONNECT(status);

    return HAILO_SUCCESS;
}

std::mutex UsbSessionHostSide::m_mutex;
Expected<std::shared_ptr<UsbSessionHostSide>> UsbSessionHostSide::connect(std::shared_ptr<UsbConnectionContext> context, uint16_t port)
{
    std::unique_lock<std::mutex> lock(m_mutex);

    TRY_WITH_ACCEPTABLE_STATUS(HAILO_INVALID_FIRMWARE, auto handle_ptr, UsbUtils::open_usb_device(context->usb_info()));
    libusb_device_handle *handle = static_cast<libusb_device_handle*>(handle_ptr);
    auto defer_device_close = defer([&] { libusb_close(handle); });

    TRY(auto usb_comm, UsbControlCommunication::create(handle));
    TRY(auto interface, get_available_interface_with_retries(context->usb_info(), port, usb_comm, lock));
    auto status = claim_interface(handle, interface->interface());
    CHECK_SUCCESS(status);

    TRY(auto session, UsbSessionHostSide::create(port, handle, std::move(interface), usb_comm));

    defer_device_close.release();
    return session;
}

Expected<std::shared_ptr<UsbInterface>> UsbSessionHostSide::get_available_interface_with_retries(const hailo_usb_device_info_t &usb_info,
    uint16_t port, std::shared_ptr<UsbControlCommunication> usb_comm, std::unique_lock<std::mutex> &lock)
{
    constexpr auto RETRY_INTERVAL = std::chrono::milliseconds(100);

    for (size_t i = 0; i < MAX_CONNECT_RETRIES; i++) {
        auto interface = get_available_interface_impl(usb_info, port, usb_comm);
        if (HAILO_DEVICE_TEMPORARILY_UNAVAILABLE != interface.status()) {
            return interface;
        }
        lock.unlock();
        std::this_thread::sleep_for(RETRY_INTERVAL);
        lock.lock();
    }

    return make_unexpected(HAILO_DEVICE_TEMPORARILY_UNAVAILABLE);
}

Expected<std::shared_ptr<UsbInterface>> UsbSessionHostSide::get_available_interface_impl(const hailo_usb_device_info_t &usb_info, uint16_t port,
    std::shared_ptr<UsbControlCommunication> usb_comm)
{
    TRY_WITH_ACCEPTABLE_STATUS(HAILO_DEVICE_TEMPORARILY_UNAVAILABLE, auto interface, UsbInterface::acquire(usb_info));

    UsbConnectRequest request = {interface->interface(), port};
    UsbConnectResponse response = {0};

    auto status = usb_comm->transfer(reinterpret_cast<const uint8_t*>(&request), sizeof(request),
        reinterpret_cast<uint8_t*>(&response), sizeof(response),
        static_cast<uint8_t>(UsbControlProtocolOpcode::CONNECT));
    CHECK_SUCCESS(status);

    status = static_cast<hailo_status>(response.status);
    CHECK_SUCCESS(status, "Failed to connect to USB device on port {}", port);

    return interface;
}

hailo_status UsbSessionHostSide::claim_interface(libusb_device_handle *handle, usb_interface_t interface)
{
    int ret = libusb_kernel_driver_active(handle, interface);
    CHECK((0 == ret) || (1 == ret), HAILO_LIBUSB_FAILURE, "Failed to get kernel driver status: {}", libusb_error_name(ret));
    if (1 == ret) {
        ret = libusb_detach_kernel_driver(handle, interface);
        CHECK(0 == ret, HAILO_LIBUSB_FAILURE, "Failed to detach kernel driver for interface {}: {}", interface, libusb_error_name(ret));
    }

    ret = libusb_claim_interface(handle, interface);
    CHECK(0 == ret, HAILO_LIBUSB_FAILURE, "Failed to claim interface {}: {}", interface, libusb_error_name(ret));

    return HAILO_SUCCESS;
}

Expected<std::pair<uint8_t, uint8_t>> UsbSessionHostSide::find_interface_endpoints(
    libusb_device_handle *handle, const usb_interface_t interface)
{
    libusb_device *const device = libusb_get_device(handle);
    CHECK_NOT_NULL(device, HAILO_LIBUSB_FAILURE);
    libusb_config_descriptor *config = nullptr;
    const int ret = libusb_get_active_config_descriptor(device, &config);
    CHECK(0 == ret, HAILO_LIBUSB_FAILURE, "Failed to get active config descriptor: {}", libusb_error_name(ret));

    auto defer_free_config = defer([&] { libusb_free_config_descriptor(config); });

    const int num_interfaces = config->bNumInterfaces;
    const libusb_interface_descriptor *intf_desc = nullptr;
    for (int i = 0; i < num_interfaces; i++) {
        // Check the altsetting to ensure we are working with the correct interface configuration that actually exposes the endpoints required by libusb.
        if (0 == config->interface[i].num_altsetting) {
            continue;
        }
        
        if (config->interface[i].altsetting[0].bInterfaceNumber == interface) {
            intf_desc = &config->interface[i].altsetting[0];
            break;
        }
    }
    
    CHECK(nullptr != intf_desc, HAILO_LIBUSB_FAILURE, "Failed to find interface descriptor for interface {}", interface);
    uint8_t receive_endpoint = 0;
    uint8_t send_endpoint = 0;
    
    for (int i = 0; i < intf_desc->bNumEndpoints; i++) {
        const libusb_endpoint_descriptor *ep_desc = &intf_desc->endpoint[i];
        const uint8_t ep_addr = ep_desc->bEndpointAddress;
        const uint8_t ep_type = ep_desc->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK;
        
        if (ep_type != LIBUSB_TRANSFER_TYPE_BULK) {
            continue;
        }
        
        if ((ep_addr & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN) {
            receive_endpoint = ep_addr;
        } else {
            send_endpoint = ep_addr;
        }
    }
    CHECK((receive_endpoint != 0) && (send_endpoint != 0), HAILO_LIBUSB_FAILURE, 
        "Failed to find both bulk endpoints for interface {}", interface);
    
    return std::make_pair(receive_endpoint, send_endpoint);
}

Expected<std::shared_ptr<UsbSessionHostSide>> UsbSessionHostSide::create(uint16_t port, libusb_device_handle *handle,
    std::shared_ptr<UsbInterface> interface, std::shared_ptr<UsbControlCommunication> usb_comm)
{
    TRY(auto endpoints, find_interface_endpoints(handle, interface->interface()));
    const auto [receive_endpoint, send_endpoint] = endpoints;

    auto status = drain_endpoints(handle, send_endpoint, receive_endpoint);
    CHECK_SUCCESS(status, "Failed to drain endpoints");

    auto session = make_shared_nothrow<UsbSessionHostSide>(port, handle, interface, usb_comm, receive_endpoint, send_endpoint);
    CHECK_NOT_NULL(session, HAILO_OUT_OF_HOST_MEMORY);

    TRY(auto libusb_ctx_void, UsbUtils::get_libusb_context());
    libusb_context *libusb_ctx = static_cast<libusb_context*>(libusb_ctx_void);
    TRY(session->m_event_thread, LibusbEventThread::create_shared(libusb_ctx));

    status = session->create_transfer_queues();
    CHECK_SUCCESS(status, "Failed to initialize host transfer manager");

    return session;
}

UsbSessionHostSide::UsbSessionHostSide(uint16_t port, libusb_device_handle *handle, std::shared_ptr<UsbInterface> interface,
    std::shared_ptr<UsbControlCommunication> usb_comm, uint8_t receive_endpoint, uint8_t send_endpoint)
        : UsbSession(port, interface->interface()), m_handle(handle), m_usb_comm(usb_comm),
        m_receive_endpoint(receive_endpoint), m_send_endpoint(send_endpoint), m_is_closed(false), m_interface(interface),
        m_write_transfer_queue(nullptr), m_read_transfer_queue(nullptr)
{}

hailo_status UsbSessionHostSide::create_transfer_queues()
{
    TRY(m_write_transfer_queue, UsbTransferQueue::create(m_handle, m_send_endpoint, m_is_closed));
    TRY(m_read_transfer_queue, UsbTransferQueue::create(m_handle, m_receive_endpoint, m_is_closed));
    return HAILO_SUCCESS;
}

hailo_status UsbSessionHostSide::wait_for_write_async_ready(size_t, std::chrono::milliseconds timeout)
{
    CHECK(!m_is_closed.load(), HAILO_COMMUNICATION_CLOSED);
    return m_write_transfer_queue->wait_for_async_ready(timeout);
}

hailo_status UsbSessionHostSide::wait_for_read_async_ready(size_t, std::chrono::milliseconds timeout)
{
    CHECK(!m_is_closed.load(), HAILO_COMMUNICATION_CLOSED);
    return m_read_transfer_queue->wait_for_async_ready(timeout);
}

hailo_status UsbSessionHostSide::write_async(TransferRequest &&request)
{
    return m_write_transfer_queue->enqueue_transfer(std::move(request));
}

hailo_status UsbSessionHostSide::read_async(TransferRequest &&request)
{
    return m_read_transfer_queue->enqueue_transfer(std::move(request));
}

UsbSessionHostSide::~UsbSessionHostSide()
{
    close();
}

hailo_status UsbSessionHostSide::send_close_message()
{
    UsbCloseRequest request = {m_port, interface()};
    UsbCloseResponse response = {0};

    auto status = m_usb_comm->transfer(reinterpret_cast<const uint8_t*>(&request), sizeof(request),
        reinterpret_cast<uint8_t*>(&response), sizeof(response),
        static_cast<uint8_t>(UsbControlProtocolOpcode::CLOSE));
    if (HAILO_SUCCESS != status) {
        return status;
    }

    status = static_cast<hailo_status>(response.status);
    CHECK_SUCCESS(status, "Failed to close interface {} on port {}, status = {}", interface(), m_port, status);

    return HAILO_SUCCESS;
}

// Drain endpoints to clear stuck data in the endpoints
hailo_status UsbSessionHostSide::drain_endpoints(libusb_device_handle *handle, uint8_t send_endpoint, uint8_t receive_endpoint)
{
    int res = libusb_clear_halt(handle, send_endpoint);
    CHECK((LIBUSB_SUCCESS == res) || (LIBUSB_ERROR_NOT_FOUND == res), HAILO_LIBUSB_FAILURE,
        "Failed to clear halt on OUT endpoint 0x{:02x}: {}", send_endpoint, libusb_error_name(static_cast<enum libusb_error>(res)));

    res = libusb_clear_halt(handle, receive_endpoint);
    CHECK((LIBUSB_SUCCESS == res) || (LIBUSB_ERROR_NOT_FOUND == res), HAILO_LIBUSB_FAILURE,
        "Failed to clear halt on IN endpoint 0x{:02x}: {}", receive_endpoint, libusb_error_name(static_cast<enum libusb_error>(res)));
    return HAILO_SUCCESS;
}

hailo_status UsbSessionHostSide::release_handles()
{
    const int ret = libusb_release_interface(m_handle, interface());
    if (0 != ret) {
        if (LIBUSB_ERROR_NO_DEVICE == ret) {
            LOGGER__INFO("USB device already disconnected, skipping interface {} release", interface());
        } else {
            LOGGER__ERROR("Failed to release interface {}: {}", interface(), libusb_error_name(ret));
        }
    }

    libusb_close(m_handle);
    m_handle = nullptr;

    if (0 == ret) {
        return HAILO_SUCCESS;
    }
    return (LIBUSB_ERROR_NO_DEVICE == ret) ? HAILO_DEVICE_NOT_CONNECTED : HAILO_LIBUSB_FAILURE;
}

hailo_status UsbSessionHostSide::cancel_pending_transfers()
{
    auto status = HAILO_SUCCESS;
    if (m_write_transfer_queue) {
        auto write_status = m_write_transfer_queue->cancel_pending_transfers();
        if ((HAILO_SUCCESS != write_status) && (HAILO_SUCCESS == status)) {
            status = write_status;
        }
    }
    if (m_read_transfer_queue) {
        auto read_status = m_read_transfer_queue->cancel_pending_transfers();
        if ((HAILO_SUCCESS != read_status) && (HAILO_SUCCESS == status)) {
            status = read_status;
        }
    }
    return status;
}

hailo_status UsbSessionHostSide::close()
{
    if (m_is_closed.exchange(true) || nullptr == m_handle) {
        return HAILO_SUCCESS;
    }

    auto status = cancel_pending_transfers();

    auto close_status = send_close_message();
    if (HAILO_SUCCESS != close_status) {
        if (is_device_unreachable(close_status)) {
            LOGGER__INFO("USB session close: device already disconnected, skipping close message (status {})", close_status);
        } else {
            LOGGER__ERROR("Failed to send close message: {}", close_status);
        }
        // Continue with cleanup even if close message failed
        if (HAILO_SUCCESS == status) {
            status = close_status;
        }
    }

    auto release_status = release_handles();
    if (HAILO_SUCCESS != release_status) {
        if (HAILO_SUCCESS == status) {
            status = release_status;
        }
    }

    // Release our shared_ptr to the event thread. If this is the last session using it,
    // the destructor stops the polling thread and joins it.
    m_event_thread.reset();
    m_interface.reset();

    return status;
}

} // namespace hailort
