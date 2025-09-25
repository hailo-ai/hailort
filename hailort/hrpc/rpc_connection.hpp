/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file rpc_connection.hpp
 * @brief RPC Connection Header
 **/

#ifndef _RPC_CONNECTION_HPP_
#define _RPC_CONNECTION_HPP_

#include "hailo/hailo_session.hpp"

#include "hailo/buffer.hpp"
#include "common/utils.hpp"
#include "common/internal_env_vars.hpp"
#include "common/buffer_pool.hpp"
#include "utils/pool_allocator.hpp"
#include "vdma/channel/transfer_common.hpp"

#define RPC_MESSAGE_MAGIC (0x8A554432)
#define HAILORT_SERVER_PORT (12133)

namespace hailort
{

#pragma pack(push, 1)
struct rpc_message_header_t
{
    uint32_t magic;
    uint32_t size;
    uint32_t message_id;
    uint32_t action_id;
};
#pragma pack(pop)

struct rpc_message_t
{
    rpc_message_header_t header;
    BufferPtr buffer;
};

class RpcConnection
{
public:
    struct Params
    {
    public:
        static Expected<Params> create(std::shared_ptr<Session> session);

        std::shared_ptr<Session> session;
        std::shared_ptr<PoolAllocator> write_rpc_headers_allocator;
        std::shared_ptr<PoolAllocator> read_rpc_headers_allocator;
        std::shared_ptr<PoolAllocator> read_rpc_body_allocator;
        std::shared_ptr<std::mutex> read_mutex;
        std::shared_ptr<std::condition_variable> read_cv;
    };

    RpcConnection() = default;
    RpcConnection(Params &&params) :
            m_session(params.session), m_write_rpc_headers_allocator(params.write_rpc_headers_allocator),
            m_read_rpc_headers_allocator(params.read_rpc_headers_allocator), m_read_rpc_body_allocator(params.read_rpc_body_allocator),
            m_read_mutex(params.read_mutex),
            m_read_cv(params.read_cv) {}
    ~RpcConnection() = default;

    Expected<rpc_message_t> read_message();

    hailo_status read_buffer(MemoryView buffer);
    hailo_status read_buffers(std::vector<TransferBuffer> &&buffers);

    hailo_status wait_for_write_message_async_ready(size_t buffer_size, std::chrono::milliseconds timeout);
    hailo_status write_message_async(const rpc_message_header_t &header, const MemoryView &buffer,
        std::function<void(hailo_status)> &&callback);

    hailo_status write_message_async(const rpc_message_header_t &header, TransferRequest &&transfer_request);

    hailo_status wait_for_write_buffer_async_ready(size_t buffer_size, std::chrono::milliseconds timeout);
    hailo_status write_buffer_async(const MemoryView &buffer, std::function<void(hailo_status)> &&callback);

    hailo_status wait_for_read_buffer_async_ready(size_t buffer_size, std::chrono::milliseconds timeout);

    hailo_status close();

private:
    std::shared_ptr<Session> m_session;
    std::shared_ptr<PoolAllocator> m_write_rpc_headers_allocator;
    std::shared_ptr<PoolAllocator> m_read_rpc_headers_allocator;
    std::shared_ptr<PoolAllocator> m_read_rpc_body_allocator;
    EventPtr m_shutdown_event;
    std::shared_ptr<std::mutex> m_read_mutex;
    std::shared_ptr<std::condition_variable> m_read_cv;
};

} // namespace hailort

#endif // _RPC_CONNECTION_HPP_