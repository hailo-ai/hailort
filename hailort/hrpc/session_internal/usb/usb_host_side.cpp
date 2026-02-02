/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file usb_host_side.cpp
 * @brief USB Host Side Session
 **/

#include "hrpc/session_internal/usb/usb_host_side.hpp"
#include "device_common/usb/usb_utils.hpp"

#ifdef HAILO_EMULATOR
static constexpr size_t MAX_CONNECT_RETRIES = 1000;
#else
static constexpr size_t MAX_CONNECT_RETRIES = 10;
#endif

namespace hailort
{

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

hailo_status UsbControlCommunication::write(const uint8_t *buffer, uint16_t size, uint8_t request_id)
{
    int bytes_written = libusb_control_transfer(m_handle,
        LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE,
        request_id, CONTROL_TRANSFER_VALUE, CONTROL_TRANSFER_INDEX,
        const_cast<uint8_t*>(buffer), size, CONTROL_TRANSFER_TIMEOUT_MS
    );
    CHECK(static_cast<size_t>(bytes_written) == size, HAILO_INTERNAL_FAILURE,
        "Failed to write control transfer: {}", libusb_error_name(bytes_written));

    return HAILO_SUCCESS;
}

hailo_status UsbControlCommunication::read(uint8_t *buffer, uint16_t size, uint8_t request_id)
{
    int bytes_read = libusb_control_transfer(m_handle,
        LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE,
        request_id, CONTROL_TRANSFER_VALUE, CONTROL_TRANSFER_INDEX, buffer, size, CONTROL_TRANSFER_TIMEOUT_MS
    );
    CHECK(static_cast<size_t>(bytes_read) == size, HAILO_INTERNAL_FAILURE,
        "Failed to read control transfer: {}", libusb_error_name(bytes_read));

    return HAILO_SUCCESS;
}

hailo_status UsbControlCommunication::transfer(const uint8_t *write_buffer, uint16_t write_size,
    uint8_t *read_buffer, uint16_t read_size, uint8_t request_id)
{
    auto status = write(write_buffer, write_size, request_id);
    CHECK_SUCCESS(status);

    status = read(read_buffer, read_size, request_id);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

std::mutex UsbSessionHostSide::m_mutex;
Expected<std::shared_ptr<UsbSessionHostSide>> UsbSessionHostSide::connect(std::shared_ptr<UsbConnectionContext> context, uint16_t port)
{
    std::unique_lock<std::mutex> lock(m_mutex);

    TRY(auto handle_ptr, UsbUtils::open_usb_device(context->usb_info()));
    libusb_device_handle *handle = static_cast<libusb_device_handle*>(handle_ptr);
    auto defer_device_close = defer([&] { libusb_close(handle); });

    auto usb_comm = make_shared_nothrow<UsbControlCommunication>(handle);
    CHECK_NOT_NULL(usb_comm, HAILO_OUT_OF_HOST_MEMORY);

    TRY(auto interface, get_available_interface_with_retries(usb_comm, port, lock));
    auto status = claim_interface(handle, interface);
    CHECK_SUCCESS(status);

    auto session = make_shared_nothrow<UsbSessionHostSide>(port, handle, interface, usb_comm);
    CHECK_NOT_NULL(session, HAILO_OUT_OF_HOST_MEMORY);

    defer_device_close.release();
    return session;
}

Expected<usb_interface_t> UsbSessionHostSide::get_available_interface_with_retries(
    std::shared_ptr<UsbControlCommunication> usb_comm, uint16_t port, std::unique_lock<std::mutex> &lock)
{
    constexpr auto RETRY_INTERVAL = std::chrono::milliseconds(100);

    for (size_t i = 0; i < MAX_CONNECT_RETRIES; i++) {
        auto interface = get_available_interface_impl(usb_comm, port);
        if (HAILO_DEVICE_TEMPORARILY_UNAVAILABLE != interface.status()) {
            return interface;
        }
        lock.unlock();
        std::this_thread::sleep_for(RETRY_INTERVAL);
        lock.lock();
    }

    return make_unexpected(HAILO_DEVICE_TEMPORARILY_UNAVAILABLE);
}

Expected<usb_interface_t> UsbSessionHostSide::get_available_interface_impl(
    std::shared_ptr<UsbControlCommunication> usb_comm, uint16_t port)
{
    UsbConnectRequest request = {port};
    UsbConnectResponse response = {0, 0};

    auto status = usb_comm->transfer(reinterpret_cast<const uint8_t*>(&request), sizeof(request),
        reinterpret_cast<uint8_t*>(&response), sizeof(response),
        static_cast<uint8_t>(UsbControlProtocolOpcode::CONNECT));
    CHECK_SUCCESS(status);

    status = static_cast<hailo_status>(response.status);
    CHECK_SUCCESS_WITH_ACCEPTABLE_STATUS(HAILO_DEVICE_TEMPORARILY_UNAVAILABLE,
        status, "Failed to connect to USB device on port {}", port);

    return usb_interface_t(response.interface);
}

hailo_status UsbSessionHostSide::claim_interface(libusb_device_handle *handle, usb_interface_t interface)
{
    int ret = libusb_kernel_driver_active(handle, interface);
    if (1 == ret) {
        ret = libusb_detach_kernel_driver(handle, interface);
        CHECK(0 == ret, HAILO_INTERNAL_FAILURE, "Failed to detach kernel driver: {}", libusb_error_name(ret));
    } else {
        CHECK(0 == ret, HAILO_INTERNAL_FAILURE, "Failed to get kernel driver status: {}", libusb_error_name(ret));
    }

    ret = libusb_claim_interface(handle, interface);
    CHECK(0 == ret, HAILO_INTERNAL_FAILURE, "Failed to claim interface {}: {}", interface, libusb_error_name(ret));

    return HAILO_SUCCESS;
}

UsbSessionHostSide::UsbSessionHostSide(uint16_t port, libusb_device_handle *handle,
    usb_interface_t interface, std::shared_ptr<UsbControlCommunication> usb_comm)
        : UsbSession(port, interface), m_handle(handle), m_usb_comm(usb_comm)
{}

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
    CHECK_SUCCESS(status);

    status = static_cast<hailo_status>(response.status);
    CHECK_SUCCESS(status, "Failed to close interface {} on port {}, status = {}", interface(), m_port, status);

    return HAILO_SUCCESS;
}

hailo_status UsbSessionHostSide::close()
{
    std::unique_lock<std::mutex> lock(m_mutex);

    if (nullptr == m_handle) {
        return HAILO_SUCCESS;
    }

    auto status = send_close_message();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to send close message: {}", status);
    }

    int ret = libusb_release_interface(m_handle, interface());
    if (0 != ret) {
        LOGGER__ERROR("Failed to release interface {}: {}", interface(), libusb_error_name(ret));
        status = HAILO_INTERNAL_FAILURE;
    }

    libusb_close(m_handle);
    m_handle = nullptr;

    return status;
}

} // namespace hailort
