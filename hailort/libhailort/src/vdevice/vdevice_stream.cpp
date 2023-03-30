/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdevice_stream.cpp
 * @brief TODO: brief
 *
 * TODO: doc
 **/

#include "hailo/hailort.h"
#include "hailo/stream.hpp"
#include "hailo/hef.hpp"
#include "hailo/hailort_common.hpp"

#include "common/utils.hpp"

#include "utils/profiler/tracer_macros.hpp"
#include "vdevice/vdevice_stream.hpp"
#include "vdevice/vdevice_native_stream.hpp"
#include "vdevice/scheduler/multi_device_scheduled_stream.hpp"
#include "vdevice/scheduler/scheduled_stream.hpp"
#include "core_op/resource_manager/resource_manager.hpp"

#include <new>


namespace hailort
{

hailo_status InputVDeviceBaseStream::deactivate_stream()
{
    auto status = HAILO_SUCCESS; // Best effort
    for (auto &stream : m_streams) {
        auto deactivate_status = stream.get().deactivate_stream();
        if (HAILO_SUCCESS != deactivate_status) {
            LOGGER__ERROR("Failed to deactivate input stream. (status: {} device: {})", deactivate_status, stream.get().get_dev_id());
            status = deactivate_status;
        }
    }
    m_is_stream_activated = false;
    return status;
}

/** Input stream **/
InputVDeviceBaseStream::~InputVDeviceBaseStream()
{
    // We want to stop the vdma channel before closing the stream in the firmware
    // because sending data to a closed stream may terminate the dma engine
    if (m_is_stream_activated) {
        (void)deactivate_stream();
    }
}

hailo_status InputVDeviceBaseStream::activate_stream(uint16_t dynamic_batch_size, bool resume_pending_stream_transfers)
{
    for (auto &stream : m_streams) {
        auto status = stream.get().activate_stream(dynamic_batch_size, resume_pending_stream_transfers);
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to activate input stream. (device: {})", stream.get().get_dev_id());
            deactivate_stream();
            return status;
        }
    }
    m_is_stream_activated = true;
    return HAILO_SUCCESS;
}

hailo_status InputVDeviceBaseStream::sync_write_all_raw_buffer_no_transform_impl(void *buffer, size_t offset, size_t size)
{
    ASSERT(NULL != buffer);

    return sync_write_raw_buffer(MemoryView(static_cast<uint8_t*>(buffer) + offset, size)).status();
}

hailo_status InputVDeviceBaseStream::send_pending_buffer(size_t device_index)
{
    assert(1 == m_streams.size());
    CHECK(0 == device_index, HAILO_INVALID_OPERATION);
    VdmaInputStream &vdma_input = static_cast<VdmaInputStream&>(m_streams[m_next_transfer_stream_index].get());
    return vdma_input.send_pending_buffer();
}

Expected<size_t> InputVDeviceBaseStream::get_buffer_frames_size() const
{
    size_t total_buffers_size = 0;
    for (auto &stream : m_streams) {
        auto stream_buffer_size = stream.get().get_buffer_frames_size();
        CHECK_EXPECTED(stream_buffer_size);
        total_buffers_size += stream_buffer_size.value();
    }

    return total_buffers_size;
}

Expected<size_t> InputVDeviceBaseStream::get_pending_frames_count() const
{
    size_t total_pending_frames_count = 0;
    for (auto &stream : m_streams) {
        auto stream_pending_frames_count = stream.get().get_pending_frames_count();
        CHECK_EXPECTED(stream_pending_frames_count);
        total_pending_frames_count += stream_pending_frames_count.value();
    }

    return total_pending_frames_count;
}

