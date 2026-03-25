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

#include <vector>
#include <string>


namespace hailort
{

inline constexpr uint16_t HAILO_USB_VENDOR_ID = 0x0b05;
inline constexpr uint16_t HAILO_USB_PRODUCT_ID = 0x1d6f;

class UsbUtils {
public:
    static Expected<std::vector<hailo_usb_device_info_t>> scan();
    static Expected<hailo_usb_device_info_t> parse_usb_device_info(const std::string &device_id);
    static Expected<std::string> usb_device_info_to_string(const hailo_usb_device_info_t &device_info);
    static bool are_usb_device_infos_equal(const hailo_usb_device_info_t &first, const hailo_usb_device_info_t &second);
    static Expected<void*> open_usb_device(const hailo_usb_device_info_t &usb_info);
    static Expected<void*> get_libusb_context();
};

} /* namespace hailort */

#endif /* _HAILO_USB_UTILS_HPP_ */
