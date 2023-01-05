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

#include "stream_internal.hpp"
#include "hailo/hailort.h"
#include "vdevice_internal.hpp"
#include "vdma_device.hpp"
#include "scheduled_stream.hpp"
#include "hailo/expected.hpp"

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

    hailo_status enqueue(const MemoryView &buff, const std::chrono::milliseconds &timeout)
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
                LOGGER__INFO("'enqueue' was aborted by user");
                return status;
            }

            std::memcpy(m_queue[m_head].data(), buff.data(), buff.size());
            m_head = static_cast<uint32_t>((m_head + 1) % m_queue.size());
            m_is_empty = false;
        }
        m_cv.notify_all();

        return HAILO_SUCCESS;
    }

    Expected<MemoryView> dequeue(const std::chrono::milliseconds &timeout)
    {
        auto status = HAILO_SUCCESS;
        size_t last_tail = 0;
        {
            std::unique_lock<std::mutex> lock(m_mutex);

            // TODO: this validation is done in scheduler logic. can be removed?
            auto wait_res = m_cv.wait_for(lock, timeout, [this, &status] {
                if (m_should_stop) {
                    status = HAILO_STREAM_ABORTED_BY_USER;
                    return true;
                }
                return 0 < size();
            });
            CHECK_AS_EXPECTED(wait_res, HAILO_TIMEOUT, "Failed to dequeue frame with status={}, timeout={}ms", HAILO_TIMEOUT, timeout.count());
            if (HAILO_STREAM_ABORTED_BY_USER == status) {
                LOGGER__INFO("'dequeue' was aborted by user");
                return make_unexpected(status);
            }

            last_tail = m_tail;
            m_tail = static_cast<uint32_t>((m_tail + 1) % m_queue.size());
            if (m_tail == m_head) {
                m_is_empty = true;
            }
        }
        m_cv.notify_all();

        return MemoryView(m_queue[last_tail]);
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
        std::unique_lock<std::mutex> lock(m_mutex);
        m_should_stop = true;
        m_cv.notify_all();
    }

    void clear_abort()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_should_stop = false;
        m_cv.notify_all();
    }

    BuffersQueue(std::vector<Buffer> &&queue) : m_queue(std::move(queue)), m_head(0), m_tail(0), m_is_empty(true), m_should_stop(false)
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
    MultiDeviceScheduledInputStream(MultiDeviceScheduledInputStream &&other) :
        ScheduledInputStream(std::move(other)),
        m_queue(std::move(other.m_queue))
    {}

    explicit MultiDeviceScheduledInputStream(
        std::vector<std::reference_wrapper<VdmaInputStream>> &&streams,
        const scheduler_ng_handle_t &network_group_handle,
        EventPtr &&network_group_activated_event,
        const LayerInfo &layer_info,
        NetworkGroupSchedulerWeakPtr network_group_scheduler,
        std::unique_ptr<BuffersQueue> &&frames_queue,
        hailo_status &status) :
            ScheduledInputStream(std::move(streams), network_group_handle,
                std::move(network_group_activated_event), layer_info, network_group_scheduler, status),
                m_queue(std::move(frames_queue))
    {
    }

    virtual hailo_status send_pending_buffer(size_t device_index = 0) override;
    virtual Expected<size_t> get_pending_frames_count() const override;

protected:
    virtual Expected<size_t> sync_write_raw_buffer(const MemoryView &buffer,
        const std::function<bool()> &should_cancel = []() { return false; }) override;
    virtual hailo_status abort() override;
    virtual hailo_status clear_abort() override;

private:
    hailo_status enqueue(const MemoryView &buffer);
    Expected<MemoryView> dequeue();
    size_t get_queue_size() const;

    std::unique_ptr<BuffersQueue> m_queue;
};

} /* namespace hailort */

#endif /* HAILO_MULTI_DEVICE_SCHEDULED_STREAM_HPP_ */
