/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file client.hpp
 * @brief RPC Client
 **/

#include "client.hpp"
#include "connection_context.hpp"
#include "vdma/pcie_session.hpp" // TODO: Remove include: (HRT-16534)

namespace hailort
{
constexpr size_t REQUEST_PROTO_MAX_SIZE (128); // TODO: HRT-16644 - make it dynamic

// TODO: HRT-16034: make common function with grpc client.
std::chrono::milliseconds get_request_timeout(const std::chrono::milliseconds default_timeout)
{
    auto timeout_seconds = get_env_variable(HAILO_REQUEST_TIMEOUT_SECONDS);
    if (timeout_seconds) {
        return std::chrono::seconds(std::stoi(timeout_seconds.value()));
    }
    return default_timeout;
}

Client::~Client()
{
    m_is_running = false;
    (void)m_connection.close();
    if (m_thread.joinable()) {
        m_thread.join();
    }

    for (const auto &callback : m_replies_callbacks) {
        rpc_message_t message {};
        callback.second(HAILO_COMMUNICATION_CLOSED, std::move(message));
    }
}

// TODO: Connect should be a static method that returns a client
hailo_status Client::connect()
{
    m_callback_dispatcher_manager = make_shared_nothrow<ClientCallbackDispatcherManager>();
    CHECK_NOT_NULL(m_callback_dispatcher_manager, HAILO_OUT_OF_HOST_MEMORY);

    TRY(m_conn_context, ConnectionContext::create_client_shared(m_device_id));
    TRY(auto conn, Session::connect(m_conn_context, HAILORT_SERVER_PORT));

    // TODO: Use conn.max_ongoing_transfers() function (HRT-16534)
    TRY(m_pool_allocator, PoolAllocator::create_shared(PcieSession::MAX_ONGOING_TRANSFERS, REQUEST_PROTO_MAX_SIZE,
        [conn] (size_t size) { return conn->allocate_buffer(size, HAILO_DMA_BUFFER_DIRECTION_H2D); }
    ));

    TRY(m_sync_requests_pool, ObjectPool<SyncRequest>::create_shared(PcieSession::MAX_ONGOING_TRANSFERS, [this] () {
        return SyncRequest(*this, m_sync_mutex, m_sync_cv);
    }));

    TRY(auto connection_params, RpcConnection::Params::create(conn));
    m_connection = RpcConnection(std::move(connection_params));
    m_thread = std::thread([this] {
        auto status = message_loop();
        if ((status != HAILO_SUCCESS) && (status != HAILO_COMMUNICATION_CLOSED)) { // TODO: Use this to prevent future requests
            LOGGER__ERROR("Error in message loop - {}", status);
        }
    });
    return HAILO_SUCCESS;
}

hailo_status Client::message_loop()
{
    while (m_is_running) {
        TRY_WITH_ACCEPTABLE_STATUS(HAILO_COMMUNICATION_CLOSED, auto message, m_connection.read_message());

        assert(message.header.action_id < static_cast<uint32_t>(HailoRpcActionID::MAX_VALUE));
        auto action_id_enum = static_cast<HailoRpcActionID>(message.header.action_id);
        if (m_custom_callbacks.find(action_id_enum) != m_custom_callbacks.end()) {
            auto status = m_custom_callbacks[action_id_enum](MemoryView(message.buffer->data(), message.header.size), m_connection);
            CHECK_SUCCESS(status);
            continue;
        }

        std::function<void(hailo_status, rpc_message_t)> reply_received_callback = nullptr;
        {
            std::unique_lock<std::mutex> lock(m_replies_mutex);
            m_replies_cv.wait(lock, [this, &message] () {
                return contains(m_replies_callbacks, message.header.message_id);
            });
            reply_received_callback = m_replies_callbacks[message.header.message_id];
            m_replies_callbacks.erase(message.header.message_id);
        }

        reply_received_callback(HAILO_SUCCESS, std::move(message));
    }

    return HAILO_SUCCESS;
}

SyncRequest::SyncRequest(Client &client, std::mutex &sync_mutex, std::condition_variable &sync_cv)
    : m_client(client), m_sync_mutex(sync_mutex), m_sync_cv(sync_cv), m_transfer_status(HAILO_UNINITIALIZED), m_out_reply({}) {}

Expected<rpc_message_t> SyncRequest::execute(HailoRpcActionID action_id, const MemoryView &request,
    std::vector<TransferBuffer> &&additional_buffers)
{
    auto status = m_client.wait_for_execute_request_ready(request, get_request_timeout(REQUEST_TIMEOUT));
    CHECK_SUCCESS(status);

    auto request_sent_callback = [] (hailo_status status) {
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to send request, status = {}", status);
        }
    };
    auto reply_received_callback = [this] (hailo_status status, rpc_message_t reply) {
        {
            std::unique_lock<std::mutex> lock(m_sync_mutex);
            assert(status != HAILO_UNINITIALIZED);
            m_transfer_status = status;

            if (HAILO_SUCCESS == status) {
                m_out_reply = std::move(reply);
            }
        }
        m_sync_cv.notify_one();
    };
    status = m_client.execute_request_async(action_id, request, request_sent_callback,
        reply_received_callback, std::move(additional_buffers));
    if (HAILO_COMMUNICATION_CLOSED == status) {
        return make_unexpected(status);
    }
    CHECK_SUCCESS_AS_EXPECTED(status);

