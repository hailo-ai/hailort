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

#include "multi_device_scheduled_stream.hpp"

namespace hailort
{

hailo_status MultiDeviceScheduledInputStream::send_pending_buffer(size_t device_index)
{
    auto buffer = dequeue();
    CHECK_EXPECTED_AS_STATUS(buffer);
    auto status = m_streams[device_index].get().write_buffer_only(buffer.value());
    CHECK_SUCCESS(status);

    VdmaInputStream &vdma_input = static_cast<VdmaInputStream&>(m_streams[device_index].get());
    return vdma_input.send_pending_buffer();
}

Expected<size_t> MultiDeviceScheduledInputStream::sync_write_raw_buffer(const MemoryView &buffer,
    const std::function<bool()> &should_cancel)
{
    auto network_group_scheduler = m_network_group_scheduler.lock();
    CHECK_AS_EXPECTED(network_group_scheduler, HAILO_INTERNAL_FAILURE);

    auto status = network_group_scheduler->wait_for_write(m_network_group_handle, name(), get_timeout(), should_cancel);
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        LOGGER__INFO("Write to stream was aborted.");
        return make_unexpected(status);
    }
    CHECK_SUCCESS_AS_EXPECTED(status);

    status = enqueue(buffer);
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        LOGGER__INFO("Enqueue was aborted.");
        network_group_scheduler->mark_failed_write(m_network_group_handle, name());
        return make_unexpected(status);
    }
    CHECK_SUCCESS_AS_EXPECTED(status);

    status = network_group_scheduler->signal_write_finish(m_network_group_handle, name());
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        return make_unexpected(status);
    }
    CHECK_SUCCESS_AS_EXPECTED(status);

    return buffer.size();
}

Expected<size_t> MultiDeviceScheduledInputStream::get_pending_frames_count() const
{
    return get_queue_size();
}

hailo_status MultiDeviceScheduledInputStream::enqueue(const MemoryView &buffer)
{
    return m_queue->enqueue(buffer, get_timeout());
}

Expected<MemoryView> MultiDeviceScheduledInputStream::dequeue()
{
    return m_queue->dequeue(get_timeout());
}

size_t MultiDeviceScheduledInputStream::get_queue_size() const
{
    return m_queue->size();
}

hailo_status MultiDeviceScheduledInputStream::abort()
{
    auto status = HAILO_SUCCESS; // Best effort
    for (auto &stream : m_streams) {
        auto abort_status = stream.get().abort();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to abort input stream. (status: {} device: {})", status, stream.get().get_dev_id());
            status = abort_status;
        }
    }
    m_queue->abort();

    auto network_group_scheduler = m_network_group_scheduler.lock();
    CHECK(network_group_scheduler, HAILO_INTERNAL_FAILURE);

    auto disable_status = network_group_scheduler->disable_stream(m_network_group_handle, name());
    if (HAILO_SUCCESS != disable_status) {
        LOGGER__ERROR("Failed to disable stream in the network group scheduler. (status: {})", disable_status);
        status = disable_status;
    }

    return status;
}

hailo_status MultiDeviceScheduledInputStream::clear_abort()
{
    auto status = HAILO_SUCCESS; // Best effort
    for (auto &stream : m_streams) {
        auto clear_abort_status = stream.get().clear_abort();
        if ((HAILO_SUCCESS != clear_abort_status) && (HAILO_STREAM_NOT_ACTIVATED != clear_abort_status)) {
            LOGGER__ERROR("Failed to clear abort input stream. (status: {} device: {})", clear_abort_status, stream.get().get_dev_id());
            status = clear_abort_status;
        }
    }
    m_queue->clear_abort();

    auto network_group_scheduler = m_network_group_scheduler.lock();
    CHECK(network_group_scheduler, HAILO_INTERNAL_FAILURE);

    auto enable_status = network_group_scheduler->enable_stream(m_network_group_handle, name());
    if (HAILO_SUCCESS != enable_status) {
        LOGGER__ERROR("Failed to enable stream in the network group scheduler. (status: {})", enable_status);
        status = enable_status;
    }

    return status;
}

} /* namespace hailort */
