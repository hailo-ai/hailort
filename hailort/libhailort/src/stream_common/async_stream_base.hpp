/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file async_stream_base.hpp
 * @brief Base class for async streams, implements
 *          1. Sync api (over async using buffer pool).
 *          2. The full async stream api, including waiting.
 **/

#ifndef _HAILO_ASYNC_STREAM_BASE_HPP_
#define _HAILO_ASYNC_STREAM_BASE_HPP_

#include "stream_common/stream_internal.hpp"
#include "stream_common/stream_buffer_pool.hpp"
#include "queued_stream_buffer_pool.hpp"

#include "common/thread_safe_queue.hpp"

namespace hailort
{

class AsyncInputStreamBase : public InputStreamBase {
public:
    AsyncInputStreamBase(const LayerInfo &edge_layer, EventPtr core_op_activated_event,
        hailo_status &status);

    virtual hailo_status set_buffer_mode(StreamBufferMode buffer_mode) override;
    virtual std::chrono::milliseconds get_timeout() const override;
    virtual hailo_status set_timeout(std::chrono::milliseconds timeout) override;
    virtual hailo_status flush() override;

    virtual hailo_status abort_impl() override;
    virtual hailo_status clear_abort_impl() override;

    virtual Expected<size_t> get_async_max_queue_size() const override;
    virtual hailo_status wait_for_async_ready(size_t transfer_size, std::chrono::milliseconds timeout) override;
    virtual hailo_status write_async(TransferRequest &&transfer_request) override;

    virtual hailo_status write_impl(const MemoryView &buffer) override;

    virtual hailo_status activate_stream() override;
    virtual hailo_status deactivate_stream() override;

    // APIs to be implemented by subclass want to get sync over async
    virtual Expected<std::unique_ptr<StreamBufferPool>> allocate_buffer_pool() = 0;
    virtual size_t get_max_ongoing_transfers() const = 0;
    virtual hailo_status write_async_impl(TransferRequest &&transfer_request) = 0;
    virtual hailo_status activate_stream_impl() { return HAILO_SUCCESS; }
    virtual hailo_status deactivate_stream_impl() { return HAILO_SUCCESS; }

protected:
    StreamBufferMode buffer_mode() const { return m_buffer_mode; }

private:
    hailo_status call_write_async_impl(TransferRequest &&transfer_request);

    bool is_ready_for_transfer() const;
    bool is_ready_for_dequeue() const;

    template<typename Pred>
    hailo_status cv_wait_for(std::unique_lock<std::mutex> &lock, std::chrono::milliseconds timeout, Pred &&pred)
    {
        hailo_status status = HAILO_SUCCESS;
        const auto wait_done = m_has_ready_buffer.wait_for(lock, timeout,
            [this, pred, &status] {
                if (m_is_aborted) {
                    status = HAILO_STREAM_ABORT;
                    return true;
                }

                if (!m_is_stream_activated) {
                    status = HAILO_STREAM_NOT_ACTIVATED;
                    return true;
                }

                return pred();
            }
        );
        if (!wait_done) {
            LOGGER__ERROR("Got HAILO_TIMEOUT while waiting for input stream buffer {}", name());
            return HAILO_TIMEOUT;
        } else if (HAILO_SUCCESS != status) {
            LOGGER__TRACE("Waiting for stream buffer exit with {}", status);
            return status;
        }
        return status;
    }

    bool m_is_stream_activated;
    bool m_is_aborted;
    std::chrono::milliseconds m_timeout;

    std::mutex m_stream_mutex;
    StreamBufferMode m_buffer_mode;

    std::unique_ptr<StreamBufferPool> m_buffer_pool;

    std::atomic_size_t m_ongoing_transfers;

    // Conditional variable that is use to check if we have some buffer in m_buffer_pool ready to be written to.
    std::condition_variable m_has_ready_buffer;
};


class AsyncOutputStreamBase : public OutputStreamBase {
public:
    AsyncOutputStreamBase(const LayerInfo &edge_layer, EventPtr core_op_activated_event, hailo_status &status);

    virtual hailo_status set_buffer_mode(StreamBufferMode buffer_mode) override;
    virtual std::chrono::milliseconds get_timeout() const override;
    virtual hailo_status set_timeout(std::chrono::milliseconds timeout) override;

    virtual hailo_status wait_for_async_ready(size_t transfer_size, std::chrono::milliseconds timeout) override;
    virtual Expected<size_t> get_async_max_queue_size() const override;
    virtual hailo_status read_async(TransferRequest &&transfer_request) override;

    virtual hailo_status read_impl(MemoryView buffer) override;

    virtual hailo_status abort_impl() override;
    virtual hailo_status clear_abort_impl() override;

    virtual hailo_status activate_stream() override;
    virtual hailo_status deactivate_stream() override;

    // APIs to be implemented by subclass want to get sync over async
    virtual Expected<std::unique_ptr<StreamBufferPool>> allocate_buffer_pool() = 0;
    virtual size_t get_max_ongoing_transfers() const = 0;
    virtual hailo_status read_async_impl(TransferRequest &&transfer_request) = 0;
    virtual hailo_status activate_stream_impl() { return HAILO_SUCCESS; }
    virtual hailo_status deactivate_stream_impl() { return HAILO_SUCCESS; }

protected:
    StreamBufferMode buffer_mode() const { return m_buffer_mode; }

private:
    hailo_status call_read_async_impl(TransferRequest &&transfer_request);

    bool is_ready_for_transfer() const;

    // Prepare transfers ahead for future reads. This function will launch transfers until the channel queue is filled.
    hailo_status prepare_all_transfers();

    hailo_status dequeue_and_launch_transfer();

    template<typename Pred>
    hailo_status cv_wait_for(std::unique_lock<std::mutex> &lock, std::chrono::milliseconds timeout, Pred &&pred)
    {
        hailo_status status = HAILO_SUCCESS;
        const auto wait_done = m_has_ready_buffer.wait_for(lock, timeout,
            [this, pred, &status] {
                if (m_is_aborted) {
                    status = HAILO_STREAM_ABORT;
                    return true;
                }

                if (!m_is_stream_activated) {
                    status = HAILO_STREAM_NOT_ACTIVATED;
                    return true;
                }

                return pred();
            }
        );
        if (!wait_done) {
            LOGGER__ERROR("Got HAILO_TIMEOUT while waiting for output stream buffer {}", name());
            return HAILO_TIMEOUT;
        } else if (HAILO_SUCCESS != status) {
            LOGGER__TRACE("Waiting for stream buffer exit with {}", status);
            return status;
        }
        return status;
    }

    bool m_is_stream_activated;
    bool m_is_aborted;
    std::chrono::milliseconds m_timeout;

    StreamBufferMode m_buffer_mode;

    std::mutex m_stream_mutex;

    std::unique_ptr<StreamBufferPool> m_buffer_pool;

    // Queue of buffers that was read from the hw and are pending to read by the user.
    SafeQueue<TransferBuffer> m_pending_buffers;

    std::atomic_size_t m_ongoing_transfers;

    // Conditional variable that is use to check if we have some pending buffer ready to be read.
    std::condition_variable m_has_ready_buffer;
};


} /* namespace hailort */

#endif /* _HAILO_ASYNC_STREAM_BASE_HPP_ */
