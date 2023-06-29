/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file scheduled_stream.hpp
 * @brief Internal stream implementation for scheduled streams
 *
 **/

#ifndef HAILO_SCHEDULED_STREAM_HPP_
#define HAILO_SCHEDULED_STREAM_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"

#include "stream_common/stream_internal.hpp"
#include "vdevice/vdevice_internal.hpp"
#include "vdevice/vdevice_stream.hpp"
#include "vdevice/callback_reorder_queue.hpp"
#include "vdma/vdma_device.hpp"


namespace hailort
{


class ScheduledInputStreamBase : public VDeviceInputStreamBase {
public:
    ScheduledInputStreamBase(
        std::map<device_id_t, std::reference_wrapper<VdmaInputStreamBase>> &&streams,
        const scheduler_core_op_handle_t &core_op_handle,
        EventPtr &&core_op_activated_event,
        const LayerInfo &layer_info,
        CoreOpsSchedulerWeakPtr core_ops_scheduler,
        hailo_status &status) :
            VDeviceInputStreamBase(std::move(streams), std::move(core_op_activated_event), layer_info, status),
            m_core_op_handle(core_op_handle),
            m_core_ops_scheduler(core_ops_scheduler)
    {}

    virtual bool is_scheduled() override final { return true; };

    virtual void notify_all() override
    {
        auto scheduler = m_core_ops_scheduler.lock();
        if (nullptr == scheduler) {
            LOGGER__CRITICAL("Failed to acquire scheduler");
            return;
        }
        scheduler->notify_all();

        for (const auto &pair : m_streams) {
            auto &stream = pair.second;
            stream.get().notify_all();
        }
    }

    virtual hailo_status abort() override;
    virtual hailo_status clear_abort() override;
    virtual hailo_status flush() override;

protected:
    scheduler_core_op_handle_t m_core_op_handle;
    CoreOpsSchedulerWeakPtr m_core_ops_scheduler;

private:
    hailo_status abort_impl(scheduler_core_op_handle_t core_op_handle);
    hailo_status clear_abort_impl(scheduler_core_op_handle_t core_op_handle);
};

class ScheduledInputStream : public ScheduledInputStreamBase {
public:
    static Expected<std::unique_ptr<ScheduledInputStream>> create(
        std::map<device_id_t, std::reference_wrapper<VdmaInputStreamBase>> &&streams,
        const scheduler_core_op_handle_t &core_op_handle,
        EventPtr &&core_op_activated_event,
        const LayerInfo &layer_info,
        CoreOpsSchedulerWeakPtr core_ops_scheduler);

    ScheduledInputStream(
        std::map<device_id_t, std::reference_wrapper<VdmaInputStreamBase>> &&streams,
        const scheduler_core_op_handle_t &core_op_handle,
        EventPtr &&core_op_activated_event,
        const LayerInfo &layer_info,
        CoreOpsSchedulerWeakPtr core_ops_scheduler,
        hailo_status &status) :
            ScheduledInputStreamBase(std::move(streams), core_op_handle, std::move(core_op_activated_event), layer_info,
                core_ops_scheduler, status)
    {}

protected:
    virtual hailo_status write_impl(const MemoryView &buffer, const std::function<bool()> &should_cancel) override;
};

class TransferRequestsQueue final {
public:
    TransferRequestsQueue(size_t max_size) :
        m_max_size(max_size)
    {}

    ~TransferRequestsQueue()
    {
        while (!m_queue.empty()) {
            auto &request = m_queue.front();
            request.callback(HAILO_STREAM_ABORTED_BY_USER);
            m_queue.pop();
        }
    }

    TransferRequestsQueue(const TransferRequestsQueue &) = delete;
    TransferRequestsQueue &operator=(const TransferRequestsQueue &) = delete;

    hailo_status wait_for_room(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        auto result = m_dequeue_cv.wait_for(lock, timeout,
            [&] {
                return m_is_aborted || (m_queue.size() < m_max_size);
            });
        if (!result) {
            return HAILO_TIMEOUT;
        }
        if (m_is_aborted) {
            return HAILO_STREAM_ABORTED_BY_USER;
        }
        return HAILO_SUCCESS;
    }

    hailo_status enqueue(TransferRequest &&transfer_request)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_is_aborted) {
            return HAILO_STREAM_ABORTED_BY_USER;
        }
        CHECK(m_queue.size() < m_max_size, HAILO_QUEUE_IS_FULL, "No space left in stream queue");
        m_queue.emplace(std::move(transfer_request));
        return HAILO_SUCCESS;
    }

    Expected<TransferRequest> dequeue()
    {
        TransferRequest transfer_request{};
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_is_aborted) {
                return make_unexpected(HAILO_STREAM_ABORTED_BY_USER);
            }
            CHECK_AS_EXPECTED(!m_queue.empty(), HAILO_INTERNAL_FAILURE, "Queue should not be empty");
            transfer_request = m_queue.front();
            m_queue.pop();
        }
        m_dequeue_cv.notify_one();
        return transfer_request;
    }

    void abort()
    {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_is_aborted = true;
        }

        m_dequeue_cv.notify_all();
    }

    void clear_abort()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_is_aborted = false;
    }

    size_t max_size() const { return m_max_size; }

