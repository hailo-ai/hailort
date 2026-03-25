/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file usb_session_common.cpp
 * @brief USB Hailo Session Common Implementation
 **/

#include "hrpc/session_internal/usb/usb_session.hpp"
#include "common/utils.hpp"
#include "device_common/usb/usb_utils.hpp"
#include "vdma/transfer_common.hpp"

namespace hailort
{

UsbSession::UsbSession(uint16_t port, usb_interface_t interface)
    : Session(port), m_interface(interface)
{}

hailo_status UsbSession::write(const uint8_t *buffer, size_t size, std::chrono::milliseconds timeout)
{
    hailo_status transfer_status = HAILO_UNINITIALIZED;
    auto status = wait_for_write_async_ready(size, timeout);
    CHECK_SUCCESS(status);

    status = Session::write_async(buffer, size, [&] (hailo_status status) {
        {
            std::unique_lock<std::mutex> lock(m_write_mutex);
            assert(HAILO_UNINITIALIZED != status);
            transfer_status = (HAILO_STREAM_ABORT == status) ? HAILO_COMMUNICATION_CLOSED : status;
        }
        m_write_cv.notify_one();
    });
    CHECK_SUCCESS(status);

    std::unique_lock<std::mutex> lock(m_write_mutex);
    bool completed = m_write_cv.wait_for(lock, timeout, [&] {
        return HAILO_UNINITIALIZED != transfer_status;
    });
    
    CHECK(completed, HAILO_TIMEOUT, "Timeout waiting for write completion ({}B)", size);

    return transfer_status;
}

hailo_status UsbSession::read(uint8_t *buffer, size_t size, std::chrono::milliseconds timeout)
{
    hailo_status transfer_status = HAILO_UNINITIALIZED;
    auto status = wait_for_read_async_ready(size, timeout);
    CHECK_SUCCESS(status);

    status = Session::read_async(buffer, size, [&] (hailo_status status) {
        {
            std::unique_lock<std::mutex> lock(m_read_mutex);
            assert(HAILO_UNINITIALIZED != status);
            transfer_status = (HAILO_STREAM_ABORT == status) ? HAILO_COMMUNICATION_CLOSED : status;
        }
        m_read_cv.notify_one();
    });
    CHECK_SUCCESS(status);

    std::unique_lock<std::mutex> lock(m_read_mutex);
    bool completed = m_read_cv.wait_for(lock, timeout, [&] {
        return HAILO_UNINITIALIZED != transfer_status;
    });
    
    CHECK(completed, HAILO_TIMEOUT, "Timeout waiting for read completion ({}B)", size);

    return transfer_status;
}

Expected<int> UsbSession::read_fd()
{
    return make_unexpected(HAILO_NOT_SUPPORTED);
}

Expected<Buffer> UsbSession::allocate_buffer(size_t size, hailo_dma_buffer_direction_t)
{
    // TODO: HRT-20091
    return Buffer::create(size, 0);
}

} // namespace hailort
