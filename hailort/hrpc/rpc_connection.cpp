/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file rpc_connection.cpp
 * @brief RPC connection implementation
 **/

#include "rpc_connection.hpp"
#include "vdma/pcie_session.hpp"

namespace hailort
{

#define TRANSFER_TIMEOUT std::chrono::seconds(10)

Expected<RpcConnection> RpcConnection::create(std::shared_ptr<Session> raw)
{
    TRY(auto shutdown_event, Event::create_shared(Event::State::not_signalled));
    TRY(auto write_rpc_headers, DmaAbleBufferPool::create_shared(sizeof(rpc_message_header_t),
        PcieSession::MAX_ONGOING_TRANSFERS, shutdown_event));
    TRY(auto read_rpc_headers, DmaAbleBufferPool::create_shared(sizeof(rpc_message_header_t),
        PcieSession::MAX_ONGOING_TRANSFERS, shutdown_event));

    auto read_mutex = make_shared_nothrow<std::mutex>();
    CHECK_NOT_NULL(read_mutex, HAILO_OUT_OF_HOST_MEMORY);

    auto write_mutex = make_shared_nothrow<std::mutex>();
    CHECK_NOT_NULL(write_mutex, HAILO_OUT_OF_HOST_MEMORY);

    auto read_cv = make_shared_nothrow<std::condition_variable>();
    CHECK_NOT_NULL(read_cv, HAILO_OUT_OF_HOST_MEMORY);

    auto write_cv = make_shared_nothrow<std::condition_variable>();
    CHECK_NOT_NULL(write_cv, HAILO_OUT_OF_HOST_MEMORY);

    return RpcConnection(raw, write_rpc_headers, read_rpc_headers, shutdown_event, read_mutex, write_mutex, read_cv, write_cv);
}

hailo_status RpcConnection::write_message(const rpc_message_header_t &header, const MemoryView &buffer)
{
    hailo_status transfer_status = HAILO_UNINITIALIZED;

    auto status = wait_for_write_message_async_ready(buffer.size(), TRANSFER_TIMEOUT);
    CHECK_SUCCESS(status);

    status = write_message_async(header, buffer, [&] (hailo_status status) {
        {
            std::unique_lock<std::mutex> lock(*m_write_mutex);
            assert(status != HAILO_UNINITIALIZED);
            transfer_status = status;
        }
        m_write_cv->notify_one();
    });
    if (HAILO_COMMUNICATION_CLOSED == status) {
        return status;
    }
    CHECK_SUCCESS(status);

    std::unique_lock<std::mutex> lock(*m_write_mutex);
    CHECK(m_write_cv->wait_for(lock, TRANSFER_TIMEOUT, [&] { return transfer_status != HAILO_UNINITIALIZED; }),
        HAILO_TIMEOUT, "Timeout waiting for transfer completion");

    return transfer_status;
}

Expected<rpc_message_t> RpcConnection::read_message()
{
    auto expected_dma_header_ptr = m_read_rpc_headers->acquire_buffer();
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == expected_dma_header_ptr.status()) {
        return make_unexpected(HAILO_COMMUNICATION_CLOSED);
    }
    CHECK_EXPECTED(expected_dma_header_ptr);

    auto dma_header_ptr = expected_dma_header_ptr.release();
    rpc_message_header_t &dma_header = *reinterpret_cast<rpc_message_header_t*>(dma_header_ptr->data());

    auto status = m_session->read(reinterpret_cast<uint8_t*>(&dma_header), sizeof(dma_header));
    if (HAILO_COMMUNICATION_CLOSED == status) {
        return make_unexpected(status);
    }
    CHECK_SUCCESS(status);
    CHECK(RPC_MESSAGE_MAGIC == dma_header.magic, HAILO_INTERNAL_FAILURE, "Invalid magic! {} != {}",
        dma_header.magic, RPC_MESSAGE_MAGIC);

    TRY(auto buffer, Buffer::create(dma_header.size, BufferStorageParams::create_dma()));
    status = m_session->read(buffer.data(), buffer.size());
    if (HAILO_COMMUNICATION_CLOSED == status) {
        return make_unexpected(status);
    }
    CHECK_SUCCESS(status);

    rpc_message_t rpc_message = {};
    rpc_message.header = dma_header;
    rpc_message.buffer = std::move(buffer);

    status = m_read_rpc_headers->return_to_pool(dma_header_ptr);
    CHECK_SUCCESS(status);

    return rpc_message;
}

