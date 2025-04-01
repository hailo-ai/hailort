/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file stream_buffer_pool.hpp
 * @brief Base class for buffer pools used for stream objects.
 **/

#ifndef _HAILO_STREAM_BUFFER_POOL_HPP_
#define _HAILO_STREAM_BUFFER_POOL_HPP_

#include "hailo/expected.hpp"
#include "vdma/channel/transfer_common.hpp"

namespace hailort
{

// This class is NOT thread safe. All function calls must be synchronized.
class StreamBufferPool {
public:
    virtual ~StreamBufferPool() = default;

    virtual size_t max_queue_size() const = 0;

    // Dequeues buffer from the pool, fails if there is no buffer ready.
    virtual Expected<TransferBuffer> dequeue() = 0;

    // Enqueues buffer into the pool. The enqueue order must be the same as the dequeue order.
    virtual hailo_status enqueue(TransferBuffer &&buffer_info) = 0;

    // Resets the pointers to its initial state. Any dequeued buffer is lost (and the data will be overriden).
    virtual void reset_pointers() = 0;
};

} /* namespace hailort */

#endif /* _HAILO_STREAM_BUFFER_POOL_HPP_ */
