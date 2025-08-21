/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file cng_buffer_pool.cpp
 * @brief Network group buffer pool implementation
 **/

#include "cng_buffer_pool.hpp"
#include "hrpc/server_resource_manager.hpp"
#include "hailo/hailort.h"

namespace hailort
{

Expected<ObjectPoolPtr<Buffer>> BaseNetworkGroupBufferPool::create_stream_buffer_pool(size_t buffer_size,
    size_t buffer_count, hailo_dma_buffer_direction_t direction, EventPtr shutdown_event)
{
    auto map_buffer_lambda = [direction] (std::shared_ptr<VDevice> vdevice, MemoryView buffer_view) {
        return DmaMappedBuffer::create(*vdevice, buffer_view.data(), buffer_view.size(), direction);
    };

    auto create_buffer_lambda = [this, map_buffer_lambda, buffer_size] () -> Expected<Buffer> {
        TRY(auto buffer, Buffer::create(buffer_size, BufferStorageParams::create_dma()));
        TRY(auto mapped_buffer, m_map_buffer_func(m_vdevice_handle, map_buffer_lambda, buffer.as_view()));
        m_mapped_buffers.emplace_back(DmaMappedBuffer(std::move(mapped_buffer)));
        return buffer;
    };

    TRY(auto buffer_pool, ObjectPool<Buffer>::create_shared(buffer_count, create_buffer_lambda, shutdown_event));
    return buffer_pool;
}

BaseNetworkGroupBufferPool::BaseNetworkGroupBufferPool(EventPtr shutdown_event, uint32_t vdevice_handle,
    map_buffer_on_handle_func_t map_buffer_func)
    : m_stream_name_to_buffer_pool(), m_mapped_buffers(), m_shutdown_event(shutdown_event),
    m_vdevice_handle(vdevice_handle), m_map_buffer_func(map_buffer_func)
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
    auto pool_size = m_stream_name_to_buffer_pool[name]->capacity();
    m_stream_name_to_buffer_pool.erase(name);
    m_mapped_buffers.clear();

    TRY(auto buffer_pool, create_stream_buffer_pool(frame_size, pool_size, direction, m_shutdown_event));
    m_stream_name_to_buffer_pool[name] = buffer_pool;

    return HAILO_SUCCESS;
}

Expected<Pooled<Buffer>> BaseNetworkGroupBufferPool::acquire_buffer(const std::string &stream_name)
{
    CHECK_AS_EXPECTED(contains(m_stream_name_to_buffer_pool, stream_name), HAILO_INTERNAL_FAILURE,
        "acquire_buffer() for stream {} failed, stream name does not exist in buffer pool", stream_name);

    std::unique_lock<std::mutex> lock(m_mutex);
    auto pool = m_stream_name_to_buffer_pool.at(stream_name);
    TRY(auto buffer, pool->acquire());
    return buffer;
}

hailo_status BaseNetworkGroupBufferPool::shutdown()
{
    return m_shutdown_event->signal();
}

Expected<std::shared_ptr<ServerNetworkGroupBufferPool>> ServerNetworkGroupBufferPool::create(uint32_t vdevice_handle)
{
    TRY(auto shutdown_event, Event::create_shared(Event::State::not_signalled));

    auto map_buffer_func = [](uint32_t handle, execute_map_on_vdevice_func_t execute_map_buffer_func, MemoryView buffer_view) -> Expected<DmaMappedBuffer> {
        auto &vdevice_manager = ServerResourceManager<VDevice>::get_instance();
        TRY(auto mapped_buffer,
            vdevice_manager.execute<Expected<DmaMappedBuffer>>(handle, execute_map_buffer_func, buffer_view));
        return mapped_buffer;
    };
    auto cng_buffer_pool_ptr = make_shared_nothrow<ServerNetworkGroupBufferPool>(shutdown_event, vdevice_handle, map_buffer_func);
    CHECK_NOT_NULL_AS_EXPECTED(cng_buffer_pool_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return cng_buffer_pool_ptr;
}

} /* namespace hailort */
