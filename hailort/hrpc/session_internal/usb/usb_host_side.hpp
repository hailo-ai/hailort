/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file usb_host_side.hpp
 * @brief USB Host Side Header for USB host side communication
 **/

#ifndef _USB_HOST_SIDE_HPP_
#define _USB_HOST_SIDE_HPP_

#include "hailo/expected.hpp"
#include "hrpc/session_internal/usb/usb_session.hpp"
#include <libusb-1.0/libusb.h>
 
namespace hailort
{

class UsbControlCommunication final
{
public:
    UsbControlCommunication(libusb_device_handle *handle) : m_handle(handle) {}
    ~UsbControlCommunication() = default;

    hailo_status transfer(const uint8_t *write_buffer, uint16_t write_size,
        uint8_t *read_buffer, uint16_t read_size, uint8_t request_id);

private:
    static constexpr uint16_t CONTROL_TRANSFER_VALUE = 0x0000;
    static constexpr uint16_t CONTROL_TRANSFER_INDEX = 0x0000;
    static constexpr uint32_t CONTROL_TRANSFER_TIMEOUT_MS = 10 * 1000; // 10 seconds

    hailo_status write(const uint8_t *buffer, uint16_t size, uint8_t request_id);
    hailo_status read(uint8_t *buffer, uint16_t size, uint8_t request_id);

    libusb_device_handle *m_handle;
};

class UsbSessionHostSide : public UsbSession
{
public:
    static Expected<std::shared_ptr<UsbSessionHostSide>> connect(std::shared_ptr<UsbConnectionContext> context, uint16_t port);

    UsbSessionHostSide(uint16_t port, libusb_device_handle *handle, usb_interface_t interface,
        std::shared_ptr<UsbControlCommunication> usb_comm);
    virtual ~UsbSessionHostSide();

    virtual hailo_status close() override;

private:
    static Expected<usb_interface_t> get_available_interface_with_retries(std::shared_ptr<UsbControlCommunication> usb_comm,
        uint16_t port, std::unique_lock<std::mutex> &lock);
    static Expected<usb_interface_t> get_available_interface_impl(std::shared_ptr<UsbControlCommunication> usb_comm, uint16_t port);
    static hailo_status claim_interface(libusb_device_handle *handle, usb_interface_t interface);
    hailo_status send_close_message();

    libusb_device_handle *m_handle;
    std::shared_ptr<UsbControlCommunication> m_usb_comm;
    static std::mutex m_mutex; // TODO: Find out why we need this mutex here. It seems like all libusb operations are not thread safe (HRT-19951)
};

} // namespace hailort

#endif // _USB_HOST_SIDE_HPP_
