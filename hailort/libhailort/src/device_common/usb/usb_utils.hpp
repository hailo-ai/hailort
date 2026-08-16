/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file usb_utils.hpp
 * @brief Declaration of USB utils
 **/

#ifndef _HAILO_USB_UTILS_HPP_
#define _HAILO_USB_UTILS_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include <string>

struct libusb_device;
struct libusb_device_handle;

namespace hailort
{

inline constexpr uint16_t HAILO_USB_VENDOR_ID = 0x0b05;
inline constexpr uint16_t HAILO_USB_PRODUCT_ID = 0x1d6f;

class LibusbDeviceHandle final
{
public:
    static Expected<std::shared_ptr<LibusbDeviceHandle>> create(::libusb_device *device);

    explicit LibusbDeviceHandle(libusb_device_handle *handle);
    ~LibusbDeviceHandle();

    LibusbDeviceHandle(const LibusbDeviceHandle &) = delete;
    LibusbDeviceHandle &operator=(const LibusbDeviceHandle &) = delete;
    LibusbDeviceHandle(LibusbDeviceHandle &&) = delete;
    LibusbDeviceHandle &operator=(LibusbDeviceHandle &&) = delete;

    libusb_device_handle *handle() const { return m_handle; }

private:
    libusb_device_handle *m_handle;
};

class LibusbHandleRegistry final
{
public:
    static LibusbHandleRegistry &instance();

    LibusbHandleRegistry(const LibusbHandleRegistry &) = delete;
    LibusbHandleRegistry &operator=(const LibusbHandleRegistry &) = delete;
    LibusbHandleRegistry(LibusbHandleRegistry &&) = delete;
    LibusbHandleRegistry &operator=(LibusbHandleRegistry &&) = delete;

    Expected<std::shared_ptr<LibusbDeviceHandle>> get_or_open(
        const hailo_usb_device_info_t &usb_info,
        const std::function<Expected<std::shared_ptr<LibusbDeviceHandle>>()> &opener);

private:
    LibusbHandleRegistry() = default;

    struct UsbDeviceInfoLess
    {
        bool operator()(const hailo_usb_device_info_t &a, const hailo_usb_device_info_t &b) const;
    };

    std::mutex m_mutex;
    std::map<hailo_usb_device_info_t, std::weak_ptr<LibusbDeviceHandle>, UsbDeviceInfoLess> m_open_handles;
};

class UsbUtils {
public:
    static Expected<std::vector<hailo_usb_device_info_t>> scan();
    static Expected<hailo_usb_device_info_t> parse_usb_device_info(const std::string &device_id);
    static Expected<std::string> usb_device_info_to_string(const hailo_usb_device_info_t &device_info);
    static bool are_usb_device_infos_equal(const hailo_usb_device_info_t &first, const hailo_usb_device_info_t &second);
    static Expected<std::shared_ptr<LibusbDeviceHandle>> open_usb_device(const hailo_usb_device_info_t &usb_info);
    static Expected<void*> get_libusb_context();
};

} /* namespace hailort */

#endif /* _HAILO_USB_UTILS_HPP_ */
