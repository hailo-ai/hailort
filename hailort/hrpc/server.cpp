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
    constexpr size_t ACTION_THREAD_QUEUE_SIZE = 128;
    TRY(auto conn_params, RpcConnection::Params::create(session));
    TRY(auto shutdown_event, Event::create_shared(Event::State::not_signalled));
    TRY(auto action_thread_queue, SpscQueue<std::function<hailo_status()>>::create(ACTION_THREAD_QUEUE_SIZE, shutdown_event, DEFAULT_TRANSFER_TIMEOUT));
    auto ptr = make_shared_nothrow<ClientConnection>(std::move(conn_params), client_id, std::move(action_thread_queue), std::move(shutdown_event));
    CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);
    return ptr;
}

ClientConnection::ClientConnection(RpcConnection::Params &&params, uint32_t client_id,
    SpscQueue<std::function<hailo_status()>> action_thread_queue, EventPtr shutdown_event)
    : RpcConnection(std::move(params)), m_client_id(client_id),
        m_action_thread_queue(std::move(action_thread_queue)), m_shutdown_event(shutdown_event)
{
    m_action_thread = std::thread([this] () {
        while (true) {
            auto action = m_action_thread_queue.dequeue(std::chrono::milliseconds(HAILO_INFINITE));
            if (action.status() == HAILO_SHUTDOWN_EVENT_SIGNALED) {
                break;
            }
            if (HAILO_SUCCESS != action.status()) {
                LOGGER__ERROR("Failed to dequeue action from queue. status: {}", action.status());
                break;
            }
            (void)action.release()();
        }
    });
}

ClientConnection::~ClientConnection()
{
    auto status = shutdown_action_thread();
    if (HAILO_SUCCESS != status) {
        LOGGER__CRITICAL("Failed to shutdown action thread, status: {}", status);
    }
}

uint32_t ClientConnection::client_id() const
{
    return m_client_id;
}

hailo_status ClientConnection::enqueue_action(std::function<hailo_status()> action)
{
    return m_action_thread_queue.enqueue(action);
}

hailo_status ClientConnection::shutdown_action_thread()
{
    auto status = m_shutdown_event->signal();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to signal shutdown event, status = {}", status);
    }

    if (m_action_thread.joinable()) {
        m_action_thread.join();
    }

    return status;
}

void Dispatcher::register_handler(uint32_t action_id, std::function<Expected<std::shared_ptr<ActionHandler>>()> handler)
{
    m_handlers[action_id] = handler;
}

Expected<std::shared_ptr<ActionHandler>> Dispatcher::create_handler(uint32_t action_id)
{
    CHECK(m_handlers.count(action_id), HAILO_RPC_FAILED, "Failed to find RPC action {}", static_cast<int>(action_id));
    return m_handlers[action_id]();
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
    hailo_status status = HAILO_UNINITIALIZED;
    while (true) {
        // We hold the handler so that is will be freed AFTER we mark the client as waiting for close, in case of a failed request,
        // because destructing the handler may take some time (big buffers, etc.) - we want to mark as fast as possible.
        std::shared_ptr<ActionHandler> handler = nullptr;
        status = handle_client_request(client_connection, handler);
        if (HAILO_SUCCESS != status) {
            m_client_waiting_for_close_callback(client_connection->client_id());
            break;
        }
    }
    if (HAILO_COMMUNICATION_CLOSED != status) {
        LOGGER__ERROR("handle request failed with status: {}", status);
    }

    status = client_connection->shutdown_action_thread();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to shutdown action thread, status: {}", status);
    }

    status = cleanup_client_resources(client_connection);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("client cleanup failed with status: {}", status);
    }

    std::unique_lock<std::mutex> lock(*m_write_mutex);
    return client_connection->close();
}

hailo_status Server::handle_client_request(ClientConnectionPtr client_connection, std::shared_ptr<ActionHandler> &handler)
{
    TRY_WITH_ACCEPTABLE_STATUS(HAILO_COMMUNICATION_CLOSED, auto request, client_connection->read_message());
    TRY(handler, m_dispatcher.create_handler(request.header.action_id));

    auto status = handler->parse_request(MemoryView(request.buffer->data(), request.header.size), client_connection);
    if (HAILO_COMMUNICATION_CLOSED == status) {
        LOGGER__INFO("Client communication closed, client id: {}", client_connection->client_id());
        return HAILO_COMMUNICATION_CLOSED;
    }

    ResponseWriter response_writer(request.header, client_connection, m_write_mutex);
    if (HAILO_SUCCESS != status) {
        // If the read buffers failed for some reason, try to write the unsuccessful status back to the client.
        return response_writer.write(status);
    }

    status = client_connection->enqueue_action([handler, response_writer, buffer = request.buffer] () mutable -> hailo_status {
        // Capturing buffer here to prevent it from being freed before do_action, because on some actions we deserialize the request there.
        // This can be optimized by moving the deserialization to the parse_request function
        auto status = handler->do_action(response_writer);
        if (HAILO_SUCCESS != status) {
            // If the action failed for some reason, try to write the unsuccessful status back to the client.
            return response_writer.write(status);
        }
        return HAILO_SUCCESS;
    });
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

} // namespace hailort
