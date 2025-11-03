/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file response_writer.hpp
 * @brief RPC Response Writer implementing method write()
 **/

#ifndef _RESPONSE_WRITER_HPP_
#define _RESPONSE_WRITER_HPP_

#include "hailo/buffer.hpp"
#include "rpc_connection.hpp"

namespace hailort
{

constexpr auto WRITE_TIMEOUT = std::chrono::seconds(10);

class ResponseWriter
{
public:
    ResponseWriter() = default;
    ResponseWriter(const rpc_message_header_t &header, RpcConnectionPtr connection, std::shared_ptr<std::mutex> mutex) :
        m_message_header(header), m_client_connection(connection), m_mutex(mutex) {}

    template<typename BufferType=BufferPtr>
    hailo_status write(hailo_status status, Buffer &&response = {}, std::vector<BufferType> &&additional_writes = {})
    {
        if (HAILO_SUCCESS != status) {
            // Non-success responses will return an error-header only.
            response = {};
            additional_writes.clear();
        }

        m_message_header.status = status;
        m_message_header.size = static_cast<uint32_t>(response.size());

        // Capture buffers in response lambda to ensure they aren't freed.
        // TODO: Remove this allocation and make response a BufferType.
        auto response_guard = make_shared_nothrow<Buffer>(std::move(response));
        CHECK_NOT_NULL(response_guard, HAILO_OUT_OF_HOST_MEMORY);

        auto callback = [response_guard, additional_writes] (hailo_status status) {
            if (HAILO_SUCCESS != status) {
                LOGGER__ERROR("Failed to write Hrpc response. status: {}", status);
            }
        };

        std::unique_lock<std::mutex> lock(*m_mutex);

        status = m_client_connection->wait_for_write_message_async_ready(response.size(), WRITE_TIMEOUT);
        CHECK_SUCCESS(status);

        status = m_client_connection->write_message_async(m_message_header, response_guard->as_view(), callback);
        CHECK_SUCCESS(status);

        for (const auto &buffer : additional_writes) {
            status = m_client_connection->wait_for_write_message_async_ready(buffer->size(), WRITE_TIMEOUT);
            CHECK_SUCCESS(status);

            status = m_client_connection->write_buffer_async(buffer->as_view(), callback);
            CHECK_SUCCESS(status);
        }

        return HAILO_SUCCESS;
    }

    void set_action_id(uint32_t action_id)
    {
        m_message_header.action_id = action_id;
    }

private:
    rpc_message_header_t m_message_header;
    RpcConnectionPtr m_client_connection;
    std::shared_ptr<std::mutex> m_mutex;
};

} // namespace hailort

#endif // _RESPONSE_WRITER_HPP_