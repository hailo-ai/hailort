/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file  multi_device_stream.hpp
 * @brief Internal multi device stream implementation for scheduled streams
 *
 **/

#ifndef HAILO_MULTI_DEVICE_SCHEDULED_STREAM_HPP_
#define HAILO_MULTI_DEVICE_SCHEDULED_STREAM_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"

#include "stream_common/stream_internal.hpp"
#include "vdevice/vdevice_internal.hpp"
#include "vdevice/scheduler/scheduled_stream.hpp"
#include "vdma/vdma_device.hpp"


namespace hailort
{

class BuffersQueue
{
public:
    static Expected<std::unique_ptr<BuffersQueue>> create_unique(size_t buffer_size, size_t buffers_count)
    {
        std::vector<Buffer> queue;
        queue.reserve(buffers_count);
        for (size_t i = 0; i < (buffers_count); i++) {
            auto buff = Buffer::create(buffer_size);
            CHECK_EXPECTED(buff);
            queue.emplace_back(buff.release());
        }

        auto ptr = make_unique_nothrow<BuffersQueue>(std::move(queue));
        CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY);
        return ptr;
    }

    hailo_status push(const MemoryView &buff, const std::chrono::milliseconds &timeout)
    {
        auto status = HAILO_SUCCESS;
        {
            std::unique_lock<std::mutex> lock(m_mutex);

            // TODO: this validation is done in scheduler logic. can be removed?
            auto wait_res = m_cv.wait_for(lock, timeout, [this, &status] {
                if (m_should_stop) {
                    status = HAILO_STREAM_ABORTED_BY_USER;
                    return true;
                }
                return size() < m_queue.size();
            });
            CHECK(wait_res, HAILO_TIMEOUT, "Failed to enqueue frame with status={}, timeout={}ms", HAILO_TIMEOUT, timeout.count());
            if (HAILO_STREAM_ABORTED_BY_USER == status) {
                LOGGER__INFO("'push' was aborted by user");
                return status;
            }

            std::memcpy(m_queue[m_head].data(), buff.data(), buff.size());
            m_head = static_cast<uint32_t>((m_head + 1) % m_queue.size());
            m_is_empty = false;
        }
        m_cv.notify_all();

        return HAILO_SUCCESS;
    }

    Expected<MemoryView> front(const std::chrono::milliseconds &timeout)
    {
        auto status = HAILO_SUCCESS;
        {
            std::unique_lock<std::mutex> lock(m_mutex);

            auto wait_res = m_cv.wait_for(lock, timeout, [this, &status] {
                if (m_should_stop) {
                    status = HAILO_STREAM_ABORTED_BY_USER;
                    return true;
                }
                return 0 < size();
            });
            CHECK_AS_EXPECTED(wait_res, HAILO_TIMEOUT, "Failed to dequeue frame with status={}, timeout={}ms", HAILO_TIMEOUT, timeout.count());
            if (HAILO_STREAM_ABORTED_BY_USER == status) {
                LOGGER__INFO("'front' was aborted by user");
                return make_unexpected(status);
            }
        }
        m_cv.notify_all();

        return MemoryView(m_queue[m_tail]);
    }

    void pop()
    {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_tail = static_cast<uint32_t>((m_tail + 1) % m_queue.size());
            if (m_tail == m_head) {
                m_is_empty = true;
            }
        }
        m_cv.notify_all();
    }

    size_t size()
    {
        if (m_head == m_tail) {
            return m_is_empty ? 0 : m_queue.size();
        } else if (m_head > m_tail) {
            return (m_head - m_tail);
        } else {
            return (m_queue.size() - m_tail) + m_head;
        }
    }

    void abort()
    {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_should_stop = true;
        }
        m_cv.notify_all();
    }

    void clear_abort()
    {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_should_stop = false;
        }
        m_cv.notify_all();
    }

    BuffersQueue(std::vector<Buffer> &&queue) : m_queue(std::move(queue)), m_head(0), m_tail(0),
        m_is_empty(true), m_should_stop(false)
    {}

private:
    std::vector<Buffer> m_queue;
    std::atomic_uint32_t m_head;
    std::atomic_uint32_t m_tail;

    std::atomic_bool m_is_empty;

    std::condition_variable m_cv;
    std::mutex m_mutex;
    std::atomic_bool m_should_stop;
};

class MultiDeviceScheduledInputStream : public ScheduledInputStream {
public:
    MultiDeviceScheduledInputStream(
        std::vector<std::reference_wrapper<VdmaInputStream>> &&streams,
        const scheduler_core_op_handle_t &core_op_handle,
        EventPtr &&core_op_activated_event,
        const LayerInfo &layer_info,
        CoreOpsSchedulerWeakPtr core_ops_scheduler,
        std::unique_ptr<BuffersQueue> &&frames_queue,
        hailo_status &status) :
            ScheduledInputStream(std::move(streams), core_op_handle,
                std::move(core_op_activated_event), layer_info, core_ops_scheduler, status),
                m_queue(std::move(frames_queue))
    {}

    virtual hailo_status send_pending_buffer(size_t device_index = 0) override;
    virtual Expected<size_t> get_pending_frames_count() const override;

protected:
    virtual Expected<size_t> sync_write_raw_buffer(const MemoryView &buffer,
        const std::function<bool()> &should_cancel = []() { return false; }) override;
    virtual hailo_status abort() override;
    virtual hailo_status clear_abort() override;

private:
    size_t get_queue_size() const;

    std::unique_ptr<BuffersQueue> m_queue;
};

} /* namespace hailort */

#endif /* HAILO_MULTI_DEVICE_SCHEDULED_STREAM_HPP_ */
