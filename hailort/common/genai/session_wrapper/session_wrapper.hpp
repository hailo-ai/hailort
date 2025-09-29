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
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <chrono>


namespace hailort
{
namespace genai
{

constexpr static const std::chrono::seconds LONG_TIMEOUT = std::chrono::minutes(2);

class SessionWrapper final
{
public:
    SessionWrapper(std::shared_ptr<Session> session);
    ~SessionWrapper() = default;

    SessionWrapper(SessionWrapper&& other) noexcept;
    SessionWrapper& operator=(SessionWrapper&& other) noexcept;

    Expected<std::shared_ptr<Buffer>> read(std::chrono::milliseconds timeout = Session::DEFAULT_READ_TIMEOUT);

    Expected<size_t> read(MemoryView buffer, std::chrono::milliseconds timeout = Session::DEFAULT_READ_TIMEOUT);

    hailo_status write(MemoryView buffer, std::chrono::milliseconds timeout = Session::DEFAULT_WRITE_TIMEOUT);

    Expected<std::shared_ptr<Buffer>> execute(MemoryView write_buffer);

    Expected<size_t> execute(MemoryView write_buffer, MemoryView read_buffer);

    Expected<std::shared_ptr<Buffer>> execute(std::vector<MemoryView> &write_buffers);

    hailo_status close();

    // Chunked file transfer methods - consider using read/write in chunks whenever a message is larger than a certain size
    hailo_status send_file_chunked(const std::string &file_path, size_t file_size_to_send, std::chrono::milliseconds timeout = LONG_TIMEOUT);
    Expected<std::shared_ptr<Buffer>> receive_file_chunked(uint64_t expected_file_size, std::chrono::milliseconds timeout = LONG_TIMEOUT);

private:
    std::shared_ptr<Session> m_session;
    std::mutex m_mutex;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_COMMON_GENAI_SESSION_WRAPPER_HPP_ */
