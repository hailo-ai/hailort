/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
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
    std::map<device_id_t, std::reference_wrapper<InputStreamBase>> &&streams,
    const LayerInfo &layer_info,
    const scheduler_core_op_handle_t &core_op_handle,
    CoreOpsSchedulerWeakPtr core_ops_scheduler,
    EventPtr core_op_activated_event)
{
    auto max_queue_size_per_stream = streams.begin()->second.get().get_buffer_frames_size();
    CHECK_EXPECTED(max_queue_size_per_stream);
    const auto max_queue_size = max_queue_size_per_stream.value() * streams.size();

    // In all cases, the buffer mode of the low level streams is always NOT_OWNING (the buffer is owned either by
    // ScheduledInputStream or by the user)
    for (auto &stream : streams) {
        auto status = stream.second.get().set_buffer_mode(StreamBufferMode::NOT_OWNING);
        CHECK_SUCCESS_AS_EXPECTED(status);
    }

    auto status = HAILO_UNINITIALIZED;
    auto local_vdevice_stream = make_unique_nothrow<ScheduledInputStream>(std::move(streams),
        core_op_handle, std::move(core_op_activated_event), layer_info,
        core_ops_scheduler, max_queue_size, status);
    CHECK_NOT_NULL_AS_EXPECTED(local_vdevice_stream, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return local_vdevice_stream;
}

hailo_status ScheduledInputStream::launch_transfer(const device_id_t &device_id)
{
    auto core_ops_scheduler = m_core_ops_scheduler.lock();
    CHECK(core_ops_scheduler, HAILO_INTERNAL_FAILURE, "core_op_scheduler was destructed");

    auto pending_buffer = m_transfer_requests.dequeue();
    CHECK_EXPECTED_AS_STATUS(pending_buffer);

    auto reorder_queue_callback = m_callback_reorder_queue.wrap_callback(pending_buffer->callback);
    pending_buffer->callback = reorder_queue_callback;

    // Wrap callback with scheduler signal read finish.
    pending_buffer->callback = [this, device_id, callback=reorder_queue_callback](hailo_status status) {
        if (HAILO_SUCCESS == status) {
            auto scheduler = m_core_ops_scheduler.lock();
            assert(scheduler);
            scheduler->signal_frame_transferred(m_core_op_handle, name(), device_id, HAILO_H2D_STREAM);
        }

        callback(status);
    };

    assert(contains(m_streams, device_id));
    auto status = m_streams.at(device_id).get().write_async(pending_buffer.release());
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("write_async on device {} failed with {}", device_id, status);
        // The pending_buffer was already registered so we must call the callback to give the error back to the user.
        reorder_queue_callback(status);
    }
    return status;
}

hailo_stream_interface_t ScheduledInputStream::get_interface() const
{
    // All interface values of m_streams should be the same
    return m_streams.begin()->second.get().get_interface();
}

Expected<std::unique_ptr<StreamBufferPool>> ScheduledInputStream::allocate_buffer_pool()
{
    if (m_streams.size() == 1) {
        // On single device, we use the stream allocate_buffer_pool for best optimization (The buffer can be circular
        // dma buffer)
        auto &async_stream = dynamic_cast<AsyncInputStreamBase&>(m_streams.begin()->second.get());
        return async_stream.allocate_buffer_pool();
    } else {
        auto queued_pool = QueuedStreamBufferPool::create(m_transfer_requests.max_size(), get_frame_size(),
            BufferStorageParams::create_dma());
        CHECK_EXPECTED(queued_pool);

        return std::unique_ptr<StreamBufferPool>(queued_pool.release());
    }
}

size_t ScheduledInputStream::get_max_ongoing_transfers() const
{
    return m_transfer_requests.max_size();
}

