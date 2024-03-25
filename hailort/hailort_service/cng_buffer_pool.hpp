/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file cng_buffer_pool.hpp
 * @brief This model represents the buffer pools for the output reads for each network group. Used in async API
 **/

#ifndef _HAILO_CNG_BUFFER_POOL_HPP_
#define _HAILO_CNG_BUFFER_POOL_HPP_

#include "hailo/hailort.h"
#include "hailo/hailort_common.hpp"
#include "hailo/buffer.hpp"
#include "hailo/vdevice.hpp"
#include "hailo/dma_mapped_buffer.hpp"
#include "utils/thread_safe_queue.hpp"

namespace hailort
{

class ServiceStreamBufferPool
{
public:
    static Expected<std::shared_ptr<ServiceStreamBufferPool>> create(uint32_t vdevice_handle, size_t buffer_size,
        size_t buffer_count, hailo_dma_buffer_direction_t direction, EventPtr shutdown_event);

    struct AllocatedMappedBuffer {
        BufferPtr buffer;
        DmaMappedBuffer mapped_buffer;
    };

    ServiceStreamBufferPool(size_t buffer_size, std::vector<AllocatedMappedBuffer> &&buffers,
        SpscQueue<BufferPtr> &&m_free_buffers_queue, size_t buffers_count);
    virtual ~ServiceStreamBufferPool() = default;

    Expected<BufferPtr> acquire_buffer();
    hailo_status return_to_pool(BufferPtr buffer);
    size_t buffers_count();

private:

    size_t m_buffer_size;
    size_t m_buffers_count;
    std::vector<AllocatedMappedBuffer> m_buffers;
    SpscQueue<BufferPtr> m_free_buffers_queue;
    std::mutex m_mutex;
};

using BufferPoolPtr = std::shared_ptr<ServiceStreamBufferPool>;
using output_name_t = std::string;

// This object holds a buffer pool for each output streams of the network group.
// It is used to pre-allocate all the buffers necessary for the reads from the device.
// The buffers are reuseable, which also prevents allocation during inference.
// The buffers are mapped to the device during their creation, which prevent lazy mapping each frame inference.
// Currently only used in async API.
class ServiceNetworkGroupBufferPool
{
public:
    static Expected<std::shared_ptr<ServiceNetworkGroupBufferPool>> create(uint32_t vdevice_handle);

    hailo_status allocate_pool(const std::string &name, size_t frame_size, size_t pool_size);
    // Used in order to reallocate the pool buffers with different frame_size
    hailo_status reallocate_pool(const std::string &name, size_t frame_size);

    ServiceNetworkGroupBufferPool(ServiceNetworkGroupBufferPool &&) = delete;
    ServiceNetworkGroupBufferPool(const ServiceNetworkGroupBufferPool &) = delete;
    ServiceNetworkGroupBufferPool &operator=(ServiceNetworkGroupBufferPool &&) = delete;
    ServiceNetworkGroupBufferPool &operator=(const ServiceNetworkGroupBufferPool &) = delete;
    virtual ~ServiceNetworkGroupBufferPool() = default;

    ServiceNetworkGroupBufferPool(EventPtr shutdown_event, uint32_t vdevice_handle);
    Expected<BufferPtr> acquire_buffer(const std::string &output_name);
    hailo_status return_to_pool(const std::string &output_name, BufferPtr buffer);
    hailo_status shutdown();

private:
    std::unordered_map<output_name_t, BufferPoolPtr> m_output_name_to_buffer_pool;
    EventPtr m_shutdown_event;
    uint32_t m_vdevice_handle;
    std::mutex m_mutex;
};

} /* namespace hailort */

#endif /* _HAILO_CNG_BUFFER_POOL_HPP_ */
