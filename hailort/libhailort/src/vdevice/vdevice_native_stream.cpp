/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdevice_native_stream.cpp
 * @brief Internal stream implementation for native streams
 *
 **/

#include "vdevice_native_stream.hpp"

namespace hailort {

/** Input stream **/
hailo_status VDeviceNativeInputStreamBase::abort()
{
    auto status = HAILO_SUCCESS; // Best effort
    for (auto &pair: m_streams){
        auto &stream = pair.second;
        auto abort_status = stream.get().abort();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to abort input stream. (status: {} device: {})", status, stream.get().get_dev_id());
            status = abort_status;
        }
    }
    return status;
}

hailo_status VDeviceNativeInputStreamBase::clear_abort()
{
    auto status = HAILO_SUCCESS; // Best effort
    for (auto &pair: m_streams){
        auto &stream = pair.second;
        auto clear_abort_status = stream.get().clear_abort();
        if ((HAILO_SUCCESS != clear_abort_status) && (HAILO_STREAM_NOT_ACTIVATED != clear_abort_status)) {
            LOGGER__ERROR("Failed to clear abort input stream. (status: {} device: {})", clear_abort_status, stream.get().get_dev_id());
            status = clear_abort_status;
        }
    }

    return status;
}

Expected<std::unique_ptr<VDeviceNativeInputStream>> VDeviceNativeInputStream::create(
    std::map<device_id_t, std::reference_wrapper<VdmaInputStreamBase>> &&streams,
    EventPtr &&core_op_activated_event,
    const LayerInfo &layer_info)
{
    auto status = HAILO_UNINITIALIZED;
    auto stream = make_unique_nothrow<VDeviceNativeInputStream>(std::move(streams),
        std::move(core_op_activated_event), layer_info, status);
    CHECK_AS_EXPECTED((nullptr != stream), HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return stream;
}

hailo_status VDeviceNativeInputStream::write_impl(const MemoryView &buffer, const std::function<bool()> &should_cancel)
{
    if (should_cancel()) {
        return HAILO_STREAM_ABORTED_BY_USER;
    }
    auto status = m_streams.at(m_next_transfer_stream).get().write_impl(buffer);
    if (HAILO_SUCCESS != status) {
        LOGGER__INFO("Write to stream has failed! status = {}", status);
        return status;
    }

    // Update m_next_transfer_stream only if 'batch' frames has been transferred
    if (0 == (++m_acc_frames % m_streams.begin()->second.get().get_dynamic_batch_size())) {
        auto it = m_streams.upper_bound(m_next_transfer_stream);
        if (m_streams.end() == it) {
            it = m_streams.begin();
        }
        m_next_transfer_stream = it->first;
        m_acc_frames = 0;
    }
    return HAILO_SUCCESS;
}

Expected<std::unique_ptr<VDeviceNativeAsyncInputStream>> VDeviceNativeAsyncInputStream::create(
    std::map<device_id_t, std::reference_wrapper<VdmaInputStreamBase>> &&streams,
    EventPtr &&core_op_activated_event,
    const LayerInfo &layer_info)
{
    auto max_queue_size_per_stream = streams.begin()->second.get().get_buffer_frames_size();
    CHECK_EXPECTED(max_queue_size_per_stream);
    const auto max_queue_size = max_queue_size_per_stream.value() * streams.size();

    auto status = HAILO_UNINITIALIZED;
    auto stream = make_unique_nothrow<VDeviceNativeAsyncInputStream>(std::move(streams),
        std::move(core_op_activated_event), layer_info, max_queue_size, status);
    CHECK_AS_EXPECTED((nullptr != stream), HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return stream;
}

hailo_status VDeviceNativeAsyncInputStream::wait_for_async_ready(size_t transfer_size, std::chrono::milliseconds timeout)
{
    return m_streams.at(m_next_transfer_stream).get().wait_for_async_ready(transfer_size, timeout);
}

Expected<size_t> VDeviceNativeAsyncInputStream::get_async_max_queue_size() const
{
    return Expected<size_t>(m_max_queue_size);
}

hailo_status VDeviceNativeAsyncInputStream::write_async(TransferRequest &&transfer_request)
{
    // TODO HRT-10583 - allow option to remove reorder queue
    transfer_request.callback = m_callback_reorder_queue.wrap_callback(transfer_request.callback);

    auto status = m_streams.at(m_next_transfer_stream).get().write_async(std::move(transfer_request));
    if (HAILO_SUCCESS != status) {
        m_callback_reorder_queue.cancel_last_callback();
        return status;
    }

    // Update m_next_transfer_stream_index only if 'batch' frames has been transferred
    if (0 == (++m_acc_frames % m_streams.begin()->second.get().get_dynamic_batch_size())) {
        auto it = m_streams.upper_bound(m_next_transfer_stream);
        if (m_streams.end() == it) {
            it = m_streams.begin();
        }
        m_next_transfer_stream = it->first;
        m_acc_frames = 0;
    }
    return HAILO_SUCCESS;
}

hailo_status VDeviceNativeAsyncInputStream::write_impl(const MemoryView &, const std::function<bool()> &)
{
    LOGGER__ERROR("Sync write is not supported by async streams");
    return HAILO_INVALID_OPERATION;
}

/** Output stream **/
hailo_status VDeviceNativeOutputStreamBase::abort()
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

    return status;
}

hailo_status VDeviceNativeOutputStreamBase::clear_abort()
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

    return status;
}

