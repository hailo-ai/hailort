/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file buffer_pool_per_stream.hpp
 * @brief Buffers pool per stream for Network group's streams
 **/

#ifndef _HAILO_BUFFER_POOL_PER_STREAM_HPP_
#define _HAILO_BUFFER_POOL_PER_STREAM_HPP_

#include "hailo/hailort.h"
#include "hailo/hailort_common.hpp"
#include "hailo/dma_mapped_buffer.hpp"
#include "hailo/buffer.hpp"
#include "hailo/vdevice.hpp"
#include "common/thread_safe_queue.hpp"
#include "common/buffer_pool.hpp"
#include "rpc/rpc_definitions.hpp"

namespace hailort
{

using stream_name_t = std::string;

class BufferPoolPerStream
{
public:
    static Expected<std::shared_ptr<BufferPoolPerStream>> create();

    BufferPoolPerStream(BufferPoolPerStream &&) = delete;
    BufferPoolPerStream(const BufferPoolPerStream &) = delete;
    BufferPoolPerStream &operator=(BufferPoolPerStream &&) = delete;
    BufferPoolPerStream &operator=(const BufferPoolPerStream &) = delete;
    virtual ~BufferPoolPerStream() = default;

    hailo_status allocate_pool(const std::string &stream_name, NetworkGroupIdentifier identifier, size_t frame_size, size_t pool_size);
    // Used in order to reallocate the pool buffers with different frame_size
    hailo_status reallocate_pool(const std::string &stream_name, NetworkGroupIdentifier identifier, size_t frame_size);
    Expected<BufferPtr> acquire_buffer(const std::string &stream_name);
    hailo_status return_to_pool(const std::string &stream_name, BufferPtr buffer);
    hailo_status shutdown();
    Expected<size_t> get_buffer_size(const std::string &stream_name);
    Expected<BasicBufferPoolPtr> get_pool(const std::string &stream_name);

    BufferPoolPerStream(EventPtr shutdown_event);
private:
    Expected<BasicBufferPoolPtr> create_stream_buffer_pool(const std::string &stream_name,
        NetworkGroupIdentifier &identifier, size_t buffer_size, size_t buffer_count,
        EventPtr shutdown_event);

    std::unordered_map<stream_name_t, BasicBufferPoolPtr> m_stream_name_to_buffer_pool;
    std::unordered_map<stream_name_t, std::string> m_stream_name_to_shm_name;
    EventPtr m_shutdown_event;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_is_shutdown;
};

// Guard for a buffer from the pool, to return it to pool once destructed
class AcquiredBuffer 
{
public:
    static Expected<std::shared_ptr<AcquiredBuffer>> acquire_from_pool(BasicBufferPoolPtr pool);
    AcquiredBuffer(BasicBufferPoolPtr pool, BufferPtr buffer);
    virtual ~AcquiredBuffer();

    uint8_t *data();
    size_t size() const;
    BufferPtr buffer();
private:
    BasicBufferPoolPtr m_pool;
    BufferPtr m_buffer;
};
using AcquiredBufferPtr = std::shared_ptr<AcquiredBuffer>;

} /* namespace hailort */

#endif /* _HAILO_BUFFER_POOL_PER_STREAM_HPP_ */
