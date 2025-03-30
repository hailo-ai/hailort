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
#include "common/utils.hpp"

#include "common/genai/serializer/serializer.hpp"

namespace hailort
{
namespace genai
{

class SessionWrapper final
{
public:
    SessionWrapper(std::shared_ptr<Session> session) : m_session(session) {}
    ~SessionWrapper() = default;

    Expected<std::shared_ptr<Buffer>> read(std::chrono::milliseconds timeout = Session::DEFAULT_READ_TIMEOUT)
    {
        TimeoutGuard timeout_guard(timeout);
        size_t size_to_read = 0;
        CHECK_SUCCESS_AS_EXPECTED(m_session->read(reinterpret_cast<uint8_t*>(&size_to_read),
            sizeof(size_to_read), timeout_guard.get_remaining_timeout()));

        TRY(auto buffer, Buffer::create_shared(size_to_read, BufferStorageParams::create_dma()));
        CHECK_SUCCESS(m_session->read(buffer->data(), size_to_read, timeout_guard.get_remaining_timeout()));

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

        CHECK_SUCCESS(m_session->read(buffer.data(), size_to_read, timeout_guard.get_remaining_timeout()));
        return size_to_read;
    }

    hailo_status write(MemoryView buffer, std::chrono::milliseconds timeout = Session::DEFAULT_WRITE_TIMEOUT)
    {
        TimeoutGuard timeout_guard(timeout);
        // First we send the buffer's size. Then the buffer itself.
        // TODO: Use hrpc protocol
        size_t size = buffer.size();
        CHECK_SUCCESS(m_session->write(reinterpret_cast<const uint8_t*>(&size), sizeof(size), timeout_guard.get_remaining_timeout()));
        CHECK_SUCCESS(m_session->write(buffer.data(), size, timeout_guard.get_remaining_timeout()));

        return HAILO_SUCCESS;
    }

private:
    std::shared_ptr<Session> m_session;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_COMMON_GENAI_SESSION_WRAPPER_HPP_ */
