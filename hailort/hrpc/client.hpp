/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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
#include "utils/pool_allocator.hpp"
#include "vdma/channel/transfer_common.hpp"
#include "rpc_callbacks/rpc_callbacks_dispatcher.hpp"

namespace hailort
{

#ifndef HAILO_EMULATOR
constexpr std::chrono::milliseconds REQUEST_TIMEOUT(std::chrono::seconds(10));
#else /* ifndef HAILO_EMULATOR */
constexpr std::chrono::milliseconds REQUEST_TIMEOUT(std::chrono::seconds(5000));
#endif /* ifndef HAILO_EMULATOR */

class Client;
class SyncRequest
{
public:
    SyncRequest(Client &client, std::mutex &sync_mutex, std::condition_variable &sync_cv);
    Expected<rpc_message_t> execute(HailoRpcActionID action_id, const MemoryView &request,
        std::vector<TransferBuffer> &&additional_buffers = {});

private:
    Client &m_client;
    std::mutex &m_sync_mutex;
    std::condition_variable &m_sync_cv;
    hailo_status m_transfer_status;
    rpc_message_t m_out_reply;
};

class Client
{
public:
    Client(const std::string &device_id) : m_device_id(device_id), m_is_running(true), m_messages_sent(0) {}
    ~Client();

    hailo_status connect();
    Expected<rpc_message_t> execute_request(HailoRpcActionID action_id, const MemoryView &request,
        std::vector<TransferBuffer> &&additional_buffers = {});
    hailo_status wait_for_execute_request_ready(const MemoryView &request, std::chrono::milliseconds timeout);
    hailo_status execute_request_async(HailoRpcActionID action_id, const MemoryView &request,
        std::function<void(hailo_status)> request_sent_callback,
        std::function<void(hailo_status, rpc_message_t)> reply_received_callback,
        std::vector<TransferBuffer> &&additional_buffers = {});
    void register_custom_reply(HailoRpcActionID action_id, std::function<hailo_status(const MemoryView&, RpcConnection connection)> callback);
    std::shared_ptr<HailoRTDriver> get_driver() { return m_conn_context->get_driver(); };
    const std::string &device_id() const { return m_device_id; }
    Expected<BufferPtr> allocate_request_buffer();
    std::shared_ptr<ClientCallbackDispatcherManager> callback_dispatcher_manager();

protected:
    hailo_status message_loop();

    std::string m_device_id;
    bool m_is_running;
    std::shared_ptr<ConnectionContext> m_conn_context;
    RpcConnection m_connection;
    std::thread m_thread;
    std::unordered_map<uint32_t, std::function<void(hailo_status, rpc_message_t)>> m_replies_callbacks;
    std::unordered_map<HailoRpcActionID, std::function<hailo_status(const MemoryView&, RpcConnection)>> m_custom_callbacks;
    uint32_t m_messages_sent;
    std::mutex m_write_mutex;
    std::condition_variable m_replies_cv;
    std::mutex m_replies_mutex;
    std::mutex m_sync_mutex;
    std::condition_variable m_sync_cv;
    std::shared_ptr<PoolAllocator> m_pool_allocator;
    std::shared_ptr<ClientCallbackDispatcherManager> m_callback_dispatcher_manager;
    std::shared_ptr<ObjectPool<SyncRequest>> m_sync_requests_pool;
};

} // namespace hailort

#endif // _CLIENT_HPP_
