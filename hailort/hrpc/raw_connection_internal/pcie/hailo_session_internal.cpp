/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file hailo_session_internal.cpp
 * @brief PCIE Hailo Session
 **/

#include "hrpc/raw_connection_internal/pcie/hailo_session_internal.hpp"
#include "common/logger_macros.hpp"
#include "common/utils.hpp"
#include "common/internal_env_vars.hpp"
#include "hailo/hailort.h"
#include "vdma/driver/hailort_driver.hpp"

#define TRANSFER_TIMEOUT (std::chrono::seconds(10))

namespace hailort
{

Expected<std::shared_ptr<ConnectionContext>> PcieConnectionContext::create_client_shared(const std::string &device_id)
{
    if (device_id.size() > 0) {
        TRY(auto driver, HailoRTDriver::create_pcie(device_id));
        auto ptr = make_shared_nothrow<PcieConnectionContext>(std::move(driver), false);
        CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);
        return std::dynamic_pointer_cast<ConnectionContext>(ptr);
    }

    TRY(auto device_infos, HailoRTDriver::scan_devices(HailoRTDriver::AcceleratorType::SOC_ACCELERATOR));
    CHECK(device_infos.size() > 0, HAILO_NOT_FOUND, "No devices found");

    TRY(auto driver, HailoRTDriver::create(device_infos[0].device_id, device_infos[0].dev_path));
    auto ptr = make_shared_nothrow<PcieConnectionContext>(std::move(driver), false);
    CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);
    return std::dynamic_pointer_cast<ConnectionContext>(ptr);
}

Expected<std::shared_ptr<ConnectionContext>> PcieConnectionContext::create_server_shared()
{
    TRY(auto driver, HailoRTDriver::create_pcie_ep());
    auto ptr = make_shared_nothrow<PcieConnectionContext>(std::move(driver), true);
    CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);
    return std::dynamic_pointer_cast<ConnectionContext>(ptr);
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
    if (0 == m_conn_count) return; // In case number of connections is 0 - no need to mark as closed
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_conn_count--;
    }
    m_cv.notify_one();
}

Expected<std::shared_ptr<RawPcieListener>> RawPcieListener::create_shared(std::shared_ptr<PcieConnectionContext> context, uint16_t port)
{
    auto ptr = make_shared_nothrow<RawPcieListener>(context, port);
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY);

    return ptr;
}

Expected<std::shared_ptr<Session>> RawPcieListener::accept()
{
    auto status = m_context->wait_for_available_connection();
    CHECK_SUCCESS(status);

    auto new_conn = make_shared_nothrow<RawPcieSession>(m_context);
    CHECK_NOT_NULL_AS_EXPECTED(new_conn, HAILO_OUT_OF_HOST_MEMORY);

    TRY(auto session, PcieSession::accept(m_context->get_driver(), m_port));
    status = new_conn->set_session(std::move(session));
    CHECK_SUCCESS(status);

    return std::dynamic_pointer_cast<Session>(new_conn);
}

RawPcieSession::~RawPcieSession()
{
    close();
}

Expected<std::shared_ptr<RawPcieSession>> RawPcieSession::connect(std::shared_ptr<PcieConnectionContext> context, uint16_t port)
{
    auto ptr = std::make_shared<RawPcieSession>(context);

    auto status = ptr->connect(port);
    CHECK_SUCCESS(status);

    return ptr;
}

hailo_status RawPcieSession::connect(uint16_t port)
{
    TRY(auto session, PcieSession::connect(m_context->get_driver(), port));
    return set_session(std::move(session));
}