hailo_status ScheduledInputStream::write_async_impl(TransferRequest &&transfer_request)
{
    auto core_ops_scheduler = m_core_ops_scheduler.lock();
    CHECK(core_ops_scheduler, HAILO_INTERNAL_FAILURE, "core_op_scheduler was destructed");

    auto status = m_transfer_requests.enqueue(std::move(transfer_request));
    if (HAILO_QUEUE_IS_FULL == status) {
        return status;
    }
    CHECK_SUCCESS(status);

    status = core_ops_scheduler->signal_frame_pending(m_core_op_handle, name(), HAILO_H2D_STREAM);
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        return status;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status ScheduledInputStream::abort()
{
    auto core_ops_scheduler = m_core_ops_scheduler.lock();
    CHECK(core_ops_scheduler, HAILO_INTERNAL_FAILURE, "core_op_scheduler was destructed");

    core_ops_scheduler->disable_stream(m_core_op_handle, name());

    return AsyncInputStreamBase::abort();
}

hailo_status ScheduledInputStream::clear_abort()
{
    auto core_ops_scheduler = m_core_ops_scheduler.lock();
    CHECK(core_ops_scheduler, HAILO_INTERNAL_FAILURE);

    core_ops_scheduler->enable_stream(m_core_op_handle, name());

    return AsyncInputStreamBase::clear_abort();
}

/** Output stream **/
Expected<std::unique_ptr<ScheduledOutputStream>> ScheduledOutputStream::create(
    std::map<device_id_t, std::reference_wrapper<OutputStreamBase>> &&streams,
    const scheduler_core_op_handle_t &core_op_handle,
    const LayerInfo &layer_info,
    EventPtr core_op_activated_event,
    CoreOpsSchedulerWeakPtr core_ops_scheduler)
{
    auto max_queue_size_per_stream = streams.begin()->second.get().get_buffer_frames_size();
    CHECK_EXPECTED(max_queue_size_per_stream);
    const auto max_queue_size = max_queue_size_per_stream.value() * streams.size();

    // In all cases, the buffer mode of the low level streams is always NOT_OWNING (the buffer is owned either by
    // ScheduledOutputStream or by the user)
    for (auto &stream : streams) {
        auto status = stream.second.get().set_buffer_mode(StreamBufferMode::NOT_OWNING);
        CHECK_SUCCESS_AS_EXPECTED(status);
    }


    auto status = HAILO_UNINITIALIZED;
    auto stream = make_unique_nothrow<ScheduledOutputStream>(std::move(streams), core_op_handle,
        layer_info, std::move(core_op_activated_event), core_ops_scheduler, max_queue_size, status);
    CHECK_NOT_NULL_AS_EXPECTED(stream, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return stream;
}

hailo_status ScheduledOutputStream::launch_transfer(const device_id_t &device_id)
{
    auto core_ops_scheduler = m_core_ops_scheduler.lock();
    CHECK(core_ops_scheduler, HAILO_INTERNAL_FAILURE, "core_op_scheduler was destructed");

    auto pending_buffer = m_transfer_requests.dequeue();
    CHECK_EXPECTED_AS_STATUS(pending_buffer);

    // Wrap callback with reorder queue.
    auto reorder_queue_callback = m_callback_reorder_queue.wrap_callback(pending_buffer->callback);

    // Wrap callback with scheduler signal read finish.
    pending_buffer->callback = [this, device_id, callback=reorder_queue_callback](hailo_status status) {
        if (HAILO_SUCCESS == status) {
            auto scheduler = m_core_ops_scheduler.lock();
            assert(scheduler);
            scheduler->signal_frame_transferred(m_core_op_handle, name(), device_id, HAILO_D2H_STREAM);

            if (buffer_mode() == StreamBufferMode::NOT_OWNING) {
                // On OWNING mode this trace is called after read_impl is called.
                TRACE(ReadFrameTrace, m_core_op_handle, name());
            }
        }

        callback(status);
    };

    assert(contains(m_streams, device_id));
    auto status = m_streams.at(device_id).get().read_async(pending_buffer.release());
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("read_async on device {} failed with {}", device_id, status);
        // The pending_buffer was already registered so we must call the callback to give the error back to the user.
        reorder_queue_callback(status);
    }
    return status;
}

hailo_stream_interface_t ScheduledOutputStream::get_interface() const
{
    // All interface values of m_streams should be the same
    return m_streams.begin()->second.get().get_interface();
}

Expected<std::unique_ptr<StreamBufferPool>> ScheduledOutputStream::allocate_buffer_pool()
{
    if (m_streams.size() == 1) {
        // On single device, we use the stream allocate_buffer_pool for best optimization (The buffer can be circular
        // dma buffer)
        auto &async_stream = dynamic_cast<AsyncOutputStreamBase&>(m_streams.begin()->second.get());
        return async_stream.allocate_buffer_pool();
    } else {
        auto queued_pool = QueuedStreamBufferPool::create(m_transfer_requests.max_size(), get_frame_size(),
            BufferStorageParams::create_dma());
        CHECK_EXPECTED(queued_pool);

        return std::unique_ptr<StreamBufferPool>(queued_pool.release());
    }
}

size_t ScheduledOutputStream::get_max_ongoing_transfers() const
{
    return m_transfer_requests.max_size();
}


hailo_status ScheduledOutputStream::read_async_impl(TransferRequest &&transfer_request)
{
    auto core_ops_scheduler = m_core_ops_scheduler.lock();
    CHECK(core_ops_scheduler, HAILO_INTERNAL_FAILURE, "core_op_scheduler was destructed");

    auto status = m_transfer_requests.enqueue(std::move(transfer_request));
    if (HAILO_QUEUE_IS_FULL == status) {
        return status;
    }
    CHECK_SUCCESS(status);

    status = core_ops_scheduler->signal_frame_pending(m_core_op_handle, name(), HAILO_D2H_STREAM);
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        return status;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status ScheduledOutputStream::abort()
{
    auto core_ops_scheduler = m_core_ops_scheduler.lock();
    CHECK(core_ops_scheduler, HAILO_INTERNAL_FAILURE, "core_op_scheduler was destructed");

    core_ops_scheduler->disable_stream(m_core_op_handle, name());

    return AsyncOutputStreamBase::abort();
}

hailo_status ScheduledOutputStream::clear_abort()
{
    auto core_ops_scheduler = m_core_ops_scheduler.lock();
    CHECK(core_ops_scheduler, HAILO_INTERNAL_FAILURE);

    core_ops_scheduler->enable_stream(m_core_op_handle, name());

    return AsyncOutputStreamBase::clear_abort();
}

} /* namespace hailort */
