/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file client.hpp
 * @brief RPC Client Header
 **/

#ifndef _CLIENT_HPP_
#define _CLIENT_HPP_

#include <hailo/event.hpp>
#include <fcntl.h>
#include <functional>
#include <thread>

#include "rpc_connection.hpp"
#include "hrpc_protocol/serializer.hpp"
#include "hrpc/connection_context.hpp"

namespace hailort
{

#ifndef HAILO_EMULATOR
constexpr std::chrono::milliseconds REQUEST_TIMEOUT(std::chrono::seconds(10));
#else /* ifndef HAILO_EMULATOR */
constexpr std::chrono::milliseconds REQUEST_TIMEOUT(std::chrono::seconds(5000));
#endif /* ifndef HAILO_EMULATOR */

class ResultEvent
{
public:
    static Expected<std::shared_ptr<ResultEvent>> create_shared();
    ResultEvent(EventPtr event);

    Buffer &&release();
    hailo_status signal(Buffer &&value);
    hailo_status wait(std::chrono::milliseconds timeout);

private:
    Buffer m_value;
    EventPtr m_event;
};

class Client
{
public:
    Client(const std::string &device_id) : m_device_id(device_id), m_is_running(true), m_messages_sent(0) {}
    ~Client();

    hailo_status connect();
    Expected<Buffer> execute_request(HailoRpcActionID action_id, const MemoryView &request,
        std::function<hailo_status(RpcConnection)> additional_writes_lambda = nullptr);
    hailo_status wait_for_execute_request_ready(const MemoryView &request, std::chrono::milliseconds timeout);
    hailo_status execute_request_async(HailoRpcActionID action_id, const MemoryView &request,
        std::function<void(hailo_status)> request_sent_callback,
        std::function<void(hailo_status, Buffer&&)> reply_received_callback,
        std::function<hailo_status(RpcConnection)> additional_writes_lambda = nullptr);
    void register_custom_reply(HailoRpcActionID action_id, std::function<hailo_status(const MemoryView&, RpcConnection connection)> callback);
    std::shared_ptr<HailoRTDriver> get_driver() { return m_conn_context->get_driver(); };

protected:
    hailo_status message_loop();

    std::string m_device_id;
    bool m_is_running;
    std::shared_ptr<ConnectionContext> m_conn_context;
    RpcConnection m_connection;
    std::thread m_thread;
    std::unordered_map<uint32_t, std::function<void(hailo_status, Buffer&&)>> m_replies_callbacks;
    std::unordered_map<HailoRpcActionID, std::function<hailo_status(const MemoryView&, RpcConnection)>> m_custom_callbacks;
    uint32_t m_messages_sent;
    std::mutex m_write_mutex;
    std::condition_variable m_replies_cv;
    std::mutex m_replies_mutex;
    std::mutex m_sync_mutex;
    std::condition_variable m_sync_cv;
};

} // namespace hailort

#endif // _CLIENT_HPP_