hailo_status RawPcieSession::write(const uint8_t *buffer, size_t size, std::chrono::milliseconds timeout)
{
    hailo_status transfer_status = HAILO_UNINITIALIZED;

    auto status = write_async(buffer, size, [&] (hailo_status status) {
        {
            std::unique_lock<std::mutex> lock(m_write_mutex);
            assert(status != HAILO_UNINITIALIZED);
            transfer_status = status;
        }
        m_write_cv.notify_one();
    });
    if (HAILO_COMMUNICATION_CLOSED == status) {
        return status;
    }
    CHECK_SUCCESS(status);

    std::unique_lock<std::mutex> lock(m_write_mutex);
    CHECK(m_write_cv.wait_for(lock, timeout, [&] { return transfer_status != HAILO_UNINITIALIZED; }),
        HAILO_TIMEOUT, "Timeout waiting for transfer completion");

    return transfer_status;
}

hailo_status RawPcieSession::read(uint8_t *buffer, size_t size, std::chrono::milliseconds timeout)
{
    hailo_status transfer_status = HAILO_UNINITIALIZED;

    auto status = read_async(buffer, size, [&] (hailo_status status) {
        {
            std::unique_lock<std::mutex> lock(m_read_mutex);
            assert(status != HAILO_UNINITIALIZED);
            transfer_status = status;
        }
        m_read_cv.notify_one();
    });
    if (HAILO_COMMUNICATION_CLOSED == status) {
        return status;
    }
    CHECK_SUCCESS(status);

    std::unique_lock<std::mutex> lock(m_read_mutex);
    CHECK(m_read_cv.wait_for(lock, timeout, [&] { return transfer_status != HAILO_UNINITIALIZED; }),
        HAILO_TIMEOUT, "Timeout waiting for transfer completion");

    return transfer_status;
}

hailo_status RawPcieSession::close()
{
    if (m_session != nullptr) {
        auto status = m_session->close();
        CHECK_SUCCESS(status);
    }

    m_context->mark_connection_closed();

    {
        std::unique_lock<std::mutex> lock(m_ongoing_writes_mutex);
        m_ongoing_writes = 0;
        m_ongoing_writes_cv.notify_all();
    }

    {
        std::unique_lock<std::mutex> lock(m_ongoing_reads_mutex);
        m_ongoing_reads = 0;
        m_ongoing_reads_cv.notify_all();
    }

    return HAILO_SUCCESS;
}

hailo_status RawPcieSession::wait_for_write_async_ready(size_t transfer_size, std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(m_ongoing_writes_mutex);
    CHECK(m_ongoing_writes_cv.wait_for(lock, timeout, [this, transfer_size] () {
        return m_session->is_write_ready(transfer_size);
    }), HAILO_TIMEOUT, "Timeout waiting for transfer ready");
    return HAILO_SUCCESS;
}

