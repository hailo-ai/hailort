/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file usb_session_noimpl_device.cpp
 * @brief USB Hailo Session No Implementation for when USB is not available/needed - device side
 **/

#include "hrpc/session_internal/usb/usb_session.hpp"

namespace hailort
{

Expected<std::shared_ptr<ConnectionContext>> UsbConnectionContext::create_server_shared()
{
    return make_unexpected(HAILO_NOT_SUPPORTED);
}

Expected<std::shared_ptr<UsbListener>> UsbListener::create_shared(std::shared_ptr<UsbConnectionContext>, uint16_t)
{
    return make_unexpected(HAILO_NOT_SUPPORTED);
}

Expected<std::shared_ptr<Session>> UsbListener::accept()
{
    return make_unexpected(HAILO_NOT_SUPPORTED);
}

UsbListener::~UsbListener()
{}

} // namespace hailort
