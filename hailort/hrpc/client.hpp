/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file client.hpp
 * @brief RPC Client Header
 **/

#ifndef _CLIENT_HPP_
#define _CLIENT_HPP_

#include "rpc_connection.hpp"
#include "hrpc/connection_context.hpp"
#include "vdma/transfer_common.hpp"
#include "rpc_callbacks/rpc_callbacks_dispatcher.hpp"
#include "common/object_pool.hpp"
#include "utils/thread_safe_map.hpp"
#include "common/timeouts.hpp"

namespace hailort
{

using HrpcCallback = std::function<void(rpc_message_t)>;
using message_id_t = uint32_t;

struct reply_data_t
{
    HrpcCallback callback;
    std::vector<TransferBuffer> read_buffers;
};

class Client
{
public:
    static Expected<std::shared_ptr<Client>> created_connected(const std::string &device_id);
    Client(const std::string &device_id)
        : m_device_id(device_id), m_is_running(true), m_messages_sent(0) {}
    ~Client();

    Expected<rpc_message_t> execute_request(uint32_t action_id, const MemoryView &request,
        std::vector<TransferBuffer> &&write_buffers = {}, std::vector<TransferBuffer> &&read_buffers = {},
        std::chrono::milliseconds timeout = HRPC_REQUEST_TIMEOUT);
    hailo_status wait_for_execute_request_ready(const MemoryView &request, std::chrono::milliseconds timeout);
    // On success, the callback runs once on the message-loop thread, unless a started read of the reply buffers
    // times out. On failure, it never runs. When it does not run, it may be kept until the client is destroyed.
    // It must not block on sync requests of this client.
    hailo_status execute_request_async(uint32_t action_id, const MemoryView &request,
        HrpcCallback reply_received_callback, std::vector<TransferBuffer> &&write_buffers = {},
        std::vector<TransferBuffer> &&read_buffers = {});
    void set_notification_callback(std::function<hailo_status(const MemoryView&)> callback);
    std::shared_ptr<HailoRTDriver> get_driver() { return m_conn_context->get_driver(); };
    const std::string &device_id() const { return m_device_id; }
    Device::Type device_type() const { return m_conn_context->device_type(); }
    Expected<BufferPtr> allocate_request_buffer();
    std::shared_ptr<ClientCallbackDispatcherManager> callback_dispatcher_manager();
    std::thread::id message_loop_thread_id() const { return m_message_loop.get_id(); }
    // Once set, no callback is invoked anymore. Stays readable after the client is destroyed.
    std::shared_ptr<const std::atomic_bool> message_loop_finished_flag() const { return m_is_message_loop_finished; }

private:
    hailo_status connect();
    Expected<message_id_t> execute_request_async_impl(uint32_t action_id, const MemoryView &request,
        HrpcCallback reply_received_callback, std::vector<TransferBuffer> &&write_buffers,
        std::vector<TransferBuffer> &&read_buffers);
    void park_reply_until_close(reply_data_t &&reply_data);

protected:
    hailo_status message_loop();

    std::string m_device_id;
    std::atomic_bool m_is_running;
    std::shared_ptr<ConnectionContext> m_conn_context;
    RpcConnectionPtr m_connection;
    std::thread m_message_loop;
    std::shared_ptr<std::atomic_bool> m_is_message_loop_finished;
    ThreadSafeMap<message_id_t, reply_data_t> m_reply_data;
    // Callback-owned buffers of these replies may still be accessed by a transfer, until the connection closes.
    std::vector<reply_data_t> m_parked_replies;
    std::mutex m_parked_replies_mutex;
    std::function<hailo_status(const MemoryView&)> m_notification_callback;
    uint32_t m_messages_sent;
    std::mutex m_write_mutex;
    std::shared_ptr<PoolAllocator> m_pool_allocator;
    std::shared_ptr<ClientCallbackDispatcherManager> m_callback_dispatcher_manager;
};

} // namespace hailort

#endif // _CLIENT_HPP_