hailo_status RawPcieSession::write_async(const uint8_t *buffer, size_t size, std::function<void(hailo_status)> &&callback)
{
    if (0 == size) {
        callback(HAILO_SUCCESS);
        return HAILO_SUCCESS;
    }

    bool is_aligned = ((reinterpret_cast<uintptr_t>(buffer) % OsUtils::get_dma_able_alignment()) == 0);
    if (is_aligned) {
        auto status = write_async_aligned(buffer, size, std::move(callback));
        CHECK_SUCCESS(status);
    } else {
        auto status = write_async_unaligned(buffer, size, std::move(callback));
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status RawPcieSession::write_async_aligned(const uint8_t *buffer, size_t size, std::function<void(hailo_status)> &&callback)
{
    std::unique_lock<std::mutex> lock(m_ongoing_writes_mutex);
    auto status = m_session->write_async(buffer, size, [this, callback] (hailo_status status) {
        if (HAILO_STREAM_ABORT == status) {
            callback(HAILO_COMMUNICATION_CLOSED);
            return;
        }
        callback(status);

        std::unique_lock<std::mutex> lock(m_ongoing_writes_mutex);
        m_ongoing_writes--;
        m_ongoing_writes_cv.notify_all();
    });
    if (HAILO_STREAM_ABORT == status) {
        return HAILO_COMMUNICATION_CLOSED;
    }
    CHECK_SUCCESS(status);

    m_ongoing_writes++;
    return HAILO_SUCCESS;
}

hailo_status RawPcieSession::write_async_unaligned(const uint8_t *buffer, size_t size, std::function<void(hailo_status)> &&callback)
{
    TRY(auto aligned_buffer, Buffer::create_shared(buffer, size, BufferStorageParams::create_dma()));
    auto status = write_async_aligned(aligned_buffer->data(), aligned_buffer->size(),
    [callback, aligned_buffer] (hailo_status status) {
        (void)aligned_buffer; // Avoid compiler optimization
        callback(status);
    });
    if (HAILO_COMMUNICATION_CLOSED == status) {
        return HAILO_COMMUNICATION_CLOSED;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status RawPcieSession::wait_for_read_async_ready(size_t transfer_size, std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(m_ongoing_reads_mutex);
    CHECK(m_ongoing_reads_cv.wait_for(lock, timeout, [this, transfer_size] () {
        return m_session->is_read_ready(transfer_size);
    }), HAILO_TIMEOUT, "Timeout waiting for transfer ready");
    return HAILO_SUCCESS;
}

hailo_status RawPcieSession::read_async(uint8_t *buffer, size_t size, std::function<void(hailo_status)> &&callback)
{
    if (0 == size) {
        callback(HAILO_SUCCESS);
        return HAILO_SUCCESS;
    }

    bool is_aligned = ((reinterpret_cast<uintptr_t>(buffer) % OsUtils::get_dma_able_alignment()) == 0);
    if (is_aligned) {
        auto status = read_async_aligned(buffer, size, std::move(callback));
        if (HAILO_COMMUNICATION_CLOSED == status) {
            return HAILO_COMMUNICATION_CLOSED;
        }
        CHECK_SUCCESS(status);
    } else {
        auto status = read_async_unaligned(buffer, size, std::move(callback));
        if (HAILO_COMMUNICATION_CLOSED == status) {
            return HAILO_COMMUNICATION_CLOSED;
        }
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status RawPcieSession::read_async_aligned(uint8_t *buffer, size_t size, std::function<void(hailo_status)> &&callback)
{
    std::unique_lock<std::mutex> lock(m_ongoing_reads_mutex);
    auto status = m_session->read_async(buffer, size, [this, callback] (hailo_status status) {
        if (HAILO_STREAM_ABORT == status) {
            callback(HAILO_COMMUNICATION_CLOSED);
            return;
        }
        callback(status);

        std::unique_lock<std::mutex> lock(m_ongoing_reads_mutex);
        m_ongoing_reads--;
        m_ongoing_reads_cv.notify_all();
    });
    if ((HAILO_STREAM_ABORT == status) || (HAILO_STREAM_NOT_ACTIVATED == status)) {
        return HAILO_COMMUNICATION_CLOSED;
    }
    CHECK_SUCCESS(status);

    m_ongoing_reads++;
    return HAILO_SUCCESS;
}

hailo_status RawPcieSession::read_async_unaligned(uint8_t *buffer, size_t size, std::function<void(hailo_status)> &&callback)
{
    TRY(auto aligned_buffer, Buffer::create_shared(size, BufferStorageParams::create_dma()));
    auto status = read_async_aligned(aligned_buffer->data(), aligned_buffer->size(),
    [buffer, aligned_buffer, callback] (hailo_status status) {
        if (HAILO_SUCCESS == status) {
            memcpy(buffer, aligned_buffer->data(), aligned_buffer->size());
        }
        callback(status);
    });
    if (HAILO_COMMUNICATION_CLOSED == status) {
        return HAILO_COMMUNICATION_CLOSED;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status RawPcieSession::set_session(PcieSession &&session)
{
    m_session = make_shared_nothrow<PcieSession>(std::move(session));
    CHECK_NOT_NULL(m_session, HAILO_OUT_OF_HOST_MEMORY);

    return HAILO_SUCCESS;
}

} // namespace hailort