Expected<std::unique_ptr<VDeviceNativeOutputStream>> VDeviceNativeOutputStream::create(
    std::map<device_id_t, std::reference_wrapper<VdmaOutputStreamBase>> &&streams,
    EventPtr &&core_op_activated_event,
    const LayerInfo &layer_info)
{
    auto status = HAILO_UNINITIALIZED;
    auto stream = make_unique_nothrow<VDeviceNativeOutputStream>(std::move(streams),
        std::move(core_op_activated_event), layer_info, status);
    CHECK_AS_EXPECTED((nullptr != stream), HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return stream;
}

hailo_status VDeviceNativeOutputStream::read(MemoryView buffer)
{
    auto status = m_streams.at(m_next_transfer_stream).get().read(buffer);
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
      // In case of aborted by user, don't show it as infer error
      LOGGER__INFO("Stream aborted by user (device: {})", m_streams.at(m_next_transfer_stream).get().get_dev_id());
      return status;
    }
    CHECK_SUCCESS(status, "Read from stream has failed! status = {}", status);

    // Update m_next_transfer_stream_index only if 'batch' frames has been transferred
    if (0 == (++m_acc_frames % m_streams.begin()->second.get().get_dynamic_batch_size())) {
        auto it = m_streams.upper_bound(m_next_transfer_stream);
        if (m_streams.end() == it) {
            it = m_streams.begin();
        }
        m_next_transfer_stream = it->first;
        m_acc_frames = 0;
    }

    return HAILO_SUCCESS;
}

Expected<std::unique_ptr<VDeviceNativeAsyncOutputStream>> VDeviceNativeAsyncOutputStream::create(
    std::map<device_id_t, std::reference_wrapper<VdmaOutputStreamBase>> &&streams,
    EventPtr &&core_op_activated_event,
    const LayerInfo &layer_info)
{
    auto max_queue_size_per_stream = streams.begin()->second.get().get_buffer_frames_size();
    CHECK_EXPECTED(max_queue_size_per_stream);
    const auto max_queue_size = max_queue_size_per_stream.value() * streams.size();

    auto status = HAILO_UNINITIALIZED;
    auto stream = make_unique_nothrow<VDeviceNativeAsyncOutputStream>(std::move(streams),
        std::move(core_op_activated_event), layer_info, max_queue_size, status);
    CHECK_AS_EXPECTED((nullptr != stream), HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return stream;
}

hailo_status VDeviceNativeAsyncOutputStream::wait_for_async_ready(size_t transfer_size, std::chrono::milliseconds timeout)
{
    return m_streams.at(m_next_transfer_stream).get().wait_for_async_ready(transfer_size, timeout);
}

Expected<size_t> VDeviceNativeAsyncOutputStream::get_async_max_queue_size() const
{
    return Expected<size_t>(m_max_queue_size);
}

hailo_status VDeviceNativeAsyncOutputStream::read_async(TransferRequest &&transfer_request)
{
    // TODO HRT-10583 - allow option to remove reorder queue
    transfer_request.callback = m_callback_reorder_queue.wrap_callback(transfer_request.callback);
    auto status = m_streams.at(m_next_transfer_stream).get().read_async(std::move(transfer_request));
    if (HAILO_SUCCESS != status) {
        m_callback_reorder_queue.cancel_last_callback();
        return status;
    }
    // Update m_next_transfer_stream_index only if 'batch' frames has been transferred
    if (0 == (++m_acc_frames % m_streams.begin()->second.get().get_dynamic_batch_size())) {
        auto it = m_streams.upper_bound(m_next_transfer_stream);
        if (m_streams.end() == it) {
            it = m_streams.begin();
        }
        m_next_transfer_stream = it->first;
        m_acc_frames = 0;
    }

    return HAILO_SUCCESS;
}

hailo_status VDeviceNativeAsyncOutputStream::read(MemoryView)
{
    LOGGER__ERROR("The read function is not supported by async streams");
    return HAILO_INVALID_OPERATION;
}

} /* namespace hailort */