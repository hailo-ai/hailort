/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file remote_process_stream.cpp
 **/

#include "remote_process_stream.hpp"

#include "common/os_utils.hpp"

namespace hailort
{

constexpr size_t MIN_QUEUE_SIZE = 2;
constexpr size_t DEFAULT_QUEUE_SIZE = 4;
constexpr size_t RemoteProcessBufferPool::BACKING_ARRAY_LENGTH;

Expected<std::unique_ptr<RemoteProcessBufferPool>> RemoteProcessBufferPool::create(
    hailo_stream_direction_t stream_direction, size_t frame_size, size_t queue_size)
{
    CHECK((queue_size >= MIN_QUEUE_SIZE) && (queue_size < BACKING_ARRAY_LENGTH), HAILO_INVALID_ARGUMENT,
        "Queue size must be in the range [{}, {}) (received {})", MIN_QUEUE_SIZE, BACKING_ARRAY_LENGTH, queue_size);

    hailo_status status = HAILO_UNINITIALIZED;
    auto buffer_pool = make_unique_nothrow<RemoteProcessBufferPool>(stream_direction, frame_size, queue_size,
        status);
    CHECK_NOT_NULL_AS_EXPECTED(buffer_pool, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status, "Failed creating remote process buffer pool");
    return buffer_pool;
}

RemoteProcessBufferPool::RemoteProcessBufferPool(hailo_stream_direction_t stream_direction, size_t frame_size,
    size_t queue_size, hailo_status &status) :
        m_hw_buffers_queue(queue_size + 1),
        m_host_buffers_queue(queue_size + 1)
{
    // On H2D, the user will dequeue from user_buffers_queue, fill it and sent to the hw_buffers_queue.
    // On D2H, the read thread will dequeue from hw_buffers_pool, read into it and sent it to the user_buffers_queue.
    auto &queue_to_fill = (HAILO_H2D_STREAM == stream_direction) ?
        m_host_buffers_queue :
        m_hw_buffers_queue;

    for (size_t i = 0; i < queue_size; i++) {
        // We create here the buffer as dma-able since it force them to be shared between processes.
        // In the future, we may have some new buffer storage params for shared memory.
        auto buffer = Buffer::create_shared(frame_size, BufferStorageParams::create_dma());
        if (!buffer) {
            LOGGER__ERROR("Failed allocating buffer");
            status = buffer.status();
            return;
        }

        m_buffers_guard.emplace_back(buffer.release());

        auto buffer_view = MemoryView(*m_buffers_guard.back());
        queue_to_fill.push_back(SharedBuffer{buffer_view, SharedBuffer::Type::DATA});
    }

    status = HAILO_SUCCESS;
}

void RemoteProcessBufferPool::abort()
{
    {
        std::unique_lock<RecursiveSharedMutex> lock(m_mutex);
        m_is_aborted = true;
    }
    m_cv.notify_all();
}

void RemoteProcessBufferPool::clear_abort()
{
    std::unique_lock<RecursiveSharedMutex> lock(m_mutex);
    m_is_aborted = false;
}

Expected<RemoteProcessBufferPool::SharedBuffer> RemoteProcessBufferPool::dequeue_hw_buffer(
    std::chrono::milliseconds timeout)
{
    std::unique_lock<RecursiveSharedMutex> lock(m_mutex);
    auto status = cv_wait_for(lock, timeout, [this]() {
        return !m_hw_buffers_queue.empty();
    });
    if (HAILO_SUCCESS != status) {
        return make_unexpected(status);
    }

    auto result = m_hw_buffers_queue.front();
    m_hw_buffers_queue.pop_front();
    return result;
}

hailo_status RemoteProcessBufferPool::enqueue_hw_buffer(SharedBuffer buffer)
{
    {
        std::unique_lock<RecursiveSharedMutex> lock(m_mutex);
        CHECK(!m_hw_buffers_queue.full(), HAILO_INTERNAL_FAILURE, "HW buffer is full");
        m_hw_buffers_queue.push_back(buffer);
    }
    m_cv.notify_one();
    return HAILO_SUCCESS;
}

Expected<RemoteProcessBufferPool::SharedBuffer> RemoteProcessBufferPool::dequeue_host_buffer(
    std::chrono::milliseconds timeout)
{
    std::unique_lock<RecursiveSharedMutex> lock(m_mutex);
    auto status = cv_wait_for(lock, timeout, [this]() {
        return !m_host_buffers_queue.empty();
    });
    if (HAILO_SUCCESS != status) {
        return make_unexpected(status);
    }

    auto result = m_host_buffers_queue.front();
    m_host_buffers_queue.pop_front();
    return result;
}

hailo_status RemoteProcessBufferPool::enqueue_host_buffer(SharedBuffer buffer)
{
    {
        std::unique_lock<RecursiveSharedMutex> lock(m_mutex);
        CHECK(!m_host_buffers_queue.full(), HAILO_INTERNAL_FAILURE, "Host buffer is full");
        m_host_buffers_queue.push_back(buffer);
    }
    m_cv.notify_one();
    return HAILO_SUCCESS;
}

hailo_status RemoteProcessBufferPool::wait_until_host_queue_full(std::chrono::milliseconds timeout)
{
    std::unique_lock<RecursiveSharedMutex> lock(m_mutex);
    return cv_wait_for(lock, timeout, [this]() {
        return m_host_buffers_queue.full();
    });
}

/** Input stream **/
Expected<std::shared_ptr<RemoteProcessInputStream>> RemoteProcessInputStream::create(
    std::shared_ptr<InputStreamBase> base_stream)
{
    // Set when the thread needs to be stopped.
    auto thread_stop_event = Event::create_shared(Event::State::not_signalled);
    CHECK_EXPECTED(thread_stop_event);

    auto status = HAILO_UNINITIALIZED;
    auto stream = make_shared_nothrow<RemoteProcessInputStream>(std::move(base_stream),
        thread_stop_event.release(), status);
    CHECK_NOT_NULL_AS_EXPECTED(stream, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return stream;
}

RemoteProcessInputStream::~RemoteProcessInputStream()
{
    if (m_write_thread.joinable()) {
        auto status = m_wait_for_activation.shutdown();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Shutdown thread failed with {}", status);
            // continue
        }

        status = deactivate_stream();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to deactivate stream with {}", status);
            // continue
        }

        // Calling abort_impl() to make sure the thread will exit
        status = abort_impl();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to abort stream with {}", status);
            // continue
        }

