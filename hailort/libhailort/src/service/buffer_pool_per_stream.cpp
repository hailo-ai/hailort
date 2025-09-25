/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file buffer_pool_per_stream.cpp
 * @brief Buffer pool per stream implementation
 **/

#include "service/buffer_pool_per_stream.hpp"
#include "hailo/hailort.h"
#include "rpc/rpc_definitions.hpp"
#include "common/shared_memory_buffer.hpp"

#include <sstream>

namespace hailort
{

std::string create_shm_name(const std::string &stream_name, const NetworkGroupIdentifier &identifier, size_t buffer_index)
{
    auto stream_shm_name =  SharedMemoryBuffer::get_valid_shm_name(stream_name);

    std::ostringstream shm_name;
    shm_name << stream_shm_name << SHARED_MEMORY_NAME_SEPERATOR << std::to_string(buffer_index)
        << SHARED_MEMORY_NAME_SEPERATOR << std::to_string(identifier.m_vdevice_identifier.m_vdevice_handle)
        << SHARED_MEMORY_NAME_SEPERATOR << std::to_string(identifier.m_network_group_handle);
    return shm_name.str();
}

Expected<std::shared_ptr<BufferPoolPerStream>> BufferPoolPerStream::create()
{
    TRY(auto shutdown_event, Event::create_shared(Event::State::not_signalled));

    auto buffer_pool_ptr = make_shared_nothrow<BufferPoolPerStream>(shutdown_event);
    CHECK_NOT_NULL_AS_EXPECTED(buffer_pool_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return buffer_pool_ptr;
}

BufferPoolPerStream::BufferPoolPerStream(EventPtr shutdown_event) :
    m_stream_name_to_buffer_pool(), m_shutdown_event(shutdown_event), m_mutex(), m_cv(), m_is_shutdown(false)
{}

Expected<BasicBufferPoolPtr> BufferPoolPerStream::create_stream_buffer_pool(const std::string &stream_name,
    NetworkGroupIdentifier &identifier, size_t buffer_size, size_t buffer_count,
    EventPtr shutdown_event)
{
    TRY(auto free_buffers_queue,
        SpscQueue<BufferPtr>::create(buffer_count, shutdown_event, DEFAULT_TRANSFER_TIMEOUT));

    std::vector<BufferPtr> buffers;
    buffers.reserve(buffer_count);
    for (size_t i = 0; i < buffer_count; i++) {
        auto shm_name = create_shm_name(stream_name, identifier, i);
        m_stream_name_to_shm_name[stream_name] = shm_name;
        TRY(auto buffer, Buffer::create_shared(buffer_size, BufferStorageParams::create_shared_memory(shm_name)));

        auto status = free_buffers_queue.enqueue(buffer);
        CHECK_SUCCESS(status);

        buffers.emplace_back(buffer);
    }

    auto buffer_pool_ptr = make_shared_nothrow<BasicBufferPool>(buffer_size, std::move(buffers),
        std::move(free_buffers_queue), buffer_count);
    CHECK_NOT_NULL_AS_EXPECTED(buffer_pool_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return buffer_pool_ptr;
}

hailo_status BufferPoolPerStream::allocate_pool(const std::string &stream_name, NetworkGroupIdentifier identifier,
    size_t frame_size, size_t pool_size)
{
    TRY(auto buffer_pool, create_stream_buffer_pool(stream_name, identifier, frame_size, pool_size, m_shutdown_event));

    std::unique_lock<std::mutex> lock(m_mutex);
    m_stream_name_to_buffer_pool[stream_name] = buffer_pool;

    return HAILO_SUCCESS;
}

hailo_status BufferPoolPerStream::reallocate_pool(const std::string &stream_name, NetworkGroupIdentifier identifier,
    size_t frame_size)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    auto pool_size = m_stream_name_to_buffer_pool[stream_name]->buffers_count();
    m_stream_name_to_buffer_pool[stream_name].reset();

    TRY(auto buffer_pool, create_stream_buffer_pool(stream_name, identifier, frame_size, pool_size, m_shutdown_event));
    m_stream_name_to_buffer_pool[stream_name] = buffer_pool;

    return HAILO_SUCCESS;
}

Expected<BufferPtr> BufferPoolPerStream::acquire_buffer(const std::string &stream_name)
{
    CHECK_AS_EXPECTED(contains(m_stream_name_to_buffer_pool, stream_name), HAILO_INTERNAL_FAILURE,
        "acquire_buffer() for stream {} failed, stream name does not exist in buffer pool", stream_name);

    std::unique_lock<std::mutex> lock(m_mutex);
    auto pool = m_stream_name_to_buffer_pool.at(stream_name);
    m_cv.wait(lock, [this, pool] () {
        return (pool->current_size() > 0) || m_is_shutdown;
    });
    if (m_is_shutdown) {
        LOGGER__INFO("Got shutdown signal while trying to acquire_buffer() for stream {}", stream_name);
        return make_unexpected(HAILO_SHUTDOWN_EVENT_SIGNALED);
    }

    TRY(auto buffer, pool->acquire_buffer());
    return buffer;
}

hailo_status BufferPoolPerStream::return_to_pool(const std::string &stream_name, BufferPtr buffer)
{
    CHECK(contains(m_stream_name_to_buffer_pool, stream_name), HAILO_INTERNAL_FAILURE,
        "acquire_buffer() for stream {} failed, stream name does not exist in buffer pool", stream_name);

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        auto status = m_stream_name_to_buffer_pool.at(stream_name)->return_to_pool(buffer);
        if (status == HAILO_SHUTDOWN_EVENT_SIGNALED) {
            LOGGER__INFO("return_to_pool for buffer {} got status {}", stream_name, status);
        } else {
            CHECK_SUCCESS(status);
        }
    }
    m_cv.notify_all();

    return HAILO_SUCCESS;
}

hailo_status BufferPoolPerStream::shutdown()
{
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_is_shutdown = true;
    }
    m_cv.notify_all();
    return m_shutdown_event->signal();
}

