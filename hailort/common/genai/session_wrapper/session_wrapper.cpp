/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file session_wrapper.cpp
 * @brief SessionWrapper implementation
 **/

#include "session_wrapper.hpp"
#include "common/genai/serializer/serializer.hpp"
#include "common/file_utils.hpp"
#include "common/thread_safe_queue.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <atomic>
#include <thread>

namespace hailort
{
namespace genai
{

SessionWrapper::SessionWrapper(std::shared_ptr<Session> session) : m_session(session)
{
}

SessionWrapper::SessionWrapper(SessionWrapper&& other) noexcept : m_session(std::move(other.m_session))
{
}

SessionWrapper& SessionWrapper::operator=(SessionWrapper&& other) noexcept
{
    m_session = std::move(other.m_session);
    return *this;
}

Expected<std::shared_ptr<Buffer>> SessionWrapper::read(std::chrono::milliseconds timeout)
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

Expected<size_t> SessionWrapper::read(MemoryView buffer, std::chrono::milliseconds timeout)
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

hailo_status SessionWrapper::write(MemoryView buffer, std::chrono::milliseconds timeout)
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

Expected<std::shared_ptr<Buffer>> SessionWrapper::execute(MemoryView write_buffer)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    CHECK_SUCCESS(write(write_buffer));
    return read();
}

Expected<size_t> SessionWrapper::execute(MemoryView write_buffer, MemoryView read_buffer)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    CHECK_SUCCESS(write(write_buffer));
    return read(read_buffer);
}

Expected<std::shared_ptr<Buffer>> SessionWrapper::execute(std::vector<MemoryView> &write_buffers)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    for (const auto &buffer : write_buffers) {
        CHECK_SUCCESS(write(buffer, LONG_TIMEOUT));
    }
    return read();
}

hailo_status SessionWrapper::close()
{
    if (m_session != nullptr) {
        return m_session->close();
    }
    return HAILO_SUCCESS;
}

hailo_status SessionWrapper::send_file_chunked(const std::string &file_path, size_t file_size_to_send, std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    TimeoutGuard timeout_guard(timeout);

    std::ifstream file(file_path, std::ios::binary);
    CHECK(file.is_open(), HAILO_OPEN_FILE_FAILURE, "Failed to open file: {}", file_path);

    // Calculate number of chunks
    size_t chunk_size = CHUNKED_TRANSFER_CHUNK_SIZE;
    uint64_t total_chunks = (file_size_to_send + chunk_size - 1) / chunk_size;

    // DUAL BUFFERING OPTIMIZATION: Use existing SpscQueue with 2 buffers
    TRY(auto buffer_a, Buffer::create_shared(chunk_size, BufferStorageParams::create_dma()));
    TRY(auto buffer_b, Buffer::create_shared(chunk_size, BufferStorageParams::create_dma()));

    TRY(auto shutdown_event, Event::create_shared(Event::State::not_signalled));
    TRY(auto buffer_queue, SpscQueue<BufferPtr>::create(2, shutdown_event));

    // Initially enqueue both buffers - they're ready for use
    CHECK_SUCCESS(buffer_queue.enqueue(buffer_a));
    CHECK_SUCCESS(buffer_queue.enqueue(buffer_b));

    for (uint64_t chunk_index = 0; chunk_index < total_chunks; ++chunk_index) {
        uint64_t chunk_offset = chunk_index * chunk_size;
        uint64_t remaining_bytes = file_size_to_send - chunk_offset;
        uint32_t current_chunk_size = static_cast<uint32_t>(std::min(static_cast<uint64_t>(chunk_size), remaining_bytes));

        TRY(auto current_buffer, buffer_queue.dequeue());

        file.read(reinterpret_cast<char*>(current_buffer->data()), current_chunk_size);
        CHECK(file.gcount() == current_chunk_size, HAILO_FILE_OPERATION_FAILURE,
            "Failed to read chunk {} from file {}", chunk_index, file_path);

        CHECK_SUCCESS(m_session->wait_for_write_async_ready(current_chunk_size, timeout_guard.get_remaining_timeout()));
        CHECK_SUCCESS(m_session->write_async(current_buffer->data(), current_chunk_size,
            [&buffer_queue, current_buffer](hailo_status status) {
                assert(HAILO_SUCCESS == status);
                (void)status; // Mark as used to avoid compiler warning
                buffer_queue.enqueue(current_buffer);
            }));
    }

    uint8_t ack;
    CHECK_SUCCESS(m_session->read(&ack, sizeof(ack), timeout_guard.get_remaining_timeout()));
    CHECK(ack == 1, HAILO_INTERNAL_FAILURE, "Server failed to receive file chunks");

    file.close();
    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<Buffer>> SessionWrapper::receive_file_chunked(uint64_t expected_file_size, std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    TimeoutGuard timeout_guard(timeout);


    uint64_t chunk_size = CHUNKED_TRANSFER_CHUNK_SIZE;
    uint64_t total_chunks = (expected_file_size + chunk_size - 1) / chunk_size;

    TRY(auto complete_file_buffer, Buffer::create_shared(expected_file_size, BufferStorageParams::create_dma()));

    uint64_t total_bytes_received = 0;
    for (uint64_t chunk_index = 0; chunk_index < total_chunks; ++chunk_index) {
        uint64_t chunk_offset = chunk_index * chunk_size;
        uint64_t remaining_bytes = expected_file_size - chunk_offset;
        uint64_t current_chunk_size = std::min(chunk_size, remaining_bytes);

        CHECK_SUCCESS(m_session->read(complete_file_buffer->data() + chunk_offset, 
            current_chunk_size, timeout_guard.get_remaining_timeout()));

        total_bytes_received += current_chunk_size;
    }
    CHECK((total_bytes_received == expected_file_size), HAILO_INVALID_OPERATION,
        "Total bytes received {} doesn't match expected file size {}", total_bytes_received, expected_file_size);

    uint8_t ack = 1;
    CHECK_SUCCESS(m_session->write(&ack, sizeof(ack), timeout_guard.get_remaining_timeout()));
    
    return complete_file_buffer;
}

} /* namespace genai */
} /* namespace hailort */
