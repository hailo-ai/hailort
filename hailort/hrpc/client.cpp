/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file client.hpp
 * @brief RPC Client
 **/

#include "client.hpp"
#include "common/internal_env_vars.hpp"
#include "vdma/pcie_session.hpp" // TODO: Remove include: (HRT-16534)

namespace hailort
{
constexpr size_t REQUEST_PROTO_MAX_SIZE (256); // TODO: HRT-16644 - make it dynamic

inline static std::chrono::milliseconds get_request_timeout(const std::chrono::milliseconds default_timeout)
{
    auto timeout_seconds = get_env_variable(HAILO_REQUEST_TIMEOUT_SECONDS);
    if (timeout_seconds) {
        return std::chrono::seconds(std::stoi(timeout_seconds.value()));
    }
    return default_timeout;
}

inline static void request_sent_callback(hailo_status status) {
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to send request, status = {}", status);
    }
}

Client::~Client()
{
    m_is_running = false;
    if (m_connection) {
        (void)m_connection->close();
    }
    if (m_message_loop.joinable()) {
        m_message_loop.join();
    }
}

// TODO: Connect should be a static method that returns a client
hailo_status Client::connect(bool is_localhost)
{
    m_callback_dispatcher_manager = make_shared_nothrow<ClientCallbackDispatcherManager>();
    CHECK_NOT_NULL(m_callback_dispatcher_manager, HAILO_OUT_OF_HOST_MEMORY);

    std::string device_id = is_localhost ? SERVER_ADDR_USE_UNIX_SOCKET : m_device_id;
    TRY(m_conn_context, ConnectionContext::create_client_shared(device_id));
    TRY(auto conn, Session::connect(m_conn_context, HAILORT_SERVER_PORT));

    // TODO: Use conn.max_ongoing_transfers() function (HRT-16534)
    TRY(m_pool_allocator, PoolAllocator::create_shared(PcieSession::MAX_ONGOING_TRANSFERS, REQUEST_PROTO_MAX_SIZE,
        [conn] (size_t size) { return conn->allocate_buffer(size, HAILO_DMA_BUFFER_DIRECTION_H2D); }
    ));

    TRY(auto connection_params, RpcConnection::Params::create(conn));
    m_connection = make_shared_nothrow<RpcConnection>(std::move(connection_params));
    CHECK_NOT_NULL(m_connection, HAILO_OUT_OF_HOST_MEMORY);

    m_message_loop = std::thread([this] {
        auto status = message_loop();
        if ((HAILO_SUCCESS != status) && (HAILO_COMMUNICATION_CLOSED != status)) {
            LOGGER__ERROR("Error in message loop: {}", status);
        }

        m_is_running = false;

        // Notify all waiting threads when the message loop closes.
        rpc_message_header_t status_header {};
        status_header.status = HAILO_COMMUNICATION_CLOSED;
        m_reply_data.for_each([status_header] (reply_data_t reply_data) {
            reply_data.callback({status_header, {}});
        });
    });
    return HAILO_SUCCESS;
}

hailo_status Client::message_loop()
{
    while (m_is_running) {
        TRY_WITH_ACCEPTABLE_STATUS(HAILO_COMMUNICATION_CLOSED, auto message, m_connection->read_message());

        if (HailoRpcActionID::NOTIFICATION == static_cast<HailoRpcActionID>(message.header.action_id)) {
            auto status = m_notification_callback(MemoryView(message.buffer->data(), message.header.size));
            CHECK_SUCCESS(status);
        }

        auto pop_result = m_reply_data.pop(message.header.message_id);
        if (!pop_result.first) {
            continue;
        }
        auto reply_data = pop_result.second;

        // Read additional buffers only if message returned success status.
        if ((HAILO_SUCCESS == message.header.status) && (reply_data.read_buffers.size() > 0)) {
            auto status = m_connection->read_buffers(std::move(reply_data.read_buffers));
            CHECK_SUCCESS_WITH_ACCEPTABLE_STATUS(HAILO_COMMUNICATION_CLOSED, status);
        }

        reply_data.callback(std::move(message));
    }

    return HAILO_SUCCESS;
}

