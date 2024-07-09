/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file raw_connection_internal.cpp
 * @brief PCIE Raw Connection
 **/

#include "hrpc/os/pcie/raw_connection_internal.hpp"
#include "common/logger_macros.hpp"
#include "common/utils.hpp"
#include "hailo/hailort.h"
#include "vdma/driver/hailort_driver.hpp"

// TODO: Remove this after we can choose ports in the driver
#define PCIE_PORT (1213355091)

using namespace hrpc;

Expected<std::shared_ptr<ConnectionContext>> PcieConnectionContext::create_shared(bool is_accepting)
{
    const auto max_size = PcieSession::max_transfer_size();
    TRY(auto write_buffer, Buffer::create(static_cast<size_t>(max_size), BufferStorageParams::create_dma()));
    TRY(auto read_buffer, Buffer::create(static_cast<size_t>(max_size), BufferStorageParams::create_dma()));

    std::shared_ptr<PcieConnectionContext> ptr = nullptr;
    if (is_accepting) {
        // Server side
        TRY(auto driver, HailoRTDriver::create_pcie_ep());
        ptr = make_shared_nothrow<PcieConnectionContext>(std::move(driver), is_accepting,
            std::move(write_buffer), std::move(read_buffer));
        CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);
        return std::dynamic_pointer_cast<ConnectionContext>(ptr);
    } else {
        // Client side
        TRY(auto device_infos, HailoRTDriver::scan_devices());
        CHECK(device_infos.size() > 0, HAILO_NOT_FOUND, "No devices found");
        for (auto &device_info : device_infos) {
            if (HailoRTDriver::AcceleratorType::SOC_ACCELERATOR == device_info.accelerator_type) {
                TRY(auto driver, HailoRTDriver::create(device_info.device_id, device_info.dev_path));
                ptr = make_shared_nothrow<PcieConnectionContext>(std::move(driver), is_accepting,
                    std::move(write_buffer), std::move(read_buffer));
                CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);
                return std::dynamic_pointer_cast<ConnectionContext>(ptr);
            }
        }
    }
    LOGGER__ERROR("No suitable device found");
    return make_unexpected(HAILO_NOT_FOUND);
}

hailo_status PcieConnectionContext::wait_for_available_connection()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    bool was_successful = m_cv.wait_for(lock, std::chrono::milliseconds(HAILO_INFINITE), [this] () -> bool {
        return (m_conn_count == 0);
    });
    CHECK(was_successful, HAILO_TIMEOUT, "Got timeout in accept");

    m_conn_count++;
    return HAILO_SUCCESS;
}

void PcieConnectionContext::mark_connection_closed()
{
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_conn_count--;
    }
    m_cv.notify_one();
}

Expected<std::shared_ptr<RawConnection>> PcieRawConnection::create_shared(std::shared_ptr<PcieConnectionContext> context)
{
    auto ptr = make_shared_nothrow<PcieRawConnection>(context);
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY);
    return std::dynamic_pointer_cast<RawConnection>(ptr);
}

Expected<std::shared_ptr<RawConnection>> PcieRawConnection::accept()
{
    auto status = m_context->wait_for_available_connection();
    CHECK_SUCCESS(status);

    auto new_conn = make_shared_nothrow<PcieRawConnection>(m_context);
    CHECK_NOT_NULL_AS_EXPECTED(new_conn, HAILO_OUT_OF_HOST_MEMORY);

    TRY(auto session, PcieSession::accept(m_context->driver(), PCIE_PORT));
    status = new_conn->set_session(std::move(session));
    CHECK_SUCCESS(status);

    return std::dynamic_pointer_cast<RawConnection>(new_conn);
}

hailo_status PcieRawConnection::set_session(PcieSession &&session)
{
    m_session = make_shared_nothrow<PcieSession>(std::move(session));
    CHECK_NOT_NULL(m_session, HAILO_OUT_OF_HOST_MEMORY);

    return HAILO_SUCCESS;
}

hailo_status PcieRawConnection::connect()
{
    TRY(auto session, PcieSession::connect(m_context->driver(), PCIE_PORT));
    auto status = set_session(std::move(session));
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status PcieRawConnection::write(const uint8_t *buffer, size_t size)
{
    if (0 == size) {
        return HAILO_SUCCESS;
    }

    const auto alignment = OsUtils::get_dma_able_alignment();
    const auto max_size = PcieSession::max_transfer_size();
    bool is_aligned = ((reinterpret_cast<uintptr_t>(buffer) % alignment )== 0);

    size_t bytes_written = 0;
    while (bytes_written < size) {
        size_t amount_to_write = 0;
        auto size_left = size - bytes_written;
        if (is_aligned) {
            amount_to_write = std::min(static_cast<size_t>(size_left), static_cast<size_t>(max_size));
            auto status = m_session->write(buffer + bytes_written, amount_to_write, m_timeout);
            if (HAILO_STREAM_ABORT == status) {
                return HAILO_COMMUNICATION_CLOSED;
            }
            CHECK_SUCCESS(status);
        } else {
            amount_to_write = std::min(static_cast<size_t>(size_left), m_context->write_buffer().size());
            memcpy(m_context->write_buffer().data(), buffer + bytes_written, amount_to_write);
            auto status = m_session->write(m_context->write_buffer().data(), amount_to_write, m_timeout);
            if (HAILO_STREAM_ABORT == status) {
                return HAILO_COMMUNICATION_CLOSED;
            }
            CHECK_SUCCESS(status);
        }

        bytes_written += amount_to_write;
    }

    return HAILO_SUCCESS;
}

hailo_status PcieRawConnection::read(uint8_t *buffer, size_t size)
{
    if (0 == size) {
        return HAILO_SUCCESS;
    }

    const auto alignment = OsUtils::get_dma_able_alignment();
    const auto max_size = PcieSession::max_transfer_size();
    bool is_aligned = ((reinterpret_cast<uintptr_t>(buffer) % alignment) == 0);

    size_t bytes_read = 0;
    while (bytes_read < size) {
        size_t amount_to_read = 0;
        auto size_left = size - bytes_read;
        if (is_aligned) {
            amount_to_read = std::min(static_cast<size_t>(size_left), static_cast<size_t>(max_size));
            auto status = m_session->read(buffer + bytes_read, amount_to_read, m_timeout);
            if (HAILO_STREAM_ABORT == status) {
                return HAILO_COMMUNICATION_CLOSED;
            }
            CHECK_SUCCESS(status);
        } else {
            amount_to_read = std::min(static_cast<size_t>(size_left), m_context->read_buffer().size());
            auto status = m_session->read(m_context->read_buffer().data(), amount_to_read, m_timeout);
            if (HAILO_STREAM_ABORT == status) {
                return HAILO_COMMUNICATION_CLOSED;
            }
            CHECK_SUCCESS(status);

            memcpy(buffer + bytes_read, m_context->read_buffer().data(), amount_to_read);
        }

        bytes_read += amount_to_read;
    }

    return HAILO_SUCCESS;
}

hailo_status PcieRawConnection::close()
{
    auto status = m_session->close();
    CHECK_SUCCESS(status);

    m_context->mark_connection_closed();

    return HAILO_SUCCESS;
}