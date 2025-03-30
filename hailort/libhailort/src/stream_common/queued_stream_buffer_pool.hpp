/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file queued_stream_buffer_pool.hpp
 * @brief Simplest stream buffer pool, just using std::queue with max size for the buffers.
 **/

#ifndef _HAILO_QUEUED_STREAM_BUFFER_POOL_HPP_
#define _HAILO_QUEUED_STREAM_BUFFER_POOL_HPP_

#include "stream_common/stream_buffer_pool.hpp"
#include "hailo/dma_mapped_buffer.hpp"

#include <queue>

namespace hailort
{

class QueuedStreamBufferPool : public StreamBufferPool {
public:
    static Expected<std::unique_ptr<QueuedStreamBufferPool>> create(size_t max_queue_size, size_t buffer_size,
        BufferStorageParams buffer_params);

    explicit QueuedStreamBufferPool(std::vector<BufferPtr> &&storage);

    hailo_status dma_map(VDevice &vdevice, hailo_dma_buffer_direction_t direction);

    virtual size_t max_queue_size() const override;
    virtual Expected<TransferBuffer> dequeue() override;
    virtual hailo_status enqueue(TransferBuffer &&buffer_info) override;
    virtual void reset_pointers() override;

private:
    // Hold the buffer storage, keeps all buffers alive.
    std::vector<BufferPtr> m_storage;

    // Keeps mappings alive (only if dma_map was called).
    std::vector<DmaMappedBuffer> m_dma_mappings;

    std::queue<MemoryView> m_queue;

    // Used for buffer enqueue order validation.
    size_t m_next_enqueue_buffer_index = 0;
};

} /* namespace hailort */

#endif /* _HAILO_QUEUED_STREAM_BUFFER_POOL_HPP_ */
