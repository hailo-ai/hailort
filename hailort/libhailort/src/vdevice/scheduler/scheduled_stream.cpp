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

#include "utils/profiler/tracer_macros.hpp"

namespace hailort
{

/** Input stream **/
Expected<std::unique_ptr<ScheduledInputStream>> ScheduledInputStream::create(
    std::map<device_id_t, std::reference_wrapper<VdmaInputStreamBase>> &&streams,
    const scheduler_core_op_handle_t &core_op_handle,
    EventPtr &&core_op_activated_event,
    const LayerInfo &layer_info,
    CoreOpsSchedulerWeakPtr core_ops_scheduler)
{
    auto status = HAILO_UNINITIALIZED;
    auto local_vdevice_stream = make_unique_nothrow<ScheduledInputStream>(std::move(streams),
        core_op_handle, std::move(core_op_activated_event), layer_info,
        core_ops_scheduler, status);
    CHECK_NOT_NULL_AS_EXPECTED(local_vdevice_stream, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return local_vdevice_stream;
}

hailo_status ScheduledInputStreamBase::abort()
{
    return abort_impl(m_core_op_handle);
}

hailo_status ScheduledInputStreamBase::abort_impl(scheduler_core_op_handle_t core_op_handle)
{
    auto status = HAILO_SUCCESS; // Best effort
    assert(1 == m_streams.size());
    auto abort_status = m_streams.begin()->second.get().abort();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to abort input stream. (status: {} device: {})", status, m_streams.begin()->second.get().get_dev_id());
        status = abort_status;
    }

    auto core_ops_scheduler = m_core_ops_scheduler.lock();
    CHECK(core_ops_scheduler, HAILO_INTERNAL_FAILURE);

    auto disable_status = core_ops_scheduler->disable_stream(core_op_handle, name());
    if (HAILO_SUCCESS != disable_status) {
        LOGGER__ERROR("Failed to disable stream in the core-op scheduler. (status: {})", disable_status);
        status = disable_status;
    }

    return status;
}

hailo_status ScheduledInputStreamBase::clear_abort()
{
    return clear_abort_impl(m_core_op_handle);
}

hailo_status ScheduledInputStreamBase::flush()
{
    auto core_ops_scheduler = m_core_ops_scheduler.lock();
    CHECK(core_ops_scheduler, HAILO_INTERNAL_FAILURE);

    auto status = core_ops_scheduler->flush_pending_buffers(m_core_op_handle, name(), get_timeout());
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        LOGGER__INFO("Got HAILO_STREAM_ABORTED_BY_USER in flush of stream {}", name());
        return status;
    }
    CHECK_SUCCESS(status);

    return VDeviceInputStreamBase::flush();
}

hailo_status ScheduledInputStreamBase::clear_abort_impl(scheduler_core_op_handle_t core_op_handle)
{
    auto status = HAILO_SUCCESS; // Best effort
    assert(1 == m_streams.size());
    auto clear_abort_status = m_streams.begin()->second.get().clear_abort();
    if ((HAILO_SUCCESS != clear_abort_status) && (HAILO_STREAM_NOT_ACTIVATED != clear_abort_status)) {
            LOGGER__ERROR("Failed to clear abort input stream. (status: {} device: {})", clear_abort_status, m_streams.begin()->second.get().get_dev_id());
            status = clear_abort_status;
    }

    auto core_ops_scheduler = m_core_ops_scheduler.lock();
    CHECK(core_ops_scheduler, HAILO_INTERNAL_FAILURE);

    auto enable_status = core_ops_scheduler->enable_stream(core_op_handle, name());
    if (HAILO_SUCCESS != enable_status) {
        LOGGER__ERROR("Failed to enable stream in the core-op scheduler. (status: {})", enable_status);
        status = enable_status;
    }

    return status;
}

hailo_status ScheduledInputStream::write_impl(const MemoryView &buffer, const std::function<bool()> &should_cancel)
{
    auto core_ops_scheduler = m_core_ops_scheduler.lock();
    CHECK(core_ops_scheduler, HAILO_INTERNAL_FAILURE);

    assert(1 == m_streams.size());
    auto status = m_streams.begin()->second.get().write_buffer_only(buffer, should_cancel);
    if (HAILO_SUCCESS != status) {
        LOGGER__INFO("Write to stream has failed! status = {}", status);
        return status;
    }

    auto write_finish_status = core_ops_scheduler->signal_frame_pending_to_send(m_core_op_handle, name());
    if (HAILO_STREAM_ABORTED_BY_USER == write_finish_status) {
        return write_finish_status;
    }
    CHECK_SUCCESS(write_finish_status);

    return HAILO_SUCCESS;
}

