/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file usb_session_noimpl_host.cpp
 * @brief USB Hailo Session No Implementation for when USB is not available/needed - host side
 **/

#include "hrpc/session_internal/usb/usb_session.hpp"

namespace hailort
{

Expected<std::shared_ptr<ConnectionContext>> UsbConnectionContext::create_client_shared(const std::string &)
{
    return make_unexpected(HAILO_NOT_SUPPORTED);
}

Expected<std::shared_ptr<UsbSession>> UsbSession::connect(std::shared_ptr<UsbConnectionContext>, uint16_t)
{
    return make_unexpected(HAILO_NOT_SUPPORTED);
}

} // namespace hailort
