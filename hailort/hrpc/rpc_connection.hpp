/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
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

#define RPC_MESSAGE_MAGIC (0x8A554432)
#define DEFAULT_PCIE_PORT (12133)

namespace hailort
{

// TODO: HRT-15413 - Remove Env var usage. Use the API to choose port, use defaults ports for each server.
inline uint16_t get_pcie_port()
{
    auto port_str = get_env_variable(HAILO_CONNECTION_PCIE_PORT_ENV_VAR);
    if (port_str) {
        return static_cast<uint16_t>(std::stoi(port_str.value()));
    }
    return DEFAULT_PCIE_PORT;
}

#pragma pack(push, 1)
struct rpc_message_header_t
{
    uint32_t magic; // TODO: consider removing. check if hurts performance
    uint32_t size;
    uint32_t message_id;
    uint32_t action_id;
};
#pragma pack(pop)

struct rpc_message_t
{
    rpc_message_header_t header;
    Buffer buffer;
};

class RpcConnection
{
public:
    static Expected<RpcConnection> create(std::shared_ptr<Session> raw);
    RpcConnection() = default;
    explicit RpcConnection(std::shared_ptr<Session> raw, std::shared_ptr<DmaAbleBufferPool> write_rpc_headers,
        std::shared_ptr<DmaAbleBufferPool> read_rpc_headers, EventPtr shutdown_event, std::shared_ptr<std::mutex> read_mutex,
        std::shared_ptr<std::mutex> write_mutex, std::shared_ptr<std::condition_variable> read_cv, std::shared_ptr<std::condition_variable> write_cv) :
            m_session(raw), m_write_rpc_headers(write_rpc_headers), m_read_rpc_headers(read_rpc_headers), m_shutdown_event(shutdown_event),
            m_read_mutex(read_mutex), m_write_mutex(write_mutex), m_read_cv(read_cv), m_write_cv(write_cv) {}

    hailo_status write_message(const rpc_message_header_t &header, const MemoryView &buffer);
    Expected<rpc_message_t> read_message();

    hailo_status write_buffer(const MemoryView &buffer);
    hailo_status read_buffer(MemoryView buffer);

    hailo_status wait_for_write_message_async_ready(size_t buffer_size, std::chrono::milliseconds timeout);
    hailo_status write_message_async(const rpc_message_header_t &header, const MemoryView &buffer,
        std::function<void(hailo_status)> &&callback);

    hailo_status wait_for_write_buffer_async_ready(size_t buffer_size, std::chrono::milliseconds timeout);
    hailo_status write_buffer_async(const MemoryView &buffer, std::function<void(hailo_status)> &&callback);

    hailo_status wait_for_read_buffer_async_ready(size_t buffer_size, std::chrono::milliseconds timeout);
    hailo_status read_buffer_async(MemoryView buffer, std::function<void(hailo_status)> &&callback);

    hailo_status close();

private:
    std::shared_ptr<Session> m_session;
    DmaAbleBufferPoolPtr m_write_rpc_headers;
    DmaAbleBufferPoolPtr m_read_rpc_headers;
    EventPtr m_shutdown_event;
    std::shared_ptr<std::mutex> m_read_mutex;
    std::shared_ptr<std::mutex> m_write_mutex;
    std::shared_ptr<std::condition_variable> m_read_cv;
    std::shared_ptr<std::condition_variable> m_write_cv;
};

} // namespace hailort

#endif // _RPC_CONNECTION_HPP_