    std::unique_lock<std::mutex> lock(m_sync_mutex);
    CHECK_AS_EXPECTED(m_sync_cv.wait_for(lock, get_request_timeout(REQUEST_TIMEOUT), [this] { return m_transfer_status != HAILO_UNINITIALIZED; }),
        HAILO_TIMEOUT, "Timeout waiting for transfer completion");
    CHECK_SUCCESS(m_transfer_status);

    auto copy = m_out_reply;
    m_transfer_status = HAILO_UNINITIALIZED;
    m_out_reply = {};
    return copy;
}

Expected<rpc_message_t> Client::execute_request(HailoRpcActionID action_id, const MemoryView &request,
    std::vector<TransferBuffer> &&additional_buffers)
{
    TRY(auto sync_request, m_sync_requests_pool->acquire());
    TRY(auto reply, sync_request->execute(action_id, request, std::move(additional_buffers)));

    auto status = m_sync_requests_pool->return_to_pool(sync_request);
    CHECK_SUCCESS(status);

    return reply;
}

hailo_status Client::wait_for_execute_request_ready(const MemoryView &request, std::chrono::milliseconds timeout)
{
    return m_connection.wait_for_write_message_async_ready(request.size(), timeout);
}

hailo_status Client::execute_request_async(HailoRpcActionID action_id, const MemoryView &request,
    std::function<void(hailo_status)> request_sent_callback,
    std::function<void(hailo_status, rpc_message_t)> reply_received_callback,
    std::vector<TransferBuffer> &&additional_buffers)
{
    rpc_message_header_t header;
    {
        std::unique_lock<std::mutex> lock(m_write_mutex);
        header.size = static_cast<uint32_t>(request.size());
        header.message_id = m_messages_sent++;
        header.action_id = static_cast<uint32_t>(action_id);

        TransferRequest transfer_request;
        transfer_request.callback = std::move(request_sent_callback);
        if (request.size() > 0) {
            transfer_request.transfer_buffers.emplace_back(request);
        }
        transfer_request.transfer_buffers.insert(transfer_request.transfer_buffers.end(),
            additional_buffers.begin(), additional_buffers.end());

        auto status = m_connection.write_message_async(header, std::move(transfer_request));
        CHECK_SUCCESS(status);
    }

    {
        std::unique_lock<std::mutex> lock(m_replies_mutex);
        m_replies_callbacks[header.message_id] = reply_received_callback;
    }
    m_replies_cv.notify_all();

    return HAILO_SUCCESS;
}

void Client::register_custom_reply(HailoRpcActionID action_id,
    std::function<hailo_status(const MemoryView&, RpcConnection connection)> callback)
{
    m_custom_callbacks[action_id] = callback;
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