Expected<std::unique_ptr<ScheduledAsyncInputStream>> ScheduledAsyncInputStream::create(
    std::map<device_id_t, std::reference_wrapper<VdmaInputStreamBase>> &&streams,
    const scheduler_core_op_handle_t &core_op_handle,
    EventPtr &&core_op_activated_event,
    const LayerInfo &layer_info,
    CoreOpsSchedulerWeakPtr core_ops_scheduler)
{
    auto max_queue_size_per_stream = streams.begin()->second.get().get_buffer_frames_size();
    CHECK_EXPECTED(max_queue_size_per_stream);
    const auto max_queue_size = max_queue_size_per_stream.value() * streams.size();

    auto status = HAILO_UNINITIALIZED;
    auto local_vdevice_stream = make_unique_nothrow<ScheduledAsyncInputStream>(std::move(streams),
        core_op_handle, std::move(core_op_activated_event), layer_info,
        core_ops_scheduler, max_queue_size, status);
    CHECK_NOT_NULL_AS_EXPECTED(local_vdevice_stream, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return local_vdevice_stream;
}

hailo_status ScheduledAsyncInputStream::send_pending_buffer(const device_id_t &device_id)
{
    // TODO HRT-10583 - allow option to remove reorder queue
    auto pending_buffer = m_pending_buffers.dequeue();
    CHECK_EXPECTED_AS_STATUS(pending_buffer);

    pending_buffer->callback = m_callback_reorder_queue.wrap_callback(pending_buffer->callback);
    assert(contains(m_streams, device_id));
    auto status = m_streams.at(device_id).get().write_async(pending_buffer.release());
    if (HAILO_SUCCESS != status) {
        m_callback_reorder_queue.cancel_last_callback();
    }
    return status;
}

hailo_status ScheduledAsyncInputStream::wait_for_async_ready(size_t transfer_size, std::chrono::milliseconds timeout)
{
    (void)transfer_size;
    return m_pending_buffers.wait_for_room(timeout);
}

hailo_status ScheduledAsyncInputStream::write_async(TransferRequest &&transfer_request)
{
    auto core_ops_scheduler = m_core_ops_scheduler.lock();
    CHECK(core_ops_scheduler, HAILO_INTERNAL_FAILURE);

    auto status = m_pending_buffers.enqueue(std::move(transfer_request));
    CHECK_SUCCESS(status);

    auto write_finish_status = core_ops_scheduler->signal_frame_pending_to_send(m_core_op_handle, name());
    if (HAILO_STREAM_ABORTED_BY_USER == write_finish_status) {
        return write_finish_status;
    }
    CHECK_SUCCESS(write_finish_status);

    return HAILO_SUCCESS;
}

Expected<size_t> ScheduledAsyncInputStream::get_async_max_queue_size() const
{
    return m_pending_buffers.max_size();
}


hailo_status ScheduledAsyncInputStream::abort()
{
    m_pending_buffers.abort();
    return ScheduledInputStreamBase::abort();
}

hailo_status ScheduledAsyncInputStream::clear_abort()
{
    m_pending_buffers.clear_abort();
    return ScheduledInputStreamBase::clear_abort();
}

hailo_status ScheduledAsyncInputStream::write_impl(const MemoryView &, const std::function<bool()> &)
{
    LOGGER__ERROR("Sync write is not supported by async streams");
    return HAILO_NOT_SUPPORTED;
}

/** Output stream **/
Expected<std::unique_ptr<ScheduledOutputStream>> ScheduledOutputStream::create(
    std::map<device_id_t, std::reference_wrapper<VdmaOutputStreamBase>> &&streams,
    const scheduler_core_op_handle_t &core_op_handle,
    const LayerInfo &layer_info,
    EventPtr &&core_op_activated_event,
    CoreOpsSchedulerWeakPtr core_ops_scheduler)
{
    auto status = HAILO_UNINITIALIZED;
    auto stream = make_unique_nothrow<ScheduledOutputStream>(std::move(streams), core_op_handle,
        layer_info, std::move(core_op_activated_event), core_ops_scheduler, status);
    CHECK_NOT_NULL_AS_EXPECTED(stream, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return stream;
}

ScheduledOutputStream::ScheduledOutputStream(
        std::map<device_id_t, std::reference_wrapper<VdmaOutputStreamBase>> &&streams,
        const scheduler_core_op_handle_t &core_op_handle,
        const LayerInfo &layer_info,
        EventPtr &&core_op_activated_event,
        CoreOpsSchedulerWeakPtr core_ops_scheduler,
        hailo_status &status) : ScheduledOutputStreamBase(std::move(streams), core_op_handle, layer_info,
            std::move(core_op_activated_event), core_ops_scheduler, status)
    {
        for (auto &stream_pair : m_streams) {
            stream_pair.second.get().register_interrupt_callback(
                [scheduler_weak=m_core_ops_scheduler, core_op_handle=m_core_op_handle, name=name(), device_id=stream_pair.first]() {
                    auto scheduler = scheduler_weak.lock();
                    assert(scheduler);
                    scheduler->signal_frame_transferred_d2h(core_op_handle, name, device_id);
                }
            );
        }
    }

hailo_status ScheduledOutputStream::set_next_device_to_read(const device_id_t &device_id)
{
    std::lock_guard<std::mutex> lock(m_device_read_order_mutex);
    m_device_read_order.push(device_id);
    return HAILO_SUCCESS;
}

hailo_status ScheduledOutputStreamBase::abort()
{
    return abort_impl(m_core_op_handle);
}

hailo_status ScheduledOutputStreamBase::abort_impl(scheduler_core_op_handle_t core_op_handle)
{
    auto status = HAILO_SUCCESS; // Best effort
    for (const auto &pair : m_streams) {
        auto &stream = pair.second;
        auto abort_status = stream.get().abort();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to abort output stream. (status: {} device: {})", status, stream.get().get_dev_id());
            status = abort_status;
        }
    }
    auto core_ops_scheduler = m_core_ops_scheduler.lock();
    CHECK(core_ops_scheduler, HAILO_INTERNAL_FAILURE);

    auto disable_status = core_ops_scheduler->disable_stream(core_op_handle, name());
    if (HAILO_SUCCESS != disable_status) {
        LOGGER__ERROR("Failed to disable stream in the core-op scheduler. (status: {})", disable_status);
        status = disable_status;
    }

    return status;
}

hailo_status ScheduledOutputStreamBase::clear_abort()
{
    return clear_abort_impl(m_core_op_handle);
}

hailo_status ScheduledOutputStreamBase::clear_abort_impl(scheduler_core_op_handle_t core_op_handle)
{
    auto status = HAILO_SUCCESS; // Best effort
    for (const auto &pair : m_streams) {
        auto &stream = pair.second;
        auto clear_abort_status = stream.get().clear_abort();
        if ((HAILO_SUCCESS != clear_abort_status) && (HAILO_STREAM_NOT_ACTIVATED != clear_abort_status)) {
            LOGGER__ERROR("Failed to clear abort output stream. (status: {} device: {})", clear_abort_status, stream.get().get_dev_id());
            status = clear_abort_status;
        }
    }

    auto core_ops_scheduler = m_core_ops_scheduler.lock();
    CHECK(core_ops_scheduler, HAILO_INTERNAL_FAILURE);

    auto enable_status = core_ops_scheduler->enable_stream(core_op_handle, name());
    if (HAILO_SUCCESS != enable_status) {
        LOGGER__ERROR("Failed to enable stream in the core-op scheduler. (status: {})", enable_status);
        status = enable_status;
    }

    return status;
}

hailo_status ScheduledOutputStream::read(MemoryView buffer)
{
    auto core_ops_scheduler = m_core_ops_scheduler.lock();
    CHECK(core_ops_scheduler, HAILO_INTERNAL_FAILURE);

    auto status = core_ops_scheduler->signal_frame_pending_to_read(m_core_op_handle, name());
    CHECK_SUCCESS(status);

    auto device_id = wait_for_read();
    if (HAILO_STREAM_ABORTED_BY_USER == device_id.status()) {
        LOGGER__INFO("Read from stream was aborted.");
        return device_id.status();
    }
    CHECK_EXPECTED_AS_STATUS(device_id);

    assert(contains(m_streams, device_id.value()));
    status = m_streams.at(device_id.value()).get().read(buffer);
    if (HAILO_SUCCESS != status) {
        LOGGER__INFO("Read from stream has failed! status = {}", status);
        return status;
    }

    status = core_ops_scheduler->signal_read_finish(m_core_op_handle, name(), device_id.value());
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        return status;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

Expected<device_id_t> ScheduledOutputStream::wait_for_read()
{
    auto core_ops_scheduler = m_core_ops_scheduler.lock();
    CHECK_AS_EXPECTED(core_ops_scheduler, HAILO_INTERNAL_FAILURE);

    auto status = core_ops_scheduler->wait_for_read(m_core_op_handle, name(), get_timeout(), [this]() {
        std::lock_guard<std::mutex> lock(m_device_read_order_mutex);
        return !m_device_read_order.empty();
    });
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        LOGGER__INFO("Read from stream was aborted.");
        return make_unexpected(status);
    }
    CHECK_SUCCESS_AS_EXPECTED(status);

    std::lock_guard<std::mutex> lock(m_device_read_order_mutex);
    auto device_id = m_device_read_order.front();
    m_device_read_order.pop();
    return device_id;
}

} /* namespace hailort */
