/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file client.hpp
 * @brief RPC Client
 **/

#include "client.hpp"

using namespace hrpc;

Expected<std::shared_ptr<ResultEvent>> ResultEvent::create_shared()
{
    TRY(auto event, hailort::Event::create_shared(hailort::Event::State::not_signalled));
    auto ptr = make_shared_nothrow<ResultEvent>(event);
    CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);
    return ptr;
}

ResultEvent::ResultEvent(EventPtr event) :
    m_event(event)
{
}

Buffer &&ResultEvent::release()
{
    return std::move(m_value);
}

hailo_status ResultEvent::signal(Buffer &&value)
{
    m_value = std::move(value);
    return m_event->signal();
}

hailo_status ResultEvent::wait(std::chrono::milliseconds timeout)
{
    return m_event->wait(timeout);
}

Client::~Client()
{
    is_running = false;
    (void)m_connection.close();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

hailo_status Client::connect()
{
    TRY(m_conn_context, ConnectionContext::create_shared(false));
    TRY(auto conn, RawConnection::create_shared(m_conn_context));
    auto status = conn->connect();
    CHECK_SUCCESS(status);

    m_connection = RpcConnection(conn);
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
    while (is_running) {
        rpc_message_header_t header;
        TRY_WITH_ACCEPTABLE_STATUS(HAILO_COMMUNICATION_CLOSED, auto message, m_connection.read_message(header));

        assert(header.action_id < static_cast<uint32_t>(HailoRpcActionID::MAX_VALUE));
        auto action_id_enum = static_cast<HailoRpcActionID>(header.action_id);
        if (m_custom_callbacks.find(action_id_enum) != m_custom_callbacks.end()) {
            auto status = m_custom_callbacks[action_id_enum](MemoryView(message), m_connection);
            CHECK_SUCCESS(status);
            continue;
        }

        std::unique_lock<std::mutex> lock(m_message_mutex);
        auto event = m_events[header.message_id];
        lock.unlock();
        auto status = event->signal(std::move(message));
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

Expected<Buffer> Client::execute_request(HailoRpcActionID action_id, const MemoryView &request,
    std::function<hailo_status(RpcConnection)> write_buffers_callback)
{
    std::unique_lock<std::mutex> lock(m_message_mutex);
    rpc_message_header_t header;
    header.size = static_cast<uint32_t>(request.size());
    header.message_id = m_messages_sent++;
    header.action_id = static_cast<uint32_t>(action_id);

    auto status = m_connection.write_message(header, request);
    CHECK_SUCCESS_AS_EXPECTED(status);
    if (write_buffers_callback) {
        status = write_buffers_callback(m_connection);
        CHECK_SUCCESS_AS_EXPECTED(status);
    }

    TRY(auto event, ResultEvent::create_shared());
    m_events[header.message_id] = event;

    lock.unlock();
    status = event->wait(REQUEST_TIMEOUT);
    CHECK_SUCCESS_AS_EXPECTED(status);

    m_events.erase(header.message_id);
    return event->release();
}


void Client::register_custom_reply(HailoRpcActionID action_id,
    std::function<hailo_status(const MemoryView&, RpcConnection connection)> callback)
{
    m_custom_callbacks[action_id] = callback;
}