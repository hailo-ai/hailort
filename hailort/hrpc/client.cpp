/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file client.hpp
 * @brief RPC Client
 **/

#include "client.hpp"
#include "connection_context.hpp"

namespace hailort
{

std::chrono::milliseconds get_request_timeout()
{
    auto timeout_seconds = get_env_variable(HAILO_REQUEST_TIMEOUT_SECONDS);
    if (timeout_seconds) {
        return std::chrono::seconds(std::stoi(timeout_seconds.value()));
    }
    return REQUEST_TIMEOUT;
}

Expected<std::shared_ptr<ResultEvent>> ResultEvent::create_shared()
{
    TRY(auto event, Event::create_shared(Event::State::not_signalled));
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
    m_is_running = false;
    (void)m_connection.close();
    if (m_thread.joinable()) {
        m_thread.join();
    }

    for (const auto &callback : m_replies_callbacks) {
        Buffer buffer;
        callback.second(HAILO_COMMUNICATION_CLOSED, std::move(buffer));
    }
}

hailo_status Client::connect()
{
    TRY(m_conn_context, ConnectionContext::create_client_shared(m_device_id));
    auto port = get_pcie_port();
    TRY(auto conn, Session::connect(m_conn_context, port));

    TRY(m_connection, RpcConnection::create(conn));
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
            auto status = m_custom_callbacks[action_id_enum](MemoryView(message.buffer), m_connection);
            CHECK_SUCCESS(status);
            continue;
        }

        std::function<void(hailo_status, Buffer&&)> reply_received_callback = nullptr;
        {
            std::unique_lock<std::mutex> lock(m_replies_mutex);
            m_replies_cv.wait(lock, [this, &message] () {
                return contains(m_replies_callbacks, message.header.message_id);
            });
            reply_received_callback = m_replies_callbacks[message.header.message_id];
            m_replies_callbacks.erase(message.header.message_id);
        }

        reply_received_callback(HAILO_SUCCESS, std::move(message.buffer));
    }

    return HAILO_SUCCESS;
}

Expected<Buffer> Client::execute_request(HailoRpcActionID action_id, const MemoryView &request,
    std::function<hailo_status(RpcConnection)> additional_writes_lambda)
{
    auto status = wait_for_execute_request_ready(request, get_request_timeout());
    CHECK_SUCCESS(status);

    hailo_status transfer_status = HAILO_UNINITIALIZED;
    Buffer out_reply;
    auto request_sent_callback = [] (hailo_status status) {
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to send request, status = {}", status);
        }
    };
    auto reply_received_callback = [&] (hailo_status status, Buffer &&reply) {
        {
            std::unique_lock<std::mutex> lock(m_sync_mutex);
            assert(status != HAILO_UNINITIALIZED);
            transfer_status = status;
            out_reply = std::move(reply);
        }
        m_sync_cv.notify_one();
    };
    status = execute_request_async(action_id, request, request_sent_callback,
        reply_received_callback, additional_writes_lambda);
    if (HAILO_COMMUNICATION_CLOSED == status) {
        return make_unexpected(status);
    }
    CHECK_SUCCESS_AS_EXPECTED(status);

    std::unique_lock<std::mutex> lock(m_sync_mutex);
    CHECK_AS_EXPECTED(m_sync_cv.wait_for(lock, get_request_timeout(), [&] { return transfer_status != HAILO_UNINITIALIZED; }),
        HAILO_TIMEOUT, "Timeout waiting for transfer completion");
    CHECK_SUCCESS(transfer_status);

    return out_reply;
}

hailo_status Client::wait_for_execute_request_ready(const MemoryView &request, std::chrono::milliseconds timeout)
{
    return m_connection.wait_for_write_message_async_ready(request.size(), timeout);
}

hailo_status Client::execute_request_async(HailoRpcActionID action_id, const MemoryView &request,
    std::function<void(hailo_status)> request_sent_callback,
    std::function<void(hailo_status, Buffer&&)> reply_received_callback,
    std::function<hailo_status(RpcConnection)> additional_writes_lambda)
{
    rpc_message_header_t header;
    {
        std::unique_lock<std::mutex> lock(m_write_mutex);
        header.size = static_cast<uint32_t>(request.size());
        header.message_id = m_messages_sent++;
        header.action_id = static_cast<uint32_t>(action_id);

        auto status = m_connection.write_message_async(header, request, std::move(request_sent_callback));
        CHECK_SUCCESS(status);

        if (additional_writes_lambda) {
            status = additional_writes_lambda(m_connection);
            CHECK_SUCCESS(status);
        }
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

} // namespace hailort
