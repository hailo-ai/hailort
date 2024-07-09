/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file rpc_connection.cpp
 * @brief RPC connection implementation
 **/

#include "rpc_connection.hpp"

namespace hrpc
{

hailo_status RpcConnection::write_message(const rpc_message_header_t &header, const MemoryView &buffer) {
    auto header_with_magic = header;
    header_with_magic.magic = RPC_MESSAGE_MAGIC;
    auto status = m_raw->write(reinterpret_cast<const uint8_t*>(&header_with_magic), sizeof(header_with_magic));
    if (HAILO_COMMUNICATION_CLOSED == status) {
        return make_unexpected(status);
    }
    CHECK_SUCCESS(status);

    status = m_raw->write(buffer.data(), header.size);
    if (HAILO_COMMUNICATION_CLOSED == status) {
        return make_unexpected(status);
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

Expected<Buffer> RpcConnection::read_message(rpc_message_header_t &header) {
    auto status = m_raw->read(reinterpret_cast<uint8_t*>(&header), sizeof(header));
    if (HAILO_COMMUNICATION_CLOSED == status) {
        return make_unexpected(status);
    }
    CHECK_SUCCESS_AS_EXPECTED(status);
    CHECK_AS_EXPECTED(RPC_MESSAGE_MAGIC == header.magic, HAILO_INTERNAL_FAILURE, "Invalid magic! {} != {}",
        header.magic, RPC_MESSAGE_MAGIC);

    TRY(auto buffer, Buffer::create(header.size, BufferStorageParams::create_dma()));
    status = m_raw->read(buffer.data(), header.size);
    if (HAILO_COMMUNICATION_CLOSED == status) {
        return make_unexpected(status);
    }
    CHECK_SUCCESS_AS_EXPECTED(status);

    return buffer;
}

hailo_status RpcConnection::write_buffer(const MemoryView &buffer)
{
    auto status = m_raw->write(buffer.data(), buffer.size());
    if (HAILO_COMMUNICATION_CLOSED == status) {
        return make_unexpected(status);
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status RpcConnection::read_buffer(MemoryView buffer)
{
    auto status = m_raw->read(buffer.data(), buffer.size());
    if (HAILO_COMMUNICATION_CLOSED == status) {
        return make_unexpected(status);
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status RpcConnection::close()
{
    if (m_raw) {
        return m_raw->close();
    }
    return HAILO_SUCCESS;
}

} // namespace hrpc