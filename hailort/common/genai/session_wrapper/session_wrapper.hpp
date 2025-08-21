/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file session_wrapper.hpp
 * @brief a wrapper for session
 **/

#ifndef _HAILO_COMMON_GENAI_SESSION_WRAPPER_HPP_
#define _HAILO_COMMON_GENAI_SESSION_WRAPPER_HPP_

#include "hailo/hailort.h"
#include "hailo/buffer.hpp"
#include "hailo/hailo_session.hpp"
#include "hailo/genai/common.hpp"
#include "common/utils.hpp"


namespace hailort
{
namespace genai
{

constexpr static const std::chrono::seconds LONG_TIMEOUT = std::chrono::seconds(45);

class SessionWrapper final
{
public:
    SessionWrapper(std::shared_ptr<Session> session) : m_session(session) {}
    ~SessionWrapper() = default;

    SessionWrapper(SessionWrapper&& other) noexcept : m_session(std::move(other.m_session)) {}
    SessionWrapper& operator=(SessionWrapper&& other) noexcept
    {
        m_session = std::move(other.m_session);
        return *this;
    }

    Expected<std::shared_ptr<Buffer>> read(std::chrono::milliseconds timeout = Session::DEFAULT_READ_TIMEOUT)
    {
        TimeoutGuard timeout_guard(timeout);
        size_t size_to_read = 0;
        CHECK_SUCCESS_AS_EXPECTED(m_session->read(reinterpret_cast<uint8_t*>(&size_to_read),
            sizeof(size_to_read), timeout_guard.get_remaining_timeout()));

        TRY(auto buffer, Buffer::create_shared(size_to_read, BufferStorageParams::create_dma()));
        if (0 != size_to_read) {
            // In order to avoid receiving empty buffers, we check if the size is 0 - same as socket
            CHECK_SUCCESS(m_session->read(buffer->data(), size_to_read, timeout_guard.get_remaining_timeout()));
        }

        return buffer;
    }

    Expected<size_t> read(MemoryView buffer, std::chrono::milliseconds timeout = Session::DEFAULT_READ_TIMEOUT)
    {
        TimeoutGuard timeout_guard(timeout);
        size_t size_to_read = 0;
        CHECK_SUCCESS_AS_EXPECTED(m_session->read(reinterpret_cast<uint8_t*>(&size_to_read),
            sizeof(size_to_read), timeout_guard.get_remaining_timeout()));

        CHECK(size_to_read <= buffer.size(), HAILO_INVALID_OPERATION,
            "Read buffer is smaller then necessary. Buffer size = {}, generation size = {}",
            buffer.size(), size_to_read);

        if (0 != size_to_read) {
            // In order to avoid receiving empty buffers, we check if the size is 0 - same as socket
            CHECK_SUCCESS(m_session->read(buffer.data(), size_to_read, timeout_guard.get_remaining_timeout()));
        }

        return size_to_read;
    }

    hailo_status write(MemoryView buffer, std::chrono::milliseconds timeout = Session::DEFAULT_WRITE_TIMEOUT)
    {
        TimeoutGuard timeout_guard(timeout);
        // First we send the buffer's size. Then the buffer itself.
        // TODO: Use hrpc protocol
        size_t size = buffer.size();
        CHECK_SUCCESS(m_session->write(reinterpret_cast<const uint8_t*>(&size), sizeof(size), timeout_guard.get_remaining_timeout()));
        if (0 != size) {
            // In order to avoid sending empty buffers, we check if the size is 0 - same as socket
            CHECK_SUCCESS(m_session->write(buffer.data(), size, timeout_guard.get_remaining_timeout()));
        }

        return HAILO_SUCCESS;
    }

    Expected<std::shared_ptr<Buffer>> execute(MemoryView write_buffer)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        CHECK_SUCCESS(write(write_buffer));
        return read();
    }

    Expected<size_t> execute(MemoryView write_buffer, MemoryView read_buffer)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        CHECK_SUCCESS(write(write_buffer));
        return read(read_buffer);
    }

    Expected<std::shared_ptr<Buffer>> execute(std::vector<MemoryView> &write_buffers)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        for (const auto &buffer : write_buffers) {
            CHECK_SUCCESS(write(buffer, LONG_TIMEOUT));
        }
        return read();
    }

    hailo_status close()
    {
        if (m_session != nullptr) {
            return m_session->close();
        }
        return HAILO_SUCCESS;
    }

private:
    std::shared_ptr<Session> m_session;
    std::mutex m_mutex;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_COMMON_GENAI_SESSION_WRAPPER_HPP_ */
