/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file server.cpp
 * @brief RPC Server
 **/

#include "server.hpp"

namespace hailort
{

ServerContext::ServerContext(Server &server, RpcConnection connection) :
    m_server(server), m_connection(connection) {}

hailo_status ServerContext::trigger_callback(uint32_t callback_id, hailo_status callback_status,
    rpc_object_handle_t callback_owner_handle, std::function<hailo_status(RpcConnection)> additional_writes_lambda)
{
    return m_server.trigger_callback(callback_id, callback_status, callback_owner_handle, m_connection, additional_writes_lambda);
}

RpcConnection &ServerContext::connection()
{
    return m_connection;
}

void Dispatcher::register_action(HailoRpcActionID action_id,
    std::function<Expected<Buffer>(const MemoryView&, ServerContextPtr)> action)
{
    m_actions[action_id] = action;
}

Expected<Buffer> Dispatcher::call_action(HailoRpcActionID action_id, const MemoryView &request, ServerContextPtr server_context)
{
    if (m_actions.find(action_id) != m_actions.end()) {
        return m_actions[action_id](request, server_context);
    }
    LOGGER__ERROR("Failed to find RPC action {}", static_cast<int>(action_id));
    return make_unexpected(HAILO_RPC_FAILED);
}

hailo_status Server::serve()
{
    TRY(auto server_connection, SessionListener::create_shared(m_connection_context, get_pcie_port()));
    while (true) {
        TRY(auto client_connection, create_client_connection(server_connection));
        auto th = std::thread([this, client_connection]() { serve_client(client_connection); });
        th.detach();
    }
    return HAILO_SUCCESS;
}

void Server::set_dispatcher(Dispatcher dispatcher)
{
    m_dispatcher = dispatcher;
}

Expected<RpcConnection> Server::create_client_connection(std::shared_ptr<SessionListener> server_connection)
{
    TRY(auto conn, server_connection->accept());
    TRY(auto rpc_conn, RpcConnection::create(conn));
    return rpc_conn;
}

hailo_status Server::serve_client(RpcConnection client_connection)
{
    auto server_context = make_shared_nothrow<ServerContext>(*this, client_connection);
    CHECK_NOT_NULL(server_context, HAILO_OUT_OF_HOST_MEMORY);
    while (true) {
        auto request = client_connection.read_message();
        if (HAILO_COMMUNICATION_CLOSED == request.status()) {
            cleanup_client_resources(client_connection);
            break; // Client EP is disconnected, exit this loop
        }
        CHECK_EXPECTED_AS_STATUS(request);

        assert(request->header.action_id < static_cast<uint32_t>(HailoRpcActionID::MAX_VALUE));
        TRY(auto reply, m_dispatcher.call_action(static_cast<HailoRpcActionID>(request->header.action_id),
            MemoryView(request->buffer), server_context));
        request->header.size = static_cast<uint32_t>(reply.size());

        auto status = client_connection.wait_for_write_message_async_ready(reply.size(), SERVER_TIMEOUT);
        CHECK_SUCCESS(status);

        {
            std::unique_lock<std::mutex> lock(m_write_mutex);
            auto reply_memview = MemoryView(reply);
            auto reply_ptr = make_shared_nothrow<Buffer>(std::move(reply));
            CHECK_NOT_NULL(reply_ptr, HAILO_OUT_OF_HOST_MEMORY);

            status = client_connection.write_message_async(request->header, reply_memview,
            [reply_ptr] (hailo_status status) {
                if (HAILO_SUCCESS != status) {
                    LOGGER__ERROR("Failed to send reply, status = {}", status);
                }
            });
            if ((HAILO_COMMUNICATION_CLOSED == status) || (HAILO_FILE_OPERATION_FAILURE == status)) {
                lock.unlock(); // We need to acquire this lock when releasing the client resources (trigger cb)
                cleanup_client_resources(client_connection);
                break; // Client EP is disconnected, exit this loop
            }
            CHECK_SUCCESS(status);
        }
    }

    return HAILO_SUCCESS;
}

hailo_status Server::trigger_callback(uint32_t callback_id, hailo_status callback_status, rpc_object_handle_t callback_owner_handle,
    RpcConnection connection, std::function<hailo_status(RpcConnection)> additional_writes_lambda)
{
    // TODO: callback handling should be outside of HRPC (HRT-14638)
    TRY(auto reply, CallbackCalledSerializer::serialize_reply(callback_status, callback_id, callback_owner_handle));
    rpc_message_header_t header;
    header.action_id = static_cast<uint32_t>(HailoRpcActionID::CALLBACK_CALLED);
    header.message_id = callback_id;
    header.size = static_cast<uint32_t>(reply.size());

    auto reply_ptr = make_shared_nothrow<Buffer>(std::move(reply));
    CHECK_NOT_NULL(reply_ptr, HAILO_OUT_OF_HOST_MEMORY);

    auto status = connection.wait_for_write_message_async_ready(reply_ptr->size(), SERVER_TIMEOUT);
    CHECK_SUCCESS(status);

    std::unique_lock<std::mutex> lock(m_write_mutex);
    status = connection.write_message_async(header, MemoryView(*reply_ptr),
        [reply_ptr] (hailo_status status) {
            if (HAILO_SUCCESS != status) {
                LOGGER__ERROR("Failed to send callback called reply, status = {}", status);
            }
        });
    if ((HAILO_COMMUNICATION_CLOSED == status) || (HAILO_FILE_OPERATION_FAILURE == status)) {
        return status;
    }
    CHECK_SUCCESS(status);

    if (additional_writes_lambda) {
        status = additional_writes_lambda(connection);
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

} // namespace hailort
