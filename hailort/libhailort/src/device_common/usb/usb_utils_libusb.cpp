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
#include "usb_loader_protocol.h"

#include <libusb.h>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <utility>

namespace hailort
{

constexpr auto HAILO_USB_PRODUCT_NAME = HAILO_USB_FUNCTIONAL_PRODUCT_NAME;
constexpr size_t MAX_PRODUCT_NAME_LENGTH = 256;

Expected<std::shared_ptr<LibusbDeviceHandle>> LibusbDeviceHandle::create(libusb_device *device)
{
    libusb_device_handle *handle = nullptr;
    const auto ret = libusb_open(device, &handle);
    CHECK(0 == ret, HAILO_LIBUSB_FAILURE, "Failed to open USB device, libusb error: {}",
        libusb_error_name(ret));

    auto ptr = make_shared_nothrow<LibusbDeviceHandle>(handle);
    if (nullptr == ptr) {
        libusb_close(handle);
        return make_unexpected(HAILO_OUT_OF_HOST_MEMORY);
    }
    return ptr;
}

LibusbDeviceHandle::LibusbDeviceHandle(libusb_device_handle *handle)
    : m_handle(handle)
{}

LibusbDeviceHandle::~LibusbDeviceHandle()
{
    if (nullptr != m_handle) {
        libusb_close(m_handle);
        m_handle = nullptr;
    }
}

LibusbHandleRegistry &LibusbHandleRegistry::instance()
{
    static LibusbHandleRegistry s_instance;
    return s_instance;
}

Expected<std::shared_ptr<LibusbDeviceHandle>> LibusbHandleRegistry::get_or_open(
    const hailo_usb_device_info_t &usb_info,
    const std::function<Expected<std::shared_ptr<LibusbDeviceHandle>>()> &opener)
{
    std::lock_guard<std::mutex> registry_lock(m_mutex);
    const auto existing = m_open_handles.find(usb_info);
    if (existing != m_open_handles.end()) {
        if (auto shared = existing->second.lock()) {
            return shared;
        }
    }

    TRY(auto handle, opener());
    m_open_handles[usb_info] = handle;
    return handle;
}

bool LibusbHandleRegistry::UsbDeviceInfoLess::operator()(
    const hailo_usb_device_info_t &a, const hailo_usb_device_info_t &b) const
{
    return std::tie(a.bus, a.device_address) < std::tie(b.bus, b.device_address);
}

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

Expected<std::shared_ptr<LibusbDeviceHandle>> UsbUtils::open_usb_device(const hailo_usb_device_info_t &usb_info)
{
    auto ctx = load_context();
    CHECK_NOT_NULL(ctx, HAILO_INTERNAL_FAILURE);

    return LibusbHandleRegistry::instance().get_or_open(usb_info,
        [&]() -> Expected<std::shared_ptr<LibusbDeviceHandle>> {
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

                    LOGGER__DEBUG("Opening new libusb handle for bus={} addr={}", usb_info.bus, usb_info.device_address);
                    TRY(auto handle, LibusbDeviceHandle::create(device));
                    return handle;
                }
            }

            LOGGER__ERROR("Failed to find USB device with bus {} and device address {}", usb_info.bus, usb_info.device_address);
            return make_unexpected(HAILO_OUT_OF_PHYSICAL_DEVICES);
        });
}

Expected<void*> UsbUtils::get_libusb_context()
{
    auto ctx = load_context();
    CHECK_NOT_NULL(ctx, HAILO_INTERNAL_FAILURE);
    return ctx;
}

} /* namespace hailort */
