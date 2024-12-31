#ifndef _SERVER_HPP_
/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file server.hpp
 * @brief RPC Server Header
 **/

#define _SERVER_HPP_

#include <functional>
#include <thread>

#include "rpc_connection.hpp"
#include "hailort_service/service_resource_manager.hpp"
#include "hrpc_protocol/serializer.hpp"

constexpr auto SERVER_TIMEOUT = std::chrono::seconds(10);

namespace hailort
{

class Server;
class ServerContext
{
public:
    ServerContext(Server &server, RpcConnection connection);
    hailo_status trigger_callback(uint32_t callback_id, hailo_status callback_status,
        rpc_object_handle_t callback_owner_handle, std::function<hailo_status(RpcConnection)> additional_writes_lambda = nullptr);
    RpcConnection &connection();

private:
    Server &m_server;
    RpcConnection m_connection;
};
using ServerContextPtr = std::shared_ptr<ServerContext>;

class Dispatcher
{
public:
    Dispatcher() = default;

    void register_action(HailoRpcActionID action_id,
        std::function<Expected<Buffer>(const MemoryView&, ServerContextPtr)> action);
    Expected<Buffer> call_action(HailoRpcActionID action_id, const MemoryView &request, ServerContextPtr server_context);

private:
    std::unordered_map<HailoRpcActionID, std::function<Expected<Buffer>(const MemoryView&, ServerContextPtr)>> m_actions;
};

class Server
{
public:
    Server(std::shared_ptr<ConnectionContext> connection_context) : m_connection_context(connection_context) {};
    virtual ~Server() = default;

    hailo_status serve();
    void set_dispatcher(Dispatcher dispatcher);

    friend class ServerContext;

protected:
    hailo_status trigger_callback(uint32_t callback_id, hailo_status callback_status, rpc_object_handle_t callback_owner_handle,
        RpcConnection connection, std::function<hailo_status(RpcConnection)> additional_writes_lambda = nullptr);

    std::shared_ptr<ConnectionContext> m_connection_context;

private:
    Expected<RpcConnection> create_client_connection(std::shared_ptr<SessionListener> server_connection);
    hailo_status serve_client(RpcConnection client_connection);
    virtual hailo_status cleanup_client_resources(RpcConnection client_connection) = 0;

    Dispatcher m_dispatcher;
    std::mutex m_write_mutex;
};

} // namespace hailort

#endif // _SERVER_HPP_