private:
    // TODO: use SpscQueue (HRT-10554)
    const size_t m_max_size;
    std::mutex m_mutex;
    bool m_is_aborted = false;
    std::condition_variable m_dequeue_cv;
    std::queue<TransferRequest> m_queue;
};

class ScheduledAsyncInputStream : public ScheduledInputStreamBase {
public:

    static Expected<std::unique_ptr<ScheduledAsyncInputStream>> create(
        std::map<device_id_t, std::reference_wrapper<VdmaInputStreamBase>> &&streams,
        const scheduler_core_op_handle_t &core_op_handle,
        EventPtr &&core_op_activated_event,
        const LayerInfo &layer_info,
        CoreOpsSchedulerWeakPtr core_ops_scheduler);

    ScheduledAsyncInputStream(
        std::map<device_id_t, std::reference_wrapper<VdmaInputStreamBase>> &&streams,
        const scheduler_core_op_handle_t &core_op_handle,
        EventPtr &&core_op_activated_event,
        const LayerInfo &layer_info,
        CoreOpsSchedulerWeakPtr core_ops_scheduler,
        size_t max_queue_size,
        hailo_status &status) :
            ScheduledInputStreamBase(std::move(streams), core_op_handle, std::move(core_op_activated_event), layer_info,
                core_ops_scheduler, status),
            m_pending_buffers(max_queue_size),
            m_callback_reorder_queue(max_queue_size) // TODO HRT-1058 - use reorder queue only when needed
    {}

    virtual hailo_status send_pending_buffer(const device_id_t &device_id) override;
    virtual hailo_status wait_for_async_ready(size_t transfer_size, std::chrono::milliseconds timeout) override;
    virtual hailo_status write_async(TransferRequest &&transfer_request) override;
    virtual Expected<size_t> get_async_max_queue_size() const override;
    virtual hailo_status abort() override;
    virtual hailo_status clear_abort() override;

protected:
    virtual hailo_status write_impl(const MemoryView &, const std::function<bool()> &) override;

    // All buffers written by the user using write_async are first stored in this queue.
    // When the scheduler decides to activate the network on a specific device, send_pending_buffer is called, and
    // the buffers are sent to the underlying stream.
    TransferRequestsQueue m_pending_buffers;
    CallbackReorderQueue m_callback_reorder_queue;
};

class ScheduledOutputStreamBase : public VDeviceOutputStreamBase {
public:
    ScheduledOutputStreamBase(
        std::map<device_id_t, std::reference_wrapper<VdmaOutputStreamBase>> &&streams,
        const scheduler_core_op_handle_t &core_op_handle,
        const LayerInfo &layer_info,
        EventPtr &&core_op_activated_event,
        CoreOpsSchedulerWeakPtr core_ops_scheduler,
        hailo_status &status) :
            VDeviceOutputStreamBase(std::move(streams), layer_info, std::move(core_op_activated_event), status),
            m_core_op_handle(core_op_handle),
            m_core_ops_scheduler(core_ops_scheduler)
    {}

    virtual bool is_scheduled() override { return true; };

    virtual hailo_status abort() override;
    virtual hailo_status clear_abort() override;

protected:

    scheduler_core_op_handle_t m_core_op_handle;
    CoreOpsSchedulerWeakPtr m_core_ops_scheduler;

private:
    hailo_status abort_impl(scheduler_core_op_handle_t core_op_handle);
    hailo_status clear_abort_impl(scheduler_core_op_handle_t core_op_handle);
};


class ScheduledOutputStream : public ScheduledOutputStreamBase {
public:
    static Expected<std::unique_ptr<ScheduledOutputStream>> create(
        std::map<device_id_t, std::reference_wrapper<VdmaOutputStreamBase>> &&streams,
        const scheduler_core_op_handle_t &core_op_handle,
        const LayerInfo &layer_info,
        EventPtr &&core_op_activated_event,
        CoreOpsSchedulerWeakPtr core_ops_scheduler);

    ScheduledOutputStream(
        std::map<device_id_t, std::reference_wrapper<VdmaOutputStreamBase>> &&streams,
        const scheduler_core_op_handle_t &core_op_handle,
        const LayerInfo &layer_info,
        EventPtr &&core_op_activated_event,
        CoreOpsSchedulerWeakPtr core_ops_scheduler,
        hailo_status &status);

    virtual hailo_status set_next_device_to_read(const device_id_t &device_id) override;

protected:
    virtual hailo_status read(MemoryView buffer) override;

private:

    // Returns device id to read from
    Expected<device_id_t> wait_for_read();

    std::queue<device_id_t> m_device_read_order;
    std::mutex m_device_read_order_mutex;
};

} /* namespace hailort */

#endif /* HAILO_SCHEDULED_STREAM_HPP_ */
