/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file usb_control_protocol.hpp
 * @brief USB Control Protocol Header for USB control communication
 **/

#ifndef _USB_CONTROL_PROTOCOL_HPP_
#define _USB_CONTROL_PROTOCOL_HPP_

#include <limits>

namespace hailort
{

typedef uint8_t usb_interface_t;

enum class UsbControlProtocolOpcode : uint8_t {
    INVALID = 0,
    CONNECT = 1,
    CLOSE = 2
};

#pragma pack(push, 1)
// Only one interface per connection is supported
struct UsbConnectRequest {
    usb_interface_t interface;
    uint32_t port;
};

struct UsbConnectResponse {
    uint32_t status;
};

struct UsbCloseRequest {
    uint32_t port;
    usb_interface_t interface;
};

struct UsbCloseResponse {
    uint32_t status;
};
#pragma pack(pop)

} // namespace hailort

#endif // _USB_CONTROL_PROTOCOL_HPP_