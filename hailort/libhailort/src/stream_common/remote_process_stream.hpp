/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file remote_process_stream.hpp
 * @brief Stream wrapper for multi process environment (i.e python api).
 *        Using shared queue to make sure all low level operations are executed
 *        on one process.
 **/

#ifndef _HAILO_REMOTE_PROCESS_STREAM_HPP_
#define _HAILO_REMOTE_PROCESS_STREAM_HPP_

#include "common/event_internal.hpp"
#include "common/fork_support.hpp"

#include "stream_common/stream_internal.hpp"

#include "common/utils.hpp"
#include "common/circular_buffer.hpp"

#include "hailo/buffer.hpp"

namespace hailort
{

class RemoteProcessBufferPool final : public SharedAllocatedObject {
public:
   struct SharedBuffer {

        enum class Type {
            DATA,
            FLUSH, // For input streams, don't use the buffer content, just flush the stream.
        };

        MemoryView buffer;
        Type type;
    };

    // We always use unique_ptr to make sure the buffer is allocated on shared memory.
    // queue_size must be some (power-of-2 minus 1) in order to fit CircularArray.
    static Expected<std::unique_ptr<RemoteProcessBufferPool>> create(hailo_stream_direction_t stream_direction,
        size_t frame_size, size_t queue_size);

    RemoteProcessBufferPool(hailo_stream_direction_t stream_direction, size_t frame_size, size_t queue_size,
        hailo_status &status);

    Expected<SharedBuffer> dequeue_hw_buffer(std::chrono::milliseconds timeout);
    hailo_status enqueue_hw_buffer(SharedBuffer buffer);

    Expected<SharedBuffer> dequeue_host_buffer(std::chrono::milliseconds timeout);
    hailo_status enqueue_host_buffer(SharedBuffer buffer);
    hailo_status wait_until_host_queue_full(std::chrono::milliseconds timeout);

    void abort();
    void clear_abort();

    size_t capacity() const
    {
        assert(m_hw_buffers_queue.capacity() == m_host_buffers_queue.capacity());
        return m_hw_buffers_queue.capacity();
    }

private:

    template<typename CondFunc>
    hailo_status cv_wait_for(std::unique_lock<RecursiveSharedMutex> &lock,
        std::chrono::milliseconds timeout, CondFunc &&cond)
    {
        assert(lock.owns_lock());
        bool done = m_cv.wait_for(lock, timeout, [this, cond]() {
            if (m_is_aborted) {
                return true;
            }

            return cond();
        });
        CHECK(done, HAILO_TIMEOUT, "Timeout waiting on cond variable");
        if (m_is_aborted) {
            return HAILO_STREAM_ABORT;
        }
        return HAILO_SUCCESS;
    }

    // Guards memory allocation.
    std::vector<BufferPtr> m_buffers_guard;

    // Note: We use a fixed size array to avoid dynamic memory allocation, needed for shared memory.
    //       CircularArrays support working with sizes less than the backing array length.
    static constexpr size_t BACKING_ARRAY_LENGTH = 1024;
    using BufferQueue = CircularArray<SharedBuffer, IsNotPow2Tag, std::array<SharedBuffer, BACKING_ARRAY_LENGTH>>;

    // On input streams - buffers with user data, ready to be sent to the hw.
    // On output streams - buffers that are ready, the stream can receive into them.
    BufferQueue m_hw_buffers_queue;

    // On input streams - buffers that are ready, the user can write into them.
    // On output streams - buffers with data from the hw, ready to be read by the user
    BufferQueue m_host_buffers_queue;

    RecursiveSharedMutex m_mutex;
    SharedConditionVariable m_cv;

    bool m_is_aborted;
};


class RemoteProcessInputStream : public InputStreamBase {
public:
    static Expected<std::shared_ptr<RemoteProcessInputStream>> create(std::shared_ptr<InputStreamBase> base_stream);
    virtual ~RemoteProcessInputStream();

    virtual hailo_status set_buffer_mode(StreamBufferMode buffer_mode) override
    {
        // Buffer mode needs to be set by the parent process (since buffers can be allocated only there) either manually
        // or automatically. On this class, the mode will be set to OWNING automatically on the first write.
        CHECK(buffer_mode == StreamBufferMode::OWNING, HAILO_INVALID_ARGUMENT,
            "RemoteProcessInputStream streams supports only sync api");
        return HAILO_SUCCESS;
    }

    virtual hailo_stream_interface_t get_interface() const override;
    virtual std::chrono::milliseconds get_timeout() const override;
    virtual hailo_status set_timeout(std::chrono::milliseconds timeout) override;
    virtual hailo_status abort_impl() override;
    virtual hailo_status clear_abort_impl() override;
    virtual bool is_scheduled() override;
    virtual hailo_status flush() override;

    virtual hailo_status activate_stream() override;
    virtual hailo_status deactivate_stream() override;
    virtual hailo_status cancel_pending_transfers() override;


    RemoteProcessInputStream(std::shared_ptr<InputStreamBase> base_stream, EventPtr thread_stop_event,
        hailo_status &status);

    virtual hailo_status write_impl(const MemoryView &buffer) override;
protected:

    void run_write_thread();
    hailo_status write_single_buffer();

    std::shared_ptr<InputStreamBase> m_base_stream;
    std::chrono::milliseconds m_timeout;

    // Runs on parent, execute writes
    std::thread m_write_thread;

    // Store as unique_ptr to allow shared memory
    std::unique_ptr<RemoteProcessBufferPool> m_buffer_pool;

    WaitOrShutdown m_wait_for_activation;
};

class RemoteProcessOutputStream : public OutputStreamBase {
public:
    static Expected<std::shared_ptr<RemoteProcessOutputStream>> create(std::shared_ptr<OutputStreamBase> base_stream);

    virtual ~RemoteProcessOutputStream();

    virtual hailo_status set_buffer_mode(StreamBufferMode buffer_mode) override
    {
        // Buffer mode needs to be set by the parent process (since buffers can be allocated only there) either manually
        // or automatically. On this class, the mode will be set to OWNING automatically on the first write.
        CHECK(buffer_mode == StreamBufferMode::OWNING, HAILO_INVALID_ARGUMENT,
            "RemoteProcessInputStream streams supports only sync api");
        return HAILO_SUCCESS;
    }

    virtual hailo_stream_interface_t get_interface() const override;
    virtual std::chrono::milliseconds get_timeout() const override;
    virtual hailo_status set_timeout(std::chrono::milliseconds timeout) override;
    virtual hailo_status abort_impl() override;
    virtual hailo_status clear_abort_impl() override;
    virtual bool is_scheduled() override;

    virtual hailo_status activate_stream() override;
    virtual hailo_status deactivate_stream() override;
    virtual hailo_status cancel_pending_transfers() override;

    RemoteProcessOutputStream(std::shared_ptr<OutputStreamBase> base_stream, EventPtr thread_stop_event,
        hailo_status &status);

    virtual hailo_status read_impl(MemoryView buffer) override;
protected:

    void run_read_thread();
    hailo_status read_single_buffer();

    std::shared_ptr<OutputStreamBase> m_base_stream;
    std::chrono::milliseconds m_timeout;

    // Runs on parent, execute reads
    std::thread m_read_thread;

    std::unique_ptr<RemoteProcessBufferPool> m_buffer_pool;

    WaitOrShutdown m_wait_for_activation;
};

} /* namespace hailort */

#endif /* _HAILO_REMOTE_PROCESS_STREAM_HPP_ */
