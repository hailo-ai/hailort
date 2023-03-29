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

    status = HAILO_SUCCESS;
}

Expected<size_t> VdmaInputStream::sync_write_raw_buffer(const MemoryView &buffer)
{
    hailo_status status = HAILO_UNINITIALIZED;

    status = m_channel->wait(buffer.size(), m_channel_timeout);
    if ((status == HAILO_STREAM_ABORTED_BY_USER) || (status == HAILO_STREAM_NOT_ACTIVATED)) {
        return make_unexpected(status);
    }
    CHECK_AS_EXPECTED(HAILO_TIMEOUT != status, HAILO_TIMEOUT,
        "{} (H2D) failed with status={} (timeout={}ms)", name(), HAILO_TIMEOUT, m_channel_timeout.count());
    CHECK_SUCCESS_AS_EXPECTED(status);

    status = m_channel->transfer((void*)buffer.data(), buffer.size());
    if ((status == HAILO_STREAM_ABORTED_BY_USER) || (status == HAILO_STREAM_NOT_ACTIVATED)) {
        return make_unexpected(status);
    }
    CHECK_AS_EXPECTED(HAILO_TIMEOUT != status, HAILO_TIMEOUT,
        "{} (H2D) failed with status={} (timeout={}ms)", name(), HAILO_TIMEOUT, m_channel_timeout.count());
    CHECK_SUCCESS_AS_EXPECTED(status);

    return buffer.size();
}

hailo_status VdmaInputStream::write_buffer_only(const MemoryView &buffer,
    const std::function<bool()> &should_cancel)
{
    std::unique_lock<std::mutex> lock(m_write_only_mutex);
    return m_channel->write_buffer(buffer, m_channel_timeout, should_cancel);
}

hailo_status VdmaInputStream::send_pending_buffer(size_t device_index)
{
    std::unique_lock<std::mutex> lock(m_send_pending_mutex);
    CHECK(0 == device_index, HAILO_INVALID_OPERATION);
    hailo_status status = m_channel->wait(get_frame_size(), m_channel_timeout);
    if ((HAILO_STREAM_ABORTED_BY_USER == status) || (HAILO_STREAM_NOT_ACTIVATED == status)) {
        return status;
    }
    CHECK(HAILO_TIMEOUT != status, HAILO_TIMEOUT,
        "{} (H2D) failed with status={} (timeout={}ms)", name(), HAILO_TIMEOUT, m_channel_timeout.count());
    CHECK_SUCCESS(status);

    return m_channel->send_pending_buffer();
}

hailo_status VdmaInputStream::sync_write_all_raw_buffer_no_transform_impl(void *buffer, size_t offset, size_t size)
{
    ASSERT(NULL != buffer);

    return sync_write_raw_buffer(MemoryView(static_cast<uint8_t*>(buffer) + offset, size)).status();
}

/** Output stream **/

VdmaOutputStream::VdmaOutputStream(VdmaDevice &device, vdma::BoundaryChannelPtr channel, const LayerInfo &edge_layer,
                                   EventPtr core_op_activated_event, uint16_t batch_size,
                                   std::chrono::milliseconds transfer_timeout, hailo_stream_interface_t interface,
                                   hailo_status &status) :
    VdmaOutputStreamBase(device, channel, edge_layer, core_op_activated_event, batch_size, transfer_timeout, interface, status),
    m_read_mutex()
{
    // Check status for base class c'tor
    if (HAILO_SUCCESS != status) {
        return;
    }

    status = HAILO_SUCCESS;
}

Expected<size_t> VdmaOutputStream::sync_read_raw_buffer(MemoryView &buffer)
{
    hailo_status status = HAILO_UNINITIALIZED;

    status = m_channel->wait(buffer.size(), m_transfer_timeout);
    if ((status == HAILO_STREAM_ABORTED_BY_USER) || (status == HAILO_STREAM_NOT_ACTIVATED)) {
        return make_unexpected(status);
    }
    CHECK_AS_EXPECTED(HAILO_TIMEOUT != status, HAILO_TIMEOUT,
        "{} (D2H) failed with status={} (timeout={}ms)", name(), HAILO_TIMEOUT, m_transfer_timeout.count());
    CHECK_SUCCESS_AS_EXPECTED(status);

    status = m_channel->transfer(buffer.data(), buffer.size());
    if ((status == HAILO_STREAM_NOT_ACTIVATED) || (status == HAILO_STREAM_ABORTED_BY_USER)) {
        return make_unexpected(status);
    }
    CHECK_AS_EXPECTED(HAILO_TIMEOUT != status, HAILO_TIMEOUT,
        "{} (D2H) failed with status={} (timeout={}ms)", name(), HAILO_TIMEOUT, m_transfer_timeout.count());
    CHECK_SUCCESS_AS_EXPECTED(status);

    return buffer.size();
}

hailo_status VdmaOutputStream::read_all(MemoryView &buffer)
{
    std::unique_lock<std::mutex> lock(m_read_mutex);
    CHECK((buffer.size() % HailoRTCommon::HW_DATA_ALIGNMENT) == 0, HAILO_INVALID_ARGUMENT, 
        "Size must be aligned to {} (got {})", HailoRTCommon::HW_DATA_ALIGNMENT, buffer.size());

    return sync_read_raw_buffer(buffer).status();
}

} /* namespace hailort */