        m_write_thread.join();
    }
}

hailo_stream_interface_t RemoteProcessInputStream::get_interface() const
{
    return m_base_stream->get_interface();
}

std::chrono::milliseconds RemoteProcessInputStream::get_timeout() const
{
    return m_timeout;
}

hailo_status RemoteProcessInputStream::set_timeout(std::chrono::milliseconds timeout)
{
    // Should affect only m_timeout, and not base stream.
    m_timeout = timeout;
    return HAILO_SUCCESS;
}

hailo_status RemoteProcessInputStream::abort_impl()
{
    m_buffer_pool->abort();
    return HAILO_SUCCESS;
}

hailo_status RemoteProcessInputStream::clear_abort_impl()
{
    m_buffer_pool->clear_abort();
    return HAILO_SUCCESS;
}

bool RemoteProcessInputStream::is_scheduled()
{
    return m_base_stream->is_scheduled();
}

hailo_status RemoteProcessInputStream::flush()
{
    const auto flush_timeout = m_timeout * m_buffer_pool->capacity();

    // Get available buffer. We don't use the buffer, just use it to send flush request
    auto write_buffer = m_buffer_pool->dequeue_host_buffer(flush_timeout);
    if (HAILO_STREAM_ABORT == write_buffer.status()) {
        return HAILO_STREAM_ABORT;
    }
    CHECK_EXPECTED_AS_STATUS(write_buffer);

    // Set flush property. Will be cleared by writer.
    write_buffer->type = RemoteProcessBufferPool::SharedBuffer::Type::FLUSH;

    // Send flush request
    auto status = m_buffer_pool->enqueue_hw_buffer(*write_buffer);
    CHECK_SUCCESS(status);

    // Now wait until available buffers is full
    status = m_buffer_pool->wait_until_host_queue_full(flush_timeout);
    if (HAILO_STREAM_ABORT == status) {
        return HAILO_STREAM_ABORT;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status RemoteProcessInputStream::activate_stream()
{
    return m_base_stream->activate_stream();
}

hailo_status RemoteProcessInputStream::deactivate_stream()
{
    return m_base_stream->deactivate_stream();
}

hailo_status RemoteProcessInputStream::cancel_pending_transfers()
{
    return m_base_stream->cancel_pending_transfers();
}

hailo_status RemoteProcessInputStream::write_impl(const MemoryView &buffer)
{
    // Get available buffer
    auto write_buffer = m_buffer_pool->dequeue_host_buffer(m_timeout);
    if (HAILO_STREAM_ABORT == write_buffer.status()) {
        return HAILO_STREAM_ABORT;
    }
    CHECK_EXPECTED_AS_STATUS(write_buffer);

    // memcpy to write buffer
    CHECK(write_buffer->buffer.size() == buffer.size(), HAILO_INTERNAL_FAILURE, "Invalid buffer size");
    memcpy(write_buffer->buffer.data(), buffer.data(), buffer.size());

    // Send to write thread
    auto status = m_buffer_pool->enqueue_hw_buffer(*write_buffer);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

RemoteProcessInputStream::RemoteProcessInputStream(std::shared_ptr<InputStreamBase> base_stream,
    EventPtr thread_stop_event, hailo_status &status) :
        InputStreamBase(base_stream->get_layer_info(), base_stream->get_core_op_activated_event(), status),
        m_base_stream(base_stream),
        m_timeout(m_base_stream->get_timeout()),
        m_wait_for_activation(m_base_stream->get_core_op_activated_event(), thread_stop_event)
{
    if (HAILO_SUCCESS != status) {
        // Failure on base class
        return;
    }

    // Set infinite timeout on the base stream - the write will exit only on abort/deactivate.
    // It doesn't affect timeout for this class write function (m_timeout).
    auto set_timeout_status = m_base_stream->set_timeout(HAILO_INFINITE_TIMEOUT);
    if (HAILO_SUCCESS != set_timeout_status) {
        LOGGER__ERROR("Failed setting base stream timeout {}", set_timeout_status);
        status = set_timeout_status;
        return;
    }

    // Not all streams supports get_async_max_queue_size, fallback to default.
    auto queue_size = DEFAULT_QUEUE_SIZE;
    if (HAILO_STREAM_INTERFACE_ETH != m_base_stream->get_interface() && HAILO_STREAM_INTERFACE_MIPI != m_base_stream->get_interface()) {
        auto queue_size_exp = m_base_stream->get_async_max_queue_size();
        if (!queue_size_exp) {
            status = queue_size_exp.status();
            return;
        }
        queue_size = *queue_size_exp;
    }

    auto buffer_pool = RemoteProcessBufferPool::create(HAILO_H2D_STREAM, base_stream->get_frame_size(), queue_size);
    if (!buffer_pool) {
        LOGGER__ERROR("Failed creating buffer pool {}", buffer_pool.status());
        status = buffer_pool.status();
        return;
    }
    m_buffer_pool = buffer_pool.release();

    // Launch the thread
    m_write_thread = std::thread([this]() { run_write_thread(); });
    status = HAILO_SUCCESS;
}

void RemoteProcessInputStream::run_write_thread()
{
    OsUtils::set_current_thread_name("STREAM_WRITE");

    while (true) {
        auto status = m_wait_for_activation.wait(HAILO_INFINITE_TIMEOUT);
        if (HAILO_SHUTDOWN_EVENT_SIGNALED == status) {
            // Shutdown the thread
            return;
        }
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed wait for activation {}", status);
            return;
        }

        status = write_single_buffer();
        if ((HAILO_STREAM_ABORT == status) || (HAILO_STREAM_NOT_ACTIVATED == status)) {
            continue;
        } else if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failure on read thread {}", status);
            break;
        }
    }
}

hailo_status RemoteProcessInputStream::write_single_buffer()
{
    auto ready_buffer = m_buffer_pool->dequeue_hw_buffer(HAILO_INFINITE_TIMEOUT);
    if (!ready_buffer) {
        // Log on caller (if unexpected status)
        return ready_buffer.status();
    }

    hailo_status status = HAILO_UNINITIALIZED;
    if (RemoteProcessBufferPool::SharedBuffer::Type::DATA == ready_buffer->type) {
        status = m_base_stream->write(ready_buffer->buffer);
    } else if (RemoteProcessBufferPool::SharedBuffer::Type::FLUSH == ready_buffer->type) {
        ready_buffer->type = RemoteProcessBufferPool::SharedBuffer::Type::DATA; // clear flush mark.
        status = m_base_stream->flush();
    } else {
        LOGGER__ERROR("Got invalid buffer type");
        status = HAILO_INTERNAL_FAILURE;
    }

    if (HAILO_SUCCESS != status) {
        // If the read fails, we need to return the buffer to the host queue for later writes.
        auto enqueue_status = m_buffer_pool->enqueue_host_buffer(*ready_buffer);
        if (HAILO_SUCCESS != enqueue_status) {
            LOGGER__ERROR("Fail to enqueue buffer back after read was fail {}", enqueue_status);
            // continue
        }

        return status;
    }

    // buffer is now available
    status = m_buffer_pool->enqueue_host_buffer(*ready_buffer);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

/** Output stream **/
Expected<std::shared_ptr<RemoteProcessOutputStream>> RemoteProcessOutputStream::create(
    std::shared_ptr<OutputStreamBase> base_stream)
{
    // Set when the thread needs to be stopped.
    auto thread_stop_event = Event::create_shared(Event::State::not_signalled);
    CHECK_EXPECTED(thread_stop_event);

    auto status = HAILO_UNINITIALIZED;
    auto stream = make_shared_nothrow<RemoteProcessOutputStream>(std::move(base_stream),
        thread_stop_event.release(), status);
    CHECK_NOT_NULL_AS_EXPECTED(stream, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return stream;
}

RemoteProcessOutputStream::~RemoteProcessOutputStream()
{
    if (m_read_thread.joinable()) {
        auto status = m_wait_for_activation.shutdown();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Shutdown thread failed with {}", status);
            // continue
        }

        status = deactivate_stream();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to deactivate stream with {}", status);
            // continue
        }

        // Calling abort_impl() to make sure the thread will exit
        status = abort_impl();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to abort stream with {}", status);
            // continue
        }

        m_read_thread.join();
    }
}

