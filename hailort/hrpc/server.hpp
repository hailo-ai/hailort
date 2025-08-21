/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file server.hpp
 * @brief RPC Server Header
 **/

#ifndef _SERVER_HPP_
#define _SERVER_HPP_

#include "response_writer.hpp"
#include "rpc_connection.hpp"
#include "server_resource_manager.hpp"
#include "hrpc_protocol/serializer.hpp"

constexpr auto SERVER_TIMEOUT = std::chrono::seconds(10);

namespace hailort
{

class ClientConnection;
using ClientConnectionPtr = std::shared_ptr<ClientConnection>;
class ClientConnection : public RpcConnection
{
public:
    static Expected<ClientConnectionPtr> create(std::shared_ptr<Session> session, uint32_t client_id);
    ClientConnection() = default;
    ClientConnection(RpcConnection::Params &&params, uint32_t client_id) : RpcConnection(std::move(params)),
            m_client_id(client_id) {}

    uint32_t client_id() const;

private:
    uint32_t m_client_id;
};

using ActionHandlerFunc = std::function<hailo_status(const MemoryView&, ClientConnectionPtr, ResponseWriter)>;

class Dispatcher
{
public:
    Dispatcher() = default;

    void register_action(HailoRpcActionID action_id, ActionHandlerFunc action);
    hailo_status call_action(HailoRpcActionID action_id, const MemoryView &request,
        ClientConnectionPtr client_connection, ResponseWriter response_writer);

private:
    std::unordered_map<HailoRpcActionID, ActionHandlerFunc> m_actions;
};

class Server
{
public:
    Server(std::shared_ptr<ConnectionContext> connection_context, std::shared_ptr<std::mutex> write_mutex) :
        m_connection_context(connection_context), m_write_mutex(write_mutex), m_client_count(0)
    {};
    virtual ~Server() = default;

    hailo_status serve();
    void set_dispatcher(Dispatcher dispatcher);

protected:
    std::shared_ptr<ConnectionContext> m_connection_context;
    std::shared_ptr<std::mutex> m_write_mutex;

private:
    Expected<ClientConnectionPtr> create_client_connection(std::shared_ptr<SessionListener> server_connection);
    hailo_status serve_client(ClientConnectionPtr client_connection);
    hailo_status handle_client_request(ClientConnectionPtr client_connection);
    virtual hailo_status cleanup_client_resources(ClientConnectionPtr client_connection) = 0;

    Dispatcher m_dispatcher;
    uint32_t m_client_count;
};

} // namespace hailort

#endif // _SERVER_HPP_