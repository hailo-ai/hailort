/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file multi_device_scheduled_stream.cpp
 * @brief TODO: brief
 *
 * TODO: doc
 **/

#include "vdevice/scheduler/multi_device_scheduled_stream.hpp"

namespace hailort
{

Expected<std::unique_ptr<MultiDeviceScheduledInputStream>> MultiDeviceScheduledInputStream::create(
    std::map<device_id_t, std::reference_wrapper<VdmaInputStreamBase>> &&streams,
    const scheduler_core_op_handle_t &core_op_handle,
    EventPtr &&core_op_activated_event,
    const LayerInfo &layer_info,
    CoreOpsSchedulerWeakPtr core_ops_scheduler)
{
    auto buffer_frame_size = streams.begin()->second.get().get_buffer_frames_size();
    CHECK_EXPECTED(buffer_frame_size);
    auto frame_size = streams.begin()->second.get().get_frame_size();
    auto buffers_queue_ptr = BuffersQueue::create_unique(frame_size, (streams.size() * buffer_frame_size.value()));
    CHECK_EXPECTED(buffers_queue_ptr);

    auto status = HAILO_UNINITIALIZED;
    auto stream = make_unique_nothrow<MultiDeviceScheduledInputStream>(std::move(streams),
        core_op_handle, std::move(core_op_activated_event), layer_info,
        core_ops_scheduler, buffers_queue_ptr.release(), status);
    CHECK_AS_EXPECTED((nullptr != stream), HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return stream;
}

hailo_status MultiDeviceScheduledInputStream::send_pending_buffer(const device_id_t &device_id)
{
    auto buffer = m_queue->front(get_timeout()); // Counting on scheduler to not allow paralle calls to this function
    if (HAILO_STREAM_ABORTED_BY_USER == buffer.status()) {
        LOGGER__INFO("'front' was aborted.");
        return buffer.status();
    }
    CHECK_EXPECTED_AS_STATUS(buffer);
    assert(contains(m_streams, device_id));
    auto status = m_streams.at(device_id).get().write_buffer_only(buffer.value());
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        LOGGER__INFO("send_pending_buffer was aborted.");
        return status;
    }
    CHECK_SUCCESS(status);
    m_queue->pop(); // Release buffer to free the queue for other dequeues

    auto &vdma_input = dynamic_cast<VdmaInputStreamBase&>(m_streams.at(device_id).get());
    return vdma_input.send_pending_buffer(device_id);
}

hailo_status MultiDeviceScheduledInputStream::write_impl(const MemoryView &buffer,
    const std::function<bool()> &should_cancel)
{
    if (should_cancel()) {
        return HAILO_STREAM_ABORTED_BY_USER;
    }

    auto core_ops_scheduler = m_core_ops_scheduler.lock();
    CHECK(core_ops_scheduler, HAILO_INTERNAL_FAILURE);

    auto status = m_queue->push(buffer, get_timeout());
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        LOGGER__INFO("'push' was aborted.");
        return status;
    }
    CHECK_SUCCESS(status);

    auto write_finish_status = core_ops_scheduler->signal_frame_pending_to_send(m_core_op_handle, name());
    if (HAILO_STREAM_ABORTED_BY_USER == write_finish_status) {
        return write_finish_status;
    }
    CHECK_SUCCESS(write_finish_status);

    return HAILO_SUCCESS;
}

Expected<size_t> MultiDeviceScheduledInputStream::get_pending_frames_count() const
{
    return get_queue_size();
}

size_t MultiDeviceScheduledInputStream::get_queue_size() const
{
    return m_queue->size();
}

hailo_status MultiDeviceScheduledInputStream::abort()
{
    auto status = HAILO_SUCCESS; // Best effort
    for (const auto &pair : m_streams) {
        auto &stream = pair.second;
        auto abort_status = stream.get().abort();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to abort input stream. (status: {} device: {})", status, stream.get().get_dev_id());
            status = abort_status;
        }
    }
    m_queue->abort();

    auto core_ops_scheduler = m_core_ops_scheduler.lock();
    CHECK(core_ops_scheduler, HAILO_INTERNAL_FAILURE);

    auto disable_status = core_ops_scheduler->disable_stream(m_core_op_handle, name());
    if (HAILO_SUCCESS != disable_status) {
        LOGGER__ERROR("Failed to disable stream in the core-op scheduler. (status: {})", disable_status);
        status = disable_status;
    }

    return status;
}

hailo_status MultiDeviceScheduledInputStream::clear_abort()
{
    auto status = HAILO_SUCCESS; // Best effort
    for (const auto &pair : m_streams) {
        auto &stream = pair.second;
        auto clear_abort_status = stream.get().clear_abort();
        if ((HAILO_SUCCESS != clear_abort_status) && (HAILO_STREAM_NOT_ACTIVATED != clear_abort_status)) {
            LOGGER__ERROR("Failed to clear abort input stream. (status: {} device: {})", clear_abort_status, stream.get().get_dev_id());
            status = clear_abort_status;
        }
    }
    m_queue->clear_abort();

    auto core_ops_scheduler = m_core_ops_scheduler.lock();
    CHECK(core_ops_scheduler, HAILO_INTERNAL_FAILURE);

    auto enable_status = core_ops_scheduler->enable_stream(m_core_op_handle, name());
    if (HAILO_SUCCESS != enable_status) {
        LOGGER__ERROR("Failed to enable stream in the core-op scheduler. (status: {})", enable_status);
        status = enable_status;
    }

    return status;
}

} /* namespace hailort */