Expected<BasicBufferPoolPtr> BufferPoolPerStream::get_pool(const std::string &stream_name)
{
    CHECK_AS_EXPECTED(contains(m_stream_name_to_buffer_pool, stream_name), HAILO_INTERNAL_FAILURE,
        "get_buffer_size() for stream {} failed, stream name does not exist in buffer pool", stream_name);

    auto res = m_stream_name_to_buffer_pool.at(stream_name);
    return res;
}

Expected<size_t> BufferPoolPerStream::get_buffer_size(const std::string &stream_name)
{
    CHECK(contains(m_stream_name_to_buffer_pool, stream_name), HAILO_INTERNAL_FAILURE,
        "get_buffer_size() for stream {} failed, stream name does not exist in buffer pool", stream_name);

    return m_stream_name_to_buffer_pool[stream_name]->buffer_size();
}

Expected<std::shared_ptr<AcquiredBuffer>> AcquiredBuffer::acquire_from_pool(BasicBufferPoolPtr pool)
{
    TRY(auto buffer, pool->acquire_buffer());

    auto acquired_buffer_ptr = make_shared_nothrow<AcquiredBuffer>(pool, buffer);
    CHECK_NOT_NULL_AS_EXPECTED(acquired_buffer_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return acquired_buffer_ptr;
}

AcquiredBuffer::AcquiredBuffer(BasicBufferPoolPtr pool, BufferPtr buffer) :
    m_pool(pool), m_buffer(buffer)
{}

AcquiredBuffer::~AcquiredBuffer()
{
    auto status = m_pool->return_to_pool(m_buffer);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to return buffer to pool");
    }
}

uint8_t* AcquiredBuffer::data()
{
    return m_buffer->data();
}

size_t AcquiredBuffer::size() const
{
    return m_buffer->size();
}

BufferPtr AcquiredBuffer::buffer()
{
    return m_buffer;
}


} /* namespace hailort */
