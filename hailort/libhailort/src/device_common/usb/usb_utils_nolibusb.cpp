/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file usb_utils_nolibusb.cpp
 * @brief Implementation of USB utils without libusb
 **/

#include "hailo/hailort.h"
#include "device_common/usb/usb_utils.hpp"

namespace hailort
{

Expected<std::vector<hailo_usb_device_info_t>> UsbUtils::scan()
{
    return std::vector<hailo_usb_device_info_t>();
}

Expected<void*> UsbUtils::open_usb_device(const hailo_usb_device_info_t &)
{
    return make_unexpected(HAILO_NOT_SUPPORTED);
}

Expected<void*> UsbUtils::get_libusb_context()
{
    return make_unexpected(HAILO_NOT_SUPPORTED);
}

} /* namespace hailort */