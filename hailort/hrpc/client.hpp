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

#include "rpc_connection.hpp"
#include "hrpc/connection_context.hpp"
#include "vdma/channel/transfer_common.hpp"
#include "rpc_callbacks/rpc_callbacks_dispatcher.hpp"
#include "common/object_pool.hpp"
#include "utils/thread_safe_map.hpp"

namespace hailort
{

#ifndef HAILO_EMULATOR
constexpr std::chrono::milliseconds REQUEST_TIMEOUT(std::chrono::seconds(10));
#else /* ifndef HAILO_EMULATOR */
constexpr std::chrono::milliseconds REQUEST_TIMEOUT(std::chrono::seconds(5000));
#endif /* ifndef HAILO_EMULATOR */

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
    Client(const std::string &device_id) : m_device_id(device_id), m_is_running(true), m_messages_sent(0) {}
    ~Client();

    hailo_status connect(bool is_localhost = false);
    Expected<rpc_message_t> execute_request(uint32_t action_id, const MemoryView &request,
        std::vector<TransferBuffer> &&write_buffers = {}, std::vector<TransferBuffer> &&read_buffers = {},
        std::chrono::milliseconds timeout = REQUEST_TIMEOUT);
    hailo_status wait_for_execute_request_ready(const MemoryView &request, std::chrono::milliseconds timeout);
    hailo_status execute_request_async(uint32_t action_id, const MemoryView &request,
        HrpcCallback reply_received_callback, std::vector<TransferBuffer> &&write_buffers = {},
        std::vector<TransferBuffer> &&read_buffers = {});
    void set_notification_callback(std::function<hailo_status(const MemoryView&)> callback);
    std::shared_ptr<HailoRTDriver> get_driver() { return m_conn_context->get_driver(); };
    const std::string &device_id() const { return m_device_id; }
    Expected<BufferPtr> allocate_request_buffer();
    std::shared_ptr<ClientCallbackDispatcherManager> callback_dispatcher_manager();

private:
    Expected<message_id_t> execute_request_async_impl(uint32_t action_id, const MemoryView &request,
        HrpcCallback reply_received_callback, std::vector<TransferBuffer> &&write_buffers,
        std::vector<TransferBuffer> &&read_buffers);

protected:
    hailo_status message_loop();

    std::string m_device_id;
    std::atomic_bool m_is_running;
    std::shared_ptr<ConnectionContext> m_conn_context;
    RpcConnectionPtr m_connection;
    std::thread m_message_loop;
    ThreadSafeMap<message_id_t, reply_data_t> m_reply_data;
    std::function<hailo_status(const MemoryView&)> m_notification_callback;
    uint32_t m_messages_sent;
    std::mutex m_write_mutex;
    std::shared_ptr<PoolAllocator> m_pool_allocator;
    std::shared_ptr<ClientCallbackDispatcherManager> m_callback_dispatcher_manager;
};

} // namespace hailort

#endif // _CLIENT_HPP_
