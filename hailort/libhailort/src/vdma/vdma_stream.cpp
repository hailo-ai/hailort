/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_stream.cpp
 **/

#include "hailo/hailort_common.hpp"

#include "vdma/vdma_stream.hpp"


namespace hailort
{

VdmaInputStream::VdmaInputStream(VdmaDevice &device, vdma::BoundaryChannelPtr channel,
                                 const LayerInfo &edge_layer, EventPtr core_op_activated_event,
                                 uint16_t batch_size, std::chrono::milliseconds transfer_timeout,
                                 hailo_stream_interface_t stream_interface, hailo_status &status) :
    VdmaInputStreamBase(device, channel, edge_layer, core_op_activated_event, batch_size, transfer_timeout, stream_interface, status),
    m_write_only_mutex(),
    m_send_pending_mutex()
{
    // Checking status for base class c'tor
    if (HAILO_SUCCESS != status) {
        return;
    }

    if (channel->type() != vdma::BoundaryChannel::Type::BUFFERED) {
        LOGGER__ERROR("Can't create a vdma stream with a non buffered channel. Received channel type {}", channel->type());
        status = HAILO_INVALID_ARGUMENT;
        return;
    }

    status = HAILO_SUCCESS;
}

hailo_status VdmaInputStream::write_impl(const MemoryView &buffer)
{
    return m_channel->transfer_sync((void*)buffer.data(), buffer.size(), m_channel_timeout);
}

hailo_status VdmaInputStream::write_buffer_only(const MemoryView &buffer,
    const std::function<bool()> &should_cancel)
{
    std::unique_lock<std::mutex> lock(m_write_only_mutex);
    return m_channel->write_buffer(buffer, m_channel_timeout, should_cancel);
}

hailo_status VdmaInputStream::send_pending_buffer(const device_id_t &device_id)
{
    (void)device_id;
    std::unique_lock<std::mutex> lock(m_send_pending_mutex);
    hailo_status status = m_channel->wait(get_frame_size(), m_channel_timeout);
    if ((HAILO_STREAM_ABORTED_BY_USER == status) || (HAILO_STREAM_NOT_ACTIVATED == status)) {
        return status;
    }
    CHECK(HAILO_TIMEOUT != status, HAILO_TIMEOUT,
        "{} (H2D) failed with status={} (timeout={}ms)", name(), HAILO_TIMEOUT, m_channel_timeout.count());
    CHECK_SUCCESS(status);

    return m_channel->send_pending_buffer();
}

/** Output stream **/

VdmaOutputStream::VdmaOutputStream(VdmaDevice &device, vdma::BoundaryChannelPtr channel, const LayerInfo &edge_layer,
                                   EventPtr core_op_activated_event, uint16_t batch_size,
                                   std::chrono::milliseconds transfer_timeout, hailo_stream_interface_t interface,
                                   hailo_status &status) :
    VdmaOutputStreamBase(device, channel, edge_layer, core_op_activated_event, batch_size, transfer_timeout, interface, status)
{
    // Check status for base class c'tor
    if (HAILO_SUCCESS != status) {
        return;
    }

    if (channel->type() != vdma::BoundaryChannel::Type::BUFFERED) {
        LOGGER__ERROR("Can't create a vdma stream with a non buffered channel. Received channel type {}", channel->type());
        status = HAILO_INVALID_ARGUMENT;
        return;
    }

    status = HAILO_SUCCESS;
}

hailo_status VdmaOutputStream::read_impl(MemoryView &buffer)
{
    CHECK((buffer.size() % HailoRTCommon::HW_DATA_ALIGNMENT) == 0, HAILO_INVALID_ARGUMENT,
        "Size must be aligned to {} (got {})", HailoRTCommon::HW_DATA_ALIGNMENT, buffer.size());

    return m_channel->transfer_sync(buffer.data(), buffer.size(), m_transfer_timeout);
}

} /* namespace hailort */