hailo_status RpcConnection::write_buffer(const MemoryView &buffer)
{
    hailo_status transfer_status = HAILO_UNINITIALIZED;

    auto status = wait_for_write_buffer_async_ready(buffer.size(), TRANSFER_TIMEOUT);
    CHECK_SUCCESS(status);

    status = write_buffer_async(buffer, [&] (hailo_status status) {
        {
            std::unique_lock<std::mutex> lock(*m_write_mutex);
            assert(status != HAILO_UNINITIALIZED);
            transfer_status = status;
        }
        m_write_cv->notify_one();
    });
    if (HAILO_COMMUNICATION_CLOSED == status) {
        return status;
    }
    CHECK_SUCCESS(status);

    std::unique_lock<std::mutex> lock(*m_write_mutex);
    CHECK(m_write_cv->wait_for(lock, TRANSFER_TIMEOUT, [&] { return transfer_status != HAILO_UNINITIALIZED; }),
        HAILO_TIMEOUT, "Timeout waiting for transfer completion");

    return transfer_status;
}

hailo_status RpcConnection::read_buffer(MemoryView buffer)
{
    hailo_status transfer_status = HAILO_UNINITIALIZED;

    auto status = wait_for_read_buffer_async_ready(buffer.size(), TRANSFER_TIMEOUT);
    CHECK_SUCCESS(status);

    status = read_buffer_async(buffer, [&] (hailo_status status) {
        {
            std::unique_lock<std::mutex> lock(*m_read_mutex);
            assert(status != HAILO_UNINITIALIZED);
            transfer_status = status;
        }
        m_read_cv->notify_one();
    });
    if (HAILO_COMMUNICATION_CLOSED == status) {
        return status;
    }
    CHECK_SUCCESS(status);

    std::unique_lock<std::mutex> lock(*m_read_mutex);
    CHECK(m_read_cv->wait_for(lock, TRANSFER_TIMEOUT, [&] { return transfer_status != HAILO_UNINITIALIZED; }),
        HAILO_TIMEOUT, "Timeout waiting for transfer completion");

    return transfer_status;
}

hailo_status RpcConnection::wait_for_write_message_async_ready(size_t buffer_size, std::chrono::milliseconds timeout)
{
    return m_session->wait_for_write_async_ready(sizeof(rpc_message_header_t) + buffer_size, timeout);
}

hailo_status RpcConnection::write_message_async(const rpc_message_header_t &header, const MemoryView &buffer,
    std::function<void(hailo_status)> &&callback)
{
    auto expected_dma_header_ptr = m_write_rpc_headers->acquire_buffer();
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == expected_dma_header_ptr.status()) {
        return HAILO_COMMUNICATION_CLOSED;
    }
    CHECK_EXPECTED(expected_dma_header_ptr);

    auto dma_header_ptr = expected_dma_header_ptr.release();
    rpc_message_header_t &dma_header = *reinterpret_cast<rpc_message_header_t*>(dma_header_ptr->data());
    memcpy(&dma_header, &header, sizeof(header));

    dma_header.magic = RPC_MESSAGE_MAGIC;
    auto status = m_session->write_async(reinterpret_cast<const uint8_t*>(&dma_header), sizeof(dma_header),
    [write_rpc_headers = m_write_rpc_headers, dma_header_ptr] (hailo_status status) {
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to write header, status = {}", status);
        }

        status = write_rpc_headers->return_to_pool(dma_header_ptr);
        if (HAILO_SUCCESS != status) {
            LOGGER__CRITICAL("Could not return buffer to pool! status = {}", status);
        }
    });
    if (HAILO_COMMUNICATION_CLOSED == status) {
        return status;
    }
    CHECK_SUCCESS(status);

    status = m_session->write_async(buffer.data(), dma_header.size, std::move(callback));
    if (HAILO_COMMUNICATION_CLOSED == status) {
        return status;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status RpcConnection::wait_for_write_buffer_async_ready(size_t buffer_size, std::chrono::milliseconds timeout)
{
    return m_session->wait_for_write_async_ready(buffer_size, timeout);
}

hailo_status RpcConnection::write_buffer_async(const MemoryView &buffer, std::function<void(hailo_status)> &&callback)
{
    auto status = m_session->write_async(buffer.data(), buffer.size(), std::move(callback));
    if (HAILO_COMMUNICATION_CLOSED == status) {
        return status;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status RpcConnection::wait_for_read_buffer_async_ready(size_t buffer_size, std::chrono::milliseconds timeout)
{
    return m_session->wait_for_read_async_ready(buffer_size, timeout);
}

hailo_status RpcConnection::read_buffer_async(MemoryView buffer, std::function<void(hailo_status)> &&callback)
{
    auto status = m_session->read_async(buffer.data(), buffer.size(), std::move(callback));
    if (HAILO_COMMUNICATION_CLOSED == status) {
        return make_unexpected(status);
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status RpcConnection::close()
{
    hailo_status status = HAILO_UNINITIALIZED;
    if (m_session != nullptr) {
        status = m_session->close();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to close session, status = {}", status);
        }
    }

    if (m_shutdown_event != nullptr) {
        status = m_shutdown_event->signal();
        if (HAILO_SUCCESS != status) {
            LOGGER__CRITICAL("Failed to signal shutdown event, status = {}", status);
        }
    }

    return status;
}

} // namespace hailort