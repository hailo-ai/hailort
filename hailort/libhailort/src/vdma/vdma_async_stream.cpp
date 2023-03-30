/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_async_stream.cpp
 * @brief Async vdma stream implementation
 **/

#include "hailo/hailort_common.hpp"

#include "vdma/vdma_async_stream.hpp"


namespace hailort
{

VdmaAsyncInputStream::VdmaAsyncInputStream(VdmaDevice &device, vdma::BoundaryChannelPtr channel,
                                           const LayerInfo &edge_layer, EventPtr core_op_activated_event,
                                           uint16_t batch_size, std::chrono::milliseconds transfer_timeout,
                                           hailo_stream_interface_t stream_interface, hailo_status &status) :
    VdmaInputStreamBase(device, channel, edge_layer, core_op_activated_event, batch_size,
                        transfer_timeout, stream_interface, status)
{
    // Checking status for base class c'tor
    if (HAILO_SUCCESS != status) {
        return;
    }

    status = HAILO_SUCCESS;
}

Expected<size_t> VdmaAsyncInputStream::sync_write_raw_buffer(const MemoryView &)
{
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

hailo_status VdmaAsyncInputStream::sync_write_all_raw_buffer_no_transform_impl(void *, size_t, size_t)
{
    return HAILO_NOT_IMPLEMENTED;
}

hailo_status VdmaAsyncInputStream::wait_for_ready(size_t transfer_size, std::chrono::milliseconds timeout)
{
    return m_channel->wait(transfer_size, timeout);
}

hailo_status VdmaAsyncInputStream::write_async(std::shared_ptr<DmaMappedBuffer> buffer, const TransferDoneCallback &user_callback, void *opaque)
{
    return m_channel->transfer(buffer, user_callback, opaque);
}

/** Output stream **/

VdmaAsyncOutputStream::VdmaAsyncOutputStream(VdmaDevice &device, vdma::BoundaryChannelPtr channel, const LayerInfo &edge_layer,
                                             EventPtr core_op_activated_event, uint16_t batch_size,
                                             std::chrono::milliseconds transfer_timeout, hailo_stream_interface_t interface,
                                             hailo_status &status) :
    VdmaOutputStreamBase(device, channel, edge_layer, core_op_activated_event, batch_size,
                         transfer_timeout, interface, status)
{
    // Check status for base class c'tor
    if (HAILO_SUCCESS != status) {
        return;
    }

    status = HAILO_SUCCESS;
}

Expected<size_t> VdmaAsyncOutputStream::sync_read_raw_buffer(MemoryView &)
{
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

hailo_status VdmaAsyncOutputStream::read_all(MemoryView &)
{
    return HAILO_NOT_IMPLEMENTED;
}

hailo_status VdmaAsyncOutputStream::wait_for_ready(size_t transfer_size, std::chrono::milliseconds timeout)
{
    return m_channel->wait(transfer_size, timeout);
}

hailo_status VdmaAsyncOutputStream::read_async(std::shared_ptr<DmaMappedBuffer> buffer, const TransferDoneCallback &user_callback, void *opaque)
{
    return m_channel->transfer(buffer, user_callback, opaque);
}

} /* namespace hailort */
