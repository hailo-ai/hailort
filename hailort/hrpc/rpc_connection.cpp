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
constexpr size_t MAX_BODY_SIZE(2048);
constexpr size_t MAX_READ_TRANSFERS(2);

Expected<RpcConnection::Params> RpcConnection::Params::create(std::shared_ptr<Session> raw)
{
    auto create_dma_allocator = [raw](size_t pool_size, size_t buffer_size, hailo_dma_buffer_direction_t direction) {
        return PoolAllocator::create_shared(pool_size, buffer_size, [raw, direction](size_t size) {
            return raw->allocate_buffer(size, direction);
        });
    };

    constexpr size_t MESSAGE_SIZE = sizeof(rpc_message_header_t) + MAX_BODY_SIZE;
    TRY(auto write_messages_allocator, create_dma_allocator(PcieSession::MAX_ONGOING_TRANSFERS, MESSAGE_SIZE,
        HAILO_DMA_BUFFER_DIRECTION_H2D));
    TRY(auto read_messages_allocator, create_dma_allocator(MAX_READ_TRANSFERS, MESSAGE_SIZE, HAILO_DMA_BUFFER_DIRECTION_D2H));

    RpcConnection::Params params = { raw, write_messages_allocator, read_messages_allocator };
    return params;
}

Expected<BufferPtr> RpcConnection::allocate_message_buffer()
{
    TRY(auto buffer, m_write_messages_allocator->allocate());
    return buffer;
}

void RpcConnection::fill_message_buffer(BufferPtr buffer, const rpc_message_header_t &header, const MemoryView &body)
{
    auto &dma_header = *reinterpret_cast<rpc_message_header_t*>(buffer->data());
    dma_header.magic = RPC_MESSAGE_MAGIC;
    dma_header.size = static_cast<uint32_t>(body.size());
    dma_header.message_id = header.message_id;
    dma_header.action_id = header.action_id;
    dma_header.status = header.status;
    memcpy(buffer->data() + sizeof(header), body.data(), body.size());
}

Expected<BufferPtr> RpcConnection::read_message()
{
    TRY(auto buffer, m_read_messages_allocator->allocate());
    auto status = m_session->read(buffer->data(), buffer->size());
    if (HAILO_COMMUNICATION_CLOSED == status) {
        return make_unexpected(status);
    }
    CHECK_SUCCESS(status);

    rpc_message_header_t &dma_header = *reinterpret_cast<rpc_message_header_t*>(buffer->data());
    CHECK(RPC_MESSAGE_MAGIC == dma_header.magic, HAILO_INTERNAL_FAILURE, "Invalid magic! {} != {}",
        dma_header.magic, RPC_MESSAGE_MAGIC);
    CHECK(dma_header.size <= MAX_BODY_SIZE, HAILO_INTERNAL_FAILURE, "Invalid size! {} > {}",
        dma_header.size, MAX_BODY_SIZE);

    return buffer;
}

std::tuple<rpc_message_header_t, MemoryView> RpcConnection::parse_message(BufferPtr buffer)
{
    rpc_message_header_t &header = *reinterpret_cast<rpc_message_header_t*>(buffer->data());
    return std::make_tuple(header, MemoryView(buffer->data() + sizeof(rpc_message_header_t), header.size));
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

hailo_status RpcConnection::write_message_async(TransferRequest &&transfer_request)
{
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