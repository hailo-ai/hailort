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
#include "vdma/transfer_common.hpp"

namespace hailort
{

// Represents a single "logical" buffer. The buffer may be backed
// by a single TransferBuffer, or may be split into two buffers
// in the case that the underlying buffer is circular.
struct StreamBuffer {
    TransferBuffer first;
    TransferBuffer second;

    StreamBuffer() = default;

    StreamBuffer(TransferBuffer &&transfer_buffer) : first(std::move(transfer_buffer)), second() {}

    StreamBuffer(MemoryView base_buffer, size_t size, size_t offset)
    {
        if (size + offset <= base_buffer.size()) {
            first = TransferBuffer(base_buffer, size, offset);
            second = TransferBuffer();
        } else {
            // Wraparound buffer:
            // If base_buffer is circular, we may need to split our StreamBuffer into two continuous buffers:
            // base_buffer[offset : base_buffer.size] and base_buffer[0 : (size + offset - base_buffer.size)]
            size_t first_size = base_buffer.size() - offset;
            first = TransferBuffer(base_buffer, first_size, offset);
            second = TransferBuffer(base_buffer, size - first_size, 0);
        }
    }

    size_t offset() const
    {
        return first.offset();
    }

    size_t size() const
    {
        return first.size() + second.size();
    }

    Expected<MemoryView> base_buffer() const
    {
        return first.base_buffer();
    }

    hailo_status copy_to(MemoryView buffer) const
    {
        if (!is_wraparound()) {
            return first.copy_to(buffer);
        }
        CHECK_SUCCESS(first.copy_to(MemoryView(buffer.data(), first.size())));
        CHECK_SUCCESS(second.copy_to(MemoryView(buffer.data() + first.size(), second.size())));

        return HAILO_SUCCESS;
    }

    hailo_status copy_from(MemoryView buffer)
    {
        if (!is_wraparound()) {
            return first.copy_from(buffer);
        }
        CHECK_SUCCESS(first.copy_from(MemoryView(buffer.data(), first.size())));
        CHECK_SUCCESS(second.copy_from(MemoryView(buffer.data() + first.size(), second.size())));

        return HAILO_SUCCESS;
    }

    std::vector<TransferBuffer> transfer_buffers() const
    {
        if (!is_wraparound()) {
            return std::vector<TransferBuffer>{ first };
        } else {
            return std::vector<TransferBuffer>{ first, second };
        }
    }

    bool is_wraparound() const {
        return second.size() > 0;
    }
};

// This class is NOT thread safe. All function calls must be synchronized.
class StreamBufferPool {
public:
    virtual ~StreamBufferPool() = default;

    virtual size_t max_queue_size() const = 0;

    // Dequeues buffer from the pool, fails if there is no buffer ready.
    virtual Expected<StreamBuffer> dequeue() = 0;

    // Enqueues buffer into the pool. The enqueue order must be the same as the dequeue order.
    virtual hailo_status enqueue(StreamBuffer &&buffer_info) = 0;

    // Resets the pointers to its initial state. Any dequeued buffer is lost (and the data will be overriden).
    virtual void reset_pointers() = 0;
};

} /* namespace hailort */

#endif /* _HAILO_STREAM_BUFFER_POOL_HPP_ */
