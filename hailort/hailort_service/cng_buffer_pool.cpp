/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
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


Expected<std::shared_ptr<ServiceStreamBufferPool>> ServiceStreamBufferPool::create(uint32_t vdevice_handle,
    size_t buffer_size, size_t buffer_count, hailo_dma_buffer_direction_t direction, EventPtr shutdown_event)
{
    auto map_buffer_lambda = [direction](std::shared_ptr<VDevice> vdevice, BufferPtr buffer) {
        return DmaMappedBuffer::create(*vdevice, buffer->data(), buffer->size(), direction);
    };
    auto &vdevice_manager = ServiceResourceManager<VDevice>::get_instance();

    auto free_buffers_queue = SpscQueue<BufferPtr>::create(buffer_count, shutdown_event, DEFAULT_TRANSFER_TIMEOUT);
    CHECK_EXPECTED(free_buffers_queue);

    std::vector<AllocatedMappedBuffer> buffers;
    buffers.reserve(buffer_count);
    for (size_t i = 0; i < buffer_count; i++) {
        auto buffer = Buffer::create_shared(buffer_size, BufferStorageParams::create_dma());
        CHECK_EXPECTED(buffer);

        auto mapped_buffer = vdevice_manager.execute<Expected<DmaMappedBuffer>>(vdevice_handle, map_buffer_lambda, buffer.value());
        CHECK_EXPECTED(mapped_buffer);

        auto status = free_buffers_queue->enqueue(buffer.value());
        CHECK_SUCCESS(status);

        buffers.emplace_back(AllocatedMappedBuffer{ buffer.release(), mapped_buffer.release()});
    }

    auto buffer_pool_ptr = make_shared_nothrow<ServiceStreamBufferPool>(buffer_size, std::move(buffers),
        free_buffers_queue.release(), buffer_count);
    CHECK_NOT_NULL_AS_EXPECTED(buffer_pool_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return buffer_pool_ptr;
}

ServiceStreamBufferPool::ServiceStreamBufferPool(size_t buffer_size, std::vector<AllocatedMappedBuffer> &&buffers,
        SpscQueue<BufferPtr> &&free_buffers_queue, size_t buffers_count) :
    m_buffer_size(buffer_size),
    m_buffers_count(buffers_count),
    m_buffers(std::move(buffers)),
    m_free_buffers_queue(std::move(free_buffers_queue))
{}

Expected<BufferPtr> ServiceStreamBufferPool::acquire_buffer()
{
    auto buffer = m_free_buffers_queue.dequeue(DEFAULT_TRANSFER_TIMEOUT);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == buffer.status()) {
        return make_unexpected(buffer.status());
    }
    else if (HAILO_TIMEOUT == buffer.status()) {
        LOGGER__WARNING(
            "Failed to acquire buffer because the buffer pool is empty. This could be caused by uneven reading and writing speeds");
        return make_unexpected(buffer.status());
    }
    CHECK_EXPECTED(buffer);

    return buffer.release();
}

hailo_status ServiceStreamBufferPool::return_to_pool(BufferPtr buffer)
{
    CHECK(buffer->size() == m_buffer_size, HAILO_INTERNAL_FAILURE,
        "Buffer size is not the same as expected for pool! ({} != {})", buffer->size(), m_buffer_size);

    std::unique_lock<std::mutex> lock(m_mutex);
    auto status = m_free_buffers_queue.enqueue(buffer);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

size_t ServiceStreamBufferPool::buffers_count()
{
    return m_buffers_count;
}

Expected<std::shared_ptr<ServiceNetworkGroupBufferPool>> ServiceNetworkGroupBufferPool::create(uint32_t vdevice_handle)
{
    auto shutdown_event_exp = Event::create_shared(Event::State::not_signalled);
    CHECK_EXPECTED(shutdown_event_exp);
    auto shutdown_event = shutdown_event_exp.release();

    auto cng_buffer_pool_ptr = make_shared_nothrow<ServiceNetworkGroupBufferPool>(shutdown_event, vdevice_handle);
    CHECK_NOT_NULL_AS_EXPECTED(cng_buffer_pool_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return cng_buffer_pool_ptr;
}

ServiceNetworkGroupBufferPool::ServiceNetworkGroupBufferPool(EventPtr shutdown_event, uint32_t vdevice_handle) :
    m_output_name_to_buffer_pool(), m_shutdown_event(shutdown_event), m_vdevice_handle(vdevice_handle)
{}

hailo_status ServiceNetworkGroupBufferPool::allocate_pool(const std::string &name, size_t frame_size, size_t pool_size)
{
    auto buffer_pool = ServiceStreamBufferPool::create(m_vdevice_handle, frame_size,
        pool_size, HAILO_DMA_BUFFER_DIRECTION_D2H, m_shutdown_event);
    CHECK_EXPECTED(buffer_pool);

    std::unique_lock<std::mutex> lock(m_mutex);
    m_output_name_to_buffer_pool[name] = buffer_pool.release();

    return HAILO_SUCCESS;
}

hailo_status ServiceNetworkGroupBufferPool::reallocate_pool(const std::string &name, size_t frame_size)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    auto pool_size = m_output_name_to_buffer_pool[name]->buffers_count();
    m_output_name_to_buffer_pool[name].reset();

    auto buffer_pool = ServiceStreamBufferPool::create(m_vdevice_handle, frame_size,
        pool_size, HAILO_DMA_BUFFER_DIRECTION_D2H, m_shutdown_event);
    CHECK_EXPECTED(buffer_pool);
    m_output_name_to_buffer_pool[name] = buffer_pool.release();

    return HAILO_SUCCESS;
}

Expected<BufferPtr> ServiceNetworkGroupBufferPool::acquire_buffer(const std::string &output_name)
{
    CHECK_AS_EXPECTED(contains(m_output_name_to_buffer_pool, output_name), HAILO_INTERNAL_FAILURE,
        "acquire_buffer() for output {} failed, output name does not exist in buffer pool", output_name);

    std::unique_lock<std::mutex> lock(m_mutex);
    auto buffer = m_output_name_to_buffer_pool.at(output_name)->acquire_buffer();
    CHECK_EXPECTED(buffer);

    return buffer.release();
}

hailo_status ServiceNetworkGroupBufferPool::return_to_pool(const std::string &output_name, BufferPtr buffer)
{
    CHECK(contains(m_output_name_to_buffer_pool, output_name), HAILO_INTERNAL_FAILURE,
        "acquire_buffer() for output {} failed, output name does not exist in buffer pool", output_name);

    std::unique_lock<std::mutex> lock(m_mutex);
    auto status = m_output_name_to_buffer_pool.at(output_name)->return_to_pool(buffer);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status ServiceNetworkGroupBufferPool::shutdown()
{
    return m_shutdown_event->signal();
}

} /* namespace hailort */
