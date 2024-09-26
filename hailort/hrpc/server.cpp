/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file server.cpp
 * @brief RPC Server
 **/

#include "server.hpp"

namespace hrpc
{

ServerContext::ServerContext(Server &server, RpcConnection connection) :
    m_server(server), m_connection(connection) {}

hailo_status ServerContext::trigger_callback(uint32_t callback_id, hailo_status callback_status,
    rpc_object_handle_t callback_owner_handle, std::function<hailo_status(RpcConnection)> write_buffers_callback)
{
    return m_server.trigger_callback(callback_id, callback_status, callback_owner_handle, m_connection, write_buffers_callback);
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
    TRY(auto server_connection, RawConnection::create_shared(m_connection_context));
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

Expected<RpcConnection> Server::create_client_connection(std::shared_ptr<hrpc::RawConnection> server_connection)
{
    TRY(auto conn, server_connection->accept());
    return RpcConnection(conn);
}

hailo_status Server::serve_client(RpcConnection client_connection)
{
    auto server_context = make_shared_nothrow<ServerContext>(*this, client_connection);
    CHECK_NOT_NULL(server_context, HAILO_OUT_OF_HOST_MEMORY);
    while (true) {
        rpc_message_header_t header;
        auto request = client_connection.read_message(header);
        if (HAILO_COMMUNICATION_CLOSED == request.status()) {
            cleanup_client_resources(client_connection);
            break; // Client EP is disconnected, exit this loop
        }
        CHECK_EXPECTED_AS_STATUS(request);

        assert(header.action_id < static_cast<uint32_t>(HailoRpcActionID::MAX_VALUE));
        TRY(auto reply, m_dispatcher.call_action(static_cast<HailoRpcActionID>(header.action_id), MemoryView(*request), server_context));
        {
            std::unique_lock<std::mutex> lock(m_write_mutex);
            header.size = static_cast<uint32_t>(reply.size());

            auto status = client_connection.write_message(header, MemoryView(reply));
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
    RpcConnection connection, std::function<hailo_status(RpcConnection)> write_buffers_callback)
{
    // TODO: callback handling should be outside of HRPC (HRT-14638)
    TRY(auto reply, CallbackCalledSerializer::serialize_reply(callback_status, callback_id, callback_owner_handle));

    std::unique_lock<std::mutex> lock(m_write_mutex);
    rpc_message_header_t header;
    header.action_id = static_cast<uint32_t>(HailoRpcActionID::CALLBACK_CALLED);
    header.message_id = callback_id;
    header.size = static_cast<uint32_t>(reply.size());

    auto status = connection.write_message(header, MemoryView(reply));
    if ((HAILO_COMMUNICATION_CLOSED == status) || (HAILO_FILE_OPERATION_FAILURE == status)) {
        return status;
    }
    CHECK_SUCCESS(status);

    if (write_buffers_callback) {
        status = write_buffers_callback(connection);
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

} // namespace hrpc