Expected<std::unique_ptr<InputVDeviceBaseStream>> InputVDeviceBaseStream::create(std::vector<std::reference_wrapper<VdmaInputStream>> &&low_level_streams,
    const LayerInfo &edge_layer, const scheduler_core_op_handle_t &core_op_handle,
    EventPtr core_op_activated_event, CoreOpsSchedulerWeakPtr core_ops_scheduler)
{
    assert(0 < low_level_streams.size());
    auto status = HAILO_UNINITIALIZED;
    
    std::unique_ptr<InputVDeviceBaseStream> local_vdevice_stream;

    if (core_ops_scheduler.lock()) {
        if (1 < low_level_streams.size()) {
            auto buffer_frame_size = low_level_streams[0].get().get_buffer_frames_size();
            CHECK_EXPECTED(buffer_frame_size);
            auto frame_size = low_level_streams[0].get().get_frame_size();
            auto buffers_queue_ptr = BuffersQueue::create_unique(frame_size, (low_level_streams.size() * buffer_frame_size.value()));
            CHECK_EXPECTED(buffers_queue_ptr);

            local_vdevice_stream = make_unique_nothrow<MultiDeviceScheduledInputStream>(std::move(low_level_streams),
                core_op_handle, std::move(core_op_activated_event), edge_layer,
                core_ops_scheduler, buffers_queue_ptr.release(), status);
        } else {
            local_vdevice_stream = make_unique_nothrow<ScheduledInputStream>(std::move(low_level_streams),
                core_op_handle, std::move(core_op_activated_event), edge_layer,
                core_ops_scheduler, status);
        }
    } else {
        local_vdevice_stream = make_unique_nothrow<InputVDeviceNativeStream>(std::move(low_level_streams),
            std::move(core_op_activated_event), edge_layer,status);
    }

    CHECK_AS_EXPECTED((nullptr != local_vdevice_stream), HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return local_vdevice_stream;
}

hailo_status InputVDeviceBaseStream::set_timeout(std::chrono::milliseconds timeout)
{
    for (auto &stream : m_streams) {
        auto status = stream.get().set_timeout(timeout);
        CHECK_SUCCESS(status, "Failed to set timeout to input stream. (device: {})", stream.get().get_dev_id());
    }
    return HAILO_SUCCESS;
}

std::chrono::milliseconds InputVDeviceBaseStream::get_timeout() const
{
    // All timeout values of m_streams should be the same
    return m_streams[0].get().get_timeout();
}

hailo_stream_interface_t InputVDeviceBaseStream::get_interface() const
{
    // All interface values of m_streams should be the same
    return m_streams[0].get().get_interface();
}

hailo_status InputVDeviceBaseStream::flush()
{
    auto status = HAILO_SUCCESS; // Best effort
    for (auto &stream : m_streams) {
        auto flush_status = stream.get().flush();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to flush input stream. (status: {} device: {})", status, stream.get().get_dev_id());
            status = flush_status;
        }
    }
    return status;
}

Expected<size_t> ScheduledInputStream::sync_write_raw_buffer(const MemoryView &buffer, const std::function<bool()> &should_cancel)
{
    return sync_write_raw_buffer_impl(buffer, m_core_op_handle, should_cancel);
}

Expected<size_t> InputVDeviceNativeStream::sync_write_raw_buffer(const MemoryView &buffer, const std::function<bool()> &should_cancel)
{
    if (should_cancel()) {
        return make_unexpected(HAILO_STREAM_ABORTED_BY_USER);
    }

    auto expected_written_bytes = m_streams[m_next_transfer_stream_index].get().sync_write_raw_buffer(buffer);
    if (HAILO_SUCCESS != expected_written_bytes.status()) {
        LOGGER__INFO("Write to stream has failed! status = {}", expected_written_bytes.status());
        return make_unexpected(expected_written_bytes.status());
    }
    auto written_bytes = expected_written_bytes.value();

    // Update m_next_transfer_stream_index only if 'batch' frames has been transferred
    if (0 == (++m_acc_frames % m_streams[0].get().get_dynamic_batch_size())) {
        m_next_transfer_stream_index = static_cast<uint32_t>((m_next_transfer_stream_index + 1) % m_streams.size());
        m_acc_frames = 0;
    }
    return written_bytes;
}

