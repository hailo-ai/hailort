/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file cng_buffer_pool.cpp
 * @brief Network group buffer pool implementation
 **/

#include "cng_buffer_pool.hpp"
#include "service_resource_manager.hpp"
#include "hailo/hailort.h"

namespace hailort
{

Expected<BasicBufferPoolPtr> BaseNetworkGroupBufferPool::create_stream_buffer_pool(size_t buffer_size,
    size_t buffer_count, hailo_dma_buffer_direction_t direction, EventPtr shutdown_event)
{
    auto map_buffer_lambda = [direction](std::shared_ptr<VDevice> vdevice, BufferPtr buffer) {
        return DmaMappedBuffer::create(*vdevice, buffer->data(), buffer->size(), direction);
    };

    TRY(auto free_buffers_queue,
        SpscQueue<BufferPtr>::create(buffer_count, shutdown_event, DEFAULT_TRANSFER_TIMEOUT));

    std::vector<BufferPtr> buffers;
    buffers.reserve(buffer_count);
    for (size_t i = 0; i < buffer_count; i++) {
        TRY(auto buffer, Buffer::create_shared(buffer_size, BufferStorageParams::create_dma()));
        TRY(auto mapped_buffer, m_map_buffer_func(m_vdevice_handle, map_buffer_lambda, buffer));
        auto status = free_buffers_queue.enqueue(buffer);
        CHECK_SUCCESS(status);

        buffers.emplace_back(buffer);
        m_mapped_buffers.emplace_back(DmaMappedBuffer(std::move(mapped_buffer)));
    }

    auto buffer_pool_ptr = make_shared_nothrow<BasicBufferPool>(buffer_size, std::move(buffers),
        std::move(free_buffers_queue), buffer_count);
    CHECK_NOT_NULL_AS_EXPECTED(buffer_pool_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return buffer_pool_ptr;
}

BaseNetworkGroupBufferPool::BaseNetworkGroupBufferPool(EventPtr shutdown_event, uint32_t vdevice_handle,
    map_buffer_on_handle_func_t map_buffer_func)
    : m_stream_name_to_buffer_pool(), m_mapped_buffers(), m_shutdown_event(shutdown_event), m_vdevice_handle(vdevice_handle),
    m_map_buffer_func(map_buffer_func), m_is_shutdown(false)
{}

hailo_status BaseNetworkGroupBufferPool::allocate_pool(const std::string &name,
    hailo_dma_buffer_direction_t direction, size_t frame_size, size_t pool_size)
{
    TRY(auto buffer_pool, create_stream_buffer_pool(frame_size, pool_size, direction, m_shutdown_event));

    std::unique_lock<std::mutex> lock(m_mutex);
    m_stream_name_to_buffer_pool[name] = buffer_pool;

    return HAILO_SUCCESS;
}

hailo_status BaseNetworkGroupBufferPool::reallocate_pool(const std::string &name,
    hailo_dma_buffer_direction_t direction, size_t frame_size)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    auto pool_size = m_stream_name_to_buffer_pool[name]->buffers_count();
    m_stream_name_to_buffer_pool[name].reset();
    m_mapped_buffers.clear();

    TRY(auto buffer_pool, create_stream_buffer_pool(frame_size, pool_size, direction, m_shutdown_event));
    m_stream_name_to_buffer_pool[name] = buffer_pool;

    return HAILO_SUCCESS;
}

Expected<BufferPtr> BaseNetworkGroupBufferPool::acquire_buffer(const std::string &stream_name)
{
    CHECK_AS_EXPECTED(contains(m_stream_name_to_buffer_pool, stream_name), HAILO_INTERNAL_FAILURE,
        "acquire_buffer() for stream {} failed, stream name does not exist in buffer pool", stream_name);

    std::unique_lock<std::mutex> lock(m_mutex);
    auto pool = m_stream_name_to_buffer_pool.at(stream_name);
    m_cv.wait(lock, [this, pool] () {
        return (pool->current_size() > 0) || m_is_shutdown;
    });
    if (m_is_shutdown) {
        return make_unexpected(HAILO_SHUTDOWN_EVENT_SIGNALED);
    }

    TRY(auto buffer, pool->acquire_buffer());
    return buffer;
}

hailo_status BaseNetworkGroupBufferPool::return_to_pool(const std::string &stream_name, BufferPtr buffer)
{
    CHECK(contains(m_stream_name_to_buffer_pool, stream_name), HAILO_INTERNAL_FAILURE,
        "acquire_buffer() for stream {} failed, stream name does not exist in buffer pool", stream_name);

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        auto status = m_stream_name_to_buffer_pool.at(stream_name)->return_to_pool(buffer);
        CHECK_SUCCESS(status);
    }
    m_cv.notify_all();

    return HAILO_SUCCESS;
}

hailo_status BaseNetworkGroupBufferPool::shutdown()
{
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_is_shutdown = true;
    }
    m_cv.notify_all();
    return m_shutdown_event->signal();
}

Expected<std::shared_ptr<ServiceNetworkGroupBufferPool>> ServiceNetworkGroupBufferPool::create(uint32_t vdevice_handle)
{
    TRY(auto shutdown_event, Event::create_shared(Event::State::not_signalled));

    auto map_buffer_func = [](uint32_t handle, execute_map_on_vdevice_func_t execute_map_buffer_func, BufferPtr buffer) -> Expected<DmaMappedBuffer> {
        auto &vdevice_manager = ServiceResourceManager<VDevice>::get_instance();
        TRY(auto mapped_buffer,
            vdevice_manager.execute<Expected<DmaMappedBuffer>>(handle, execute_map_buffer_func, buffer));
        return mapped_buffer;
    };
    auto cng_buffer_pool_ptr = make_shared_nothrow<ServiceNetworkGroupBufferPool>(shutdown_event, vdevice_handle, map_buffer_func);
    CHECK_NOT_NULL_AS_EXPECTED(cng_buffer_pool_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return cng_buffer_pool_ptr;
}

Expected<std::shared_ptr<ServerNetworkGroupBufferPool>> ServerNetworkGroupBufferPool::create(uint32_t vdevice_handle)
{
    TRY(auto shutdown_event, Event::create_shared(Event::State::not_signalled));

    auto map_buffer_func = [](uint32_t handle, execute_map_on_vdevice_func_t execute_map_buffer_func, BufferPtr buffer) -> Expected<DmaMappedBuffer> {
        auto &vdevice_manager = ServerResourceManager<VDevice>::get_instance();
        TRY(auto mapped_buffer,
            vdevice_manager.execute<Expected<DmaMappedBuffer>>(handle, execute_map_buffer_func, buffer));
        return mapped_buffer;
    };
    auto cng_buffer_pool_ptr = make_shared_nothrow<ServerNetworkGroupBufferPool>(shutdown_event, vdevice_handle, map_buffer_func);
    CHECK_NOT_NULL_AS_EXPECTED(cng_buffer_pool_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return cng_buffer_pool_ptr;
}

} /* namespace hailort */