hailo_stream_interface_t RemoteProcessOutputStream::get_interface() const
{
    return m_base_stream->get_interface();
}

std::chrono::milliseconds RemoteProcessOutputStream::get_timeout() const
{
    return m_timeout;
}

hailo_status RemoteProcessOutputStream::set_timeout(std::chrono::milliseconds timeout)
{
    // Should affect only m_timeout, and not base stream.
    m_timeout = timeout;
    return HAILO_SUCCESS;
}

hailo_status RemoteProcessOutputStream::abort_impl()
{
    m_buffer_pool->abort();
    return HAILO_SUCCESS;
}

hailo_status RemoteProcessOutputStream::clear_abort_impl()
{
    m_buffer_pool->clear_abort();
    return HAILO_SUCCESS;
}

bool RemoteProcessOutputStream::is_scheduled()
{
    return m_base_stream->is_scheduled();
}

hailo_status RemoteProcessOutputStream::activate_stream()
{
    return m_base_stream->activate_stream();
}

hailo_status RemoteProcessOutputStream::deactivate_stream()
{
    return m_base_stream->deactivate_stream();
}

hailo_status RemoteProcessOutputStream::cancel_pending_transfers()
{
    return m_base_stream->cancel_pending_transfers();
}

hailo_status RemoteProcessOutputStream::read_impl(MemoryView buffer)
{
    auto read_buffer = m_buffer_pool->dequeue_host_buffer(m_timeout);
    if (HAILO_STREAM_ABORT == read_buffer.status()) {
        return HAILO_STREAM_ABORT;
    }
    CHECK_EXPECTED_AS_STATUS(read_buffer);

    // memcpy to user
    CHECK(read_buffer->buffer.size() == buffer.size(), HAILO_INTERNAL_FAILURE, "Invalid buffer size");
    memcpy(buffer.data(), read_buffer->buffer.data(), buffer.size());

    auto status = m_buffer_pool->enqueue_hw_buffer(*read_buffer);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

RemoteProcessOutputStream::RemoteProcessOutputStream(std::shared_ptr<OutputStreamBase> base_stream,
    EventPtr thread_stop_event, hailo_status &status) :
        OutputStreamBase(base_stream->get_layer_info(), base_stream->get_core_op_activated_event(), status),
        m_base_stream(base_stream),
        m_timeout(m_base_stream->get_timeout()),
        m_wait_for_activation(m_base_stream->get_core_op_activated_event(), thread_stop_event)
{
    if (HAILO_SUCCESS != status) {
        return;
    }

    // Set infinite timeout on the base stream - the read will exit only on abort/deactivate.
    // It doesn't affect timeout for this class write function (m_timeout).
    auto set_timeout_status = m_base_stream->set_timeout(HAILO_INFINITE_TIMEOUT);
    if (HAILO_SUCCESS != set_timeout_status) {
        LOGGER__ERROR("Failed setting base stream timeout {}", set_timeout_status);
        status = set_timeout_status;
        return;
    }

    // Not all streams supports get_async_max_queue_size, fallback to default.
    auto queue_size = DEFAULT_QUEUE_SIZE;
    if (HAILO_STREAM_INTERFACE_ETH != m_base_stream->get_interface() && HAILO_STREAM_INTERFACE_MIPI != m_base_stream->get_interface()) {
        auto queue_size_exp = m_base_stream->get_async_max_queue_size();
        if (!queue_size_exp) {
            status = queue_size_exp.status();
            return;
        }
        queue_size = *queue_size_exp;
    }

    auto buffer_pool = RemoteProcessBufferPool::create(HAILO_D2H_STREAM, base_stream->get_frame_size(), queue_size);
    if (!buffer_pool) {
        LOGGER__ERROR("Failed creating buffer pool {}", buffer_pool.status());
        status = buffer_pool.status();
        return;
    }
    m_buffer_pool = buffer_pool.release();


    // Launch the thread
    m_read_thread = std::thread([this]() { run_read_thread(); });

    status = HAILO_SUCCESS;
}

void RemoteProcessOutputStream::run_read_thread()
{
    OsUtils::set_current_thread_name("STREAM_READ");

    // Breaks when the thread shutdown event is signaled..
    while (true) {
        auto status = m_wait_for_activation.wait(HAILO_INFINITE_TIMEOUT);
        if (HAILO_SHUTDOWN_EVENT_SIGNALED == status) {
            // Shutdown the thread
            return;
        }
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed wait for activation {}", status);
            return;
        }

        status = read_single_buffer();
        if ((HAILO_STREAM_ABORT == status) || (HAILO_STREAM_NOT_ACTIVATED == status)) {
            continue;
        } else if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failure on read thread {}", status);
            break;
        }
    }
}

hailo_status RemoteProcessOutputStream::read_single_buffer()
{
    auto ready_buffer = m_buffer_pool->dequeue_hw_buffer(HAILO_INFINITE_TIMEOUT);
    if (!ready_buffer) {
        // Log on caller (if unexpected status)
        return ready_buffer.status();
    }

    assert(RemoteProcessBufferPool::SharedBuffer::Type::DATA == ready_buffer->type);
    auto status = m_base_stream->read(ready_buffer->buffer);
    if (HAILO_SUCCESS != status) {
        // If the read fails, we need to return the buffer to the hw queue for later reads.
        auto enqueue_status = m_buffer_pool->enqueue_hw_buffer(*ready_buffer);
        if (HAILO_SUCCESS != enqueue_status) {
            LOGGER__ERROR("Fail to enqueue buffer back after read was fail {}", enqueue_status);
            // continue
        }

        return status;
    }

    // buffer is now available
    status = m_buffer_pool->enqueue_host_buffer(*ready_buffer);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

} /* namespace hailort */