Expected<size_t> ScheduledInputStream::sync_write_raw_buffer_impl(const MemoryView &buffer, scheduler_core_op_handle_t core_op_handle,
    const std::function<bool()> &should_cancel)
{
    auto core_ops_scheduler = m_core_ops_scheduler.lock();
    CHECK_AS_EXPECTED(core_ops_scheduler, HAILO_INTERNAL_FAILURE);

    auto status = core_ops_scheduler->wait_for_write(core_op_handle, name(), get_timeout(), should_cancel);
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        LOGGER__INFO("Write to stream was aborted.");
        return make_unexpected(status);
    }
    CHECK_SUCCESS_AS_EXPECTED(status);

    TRACE(WriteFrameTrace, "", core_op_handle, m_stream_info.name);

    assert(1 == m_streams.size());
    status = m_streams[0].get().write_buffer_only(buffer, should_cancel);

    auto write_finish_status = core_ops_scheduler->signal_write_finish(core_op_handle, name(), status != HAILO_SUCCESS);
    if (HAILO_SUCCESS != status) {
        LOGGER__INFO("Write to stream has failed! status = {}", status);
        return make_unexpected(status);
    }

    if (HAILO_STREAM_ABORTED_BY_USER == write_finish_status) {
        return make_unexpected(write_finish_status);
    }
    CHECK_SUCCESS_AS_EXPECTED(write_finish_status);

    auto written_bytes = buffer.size();
    return written_bytes;
}

hailo_status ScheduledInputStream::abort()
{
    return abort_impl(m_core_op_handle);
}

hailo_status InputVDeviceNativeStream::abort()
{
    auto status = HAILO_SUCCESS; // Best effort
    for (auto &stream : m_streams) {
        auto abort_status = stream.get().abort();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to abort input stream. (status: {} device: {})", status, stream.get().get_dev_id());
            status = abort_status;
        }
    }

    return status;
}

