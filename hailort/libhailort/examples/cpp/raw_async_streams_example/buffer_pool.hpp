/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file buffer_pool.hpp
 * @brief Pool of vdma mapped buffers, allowing FIFO queue access to buffers
 **/

#ifndef _HAILO_BUFFER_POOL_HPP_
#define _HAILO_BUFFER_POOL_HPP_

#include "hailo/hailort.hpp"
#include "hailo/expected.hpp"

#include <memory>
#include <mutex>
#include <condition_variable>
#include <queue>


using namespace hailort;

class BufferPool;
using BufferPoolPtr = std::shared_ptr<BufferPool>;

class BufferPool final
{
public:
    static Expected<BufferPoolPtr> create(size_t num_buffers, size_t buffer_size,
        hailo_vdma_buffer_direction_flags_t data_direction_flags, Device &device);
    BufferPool(size_t max_size, std::queue<std::shared_ptr<DmaMappedBuffer>> &&queue);
    BufferPool(BufferPool &&) = delete;
    BufferPool(const BufferPool &) = delete;
    BufferPool &operator=(BufferPool &&) = delete;
    BufferPool &operator=(const BufferPool &) = delete;
    ~BufferPool();

    std::shared_ptr<DmaMappedBuffer> dequeue();
    void enqueue(std::shared_ptr<DmaMappedBuffer> buffer);
    void wait_for_pending_buffers();

private:
    const size_t m_max_size;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<std::shared_ptr<DmaMappedBuffer>> m_queue;
};

#endif /* _HAILO_BUFFER_POOL_HPP_ */
