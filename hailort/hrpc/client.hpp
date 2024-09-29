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


namespace hrpc
{

#define REQUEST_TIMEOUT std::chrono::milliseconds(10000)

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
    Client(const std::string &device_id) : m_device_id(device_id), m_is_running(true) {}
    ~Client();

    hailo_status connect();
    Expected<Buffer> execute_request(HailoRpcActionID action_id, const MemoryView &request,
        std::function<hailo_status(RpcConnection)> write_buffers_callback = nullptr);
    void register_custom_reply(HailoRpcActionID action_id, std::function<hailo_status(const MemoryView&, RpcConnection connection)> callback);

protected:
    hailo_status message_loop();

    std::string m_device_id;
    bool m_is_running;
    std::shared_ptr<ConnectionContext> m_conn_context;
    RpcConnection m_connection;
    std::thread m_thread;
    std::unordered_map<uint32_t, std::shared_ptr<ResultEvent>> m_events;
    std::unordered_map<HailoRpcActionID, std::function<hailo_status(const MemoryView&, RpcConnection)>> m_custom_callbacks;
    uint32_t m_messages_sent = 0;
    std::mutex m_write_mutex;
    std::condition_variable m_events_cv;
    std::mutex m_events_mutex;
};

} // namespace hrpc

#endif // _CLIENT_HPP_
