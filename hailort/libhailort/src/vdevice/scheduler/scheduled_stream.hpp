/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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
#include "vdevice/scheduler/infer_request_accumulator.hpp"
#include "stream_common/stream_buffer_pool.hpp"
#include "stream_common/async_stream_base.hpp"
#include "vdma/vdma_device.hpp"


namespace hailort
{

class ScheduledInputStream : public AsyncInputStreamBase {
public:

    static Expected<std::unique_ptr<ScheduledInputStream>> create(
        VDevice &vdevice,
        std::map<device_id_t, std::reference_wrapper<InputStreamBase>> &&streams,
        const LayerInfo &layer_info,
        const scheduler_core_op_handle_t &core_op_handle,
        EventPtr core_op_activated_event,
        std::shared_ptr<InferRequestAccumulator> infer_requests_accumulator);

    ScheduledInputStream(
        VDevice &vdevice,
        std::map<device_id_t, std::reference_wrapper<InputStreamBase>> &&streams,
        const scheduler_core_op_handle_t &core_op_handle,
        EventPtr &&core_op_activated_event,
        const LayerInfo &layer_info,
        std::shared_ptr<InferRequestAccumulator> &&infer_requests_accumulator,
        hailo_status &status) :
            AsyncInputStreamBase(layer_info, std::move(core_op_activated_event), status),
            m_vdevice(vdevice),
            m_streams(std::move(streams)),
            m_core_op_handle(core_op_handle),
            m_infer_requests_accumulator(infer_requests_accumulator),
            m_callback_reorder_queue(infer_requests_accumulator->queue_size()) // TODO HRT-1058 - use reorder queue only when needed
    {}

    virtual hailo_stream_interface_t get_interface() const override;

    virtual Expected<std::unique_ptr<StreamBufferPool>> allocate_buffer_pool() override;
    virtual size_t get_max_ongoing_transfers() const override;
    virtual hailo_status write_async_impl(TransferRequest &&transfer_request) override;


    virtual bool is_scheduled() override final { return true; };

private:
    VDevice &m_vdevice;
    std::map<device_id_t, std::reference_wrapper<InputStreamBase>> m_streams;
    scheduler_core_op_handle_t m_core_op_handle;
    std::shared_ptr<InferRequestAccumulator> m_infer_requests_accumulator;

    CallbackReorderQueue m_callback_reorder_queue;
};

class ScheduledOutputStream : public AsyncOutputStreamBase {
public:
    static Expected<std::unique_ptr<ScheduledOutputStream>> create(
        VDevice &vdevice,
        std::map<device_id_t, std::reference_wrapper<OutputStreamBase>> &&streams,
        const scheduler_core_op_handle_t &core_op_handle,
        const LayerInfo &layer_info,
        EventPtr core_op_activated_event,
        std::shared_ptr<InferRequestAccumulator> infer_requests_accumulator);

    ScheduledOutputStream(
        VDevice &vdevice,
        std::map<device_id_t, std::reference_wrapper<OutputStreamBase>> &&streams,
        const scheduler_core_op_handle_t &core_op_handle,
        const LayerInfo &layer_info,
        EventPtr &&core_op_activated_event,
        std::shared_ptr<InferRequestAccumulator> &&infer_requests_accumulator,
        hailo_status &status) :
            AsyncOutputStreamBase(layer_info, std::move(core_op_activated_event), status),
            m_vdevice(vdevice),
            m_streams(std::move(streams)),
            m_core_op_handle(core_op_handle),
            m_infer_requests_accumulator(infer_requests_accumulator),
            m_callback_reorder_queue(infer_requests_accumulator->queue_size()) // TODO HRT-1058 - use reorder queue only when needed
    {}

    virtual hailo_stream_interface_t get_interface() const override;

    virtual Expected<std::unique_ptr<StreamBufferPool>> allocate_buffer_pool() override;
    virtual size_t get_max_ongoing_transfers() const override;
    virtual hailo_status read_async(TransferRequest &&transfer_request) override
    {
        transfer_request.callback = [original_callback=transfer_request.callback, this](hailo_status status) {
            original_callback(status);
            if ((HAILO_SUCCESS == status) && (INVALID_CORE_OP_HANDLE != m_core_op_handle)) {
                TRACE(FrameDequeueD2HTrace, m_core_op_handle, name());
            }
        };
        return AsyncOutputStreamBase::read_async(std::move(transfer_request));
    }

    virtual hailo_status read_async_impl(TransferRequest &&transfer_request) override;

    virtual bool is_scheduled() override final { return true; };

    virtual hailo_status read_impl(MemoryView user_buffer) override
    {
        auto status = AsyncOutputStreamBase::read_impl(user_buffer);
        if ((HAILO_SUCCESS == status) && (INVALID_CORE_OP_HANDLE != m_core_op_handle)) {
            TRACE(FrameDequeueD2HTrace, m_core_op_handle, name());
        }
        return status;
    }


private:
    VDevice &m_vdevice;
    std::map<device_id_t, std::reference_wrapper<OutputStreamBase>> m_streams;
    scheduler_core_op_handle_t m_core_op_handle;
    std::shared_ptr<InferRequestAccumulator> m_infer_requests_accumulator;

    CallbackReorderQueue m_callback_reorder_queue;
};

} /* namespace hailort */

#endif /* HAILO_SCHEDULED_STREAM_HPP_ */
