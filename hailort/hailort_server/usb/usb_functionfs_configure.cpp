/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file usb_functionfs_configure.cpp
 * @brief FunctionFS configuration implementation.
 * 
 * Creates USB interfaces in a single FunctionFS function.
 * Each interface has bulk IN and OUT endpoints.
 * Handles USB descriptor building and writing to EP0.
 */

#include "usb/usb_functionfs_configure.hpp"

#include "common/logger_macros.hpp"
#include "common/utils.hpp"

#include "byte_order.h"

#include <linux/usb/functionfs.h>
#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>
 
 namespace hailort
 {
 
static constexpr uint8_t INTERFACE_SUBCLASS = 0xFF;
static constexpr uint8_t INTERFACE_PROTOCOL = 0x01;

static constexpr uint8_t SS_MAX_BURST = 15;

static constexpr size_t DESCRIPTOR_BUFFER_SIZE = 512;

UsbFunctionFsConfiguration::UsbFunctionFsConfiguration(std::shared_ptr<FileDescriptor> control_ep_fd) :
    m_control_ep_fd(control_ep_fd)
{}

hailo_status UsbFunctionFsConfiguration::append_to_buffer(Buffer &buffer, size_t &offset, const void *data, size_t size)
{
    CHECK(offset + size <= buffer.size(), HAILO_INTERNAL_FAILURE, 
        "Descriptor buffer would exceed capacity: {} + {} > {}", offset, size, buffer.size());
    
    std::memcpy(buffer.data() + offset, data, size);
    offset += size;
    return HAILO_SUCCESS;
}
 
hailo_status UsbFunctionFsConfiguration::write_all(const void *data, size_t size)
{
    const uint8_t *bytes = static_cast<const uint8_t*>(data);
    size_t remaining = size;

    while (remaining > 0) {
        const ssize_t written = ::write(*m_control_ep_fd, bytes, remaining);
        CHECK(written > 0, HAILO_FILE_OPERATION_FAILURE, "Write to EP0 failed: {} (written={}, errno={})", strerror(errno), written, errno);
        bytes += written;
        remaining -= static_cast<size_t>(written);
    }
    return HAILO_SUCCESS;
}
 
hailo_status UsbFunctionFsConfiguration::append_interface_descriptor(Buffer &buffer, size_t &offset, uint8_t interface_number)
{
    usb_interface_descriptor interface = {};
    interface.bLength = sizeof(usb_interface_descriptor);
    interface.bDescriptorType = USB_DT_INTERFACE;
    interface.bInterfaceNumber = interface_number;
    interface.bAlternateSetting = 0;
    interface.bNumEndpoints = ENDPOINTS_PER_INTERFACE;
    interface.bInterfaceClass = USB_CLASS_VENDOR_SPEC;
    interface.bInterfaceSubClass = INTERFACE_SUBCLASS;
    interface.bInterfaceProtocol = INTERFACE_PROTOCOL;
    interface.iInterface = 0;
    return append_to_buffer(buffer, offset, &interface, sizeof(interface));
}

hailo_status UsbFunctionFsConfiguration::append_bulk_endpoint(Buffer &buffer, size_t &offset, uint8_t endpoint_address, uint16_t max_packet_size)
{
    usb_endpoint_descriptor endpoint = {};
    endpoint.bLength = sizeof(usb_endpoint_descriptor);
    endpoint.bDescriptorType = USB_DT_ENDPOINT;
    endpoint.bEndpointAddress = endpoint_address;
    endpoint.bmAttributes = USB_ENDPOINT_XFER_BULK;
    endpoint.wMaxPacketSize = BYTE_ORDER__htole16(max_packet_size);
    endpoint.bInterval = 0;
    return append_to_buffer(buffer, offset, &endpoint, sizeof(endpoint));
}

hailo_status UsbFunctionFsConfiguration::append_superspeed_companion(Buffer &buffer, size_t &offset)
{
    usb_ss_ep_comp_descriptor companion = {};
    companion.bLength = sizeof(usb_ss_ep_comp_descriptor);
    companion.bDescriptorType = USB_DT_SS_ENDPOINT_COMP;
    companion.bMaxBurst = SS_MAX_BURST;
    companion.bmAttributes = 0;
    companion.wBytesPerInterval = BYTE_ORDER__htole16(0);
    return append_to_buffer(buffer, offset, &companion, sizeof(companion));
}

hailo_status UsbFunctionFsConfiguration::build_descriptors(Buffer &buffer, size_t &offset)
{
    usb_functionfs_descs_head_v2 header = {};
    header.magic = BYTE_ORDER__htole32(FUNCTIONFS_DESCRIPTORS_MAGIC_V2);
    header.flags = BYTE_ORDER__htole32(FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC | FUNCTIONFS_HAS_SS_DESC);
    header.length = 0;
    auto status = append_to_buffer(buffer, offset, &header, sizeof(header));
    CHECK_SUCCESS(status);

    for (const auto &speed : SPEEDS) {
        const uint32_t count_le = BYTE_ORDER__htole32(speed.descriptor_count);
        status = append_to_buffer(buffer, offset, &count_le, sizeof(uint32_t));
        CHECK_SUCCESS(status);
    }

    for (const auto &speed : SPEEDS) {
        for (uint8_t i = 0; i < MAX_USB_INTERFACES; ++i) {
            status = append_interface_descriptor(buffer, offset, i);
            CHECK_SUCCESS(status);
            
            const uint8_t ep_num = static_cast<uint8_t>(i + 1);

            status = append_bulk_endpoint(buffer, offset, USB_DIR_OUT | ep_num, speed.max_packet_size);
            CHECK_SUCCESS(status);
            if (speed.has_companion) {
                status = append_superspeed_companion(buffer, offset);
                CHECK_SUCCESS(status);
            }
            
            status = append_bulk_endpoint(buffer, offset, USB_DIR_IN | ep_num, speed.max_packet_size);
            CHECK_SUCCESS(status);
            if (speed.has_companion) {
                status = append_superspeed_companion(buffer, offset);
                CHECK_SUCCESS(status);
            }
        }
    }

    reinterpret_cast<usb_functionfs_descs_head_v2*>(buffer.data())->length = 
        BYTE_ORDER__htole32(static_cast<uint32_t>(offset));
    return HAILO_SUCCESS;
}
 
hailo_status UsbFunctionFsConfiguration::write_empty_strings()
{
    struct __attribute__((packed)) {
        usb_functionfs_strings_head header;
    } empty_strings = {
        .header = {
            .magic = BYTE_ORDER__htole32(FUNCTIONFS_STRINGS_MAGIC),
            .length = BYTE_ORDER__htole32(sizeof(empty_strings)),
            .str_count = BYTE_ORDER__htole32(0),
            .lang_count = BYTE_ORDER__htole32(0),
        }
    };

    return write_all(&empty_strings, sizeof(empty_strings));
}
 
hailo_status UsbFunctionFsConfiguration::write_descriptors()
{
    TRY(auto descriptor_buffer, Buffer::create(DESCRIPTOR_BUFFER_SIZE));
    
    size_t offset = 0;
    
    CHECK_SUCCESS(build_descriptors(descriptor_buffer, offset));
    CHECK_SUCCESS(write_all(descriptor_buffer.data(), offset), "Failed to write FunctionFS descriptors blob");

    return write_empty_strings();
}

hailo_status UsbFunctionFsConfiguration::write_descriptors_and_strings()
{
    return write_descriptors();
}

hailo_status UsbFunctionFsConfiguration::enable_udc()
{
    const int enable_result = std::system("/usr/bin/hailort_usb_setup.sh enable");
    CHECK(0 == enable_result, HAILO_INTERNAL_FAILURE, "Failed to enable UDC via script (exit code: {})", enable_result);

    return HAILO_SUCCESS;
}

hailo_status UsbFunctionFsConfiguration::disable_udc()
{
    const int disable_result = std::system("/usr/bin/hailort_usb_setup.sh disable");
    CHECK(0 == disable_result, HAILO_INTERNAL_FAILURE, "Failed to disable UDC via script (exit code: {})", disable_result);

    return HAILO_SUCCESS;
}

} /* namespace hailort */
