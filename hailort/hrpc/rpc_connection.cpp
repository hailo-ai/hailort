/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file rpc_connection.cpp
 * @brief RPC connection implementation
 **/

#include "rpc_connection.hpp"
#include "vdma/pcie_session.hpp"

#include <numeric>

namespace hailort
{

constexpr std::chrono::seconds TRANSFER_TIMEOUT(10);
constexpr size_t READ_RPC_BUFFER_MAX_SIZE(2048);
constexpr size_t MAX_READ_TRANSFERS(2);

Expected<RpcConnection::Params> RpcConnection::Params::create(std::shared_ptr<Session> raw)
{
    auto create_dma_allocator = [raw](size_t pool_size, size_t buffer_size, hailo_dma_buffer_direction_t direction) {
        return PoolAllocator::create_shared(pool_size, buffer_size, [raw, direction](size_t size) {
            return raw->allocate_buffer(size, direction);
        });
    };

    TRY(auto write_rpc_headers_allocator, create_dma_allocator(PcieSession::MAX_ONGOING_TRANSFERS, sizeof(rpc_message_header_t),
        HAILO_DMA_BUFFER_DIRECTION_H2D));
    TRY(auto read_rpc_headers_allocator, create_dma_allocator(MAX_READ_TRANSFERS, sizeof(rpc_message_header_t), HAILO_DMA_BUFFER_DIRECTION_D2H));
    TRY(auto read_rpc_body_allocator, create_dma_allocator(MAX_READ_TRANSFERS, READ_RPC_BUFFER_MAX_SIZE, HAILO_DMA_BUFFER_DIRECTION_D2H));

    RpcConnection::Params params = { raw, write_rpc_headers_allocator, read_rpc_headers_allocator, read_rpc_body_allocator };
    return params;
}

Expected<rpc_message_t> RpcConnection::read_message()
{
    TRY(auto dma_header_ptr, m_read_rpc_headers_allocator->allocate());
    rpc_message_header_t &dma_header = *reinterpret_cast<rpc_message_header_t*>(dma_header_ptr->data());

    auto status = m_session->read(reinterpret_cast<uint8_t*>(&dma_header), sizeof(dma_header));
    if (HAILO_COMMUNICATION_CLOSED == status) {
        return make_unexpected(status);
    }
    CHECK_SUCCESS(status);
    CHECK(RPC_MESSAGE_MAGIC == dma_header.magic, HAILO_INTERNAL_FAILURE, "Invalid magic! {} != {}",
        dma_header.magic, RPC_MESSAGE_MAGIC);
    CHECK(dma_header.size <= READ_RPC_BUFFER_MAX_SIZE, HAILO_INTERNAL_FAILURE, "Invalid size! {} > {}",
        dma_header.size, READ_RPC_BUFFER_MAX_SIZE);

    TRY(auto buffer, m_read_rpc_body_allocator->allocate());
    if (dma_header.size > 0) {
        status = m_session->read(buffer->data(), dma_header.size);
        if (HAILO_COMMUNICATION_CLOSED == status) {
            return make_unexpected(status);
        }
        CHECK_SUCCESS(status);
    }

    rpc_message_t rpc_message = {};
    rpc_message.header = dma_header;
    rpc_message.buffer = std::move(buffer);

    return rpc_message;
}

hailo_status RpcConnection::read_buffer(MemoryView buffer)
{
    return read_buffers({ TransferBuffer(buffer) });
}

hailo_status RpcConnection::read_buffers(std::vector<TransferBuffer> &&buffers)
{
    hailo_status transfer_status = HAILO_UNINITIALIZED;

    const size_t total_size = std::accumulate(buffers.begin(), buffers.end(), size_t{0},
        [] (size_t acc, const TransferBuffer &buffer) { return acc + buffer.size(); });

    auto status = wait_for_read_buffer_async_ready(total_size, TRANSFER_TIMEOUT);
    CHECK_SUCCESS(status);

    TransferRequest transfer_request;
    transfer_request.transfer_buffers = std::move(buffers);
    transfer_request.callback = [&] (hailo_status status) {
        {
            std::unique_lock<std::mutex> lock(m_read_mutex);
            assert(status != HAILO_UNINITIALIZED);
            transfer_status = status;
        }
        m_read_cv.notify_one();
    };

    status = m_session->read_async(std::move(transfer_request));
    if (HAILO_COMMUNICATION_CLOSED == status) {
        return status;
    }
    CHECK_SUCCESS(status);

    std::unique_lock<std::mutex> lock(m_read_mutex);
    CHECK(m_read_cv.wait_for(lock, TRANSFER_TIMEOUT, [&] { return transfer_status != HAILO_UNINITIALIZED; }),
        HAILO_TIMEOUT, "Timeout waiting for transfer completion");

    return transfer_status;
}

#ifdef __linux__

Expected<std::shared_ptr<FileDescriptor>> RpcConnection::read_dmabuf_fd()
{
    TRY(auto fd, m_session->read_fd());

    auto fd_ptr = make_shared_nothrow<FileDescriptor>(fd);
    CHECK_NOT_NULL(fd_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return fd_ptr;
}

#else

Expected<std::shared_ptr<FileDescriptor>> RpcConnection::read_dmabuf_fd()
{
    return make_unexpected(HAILO_NOT_SUPPORTED);
}

#endif

hailo_status RpcConnection::wait_for_write_message_async_ready(size_t buffer_size, std::chrono::milliseconds timeout)
{
    return m_session->wait_for_write_async_ready(sizeof(rpc_message_header_t) + buffer_size, timeout);
}

hailo_status RpcConnection::write_message_async(const rpc_message_header_t &header, const MemoryView &buffer,
    std::function<void(hailo_status)> &&callback)
{
    TransferRequest transfer_request;
    transfer_request.callback = std::move(callback);
    if (buffer.size() > 0) {
        transfer_request.transfer_buffers.emplace_back(buffer);
    }

    return write_message_async(header, std::move(transfer_request));
}

hailo_status RpcConnection::write_message_async(const rpc_message_header_t &header, TransferRequest &&transfer_request)
{
    TRY(auto dma_header_ptr, m_write_rpc_headers_allocator->allocate());
    rpc_message_header_t &dma_header = *reinterpret_cast<rpc_message_header_t*>(dma_header_ptr->data());
    memcpy(&dma_header, &header, sizeof(header));
    dma_header.magic = RPC_MESSAGE_MAGIC;

    // Insert the dma_header before all other buffers
    transfer_request.transfer_buffers.insert(transfer_request.transfer_buffers.begin(),
        MemoryView(reinterpret_cast<uint8_t*>(&dma_header), sizeof(dma_header)));

    // Callback should capture the dma_header_ptr
    transfer_request.callback = [dma_header_ptr,
                                 original_callback=transfer_request.callback](hailo_status status) mutable {
        dma_header_ptr.reset();
        original_callback(status);
    };

    return m_session->write_async(std::move(transfer_request));
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

    m_is_active = false;
    return status;
}

bool RpcConnection::is_closed()
{
    return !m_is_active;
}

} // namespace hailort