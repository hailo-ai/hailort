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

#include "vdevice/vdevice_stream.hpp"
#include "vdevice/vdevice_native_stream.hpp"
#include "vdevice/scheduler/multi_device_scheduled_stream.hpp"
#include "vdevice/scheduler/scheduled_stream.hpp"
#include "core_op/resource_manager/resource_manager.hpp"

#include <new>


namespace hailort
{

/** Input stream **/
VDeviceInputStreamBase::~VDeviceInputStreamBase()
{
    // We want to stop the vdma channel before closing the stream in the firmware
    // because sending data to a closed stream may terminate the dma engine
    if (m_is_stream_activated) {
        (void)deactivate_stream();
    }
}

hailo_status VDeviceInputStreamBase::activate_stream(uint16_t dynamic_batch_size, bool resume_pending_stream_transfers)
{
    for (const auto &pair : m_streams) {
        auto &stream = pair.second;
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

hailo_status VDeviceInputStreamBase::deactivate_stream()
{
    auto status = HAILO_SUCCESS; // Best effort
    for (const auto &pair : m_streams) {
        auto &stream = pair.second;
        auto deactivate_status = stream.get().deactivate_stream();
        if (HAILO_SUCCESS != deactivate_status) {
            LOGGER__ERROR("Failed to deactivate input stream. (status: {} device: {})", deactivate_status, stream.get().get_dev_id());
            status = deactivate_status;
        }
    }
    m_is_stream_activated = false;
    return status;
}

hailo_status VDeviceInputStreamBase::send_pending_buffer(const device_id_t &device_id)
{
    assert(1 == m_streams.size());
    auto &vdma_input = dynamic_cast<VdmaInputStreamBase&>(m_streams.at(m_next_transfer_stream).get());
    return vdma_input.send_pending_buffer(device_id);
}

Expected<size_t> VDeviceInputStreamBase::get_buffer_frames_size() const
{
    return m_streams.begin()->second.get().get_buffer_frames_size();
}

Expected<size_t> VDeviceInputStreamBase::get_pending_frames_count() const
{
    size_t total_pending_frames_count = 0;
    for (const auto &pair : m_streams) {
        auto &stream = pair.second;
        auto stream_pending_frames_count = stream.get().get_pending_frames_count();
        CHECK_EXPECTED(stream_pending_frames_count);
        total_pending_frames_count += stream_pending_frames_count.value();
    }
    return total_pending_frames_count;
}

Expected<std::unique_ptr<VDeviceInputStreamBase>> VDeviceInputStreamBase::create(
    std::map<device_id_t, std::reference_wrapper<VdmaInputStreamBase>> &&low_level_streams,
    const hailo_stream_parameters_t &stream_params, const LayerInfo &edge_layer,
    const scheduler_core_op_handle_t &core_op_handle, EventPtr core_op_activated_event,
    CoreOpsSchedulerWeakPtr core_ops_scheduler)
{
    assert(0 < low_level_streams.size());

    if (core_ops_scheduler.lock()) {
        if ((stream_params.flags & HAILO_STREAM_FLAGS_ASYNC) != 0) {
            auto stream = ScheduledAsyncInputStream::create(std::move(low_level_streams),
                core_op_handle, std::move(core_op_activated_event), edge_layer,
                core_ops_scheduler);
            CHECK_EXPECTED(stream);
            return std::unique_ptr<VDeviceInputStreamBase>(stream.release());
        } else {
            if (1 < low_level_streams.size()) {
                auto stream = MultiDeviceScheduledInputStream::create(std::move(low_level_streams),
                    core_op_handle, std::move(core_op_activated_event), edge_layer,
                    core_ops_scheduler);
                CHECK_EXPECTED(stream);
                return std::unique_ptr<VDeviceInputStreamBase>(stream.release());
            } else {
                auto stream = ScheduledInputStream::create(std::move(low_level_streams),
                    core_op_handle, std::move(core_op_activated_event), edge_layer,
                    core_ops_scheduler);
                CHECK_EXPECTED(stream);
                return std::unique_ptr<VDeviceInputStreamBase>(stream.release());
            }
        }
    } else {
        if ((stream_params.flags & HAILO_STREAM_FLAGS_ASYNC) != 0) {
            auto stream = VDeviceNativeAsyncInputStream::create(std::move(low_level_streams),
                std::move(core_op_activated_event), edge_layer);
            CHECK_EXPECTED(stream);
            return std::unique_ptr<VDeviceInputStreamBase>(stream.release());
        } else {
            auto stream = VDeviceNativeInputStream::create(std::move(low_level_streams),
                std::move(core_op_activated_event), edge_layer);
            CHECK_EXPECTED(stream);
            return std::unique_ptr<VDeviceInputStreamBase>(stream.release());
        }

    }
}

hailo_status VDeviceInputStreamBase::set_timeout(std::chrono::milliseconds timeout)
{
    for (const auto &pair : m_streams) {
        auto &stream = pair.second;
        auto status = stream.get().set_timeout(timeout);
        CHECK_SUCCESS(status, "Failed to set timeout to input stream. (device: {})", stream.get().get_dev_id());
    }
    return HAILO_SUCCESS;
}

std::chrono::milliseconds VDeviceInputStreamBase::get_timeout() const
{
    // All timeout values of m_streams should be the same
    return m_streams.begin()->second.get().get_timeout();
}

hailo_stream_interface_t VDeviceInputStreamBase::get_interface() const
{
    // All interface values of m_streams should be the same
    return m_streams.begin()->second.get().get_interface();
}

hailo_status VDeviceInputStreamBase::flush()
{
    auto status = HAILO_SUCCESS; // Best effort
    for (const auto &pair : m_streams) {
        auto &stream = pair.second;
        auto flush_status = stream.get().flush();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to flush input stream. (status: {} device: {})", status, stream.get().get_dev_id());
            status = flush_status;
        }
    }
    return status;
}

hailo_status VDeviceInputStreamBase::write_impl(const MemoryView &buffer)
{
    return write_impl(buffer, []() { return false; });
}

/** Output stream **/
hailo_status VDeviceOutputStreamBase::deactivate_stream()
{
    auto status = HAILO_SUCCESS; // Best effort
    for (const auto &pair : m_streams) {
        auto &stream = pair.second;
        auto deactivate_status = stream.get().deactivate_stream();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to deactivate output stream. (status: {} device: {})", status, stream.get().get_dev_id());
            status = deactivate_status;
        }
    }
    m_is_stream_activated = false;
    return status;
}

VDeviceOutputStreamBase::~VDeviceOutputStreamBase()
{
    // We want to stop the vdma channel before closing the stream in the firmware
    // because sending data to a closed stream may terminate the dma engine
    if (m_is_stream_activated) {
        (void)deactivate_stream();
    }
}

hailo_status VDeviceOutputStreamBase::activate_stream(uint16_t dynamic_batch_size, bool resume_pending_stream_transfers)
{
    for (const auto &pair : m_streams) {
        auto &stream = pair.second;
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

hailo_status VDeviceOutputStreamBase::read_impl(MemoryView &/*buffer*/)
{
    LOGGER__ERROR("read_impl should not be called in vdevice flow");
    return HAILO_INTERNAL_FAILURE;
}

Expected<std::unique_ptr<VDeviceOutputStreamBase>> VDeviceOutputStreamBase::create(
    std::map<device_id_t, std::reference_wrapper<VdmaOutputStreamBase>> &&low_level_streams,
    const hailo_stream_parameters_t &stream_params, const LayerInfo &edge_layer,
    const scheduler_core_op_handle_t &core_op_handle, EventPtr core_op_activated_event,
    CoreOpsSchedulerWeakPtr core_ops_scheduler)
{
    assert(0 < low_level_streams.size());

    if (core_ops_scheduler.lock()) {
        if ((stream_params.flags & HAILO_STREAM_FLAGS_ASYNC) != 0) {
            LOGGER__ERROR("Async output streams are not supported with scheduler");
            return make_unexpected(HAILO_NOT_IMPLEMENTED);
        } else {
            auto stream = ScheduledOutputStream::create(std::move(low_level_streams), core_op_handle,
                edge_layer, std::move(core_op_activated_event), core_ops_scheduler);
            CHECK_EXPECTED(stream);
            return std::unique_ptr<VDeviceOutputStreamBase>(stream.release());
        }
    } else {
        if ((stream_params.flags & HAILO_STREAM_FLAGS_ASYNC) != 0) {
            auto stream = VDeviceNativeAsyncOutputStream::create(std::move(low_level_streams),
                std::move(core_op_activated_event), edge_layer);
            CHECK_EXPECTED(stream);
            return std::unique_ptr<VDeviceOutputStreamBase>(stream.release());
        } else {
            auto stream = VDeviceNativeOutputStream::create(std::move(low_level_streams),
                std::move(core_op_activated_event), edge_layer);
            CHECK_EXPECTED(stream);
            return std::unique_ptr<VDeviceOutputStreamBase>(stream.release());
        }
    }
}

hailo_status VDeviceOutputStreamBase::set_timeout(std::chrono::milliseconds timeout)
{
    for (const auto &pair : m_streams) {
        auto &stream = pair.second;
        auto status = stream.get().set_timeout(timeout);
        CHECK_SUCCESS(status, "Failed to set timeout to output stream. (device: {})", stream.get().get_dev_id());
    }
    return HAILO_SUCCESS;
}

std::chrono::milliseconds VDeviceOutputStreamBase::get_timeout() const
{
    // All timeout values of m_streams should be the same
    return m_streams.begin()->second.get().get_timeout();
}

hailo_stream_interface_t VDeviceOutputStreamBase::get_interface() const
{
    // All interface values of m_streams should be the same
    return m_streams.begin()->second.get().get_interface();
}

Expected<size_t> VDeviceOutputStreamBase::get_buffer_frames_size() const
{
    return m_streams.begin()->second.get().get_buffer_frames_size();
}

Expected<size_t> VDeviceOutputStreamBase::get_pending_frames_count() const
{
    size_t total_pending_frames_count = 0;
    for (const auto &pair : m_streams) {
        auto &stream = pair.second;
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
