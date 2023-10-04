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
#include "stream_common/async_stream_base.hpp"
#include "vdevice/vdevice_internal.hpp"
#include "vdevice/callback_reorder_queue.hpp"
#include "vdevice/scheduler/scheduler.hpp"
#include "stream_common/stream_buffer_pool.hpp"
#include "stream_common/async_stream_base.hpp"
#include "vdma/vdma_device.hpp"


namespace hailort
{

class ScheduledInputStream : public AsyncInputStreamBase {
public:

    static Expected<std::unique_ptr<ScheduledInputStream>> create(
        std::map<device_id_t, std::reference_wrapper<InputStreamBase>> &&streams,
        const LayerInfo &layer_info,
        const scheduler_core_op_handle_t &core_op_handle,
        CoreOpsSchedulerWeakPtr core_ops_scheduler,
        EventPtr core_op_activated_event);

    ScheduledInputStream(
        std::map<device_id_t, std::reference_wrapper<InputStreamBase>> &&streams,
        const scheduler_core_op_handle_t &core_op_handle,
        EventPtr &&core_op_activated_event,
        const LayerInfo &layer_info,
        CoreOpsSchedulerWeakPtr core_ops_scheduler,
        size_t max_queue_size,
        hailo_status &status) :
            AsyncInputStreamBase(layer_info, streams.begin()->second.get().get_interface(),
                                   std::move(core_op_activated_event), status),
            m_streams(std::move(streams)),
            m_core_ops_scheduler(core_ops_scheduler),
            m_core_op_handle(core_op_handle),
            m_transfer_requests(max_queue_size),
            m_callback_reorder_queue(max_queue_size) // TODO HRT-1058 - use reorder queue only when needed
    {}

    virtual hailo_stream_interface_t get_interface() const override;

    virtual Expected<std::unique_ptr<StreamBufferPool>> allocate_buffer_pool() override;
    virtual size_t get_max_ongoing_transfers() const override;
    virtual hailo_status write_async_impl(TransferRequest &&transfer_request) override;

    virtual hailo_status launch_transfer(const device_id_t &device_id) override;
    virtual hailo_status abort() override;
    virtual hailo_status clear_abort() override;

    virtual bool is_scheduled() override final { return true; };

    // Returns the amount of frames buffered on a single device.
    virtual Expected<size_t> get_buffer_frames_size() const override
    {
        return m_streams.begin()->second.get().get_buffer_frames_size();
    }

private:
    std::map<device_id_t, std::reference_wrapper<InputStreamBase>> m_streams;
    CoreOpsSchedulerWeakPtr m_core_ops_scheduler;
    scheduler_core_op_handle_t m_core_op_handle;

    // All buffers written by the user using write_async are first stored in this queue.
    // When the scheduler decides to activate the network on a specific device, send_pending_buffer is called, and
    // the buffers are sent to the underlying stream.
    SafeQueue<TransferRequest> m_transfer_requests;

    CallbackReorderQueue m_callback_reorder_queue;
};

class ScheduledOutputStream : public AsyncOutputStreamBase {
public:
    static Expected<std::unique_ptr<ScheduledOutputStream>> create(
        std::map<device_id_t, std::reference_wrapper<OutputStreamBase>> &&streams,
        const scheduler_core_op_handle_t &core_op_handle,
        const LayerInfo &layer_info,
        EventPtr core_op_activated_event,
        CoreOpsSchedulerWeakPtr core_ops_scheduler);

    ScheduledOutputStream(
        std::map<device_id_t, std::reference_wrapper<OutputStreamBase>> &&streams,
        const scheduler_core_op_handle_t &core_op_handle,
        const LayerInfo &layer_info,
        EventPtr &&core_op_activated_event,
        CoreOpsSchedulerWeakPtr core_ops_scheduler,
        size_t max_queue_size,
        hailo_status &status) :
            AsyncOutputStreamBase(layer_info, streams.begin()->second.get().get_interface(),
                                  std::move(core_op_activated_event), status),
            m_streams(std::move(streams)),
            m_core_ops_scheduler(core_ops_scheduler),
            m_core_op_handle(core_op_handle),
            m_transfer_requests(max_queue_size),
            m_callback_reorder_queue(max_queue_size) // TODO HRT-1058 - use reorder queue only when needed
    {}

    virtual hailo_status launch_transfer(const device_id_t &device_id) override;

    virtual hailo_status abort() override;
    virtual hailo_status clear_abort() override;

    virtual hailo_stream_interface_t get_interface() const override;

    virtual Expected<std::unique_ptr<StreamBufferPool>> allocate_buffer_pool() override;
    virtual size_t get_max_ongoing_transfers() const override;
    virtual hailo_status read_async_impl(TransferRequest &&transfer_request) override;

    virtual bool is_scheduled() override final { return true; };

    // Returns the amount of frames buffered on a single device.
    virtual Expected<size_t> get_buffer_frames_size() const override
    {
        return m_streams.begin()->second.get().get_buffer_frames_size();
    }

    virtual hailo_status read_impl(MemoryView user_buffer) override
    {
        auto status = AsyncOutputStreamBase::read_impl(user_buffer);
        if (HAILO_SUCCESS == status) {
            TRACE(ReadFrameTrace, m_core_op_handle, name());
        }
        return status;
    }

private:
    std::map<device_id_t, std::reference_wrapper<OutputStreamBase>> m_streams;
    CoreOpsSchedulerWeakPtr m_core_ops_scheduler;
    scheduler_core_op_handle_t m_core_op_handle;

    // All buffers written by the user using write_async are first stored in this queue.
    // When the scheduler decides to activate the network on a specific device, send_pending_buffer is called, and
    // the buffers are sent to the underlying stream.
    SafeQueue<TransferRequest> m_transfer_requests;

    CallbackReorderQueue m_callback_reorder_queue;
};

} /* namespace hailort */

#endif /* HAILO_SCHEDULED_STREAM_HPP_ */
