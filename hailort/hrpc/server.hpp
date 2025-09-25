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

#include <functional>
#include <thread>

#include "rpc_connection.hpp"
#include "hailort_service/service_resource_manager.hpp"
#include "hrpc_protocol/serializer.hpp"

constexpr auto SERVER_TIMEOUT = std::chrono::seconds(10);

namespace hailort
{

class ClientConnection : public RpcConnection
{
public:
    static Expected<ClientConnection> create(std::shared_ptr<Session> session, uint32_t client_id);
    ClientConnection() = default;
    ClientConnection(RpcConnection::Params &&params, uint32_t client_id) : RpcConnection(std::move(params)),
            m_client_id(client_id) {}

    uint32_t client_id() const;

private:
    uint32_t m_client_id;
};

class Dispatcher
{
public:
    Dispatcher() = default;

    void register_action(HailoRpcActionID action_id,
        std::function<Expected<Buffer>(const MemoryView&, ClientConnection)> action);
    Expected<Buffer> call_action(HailoRpcActionID action_id, const MemoryView &request, ClientConnection client_connection);

private:
    std::unordered_map<HailoRpcActionID, std::function<Expected<Buffer>(const MemoryView&, ClientConnection)>> m_actions;
};

class Server
{
public:
    Server(std::shared_ptr<ConnectionContext> connection_context) : m_connection_context(connection_context), m_client_count(0) {};
    virtual ~Server() = default;

    hailo_status serve();
    void set_dispatcher(Dispatcher dispatcher);

protected:
    hailo_status trigger_callback(const RpcCallback &callback, ClientConnection connection,
        std::function<hailo_status(ClientConnection)> additional_writes_lambda = nullptr);

    std::shared_ptr<ConnectionContext> m_connection_context;

private:
    Expected<ClientConnection> create_client_connection(std::shared_ptr<SessionListener> server_connection);
    hailo_status serve_client(ClientConnection client_connection);
    virtual hailo_status cleanup_client_resources(ClientConnection client_connection) = 0;

    Dispatcher m_dispatcher;
    std::mutex m_write_mutex;
    uint32_t m_client_count;
};

} // namespace hailort

#endif // _SERVER_HPP_