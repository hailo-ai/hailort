/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file cng_buffer_pool.hpp
 * @brief This model represents the buffer pools for the streams of each network group. Used in async API
 **/

#ifndef _HAILO_CNG_BUFFER_POOL_HPP_
#define _HAILO_CNG_BUFFER_POOL_HPP_

#include "hailo/hailort.h"
#include "hailo/hailort_common.hpp"
#include "hailo/buffer.hpp"
#include "hailo/vdevice.hpp"
#include "hailo/dma_mapped_buffer.hpp"
#include "common/thread_safe_queue.hpp"
#include "common/buffer_pool.hpp"

namespace hailort
{

using stream_name_t = std::string;
using execute_map_on_vdevice_func_t = std::function<Expected<DmaMappedBuffer>(std::shared_ptr<VDevice>, BufferPtr)>;
using map_buffer_on_handle_func_t = std::function<Expected<DmaMappedBuffer>(uint32_t, execute_map_on_vdevice_func_t, BufferPtr)>;

// This object holds a buffer pool for each stream of the network group.
// It is used to pre-allocate all the buffers necessary for the reads from the device.
// The buffers are reuseable, which also prevents allocation during inference.
// The buffers are mapped to the device during their creation, which prevent lazy mapping each frame inference.
// Currently only used in async API.
class BaseNetworkGroupBufferPool
{
public:
    hailo_status allocate_pool(const std::string &name, hailo_dma_buffer_direction_t direction, size_t frame_size, size_t pool_size);
    // Used in order to reallocate the pool buffers with different frame_size
    hailo_status reallocate_pool(const std::string &name, hailo_dma_buffer_direction_t direction, size_t frame_size);

    BaseNetworkGroupBufferPool(BaseNetworkGroupBufferPool &&) = delete;
    BaseNetworkGroupBufferPool(const BaseNetworkGroupBufferPool &) = delete;
    BaseNetworkGroupBufferPool &operator=(BaseNetworkGroupBufferPool &&) = delete;
    BaseNetworkGroupBufferPool &operator=(const BaseNetworkGroupBufferPool &) = delete;
    virtual ~BaseNetworkGroupBufferPool() = default;

    BaseNetworkGroupBufferPool(EventPtr shutdown_event, uint32_t vdevice_handle, map_buffer_on_handle_func_t map_buffer_func);
    Expected<BufferPtr> acquire_buffer(const std::string &stream_name);
    hailo_status return_to_pool(const std::string &stream_name, BufferPtr buffer);
    hailo_status shutdown();

private:
    Expected<BasicBufferPoolPtr> create_stream_buffer_pool(size_t buffer_size,
        size_t buffer_count, hailo_dma_buffer_direction_t direction, EventPtr shutdown_event);

    std::unordered_map<stream_name_t, BasicBufferPoolPtr> m_stream_name_to_buffer_pool;
    // This is in order to keep the DmaMappedBuffer buffers alive while using the buffers pool.
    std::vector<DmaMappedBuffer> m_mapped_buffers;
    EventPtr m_shutdown_event;
    uint32_t m_vdevice_handle;
    map_buffer_on_handle_func_t m_map_buffer_func;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_is_shutdown;
};

class ServiceNetworkGroupBufferPool : public BaseNetworkGroupBufferPool
{
public:
    static Expected<std::shared_ptr<ServiceNetworkGroupBufferPool>> create(uint32_t vdevice_handle);
    ServiceNetworkGroupBufferPool(EventPtr shutdown_event, uint32_t vdevice_handle, map_buffer_on_handle_func_t map_buffer_func)
        : BaseNetworkGroupBufferPool(shutdown_event, vdevice_handle, map_buffer_func) {}

    ServiceNetworkGroupBufferPool(ServiceNetworkGroupBufferPool &&) = delete;
    ServiceNetworkGroupBufferPool(const ServiceNetworkGroupBufferPool &) = delete;
    ServiceNetworkGroupBufferPool &operator=(ServiceNetworkGroupBufferPool &&) = delete;
    ServiceNetworkGroupBufferPool &operator=(const ServiceNetworkGroupBufferPool &) = delete;
    virtual ~ServiceNetworkGroupBufferPool() = default;
};

class ServerNetworkGroupBufferPool : public BaseNetworkGroupBufferPool
{
public:
    static Expected<std::shared_ptr<ServerNetworkGroupBufferPool>> create(uint32_t vdevice_handle);
    ServerNetworkGroupBufferPool(EventPtr shutdown_event, uint32_t vdevice_handle, map_buffer_on_handle_func_t map_buffer_func)
        : BaseNetworkGroupBufferPool(shutdown_event, vdevice_handle, map_buffer_func) {}

    ServerNetworkGroupBufferPool(ServerNetworkGroupBufferPool &&) = delete;
    ServerNetworkGroupBufferPool(const ServerNetworkGroupBufferPool &) = delete;
    ServerNetworkGroupBufferPool &operator=(ServerNetworkGroupBufferPool &&) = delete;
    ServerNetworkGroupBufferPool &operator=(const ServerNetworkGroupBufferPool &) = delete;
    virtual ~ServerNetworkGroupBufferPool() = default;
};

} /* namespace hailort */

#endif /* _HAILO_CNG_BUFFER_POOL_HPP_ */
