/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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
Expected<std::unique_ptr<VDeviceNativeInputStream>> VDeviceNativeInputStream::create(
    std::map<device_id_t, std::reference_wrapper<InputStreamBase>> &&streams,
    EventPtr core_op_activated_event,
    const LayerInfo &layer_info,
    uint16_t batch_size,
    vdevice_core_op_handle_t core_op_handle)
{
    std::unique_ptr<CallbackReorderQueue> reorder_queue = nullptr;
    // Ifaces of all streams should be the same
    auto iface = streams.begin()->second.get().get_interface();
    if ((iface != HAILO_STREAM_INTERFACE_ETH) && (iface != HAILO_STREAM_INTERFACE_MIPI)) {
        auto max_queue_size_per_stream = streams.begin()->second.get().get_async_max_queue_size();
        CHECK_EXPECTED(max_queue_size_per_stream);
        const auto max_queue_size = max_queue_size_per_stream.value() * streams.size();
        reorder_queue = make_unique_nothrow<CallbackReorderQueue>(max_queue_size);
        CHECK_NOT_NULL_AS_EXPECTED(reorder_queue, HAILO_OUT_OF_HOST_MEMORY);
    }

    auto status = HAILO_UNINITIALIZED;
    auto stream = make_unique_nothrow<VDeviceNativeInputStream>(std::move(streams),
        std::move(core_op_activated_event), layer_info, batch_size, core_op_handle, std::move(reorder_queue), status);
    CHECK_AS_EXPECTED((nullptr != stream), HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return stream;
}

hailo_status VDeviceNativeInputStream::set_buffer_mode(StreamBufferMode buffer_mode)
{
    // The buffer is not owned by this class so we just forward the mode to base streams
    for (const auto &pair : m_streams) {
        auto &stream = pair.second.get();
        auto status = stream.set_buffer_mode(buffer_mode);
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status VDeviceNativeInputStream::activate_stream()
{
    // m_streams should be activate when the specific core op is activated.
    return HAILO_SUCCESS;
}

hailo_status VDeviceNativeInputStream::deactivate_stream()
{
    // m_streams should be deactivated when the specific core op is activated.
    return HAILO_SUCCESS;
}

hailo_status VDeviceNativeInputStream::abort_impl()
{
    auto status = HAILO_SUCCESS; // Best effort
    for (auto &pair: m_streams){
        const auto &device_id = pair.first;
        auto &stream = pair.second;
        auto abort_status = stream.get().abort_impl();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to abort input stream. (status: {} device: {})", status, device_id);
            status = abort_status;
        }
    }
    return status;
}

hailo_status VDeviceNativeInputStream::clear_abort_impl()
{
    auto status = HAILO_SUCCESS; // Best effort
    for (auto &pair: m_streams){
        const auto &device_id = pair.first;
        auto &stream = pair.second;
        auto clear_abort_status = stream.get().clear_abort_impl();
        if ((HAILO_SUCCESS != clear_abort_status) && (HAILO_STREAM_NOT_ACTIVATED != clear_abort_status)) {
            LOGGER__ERROR("Failed to clear abort input stream. (status: {} device: {})", clear_abort_status, device_id);
            status = clear_abort_status;
        }
    }

    return status;
}

std::chrono::milliseconds VDeviceNativeInputStream::get_timeout() const
{
    // All timeout values of m_streams should be the same
    return m_streams.begin()->second.get().get_timeout();
}

hailo_status VDeviceNativeInputStream::set_timeout(std::chrono::milliseconds timeout)
{
    for (const auto &pair : m_streams) {
        const auto &device_id = pair.first;
        auto &stream = pair.second;
        auto status = stream.get().set_timeout(timeout);
        CHECK_SUCCESS(status, "Failed to set timeout to input stream. (device: {})", device_id);
    }
    return HAILO_SUCCESS;
}

hailo_stream_interface_t VDeviceNativeInputStream::get_interface() const
{
    // All interface values of m_streams should be the same
    return m_streams.begin()->second.get().get_interface();
}

hailo_status VDeviceNativeInputStream::flush()
{
    auto status = HAILO_SUCCESS; // Best effort
    for (const auto &pair : m_streams) {
        const auto &device_id = pair.first;
        auto &stream = pair.second;
        auto flush_status = stream.get().flush();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to flush input stream. (status: {} device: {})", status, device_id);
            status = flush_status;
        }
    }
    return status;
}

hailo_status VDeviceNativeInputStream::write_impl(const MemoryView &buffer)
{
    TRACE(FrameEnqueueH2DTrace, m_core_op_handle, name());

    auto status = next_stream().write_impl(buffer);
    if ((HAILO_STREAM_ABORT == status) || (HAILO_STREAM_NOT_ACTIVATED == status)){
        LOGGER__INFO("Failed write to stream {} (device: {}) with status={}", name(), m_next_transfer_stream, status);
        return status;
    }
    CHECK_SUCCESS(status, "Failed write to stream (device: {})", m_next_transfer_stream);

    advance_stream();
    return HAILO_SUCCESS;
}

hailo_status VDeviceNativeInputStream::wait_for_async_ready(size_t transfer_size, std::chrono::milliseconds timeout)
{
    return m_streams.at(m_next_transfer_stream).get().wait_for_async_ready(transfer_size, timeout);
}

Expected<size_t> VDeviceNativeInputStream::get_async_max_queue_size() const
{
    // The actual max queue size can't be just max_queue_per_stream * m_streams.size(),
    // since we transfer an entire batch for each device at a time (so even if we have place
    // to transfer in other streams, we first finishes the batch).
    // To overcome this problem, we check how many "batches" we can transfer at a time (batch_count_queued)
    // and make sure the queue for each stream contains a specific batch. We can potentially transfer
    // the residue of the batch from last device, but then we will have problems with non-batch aligned
    // transfers.
    auto &first_stream = m_streams.begin()->second.get();
    const auto max_queue_per_stream = first_stream.get_async_max_queue_size();
    if (!max_queue_per_stream) {
        return make_unexpected(max_queue_per_stream.status()); // Not all streams has max_queue_size (e.g. eth) , so its not necessarily an error
    }

    if (*max_queue_per_stream >= m_batch_size) {
        const auto batch_count_queued = *max_queue_per_stream / m_batch_size;
        const auto actual_queue_per_stream = m_batch_size * batch_count_queued;
        return actual_queue_per_stream * m_streams.size();
    } else {
        size_t max_queue_size = 0;
        for (size_t i = 1; i <= *max_queue_per_stream; i++) {
            if ((m_batch_size % i) == 0) {
                max_queue_size = i;
            }
        }
        return max_queue_size * m_streams.size();
    }
}

hailo_status VDeviceNativeInputStream::write_async(TransferRequest &&transfer_request)
{
    // TODO HRT-10583 - allow option to remove reorder queue
    CHECK(m_callback_reorder_queue, HAILO_INVALID_OPERATION, "Stream does not support async api");
    transfer_request.callback = m_callback_reorder_queue->wrap_callback(transfer_request.callback);

    TRACE(FrameEnqueueH2DTrace, m_core_op_handle, name());

    auto status = next_stream().write_async(std::move(transfer_request));
    if (HAILO_SUCCESS != status) {
        m_callback_reorder_queue->cancel_last_callback();
        return status;
    }

    advance_stream();
    return HAILO_SUCCESS;
}

InputStreamBase &VDeviceNativeInputStream::next_stream()
{
    return m_streams.at(m_next_transfer_stream).get();
}

void VDeviceNativeInputStream::advance_stream()
{
    if (0 == (++m_acc_frames % m_batch_size)) {
        auto it = m_streams.upper_bound(m_next_transfer_stream);
        if (m_streams.end() == it) {
            it = m_streams.begin();
        }
        m_next_transfer_stream = it->first;
        m_acc_frames = 0;
    }
}

/** Output stream **/
Expected<std::unique_ptr<VDeviceNativeOutputStream>> VDeviceNativeOutputStream::create(
    std::map<device_id_t, std::reference_wrapper<OutputStreamBase>> &&streams,
    EventPtr core_op_activated_event,
    const LayerInfo &layer_info,
    uint16_t batch_size,
    vdevice_core_op_handle_t core_op_handle)
{
    std::unique_ptr<CallbackReorderQueue> reorder_queue = nullptr;
    // Ifaces of all streams should be the same
    auto iface = streams.begin()->second.get().get_interface();
    if ((iface != HAILO_STREAM_INTERFACE_ETH) && (iface != HAILO_STREAM_INTERFACE_MIPI)) {
        auto max_queue_size_per_stream = streams.begin()->second.get().get_async_max_queue_size();
        CHECK_EXPECTED(max_queue_size_per_stream);
        const auto max_queue_size = max_queue_size_per_stream.value() * streams.size();
        reorder_queue = make_unique_nothrow<CallbackReorderQueue>(max_queue_size);
        CHECK_NOT_NULL_AS_EXPECTED(reorder_queue, HAILO_OUT_OF_HOST_MEMORY);
    }

    auto status = HAILO_UNINITIALIZED;
    auto stream = make_unique_nothrow<VDeviceNativeOutputStream>(std::move(streams),
        std::move(core_op_activated_event), layer_info, batch_size, core_op_handle, std::move(reorder_queue), status);
    CHECK_AS_EXPECTED((nullptr != stream), HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return stream;
}

hailo_status VDeviceNativeOutputStream::set_buffer_mode(StreamBufferMode buffer_mode)
{
    // The buffer is not owned by this class so we just forward the mode to base streams
    for (const auto &pair : m_streams) {
        auto &stream = pair.second.get();
        auto status = stream.set_buffer_mode(buffer_mode);
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status VDeviceNativeOutputStream::activate_stream()
{
    // m_streams should be activate when the specific core op is activated.
    return HAILO_SUCCESS;
}

hailo_status VDeviceNativeOutputStream::deactivate_stream()
{
    // m_streams should be deactivated when the specific core op is activated.
    return HAILO_SUCCESS;
}

hailo_status VDeviceNativeOutputStream::abort_impl()
{
    auto status = HAILO_SUCCESS; // Best effort
    for (const auto &pair : m_streams) {
        const auto &device_id = pair.first;
        auto &stream = pair.second;
        auto abort_status = stream.get().abort_impl();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to abort output stream. (status: {} device: {})", status, device_id);
            status = abort_status;
        }
    }

    return status;
}

hailo_status VDeviceNativeOutputStream::clear_abort_impl()
{
    auto status = HAILO_SUCCESS; // Best effort
    for (const auto &pair : m_streams) {
        const auto &device_id = pair.first;
        auto &stream = pair.second;
        auto clear_abort_status = stream.get().clear_abort_impl();
        if ((HAILO_SUCCESS != clear_abort_status) && (HAILO_STREAM_NOT_ACTIVATED != clear_abort_status)) {
            LOGGER__ERROR("Failed to clear abort output stream. (status: {} device: {})", clear_abort_status, device_id);
            status = clear_abort_status;
        }
    }

    return status;
}

std::chrono::milliseconds VDeviceNativeOutputStream::get_timeout() const
{
    // All timeout values of m_streams should be the same
    return m_streams.begin()->second.get().get_timeout();
}

hailo_status VDeviceNativeOutputStream::set_timeout(std::chrono::milliseconds timeout)
{
    for (const auto &pair : m_streams) {
        const auto &device_id = pair.first;
        auto &stream = pair.second;
        auto status = stream.get().set_timeout(timeout);
        CHECK_SUCCESS(status, "Failed to set timeout to output stream. (device: {})", device_id);
    }
    return HAILO_SUCCESS;
}

hailo_stream_interface_t VDeviceNativeOutputStream::get_interface() const
{
    // All interface values of m_streams should be the same
    return m_streams.begin()->second.get().get_interface();
}

hailo_status VDeviceNativeOutputStream::read_impl(MemoryView buffer)
{
    auto status = next_stream().read_impl(buffer);
    if ((HAILO_STREAM_ABORT == status) || (HAILO_STREAM_NOT_ACTIVATED == status)){
      LOGGER__INFO("Failed read from stream {} (device: {})", status, m_next_transfer_stream);
      return status;
    }
    CHECK_SUCCESS(status, "Failed read from stream (device: {})", m_next_transfer_stream);

    if (INVALID_CORE_OP_HANDLE != m_core_op_handle) {
        TRACE(FrameDequeueD2HTrace, m_core_op_handle, name());
    }

    advance_stream();
    return HAILO_SUCCESS;
}

hailo_status VDeviceNativeOutputStream::wait_for_async_ready(size_t transfer_size, std::chrono::milliseconds timeout)
{
    return next_stream().wait_for_async_ready(transfer_size, timeout);
}

Expected<size_t> VDeviceNativeOutputStream::get_async_max_queue_size() const
{
    // The actual max queue size can't be just max_queue_per_stream * m_streams.size(),
    // since we transfer an entire batch for each device at a time (so even if we have place
    // to transfer in other streams, we first finishes the batch).
    // To overcome this problem, we check how many "batches" we can transfer at a time (batch_count_queued)
    // and make sure the queue for each stream contains a specific batch. We can potentaily transfer
    // the resuide of the batch from last device, but then we will have problems with non-batch aligned
    // transfers.
    auto &first_stream = m_streams.begin()->second.get();
    const auto max_queue_per_stream = first_stream.get_async_max_queue_size();
    if (!max_queue_per_stream) {
        return make_unexpected(max_queue_per_stream.status()); // Not all streams has max_queue_size (e.g. eth) , so its not necessarily an error
    }

    if (*max_queue_per_stream >= m_batch_size) {
        const auto batch_count_queued = *max_queue_per_stream / m_batch_size;
        const auto actual_queue_per_stream = m_batch_size * batch_count_queued;
        return actual_queue_per_stream * m_streams.size();
    } else {
        size_t max_queue_size = 0;
        for (size_t i = 1; i <= *max_queue_per_stream; i++) {
            if ((m_batch_size % i) == 0) {
                max_queue_size = i;
            }
        }
        return max_queue_size * m_streams.size();
    }
}

hailo_status VDeviceNativeOutputStream::read_async(TransferRequest &&transfer_request)
{
    // TODO HRT-10583 - allow option to remove reorder queue
    CHECK(m_callback_reorder_queue, HAILO_INVALID_OPERATION, "Stream does not support async api");


    auto reorder_queue_callback = m_callback_reorder_queue->wrap_callback(transfer_request.callback);

    transfer_request.callback = [this, callback=reorder_queue_callback](hailo_status status) {
        callback(status);
        if ((HAILO_SUCCESS == status) && (INVALID_CORE_OP_HANDLE != m_core_op_handle)) {
            TRACE(FrameDequeueD2HTrace, m_core_op_handle, name());
        }
    };

    auto status = next_stream().read_async(std::move(transfer_request));
    if (HAILO_SUCCESS != status) {
        m_callback_reorder_queue->cancel_last_callback();
        return status;
    }

    // Update m_next_transfer_stream_index only if 'batch' frames has been transferred
    advance_stream();
    return HAILO_SUCCESS;
}

hailo_status VDeviceNativeOutputStream::read_unaligned_address_async(const MemoryView &buffer,
    const TransferDoneCallback &user_callback)
{
    auto status = next_stream().read_unaligned_address_async(buffer, user_callback);
    CHECK_SUCCESS(status);
    advance_stream();
    return HAILO_SUCCESS;
}

OutputStreamBase &VDeviceNativeOutputStream::next_stream()
{
    return m_streams.at(m_next_transfer_stream).get();
}

void VDeviceNativeOutputStream::advance_stream()
{
    if (0 == (++m_acc_frames % m_batch_size)) {
        auto it = m_streams.upper_bound(m_next_transfer_stream);
        if (m_streams.end() == it) {
            it = m_streams.begin();
        }
        m_next_transfer_stream = it->first;
        m_acc_frames = 0;
    }
}

} /* namespace hailort */