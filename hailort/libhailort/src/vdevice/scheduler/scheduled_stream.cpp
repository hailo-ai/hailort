/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file scheduled_stream.cpp
 * @brief Internal stream implementation for scheduled streams
 *
 **/

#include "scheduled_stream.hpp"

#include "stream_common/queued_stream_buffer_pool.hpp"

#include "utils/profiler/tracer_macros.hpp"

#include "stream_common/queued_stream_buffer_pool.hpp"

namespace hailort
{

/** Input stream **/
Expected<std::unique_ptr<ScheduledInputStream>> ScheduledInputStream::create(
    VDevice &vdevice,
    std::map<device_id_t, std::reference_wrapper<InputStreamBase>> &&streams,
    const LayerInfo &layer_info,
    const scheduler_core_op_handle_t &core_op_handle,
    EventPtr core_op_activated_event,
    std::shared_ptr<InferRequestAccumulator> infer_requests_accumulator)
{
    // In all cases, the buffer mode of the low level streams is always NOT_OWNING (the buffer is owned either by
    // ScheduledInputStream or by the user)
    for (auto &stream : streams) {
        auto status = stream.second.get().set_buffer_mode(StreamBufferMode::NOT_OWNING);
        CHECK_SUCCESS_AS_EXPECTED(status);
    }

    auto status = HAILO_UNINITIALIZED;
    auto local_vdevice_stream = make_unique_nothrow<ScheduledInputStream>(vdevice, std::move(streams), core_op_handle,
        std::move(core_op_activated_event), layer_info, std::move(infer_requests_accumulator), status);
    CHECK_NOT_NULL_AS_EXPECTED(local_vdevice_stream, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return local_vdevice_stream;
}

hailo_stream_interface_t ScheduledInputStream::get_interface() const
{
    // All interface values of m_streams should be the same
    return m_streams.begin()->second.get().get_interface();
}

Expected<std::unique_ptr<StreamBufferPool>> ScheduledInputStream::allocate_buffer_pool()
{
    TRY(auto queued_pool, QueuedStreamBufferPool::create(m_infer_requests_accumulator->queue_size(), get_frame_size(),
        BufferStorageParams::create_dma()));

    CHECK_SUCCESS(queued_pool->dma_map(m_vdevice, HAILO_DMA_BUFFER_DIRECTION_H2D));

    return std::unique_ptr<StreamBufferPool>(std::move(queued_pool));
}

size_t ScheduledInputStream::get_max_ongoing_transfers() const
{
    return m_infer_requests_accumulator->queue_size();
}

hailo_status ScheduledInputStream::write_async_impl(TransferRequest &&transfer_request)
{
    TRACE(FrameEnqueueH2DTrace, m_core_op_handle, name());

    transfer_request.callback = m_callback_reorder_queue.wrap_callback(transfer_request.callback);
    auto status = m_infer_requests_accumulator->add_transfer_request(name(), std::move(transfer_request));
    if (HAILO_SUCCESS != status) {
        m_callback_reorder_queue.cancel_last_callback();
        if (HAILO_QUEUE_IS_FULL == status) {
            return status;
        }
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

/** Output stream **/
Expected<std::unique_ptr<ScheduledOutputStream>> ScheduledOutputStream::create(
    VDevice &vdevice,
    std::map<device_id_t, std::reference_wrapper<OutputStreamBase>> &&streams,
    const scheduler_core_op_handle_t &core_op_handle,
    const LayerInfo &layer_info,
    EventPtr core_op_activated_event,
    std::shared_ptr<InferRequestAccumulator> infer_requests_accumulator)
{
    // In all cases, the buffer mode of the low level streams is always NOT_OWNING (the buffer is owned either by
    // ScheduledOutputStream or by the user)
    for (auto &stream : streams) {
        auto status = stream.second.get().set_buffer_mode(StreamBufferMode::NOT_OWNING);
        CHECK_SUCCESS_AS_EXPECTED(status);
    }


    auto status = HAILO_UNINITIALIZED;
    auto stream = make_unique_nothrow<ScheduledOutputStream>(vdevice, std::move(streams), core_op_handle,
        layer_info, std::move(core_op_activated_event), std::move(infer_requests_accumulator), status);
    CHECK_NOT_NULL_AS_EXPECTED(stream, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return stream;
}

hailo_stream_interface_t ScheduledOutputStream::get_interface() const
{
    // All interface values of m_streams should be the same
    return m_streams.begin()->second.get().get_interface();
}

Expected<std::unique_ptr<StreamBufferPool>> ScheduledOutputStream::allocate_buffer_pool()
{
    TRY(auto queued_pool, QueuedStreamBufferPool::create(m_infer_requests_accumulator->queue_size(), get_frame_size(),
        BufferStorageParams::create_dma()));

    CHECK_SUCCESS(queued_pool->dma_map(m_vdevice, HAILO_DMA_BUFFER_DIRECTION_D2H));

    return std::unique_ptr<StreamBufferPool>(std::move(queued_pool));
}

size_t ScheduledOutputStream::get_max_ongoing_transfers() const
{
    return m_infer_requests_accumulator->queue_size();
}

hailo_status ScheduledOutputStream::read_async_impl(TransferRequest &&transfer_request)
{
    transfer_request.callback = m_callback_reorder_queue.wrap_callback(transfer_request.callback);
    auto status = m_infer_requests_accumulator->add_transfer_request(name(), std::move(transfer_request));
    if (HAILO_SUCCESS != status) {
        m_callback_reorder_queue.cancel_last_callback();
        if (HAILO_QUEUE_IS_FULL == status) {
            return status;
        }
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

} /* namespace hailort */