hailo_status ScheduledInputStream::abort_impl(scheduler_core_op_handle_t core_op_handle)
{
    auto status = HAILO_SUCCESS; // Best effort
    assert(1 == m_streams.size());
    auto abort_status = m_streams[0].get().abort();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to abort input stream. (status: {} device: {})", status, m_streams[0].get().get_dev_id());
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

hailo_status ScheduledInputStream::clear_abort()
{
    return clear_abort_impl(m_core_op_handle);
}

hailo_status InputVDeviceNativeStream::clear_abort()
{
    auto status = HAILO_SUCCESS; // Best effort
    for (auto &stream : m_streams) {
        auto clear_abort_status = stream.get().clear_abort();
        if ((HAILO_SUCCESS != clear_abort_status) && (HAILO_STREAM_NOT_ACTIVATED != clear_abort_status)) {
            LOGGER__ERROR("Failed to clear abort input stream. (status: {} device: {})", clear_abort_status, stream.get().get_dev_id());
            status = clear_abort_status;
        }
    }

    return status;
}

hailo_status ScheduledInputStream::clear_abort_impl(scheduler_core_op_handle_t core_op_handle)
{
    auto status = HAILO_SUCCESS; // Best effort
    assert(1 == m_streams.size());
    auto clear_abort_status = m_streams[0].get().clear_abort();
    if ((HAILO_SUCCESS != clear_abort_status) && (HAILO_STREAM_NOT_ACTIVATED != clear_abort_status)) {
            LOGGER__ERROR("Failed to clear abort input stream. (status: {} device: {})", clear_abort_status, m_streams[0].get().get_dev_id());
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

/** Output stream **/
hailo_status OutputVDeviceBaseStream::deactivate_stream()
{
    auto status = HAILO_SUCCESS; // Best effort
    for (auto &stream : m_streams) {
        auto deactivate_status = stream.get().deactivate_stream();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to deactivate output stream. (status: {} device: {})", status, stream.get().get_dev_id());
            status = deactivate_status;
        }
    }
    m_is_stream_activated = false;
    return status;
}

OutputVDeviceBaseStream::~OutputVDeviceBaseStream()
{
    // We want to stop the vdma channel before closing the stream in the firmware
    // because sending data to a closed stream may terminate the dma engine
    if (m_is_stream_activated) {
        (void)deactivate_stream();
    }
}

hailo_status OutputVDeviceBaseStream::activate_stream(uint16_t dynamic_batch_size, bool resume_pending_stream_transfers)
{
    for (auto &stream : m_streams) {
        auto status = stream.get().activate_stream(dynamic_batch_size, resume_pending_stream_transfers);
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to activate output stream. (device: {})", stream.get().get_dev_id());
            deactivate_stream();
            return status;
        }
    }
    m_is_stream_activated = true;
    return HAILO_SUCCESS;
}

hailo_status OutputVDeviceBaseStream::read_all(MemoryView &/*buffer*/)
{
    LOGGER__ERROR("read_all should not be called in vdevice flow");
    return HAILO_INTERNAL_FAILURE;
}

Expected<size_t> OutputVDeviceBaseStream::sync_read_raw_buffer(MemoryView &/*buffer*/)
{
    LOGGER__ERROR("sync_read_raw_buffer should not be called in vdevice flow");
    return make_unexpected(HAILO_INTERNAL_FAILURE);
}

hailo_status ScheduledOutputStream::read(MemoryView buffer)
{
    return read_impl(buffer, m_core_op_handle);
}

hailo_status OutputVDeviceNativeStream::read(MemoryView buffer)
{
    auto status = m_streams[m_next_transfer_stream_index].get().read(buffer);
    if (HAILO_SUCCESS != status) {
        LOGGER__INFO("Read from stream has failed! status = {}", status);
        return status;
    }

    // Update m_next_transfer_stream_index only if 'batch' frames has been transferred
    if (0 == (++m_acc_frames % m_streams[0].get().get_dynamic_batch_size())) {
        m_next_transfer_stream_index = static_cast<uint32_t>((m_next_transfer_stream_index + 1) % m_streams.size());
        m_acc_frames = 0;
    }

    return HAILO_SUCCESS;
}

hailo_status ScheduledOutputStream::read_impl(MemoryView buffer, scheduler_core_op_handle_t core_op_handle)
{
    auto core_ops_scheduler = m_core_ops_scheduler.lock();
    CHECK(core_ops_scheduler, HAILO_INTERNAL_FAILURE);

    auto device_id = core_ops_scheduler->wait_for_read(core_op_handle, name(), get_timeout());
    if (HAILO_STREAM_ABORTED_BY_USER == device_id.status()) {
        LOGGER__INFO("Read from stream was aborted.");
        return device_id.status();
    }
    CHECK_EXPECTED_AS_STATUS(device_id);

    TRACE(ReadFrameTrace, "", core_op_handle, m_stream_info.name);
    auto status = m_streams[device_id.value()].get().read(buffer);
    if (HAILO_SUCCESS != status) {
        LOGGER__INFO("Read from stream has failed! status = {}", status);
        return status;
    }

    status = core_ops_scheduler->signal_read_finish(core_op_handle, name(), device_id.value());
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        return status;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

Expected<std::unique_ptr<OutputVDeviceBaseStream>> OutputVDeviceBaseStream::create(std::vector<std::reference_wrapper<VdmaOutputStream>> &&low_level_streams,
    const LayerInfo &edge_layer, const scheduler_core_op_handle_t &core_op_handle, EventPtr core_op_activated_event,
    CoreOpsSchedulerWeakPtr core_ops_scheduler)
{
    assert(0 < low_level_streams.size());
    auto status = HAILO_UNINITIALIZED;
    
    std::unique_ptr<OutputVDeviceBaseStream> local_vdevice_stream;
    if (core_ops_scheduler.lock()) {
        local_vdevice_stream = make_unique_nothrow<ScheduledOutputStream>(std::move(low_level_streams), core_op_handle,
            edge_layer, std::move(core_op_activated_event), core_ops_scheduler, status);
    } else {
        local_vdevice_stream = make_unique_nothrow<OutputVDeviceNativeStream>(std::move(low_level_streams), edge_layer,
            std::move(core_op_activated_event), status);
    }

    CHECK_AS_EXPECTED((nullptr != local_vdevice_stream), HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return local_vdevice_stream;
}

hailo_status OutputVDeviceBaseStream::set_timeout(std::chrono::milliseconds timeout)
{
    for (auto &stream : m_streams) {
        auto status = stream.get().set_timeout(timeout);
        CHECK_SUCCESS(status, "Failed to set timeout to output stream. (device: {})", stream.get().get_dev_id());
    }
    return HAILO_SUCCESS;
}

std::chrono::milliseconds OutputVDeviceBaseStream::get_timeout() const
{
    // All timeout values of m_streams should be the same
    return m_streams[0].get().get_timeout();
}

hailo_stream_interface_t OutputVDeviceBaseStream::get_interface() const
{
    // All interface values of m_streams should be the same
    return m_streams[0].get().get_interface();
}

hailo_status ScheduledOutputStream::abort()
{
    return abort_impl(m_core_op_handle);
}

hailo_status OutputVDeviceNativeStream::abort()
{
    auto status = HAILO_SUCCESS; // Best effort
    for (auto &stream : m_streams) {
        auto abort_status = stream.get().abort();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to abort output stream. (status: {} device: {})", status, stream.get().get_dev_id());
            status = abort_status;
        }
    }

    return status;
}

hailo_status ScheduledOutputStream::abort_impl(scheduler_core_op_handle_t core_op_handle)
{
    auto status = HAILO_SUCCESS; // Best effort
    for (auto& stream : m_streams) {
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

hailo_status ScheduledOutputStream::clear_abort()
{
    return clear_abort_impl(m_core_op_handle);
}

hailo_status OutputVDeviceNativeStream::clear_abort()
{
    auto status = HAILO_SUCCESS; // Best effort
    for (auto &stream : m_streams) {
        auto clear_abort_status = stream.get().clear_abort();
        if ((HAILO_SUCCESS != clear_abort_status) && (HAILO_STREAM_NOT_ACTIVATED != clear_abort_status)) {
            LOGGER__ERROR("Failed to clear abort output stream. (status: {} device: {})", clear_abort_status, stream.get().get_dev_id());
            status = clear_abort_status;
        }
    }

    return status;
}

hailo_status ScheduledOutputStream::clear_abort_impl(scheduler_core_op_handle_t core_op_handle)
{
    auto status = HAILO_SUCCESS; // Best effort
    for (auto& stream : m_streams) {
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

Expected<size_t> OutputVDeviceBaseStream::get_buffer_frames_size() const
{
    size_t total_buffers_size = 0;
    for (auto &stream : m_streams) {
        auto stream_buffer_size = stream.get().get_buffer_frames_size();
        if (HAILO_NOT_AVAILABLE == stream_buffer_size.status()) {
            return make_unexpected(HAILO_NOT_AVAILABLE);
        }
        CHECK_EXPECTED(stream_buffer_size);
        total_buffers_size += stream_buffer_size.value();
    }

    return total_buffers_size;
}

Expected<size_t> OutputVDeviceBaseStream::get_pending_frames_count() const
{
    size_t total_pending_frames_count = 0;
    for (auto &stream : m_streams) {
        auto stream_pending_frames_count = stream.get().get_pending_frames_count();
        if (HAILO_NOT_AVAILABLE == stream_pending_frames_count.status()) {
            return make_unexpected(HAILO_NOT_AVAILABLE);
        }
        CHECK_EXPECTED(stream_pending_frames_count);
        total_pending_frames_count += stream_pending_frames_count.value();
    }

    return total_pending_frames_count;
}

} /* namespace hailort */
