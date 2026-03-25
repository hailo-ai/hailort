/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file usb_functionfs_configure.hpp
 * @brief FunctionFS configuration: writes descriptors and configures USB endpoints.
 */

#ifndef _HAILO_USB_FUNCTIONFS_CONFIGURE_HPP_
#define _HAILO_USB_FUNCTIONFS_CONFIGURE_HPP_

#include "hailo/hailort.h"
#include "hailo/buffer.hpp"
#include "common/file_descriptor.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>

namespace hailort
{

static constexpr uint8_t MAX_USB_INTERFACES = 5; //TODO: HRT-19916 - change to 6 interfaces

/**
 * Max Packet Sizes for Bulk Endpoints per USB Specification:
 * - USB 2.0 (Full Speed):  64 bytes
 * - USB 2.0 (High Speed):  512 bytes
 * - USB 3.0+ (SuperSpeed): 1024 bytes
 */
static constexpr uint16_t FS_MAX_PACKET_SIZE = 64;
static constexpr uint16_t HS_MAX_PACKET_SIZE = 512;
static constexpr uint16_t SS_MAX_PACKET_SIZE = 1024;
static constexpr uint8_t ENDPOINTS_PER_INTERFACE = 2;

class UsbFunctionFsConfiguration final
{
public:
    explicit UsbFunctionFsConfiguration(std::shared_ptr<FileDescriptor> control_ep_fd);

    hailo_status write_descriptors_and_strings();
    hailo_status enable_udc();
    hailo_status disable_udc();

private:
    struct SpeedConfig {
        uint16_t max_packet_size;
        bool has_companion;
        uint32_t descriptor_count;
    };

    static constexpr SpeedConfig SPEEDS[] = {
        {FS_MAX_PACKET_SIZE, false, MAX_USB_INTERFACES * (1 + ENDPOINTS_PER_INTERFACE)},
        {HS_MAX_PACKET_SIZE, false, MAX_USB_INTERFACES * (1 + ENDPOINTS_PER_INTERFACE)},
        {SS_MAX_PACKET_SIZE, true,  MAX_USB_INTERFACES * (1 + 2 * ENDPOINTS_PER_INTERFACE)},
    };
    hailo_status append_to_buffer(Buffer &buffer, size_t &offset, const void *data, size_t size);
    hailo_status write_all(const void *data, size_t size);
    hailo_status append_interface_descriptor(Buffer &buffer, size_t &offset, uint8_t interface_number);
    hailo_status append_bulk_endpoint(Buffer &buffer, size_t &offset, uint8_t endpoint_address, uint16_t max_packet_size);
    hailo_status append_superspeed_companion(Buffer &buffer, size_t &offset);
    hailo_status build_descriptors(Buffer &buffer, size_t &offset);
    hailo_status write_empty_strings();
    hailo_status write_descriptors();

    std::shared_ptr<FileDescriptor> m_control_ep_fd;
};
} /* namespace hailort */

#endif /* _HAILO_USB_FUNCTIONFS_CONFIGURE_HPP_ */
