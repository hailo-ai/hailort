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

#include "raw_connection.hpp"

#include "hailo/buffer.hpp"
#include "common/utils.hpp"

#define RPC_MESSAGE_MAGIC (0x8A554432)


namespace hrpc
{

#pragma pack(push, 1)
struct rpc_message_header_t
{
    uint32_t magic; // TODO: consider removing. check if hurts performance
    uint32_t size;
    uint32_t message_id;
    uint32_t action_id;
};
#pragma pack(pop)

class RpcConnection
{
public:
    RpcConnection() = default;
    explicit RpcConnection(std::shared_ptr<RawConnection> raw) : m_raw(raw) {}

    hailo_status write_message(const rpc_message_header_t &header, const MemoryView &buffer);
    Expected<Buffer> read_message(rpc_message_header_t &header);

    hailo_status write_buffer(const MemoryView &buffer);
    hailo_status read_buffer(MemoryView buffer);

    hailo_status close();

private:
    std::shared_ptr<RawConnection> m_raw;
};

} // namespace hrpc

#endif // _RPC_CONNECTION_HPP_