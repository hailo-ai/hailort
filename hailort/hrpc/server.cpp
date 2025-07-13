/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file server.cpp
 * @brief RPC Server
 **/

#include "server.hpp"

namespace hailort
{

Expected<ClientConnectionPtr> ClientConnection::create(std::shared_ptr<Session> session, uint32_t client_id)
{
    TRY(auto conn_params, RpcConnection::Params::create(session));
    auto ptr = make_shared_nothrow<ClientConnection>(std::move(conn_params), client_id);
    CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);
    return ptr;
}

uint32_t ClientConnection::client_id() const
{
    return m_client_id;
}

void Dispatcher::register_action(HailoRpcActionID action_id, ActionHandlerFunc action)
{
    m_actions[action_id] = action;
}

hailo_status Dispatcher::call_action(HailoRpcActionID action_id, const MemoryView &request,
    ClientConnectionPtr client_connection, ResponseWriter response_writer)
{
    CHECK(m_actions.count(action_id), HAILO_RPC_FAILED, "Failed to find RPC action {}", static_cast<int>(action_id));

    auto status = m_actions[action_id](request, client_connection, response_writer);
    if (HAILO_SUCCESS != status) {
        // If the handler failed for some reason, try to write the unsuccessful status back to the client.
        return response_writer.write(status);
    }
    return HAILO_SUCCESS;
}

hailo_status Server::serve()
{
    TRY(auto server_connection, SessionListener::create_shared(m_connection_context, HAILORT_SERVER_PORT));
    while (true) {
        TRY(auto client_connection, create_client_connection(server_connection));
        auto th = std::thread([this, client_connection]() { (void)serve_client(client_connection); });
        th.detach();
    }
    return HAILO_SUCCESS;
}

void Server::set_dispatcher(Dispatcher dispatcher)
{
    m_dispatcher = dispatcher;
}

Expected<ClientConnectionPtr> Server::create_client_connection(std::shared_ptr<SessionListener> server_connection)
{
    TRY(auto session, server_connection->accept());
    TRY(auto connection, ClientConnection::create(session, ++m_client_count));
    return connection;
}

hailo_status Server::serve_client(ClientConnectionPtr client_connection)
{
    auto status = HAILO_SUCCESS;
    do {
        status = handle_client_request(client_connection);
    } while (HAILO_SUCCESS == status);
    if (HAILO_COMMUNICATION_CLOSED != status) {
        LOGGER__ERROR("handle request failed with status: {}", status);
    }

    status = cleanup_client_resources(client_connection);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("client cleanup failed with status: {}", status);
    }

    std::unique_lock<std::mutex> lock(*m_write_mutex);
    return client_connection->close();
}

hailo_status Server::handle_client_request(ClientConnectionPtr client_connection)
{
    TRY_WITH_ACCEPTABLE_STATUS(HAILO_COMMUNICATION_CLOSED, auto request, client_connection->read_message());

    assert(request.header.action_id < static_cast<uint32_t>(HailoRpcActionID::MAX_VALUE));
    auto action_id = static_cast<HailoRpcActionID>(request.header.action_id);
    MemoryView raw_request(request.buffer->data(), request.header.size);
    ResponseWriter response_writer(request.header, client_connection, m_write_mutex);

    auto status = m_dispatcher.call_action(action_id, raw_request, client_connection, response_writer);
    CHECK_SUCCESS_WITH_ACCEPTABLE_STATUS(HAILO_COMMUNICATION_CLOSED, status);

    return HAILO_SUCCESS;
}

} // namespace hailort
