/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file usb_utils_libusb.cpp
 * @brief Implementation of USB utils with libusb
 **/

#include "hailo/hailort.h"
#include "device_common/usb/usb_utils.hpp"
#include "common/utils.hpp"

#include <libusb.h>
#include <array>
#include <mutex>
#include <string>

namespace hailort
{

// Must match USB_PRODUCT in hailort/hailort_server/usb/hailort_usb_setup.sh
constexpr auto HAILO_USB_PRODUCT_NAME = "UGen300 USB AI Accelerator (Hailo-10H)";
constexpr size_t MAX_PRODUCT_NAME_LENGTH = 256;

static hailo_status is_hailo_functionfs_gadget(libusb_device *device, const libusb_device_descriptor &desc)
{
    libusb_device_handle *handle = nullptr;
    const auto ret = libusb_open(device, &handle);
    CHECK(0 == ret, HAILO_LIBUSB_FAILURE, "Failed to open USB device, libusb error: {}",
        libusb_error_name(ret));
 
    auto close_handle = defer([&] { libusb_close(handle); });

    std::array<unsigned char, MAX_PRODUCT_NAME_LENGTH> product = {};
    int string_length = libusb_get_string_descriptor_ascii(handle, desc.iProduct, product.data(), static_cast<int>(product.size()));
    
    CHECK(string_length >= 0, HAILO_LIBUSB_FAILURE, "Failed to get USB device product name");
  
    auto product_name = std::string(reinterpret_cast<const char*>(product.data()), string_length);
    return (HAILO_USB_PRODUCT_NAME == product_name) ? HAILO_SUCCESS : HAILO_NOT_FOUND;
}

static libusb_context *load_context()
{
    static std::once_flag init_flag;
    static std::unique_ptr<libusb_context, void(*)(libusb_context*)> libusb_context_ptr = {nullptr, nullptr};

    std::call_once(init_flag, []() {
        libusb_context *ctx = nullptr;
        auto ret = libusb_init(&ctx);
        if (0 != ret) {
            LOGGER__ERROR("Could not initialize libusb context, error: {}", libusb_error_name(ret));
            return;
        }
        libusb_context_ptr = {ctx, [](libusb_context *ctx) { libusb_exit(ctx); }};
    });

    return libusb_context_ptr.get();
}

Expected<std::vector<hailo_usb_device_info_t>> UsbUtils::scan()
{
    auto ctx = load_context();
    CHECK_NOT_NULL(ctx, HAILO_INTERNAL_FAILURE);

    std::vector<hailo_usb_device_info_t> devices;
    libusb_device **list = nullptr;
    auto device_count = libusb_get_device_list(ctx, &list);
    if (device_count < 0) {
        return devices;
    }
    auto defer_free_device_list = defer([&] () { libusb_free_device_list(list, true); });

    for (uint32_t i = 0; i < device_count; i++) {
        libusb_device *device = list[i];

        libusb_device_descriptor desc = {};
        auto ret = libusb_get_device_descriptor(device, &desc);
        CHECK(0 == ret, HAILO_LIBUSB_FAILURE, "Failed to get USB device descriptor");

        if ((HAILO_USB_VENDOR_ID == desc.idVendor) && (HAILO_USB_PRODUCT_ID == desc.idProduct)) {
            auto bus = libusb_get_bus_number(device);
            auto device_address = libusb_get_device_address(device);
            devices.push_back(hailo_usb_device_info_t{bus, device_address});
        }
    }

    return devices;
}

Expected<void*> UsbUtils::open_usb_device(const hailo_usb_device_info_t &usb_info)
{
    auto ctx = load_context();
    CHECK_NOT_NULL(ctx, HAILO_INTERNAL_FAILURE);

    libusb_device **list = nullptr;
    auto device_count = libusb_get_device_list(ctx, &list);
    CHECK(device_count > 0, HAILO_OUT_OF_PHYSICAL_DEVICES, "No USB devices found, libusb error: {}",
        libusb_error_name(static_cast<libusb_error>(device_count)));
    auto defer_free_device_list = defer([&] () { libusb_free_device_list(list, true); });

    for (uint32_t i = 0; i < device_count; i++) {
        libusb_device *device = list[i];
        libusb_device_descriptor desc = {};
        auto ret = libusb_get_device_descriptor(device, &desc);
        CHECK(0 == ret, HAILO_INTERNAL_FAILURE, "Failed to get USB device descriptor, libusb error: {}", libusb_error_name(ret));

        if ((HAILO_USB_VENDOR_ID == desc.idVendor) && (HAILO_USB_PRODUCT_ID == desc.idProduct)
            && (libusb_get_bus_number(device) == usb_info.bus)
            && (libusb_get_device_address(device) == usb_info.device_address)) {
            auto status = is_hailo_functionfs_gadget(device, desc);
            if (HAILO_NOT_FOUND == status) {
                LOGGER__WARNING("Hailo USB device detected, but its firmware version is incompatible." 
                    " Please update the firmware by running: hailo_usb_loader fw-update");
                return make_unexpected(HAILO_INVALID_FIRMWARE);
            }

            libusb_device_handle *handle = nullptr;
            ret = libusb_open(device, &handle);
            CHECK(0 == ret, HAILO_INTERNAL_FAILURE, "Failed to open USB device, libusb error: {}", libusb_error_name(ret));
            return handle;
        }
    }

    LOGGER__ERROR("Failed to find USB device with bus {} and device address {}", usb_info.bus, usb_info.device_address);
    return make_unexpected(HAILO_OUT_OF_PHYSICAL_DEVICES);
}

Expected<void*> UsbUtils::get_libusb_context()
{
    auto ctx = load_context();
    CHECK_NOT_NULL(ctx, HAILO_INTERNAL_FAILURE);
    return ctx;
}

} /* namespace hailort */
