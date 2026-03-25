/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file usb_control_listener.cpp
 * @brief USB Control Listener for USB control communication
 **/

#include "hrpc/session_internal/usb/usb_device/usb_control_listener.hpp"
#include "common/utils.hpp"

#include <fcntl.h> 
#include <linux/usb/functionfs.h>

namespace hailort
{
 
static constexpr const char *CONTROL_ENDPOINT_PATH = "/dev/ffs-hailo/ep0";
static constexpr const char *USB_FUNCTIONFS_BASE_PATH = "/dev/ffs-hailo/ep";
static constexpr std::chrono::milliseconds EP0_WAIT_TIMEOUT{2000};
static constexpr std::chrono::milliseconds EP0_POLL_INTERVAL{50};

// USB endpoint numbering: each interface has 2 endpoints (IN and OUT)
// Interface N uses endpoints: (N*2+2) for send endpoint and (N*2+1) for receive endpoint
static constexpr uint8_t USB_ENDPOINTS_PER_INTERFACE = 2;
static constexpr uint8_t USB_ENDPOINT_RECEIVE_OFFSET = 1;
static constexpr uint8_t USB_ENDPOINT_SEND_OFFSET = 2;

UsbControlListener::UsbControlListener()
    : m_control_fd(nullptr), m_is_running(false), m_ffs_config(nullptr), m_event_loop(nullptr)
{
}

static Expected<std::shared_ptr<FileDescriptor>> open_ep0_with_retries()
{
    const auto start = std::chrono::steady_clock::now();
    while (true) {
        int fd = ::open(CONTROL_ENDPOINT_PATH, O_RDWR);
        if (fd >= 0) {
            auto ptr = make_shared_nothrow<FileDescriptor>(fd);
            CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);
            return ptr;
        }

        CHECK(errno != ENOENT, HAILO_DEVICE_NOT_CONNECTED,
            "Failed to open EP0: {} (errno={})", strerror(errno), errno);

        CHECK(std::chrono::steady_clock::now() - start >= EP0_WAIT_TIMEOUT, HAILO_TIMEOUT,
            "Timeout waiting for EP0 to become available: {}", CONTROL_ENDPOINT_PATH);

        std::this_thread::sleep_for(EP0_POLL_INTERVAL);
    }
}

hailo_status UsbControlListener::listen_ep0()
{
    auto status = m_event_loop->loop();
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status UsbControlListener::start_control_listener()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_is_running) {
        return HAILO_SUCCESS;
    }

    TRY(m_control_fd, open_ep0_with_retries());
    m_ffs_config = make_unique_nothrow<UsbFunctionFsConfiguration>(m_control_fd);
    CHECK_NOT_NULL(m_ffs_config, HAILO_OUT_OF_HOST_MEMORY);

    m_event_loop = make_unique_nothrow<UsbEventLoop>(m_control_fd);
    CHECK_NOT_NULL(m_event_loop, HAILO_OUT_OF_HOST_MEMORY);

    auto status = m_ffs_config->write_descriptors_and_strings();
    CHECK_SUCCESS(status, "Failed to write USB descriptors");

    status = m_ffs_config->enable_udc();
    CHECK_SUCCESS(status, "Failed to enable UDC");

    m_is_running = true;
    m_control_thread = std::thread([this]() {
        auto status = listen_ep0();
        if (status != HAILO_SUCCESS) {
            LOGGER__ERROR("Failed to listen to EP0: {}", status);
            m_is_running = false;
        }
    });
    return HAILO_SUCCESS;
}

hailo_status UsbControlListener::stop_control_listener()
{
    m_is_running = false;

    if (m_ffs_config) {
        auto status = m_ffs_config->disable_udc(); 
        if (status != HAILO_SUCCESS) {
            LOGGER__ERROR("Failed to disable UDC: {}", status);
        }
        m_ffs_config.reset();
    }

    if (m_control_thread.joinable()) {
        m_control_thread.join();
    }
    
    m_control_fd.reset();
    m_event_loop.reset();

    return HAILO_SUCCESS;
}

UsbControlListener::~UsbControlListener()
{
    auto status = stop_control_listener();
    if (status != HAILO_SUCCESS) {
        LOGGER__ERROR("Failed to stop control listener. status: {}", status);
    }
}

hailo_status UsbControlListener::set_event_handler(uint32_t port, std::shared_ptr<UsbEventHandler> handler)
{
    return m_event_loop->set_event_handler(port, handler);
}

hailo_status UsbControlListener::remove_event_handler(uint32_t port)
{
    return m_event_loop->remove_event_handler(port);
}

std::string UsbControlListener::get_receive_fd_path(usb_interface_t interface)
{
    const int endpoint_number = static_cast<int>(interface) * USB_ENDPOINTS_PER_INTERFACE + USB_ENDPOINT_RECEIVE_OFFSET;
    return std::string(USB_FUNCTIONFS_BASE_PATH) + std::to_string(endpoint_number);
}

std::string UsbControlListener::get_send_fd_path(usb_interface_t interface)
{
    const int endpoint_number = static_cast<int>(interface) * USB_ENDPOINTS_PER_INTERFACE + USB_ENDPOINT_SEND_OFFSET;
    return std::string(USB_FUNCTIONFS_BASE_PATH) + std::to_string(endpoint_number);
}

} // namespace hailort