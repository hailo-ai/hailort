/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file usb_utils_common.cpp
 * @brief Implementation of common USB utils
 **/

#include "hailo/hailort.h"
#include "device_common/usb/usb_utils.hpp"

#include <limits>
#include <sstream>
#include <iomanip>

namespace hailort
{

Expected<hailo_usb_device_info_t> UsbUtils::parse_usb_device_info(const std::string &device_id)
{
    uint32_t bus = 0;
    uint32_t device_address = 0;

    int scanf_res = sscanf(device_id.c_str(), "usb/%03d:%03d", &bus, &device_address);
    if (2 != scanf_res) {
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }

    if((bus > std::numeric_limits<uint8_t>::max()) || (device_address > std::numeric_limits<uint8_t>::max())) {
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }

    return hailo_usb_device_info_t{static_cast<uint8_t>(bus), static_cast<uint8_t>(device_address)};
}

Expected<std::string> UsbUtils::usb_device_info_to_string(const hailo_usb_device_info_t &usb_info)
{
    std::ostringstream ss;
    ss << "usb/" << std::setfill('0') << std::setw(3) << static_cast<uint32_t>(usb_info.bus)
        << ":" << std::setw(3) << static_cast<uint32_t>(usb_info.device_address);
    return ss.str();
}

bool UsbUtils::are_usb_device_infos_equal(const hailo_usb_device_info_t &first, const hailo_usb_device_info_t &second)
{
    return (first.bus == second.bus) && (first.device_address == second.device_address);
}

} /* namespace hailort */