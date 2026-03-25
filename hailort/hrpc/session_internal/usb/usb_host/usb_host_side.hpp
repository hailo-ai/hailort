/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
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
#include "common/named_mutex.hpp"
#include "hrpc/session_internal/usb/usb_control_protocol.hpp"
#include "hailort_server/usb/usb_functionfs_configure.hpp"
#include "vdma/transfer_common.hpp"

#include "hrpc/session_internal/usb/usb_host/libusb_event_thread.hpp"
#include "hrpc/session_internal/usb/usb_host/usb_transfer_queue.hpp"

#include <libusb-1.0/libusb.h>

namespace hailort
{

class UsbInterface final
{
public:
    static Expected<std::shared_ptr<UsbInterface>> acquire(const hailo_usb_device_info_t &usb_info);
    UsbInterface(usb_interface_t interface, NamedMutex::LockGuard &&guard)
        : m_interface(interface), m_guard(std::move(guard)) {}
    ~UsbInterface() = default;

    UsbInterface &operator=(UsbInterface &&other) noexcept = default;
    UsbInterface(UsbInterface &&other) noexcept = default;
    UsbInterface(const UsbInterface &other) = delete;
    UsbInterface &operator=(const UsbInterface &other) = delete;

    usb_interface_t interface() const { return m_interface; }

private:
    usb_interface_t m_interface;
    NamedMutex::LockGuard m_guard;
};

class UsbControlCommunication final
{
public:
    static Expected<std::shared_ptr<UsbControlCommunication>> create(libusb_device_handle *handle);
    UsbControlCommunication(libusb_device_handle *handle, std::shared_ptr<NamedMutex> mutex)
        : m_handle(handle), m_mutex(mutex) {}
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
    std::shared_ptr<NamedMutex> m_mutex;
};

class UsbSessionHostSide : public UsbSession
{
public:
    static Expected<std::shared_ptr<UsbSessionHostSide>> connect(std::shared_ptr<UsbConnectionContext> context, uint16_t port);
    static Expected<std::shared_ptr<UsbSessionHostSide>> create(uint16_t port, libusb_device_handle *handle,
        std::shared_ptr<UsbInterface> interface, std::shared_ptr<UsbControlCommunication> usb_comm);

    UsbSessionHostSide(uint16_t port, libusb_device_handle *handle, std::shared_ptr<UsbInterface> interface,
        std::shared_ptr<UsbControlCommunication> usb_comm, uint8_t receive_endpoint, uint8_t send_endpoint);
    
    virtual hailo_status close() override;

    virtual hailo_status wait_for_write_async_ready(size_t transfer_size, std::chrono::milliseconds timeout) override;
    virtual hailo_status write_async(TransferRequest &&request) override;
    virtual hailo_status wait_for_read_async_ready(size_t transfer_size, std::chrono::milliseconds timeout) override;
    virtual hailo_status read_async(TransferRequest &&request) override;
    
    virtual ~UsbSessionHostSide();

private:
    static Expected<std::shared_ptr<UsbInterface>> get_available_interface_with_retries(const hailo_usb_device_info_t &usb_info,
        uint16_t port, std::shared_ptr<UsbControlCommunication> usb_comm, std::unique_lock<std::mutex> &lock);
    static Expected<std::shared_ptr<UsbInterface>> get_available_interface_impl(const hailo_usb_device_info_t &usb_info, uint16_t port,
        std::shared_ptr<UsbControlCommunication> usb_comm);
    static hailo_status claim_interface(libusb_device_handle *handle, usb_interface_t interface);
    static Expected<std::pair<uint8_t, uint8_t>> find_interface_endpoints(libusb_device_handle *handle,
        usb_interface_t interface);
    static hailo_status drain_endpoints(libusb_device_handle *handle, uint8_t send_endpoint, uint8_t receive_endpoint);

    hailo_status create_transfer_queues();

    hailo_status send_close_message();
    hailo_status cancel_pending_transfers();
    hailo_status release_handles();

    static std::mutex m_mutex; // TODO: Find out why we need this mutex here. It seems like all libusb operations are not thread safe (HRT-19951)
    libusb_device_handle *m_handle;
    std::shared_ptr<UsbControlCommunication> m_usb_comm;
    uint8_t m_receive_endpoint;
    uint8_t m_send_endpoint;
    std::atomic_bool m_is_closed;
    std::shared_ptr<UsbInterface> m_interface;
    std::unique_ptr<UsbTransferQueue> m_write_transfer_queue;
    std::unique_ptr<UsbTransferQueue> m_read_transfer_queue;
    std::shared_ptr<LibusbEventThread> m_event_thread;
};

} // namespace hailort

#endif // _USB_HOST_SIDE_HPP_