Expected<rpc_message_t> Client::execute_request(uint32_t action_id, const MemoryView &request,
    std::vector<TransferBuffer> &&write_buffers, std::vector<TransferBuffer> &&read_buffers,
    std::chrono::milliseconds timeout)
{
    auto status = wait_for_execute_request_ready(request, get_request_timeout(timeout));
    CHECK_SUCCESS(status);

    std::mutex mutex;
    std::condition_variable cv;
    rpc_message_t out_reply {};
    auto reply_received_callback = [&mutex, &cv, &out_reply] (rpc_message_t reply) {
        {
            std::unique_lock<std::mutex> lock(mutex);
            out_reply = reply;
        }
        cv.notify_one();
    };

    TRY_WITH_ACCEPTABLE_STATUS(HAILO_COMMUNICATION_CLOSED, auto message_id,
        execute_request_async_impl(action_id, request, reply_received_callback, std::move(write_buffers), std::move(read_buffers)));

    std::unique_lock<std::mutex> lock(mutex);
    auto wait_status = cv.wait_for(lock, get_request_timeout(timeout));
    if (std::cv_status::timeout == wait_status) {
        // Erase to avoid callback being called with uninitialized memory.
        (void)m_reply_data.pop(message_id);
        return make_unexpected(HAILO_TIMEOUT);
    }

    if (HAILO_SUCCESS != out_reply.header.status) {
        return make_unexpected(static_cast<hailo_status>(out_reply.header.status));
    }
    return out_reply;
}

hailo_status Client::wait_for_execute_request_ready(const MemoryView &request, std::chrono::milliseconds timeout)
{
    return m_connection->wait_for_write_message_async_ready(request.size(), timeout);
}

Expected<message_id_t> Client::execute_request_async_impl(uint32_t action_id, const MemoryView &request,
    HrpcCallback callback, std::vector<TransferBuffer> &&write_buffers, std::vector<TransferBuffer> &&read_buffers)
{
    rpc_message_header_t header;

    if (!m_is_running) {
        return make_unexpected(HAILO_COMMUNICATION_CLOSED);
    }

    std::unique_lock<std::mutex> lock(m_write_mutex);

    auto message_id = m_messages_sent++;
    header.size = static_cast<uint32_t>(request.size());
    header.message_id = message_id;
    header.action_id = static_cast<uint32_t>(action_id);
    header.status = HAILO_UNINITIALIZED;

    TransferRequest transfer_request;
    transfer_request.callback = request_sent_callback;
    transfer_request.transfer_buffers.reserve(write_buffers.size() + 1); // Request buffer + additional writes.
    if (request.size() > 0) {
        transfer_request.transfer_buffers.emplace_back(request);
    }
    transfer_request.transfer_buffers.insert(transfer_request.transfer_buffers.end(),
        write_buffers.begin(), write_buffers.end());

    m_reply_data.emplace(message_id, reply_data_t{callback, std::move(read_buffers)});

    auto status = m_connection->write_message_async(header, std::move(transfer_request));
    if ((HAILO_SUCCESS != status) || (!m_is_running)) {
        (void)m_reply_data.pop(message_id);
    }
    CHECK_SUCCESS(status);

    return message_id;
}

hailo_status Client::execute_request_async(uint32_t action_id, const MemoryView &request,
    HrpcCallback reply_received_callback, std::vector<TransferBuffer> &&write_buffers,
    std::vector<TransferBuffer> &&read_buffers)
{
    auto expected = execute_request_async_impl(action_id, request, reply_received_callback, std::move(write_buffers), std::move(read_buffers));
    return expected.status();
}

void Client::set_notification_callback(std::function<hailo_status(const MemoryView&)> callback)
{
    m_notification_callback = callback;
}

Expected<BufferPtr> Client::allocate_request_buffer()
{
    return m_pool_allocator->allocate();
}

std::shared_ptr<ClientCallbackDispatcherManager> Client::callback_dispatcher_manager()
{
    return m_callback_dispatcher_manager;
}

} // namespace hailort
