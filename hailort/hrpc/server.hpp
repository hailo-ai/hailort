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
    ClientConnection() = delete;
    ClientConnection(RpcConnection::Params &&params, uint32_t client_id,
        SpscQueue<std::function<hailo_status()>> action_thread_queue, EventPtr shutdown_event);
    virtual ~ClientConnection();

    uint32_t client_id() const;
    hailo_status enqueue_action(std::function<hailo_status()> action);
    hailo_status shutdown_action_thread();

private:
    uint32_t m_client_id;
    std::thread m_action_thread;
    SpscQueue<std::function<hailo_status()>> m_action_thread_queue;
    EventPtr m_shutdown_event;
};

class ActionHandler {
public:
    virtual ~ActionHandler() = default;
    virtual hailo_status parse_request(const MemoryView &request, ClientConnectionPtr client_connection) = 0;
    virtual hailo_status do_action(ResponseWriter response_writer) = 0;
};

using ActionHandlerFunc = std::function<hailo_status(const MemoryView&, ClientConnectionPtr, ResponseWriter)>;
class NoReadsActionHandler : public ActionHandler {
public:
    NoReadsActionHandler(ActionHandlerFunc action_func) 
        : m_action_func(action_func)
    {}

    hailo_status parse_request(const MemoryView &request, ClientConnectionPtr client_connection) override
    {
        m_request = request;
        m_client_connection = client_connection;
        return HAILO_SUCCESS;
    }
    hailo_status do_action(ResponseWriter response_writer) override
    {
        return m_action_func(m_request, m_client_connection, response_writer);
    }

private:
    ActionHandlerFunc m_action_func;
    MemoryView m_request;
    ClientConnectionPtr m_client_connection;
};

class Dispatcher
{
public:
    Dispatcher() = default;

    void register_handler(uint32_t action_id, std::function<Expected<std::shared_ptr<ActionHandler>>()> handler);
    Expected<std::shared_ptr<ActionHandler>> create_handler(uint32_t action_id);

private:
    std::unordered_map<uint32_t, std::function<Expected<std::shared_ptr<ActionHandler>>()>> m_handlers;
};

class Server
{
public:
    Server(std::shared_ptr<ConnectionContext> connection_context, std::shared_ptr<std::mutex> write_mutex,
        std::function<void(uint32_t)> client_waiting_for_close_callback) :
            m_connection_context(connection_context), m_write_mutex(write_mutex), m_client_count(0),
            m_client_waiting_for_close_callback(client_waiting_for_close_callback) {}
    virtual ~Server() = default;

    hailo_status serve();
    void set_dispatcher(Dispatcher dispatcher);

protected:
    std::shared_ptr<ConnectionContext> m_connection_context;
    std::shared_ptr<std::mutex> m_write_mutex;

private:
    Expected<ClientConnectionPtr> create_client_connection(std::shared_ptr<SessionListener> server_connection);
    hailo_status serve_client(ClientConnectionPtr client_connection);
    hailo_status handle_client_request(ClientConnectionPtr client_connection, std::shared_ptr<ActionHandler> &handler);
    virtual hailo_status cleanup_client_resources(ClientConnectionPtr client_connection) = 0;

    Dispatcher m_dispatcher;
    uint32_t m_client_count;
    std::function<void(uint32_t)> m_client_waiting_for_close_callback;
};

} // namespace hailort

#endif // _SERVER_HPP_