/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_stream.cpp
 **/

#include "vdma_stream.hpp"

namespace hailort
{

VdmaInputStream::VdmaInputStream(VdmaDevice &device, uint8_t channel_index, const LayerInfo &edge_layer,
                                 EventPtr network_group_activated_event, uint16_t batch_size,
                                 LatencyMeterPtr latency_meter, std::chrono::milliseconds transfer_timeout,
                                 hailo_stream_interface_t stream_interface, hailo_status &status) :
    InputStreamBase(edge_layer, stream_interface, std::move(network_group_activated_event), status),
    m_device(&device),
    m_channel_index(channel_index),
    is_stream_activated(false),
    m_channel_timeout(transfer_timeout),
    m_latency_meter(latency_meter),
    m_batch_size(batch_size)
{
    // Checking status for base class c'tor
    if (HAILO_SUCCESS != status) {
        return;
    }

    status = config_stream();
    if (HAILO_SUCCESS != status) {
        return;
    }

    status = HAILO_SUCCESS;
}

VdmaInputStream::~VdmaInputStream()
{
    auto status = HAILO_UNINITIALIZED;
    // We want to stop the vdma channel before closing the stream in the firmware
    // because sending data to a closed stream may terminate the dma engine
    if (this->is_stream_activated) {
        status = VdmaInputStream::deactivate_stream();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to deactivate stream with error status {}", status);
        }
    }
}

VdmaInputStream::VdmaInputStream(VdmaInputStream &&other) :
    InputStreamBase(std::move(other)),
    m_device(std::move(other.m_device)),
    m_channel_index(std::move(other.m_channel_index)),
    m_channel(std::move(other.m_channel)),
    is_stream_activated(std::exchange(other.is_stream_activated, false)),
    m_channel_timeout(std::move(other.m_channel_timeout)),
    m_latency_meter(std::move(other.m_latency_meter)),
    m_batch_size(other.m_batch_size)
{}

std::chrono::milliseconds VdmaInputStream::get_timeout() const
{
    return this->m_channel_timeout;
}

hailo_status VdmaInputStream::set_timeout(std::chrono::milliseconds timeout)
{
    this->m_channel_timeout = timeout;
    return HAILO_SUCCESS;
}

hailo_status VdmaInputStream::abort()
{
    return m_channel->abort();
}

hailo_status VdmaInputStream::clear_abort()
{
    return m_channel->clear_abort();
}

hailo_status VdmaInputStream::flush()
{
    return m_channel->flush((m_channel_timeout * m_batch_size));
}

hailo_status VdmaInputStream::activate_stream()
{
    auto status = m_channel->start_allocated_channel(0);
    CHECK_SUCCESS(status);

    this->is_stream_activated = true;
    return HAILO_SUCCESS;
}

hailo_status VdmaInputStream::deactivate_stream()
{
    if (!is_stream_activated) {
        return HAILO_SUCCESS;
    }

    /* Flush is best effort */
    auto status = m_channel->flush(VDMA_FLUSH_TIMEOUT);
    if (HAILO_STREAM_INTERNAL_ABORT == status) {
        LOGGER__INFO("Flush input_channel is not needed because channel was aborted. (channel {})", m_channel_index);
        status = HAILO_SUCCESS;
    } else if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to flush input_channel. (status {} channel {})", status, m_channel_index);
    }

    /* Close channel is best effort. */
    auto stop_channel_status = m_channel->stop_channel();
    if (HAILO_SUCCESS != stop_channel_status) {
        LOGGER__ERROR("Failed to stop channel with error status {}", stop_channel_status);
        status = (status == HAILO_SUCCESS) ? stop_channel_status : status;
    }

    this->is_stream_activated = false;
    return status;
}

Expected<size_t> VdmaInputStream::sync_write_raw_buffer(const MemoryView &buffer)
{
    hailo_status status = HAILO_UNINITIALIZED;

    status = m_channel->wait(buffer.size(), m_channel_timeout);
    if ((HAILO_STREAM_INTERNAL_ABORT == status) || (HAILO_STREAM_NOT_ACTIVATED == status)) {
        return make_unexpected(status);
    }
    CHECK_SUCCESS_AS_EXPECTED(status);

    status = m_channel->transfer((void*)buffer.data(), buffer.size());
    CHECK_SUCCESS_AS_EXPECTED(status);

    return buffer.size();
}

hailo_status VdmaInputStream::sync_write_all_raw_buffer_no_transform_impl(void *buffer, size_t offset, size_t size)
{
    ASSERT(NULL != buffer);

    return sync_write_raw_buffer(MemoryView(static_cast<uint8_t*>(buffer) + offset, size)).status();
}

hailo_status VdmaInputStream::config_stream()
{
    hailo_status status = HAILO_UNINITIALIZED;
    uint32_t descs_count = 0;
    uint16_t page_size = 0;
    uint32_t min_active_trans = MIN_ACTIVE_TRANSFERS_SCALE * m_batch_size;
    uint32_t max_active_trans = MAX_ACTIVE_TRANSFERS_SCALE * m_batch_size;
    assert(min_active_trans <= max_active_trans);

    CHECK(IS_FIT_IN_UINT16(min_active_trans), HAILO_INVALID_ARGUMENT, 
        "calculated min_active_trans for vdma descriptor list is out of UINT16 range");

    CHECK(IS_FIT_IN_UINT16(max_active_trans), HAILO_INVALID_ARGUMENT, 
        "calculated min_active_trans for vdma descriptor list is out of UINT16 range");

    auto desc_sizes_pair = VdmaDescriptorList::get_desc_buffer_sizes_for_single_transfer(m_device->get_driver(),
        static_cast<uint16_t>(min_active_trans), static_cast<uint16_t>(max_active_trans), m_stream_info.hw_frame_size);
    CHECK_EXPECTED_AS_STATUS(desc_sizes_pair);

    page_size = desc_sizes_pair->first;
    descs_count = desc_sizes_pair->second;

    auto channel = VdmaChannel::create(m_channel_index, VdmaChannel::Direction::H2D, m_device->get_driver(), page_size,
        m_stream_info.index, m_latency_meter, m_batch_size);

    m_channel = make_unique_nothrow<VdmaChannel>(channel.release());
    CHECK(nullptr != m_channel, HAILO_OUT_OF_HOST_MEMORY);

    status = m_channel->allocate_resources(descs_count);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

/** Output stream **/
VdmaOutputStream::VdmaOutputStream(VdmaDevice &device, uint8_t channel_index, const LayerInfo &edge_layer,
                                   EventPtr network_group_activated_event, uint16_t batch_size,
                                   LatencyMeterPtr latency_meter, std::chrono::milliseconds transfer_timeout,
                                   hailo_status &status) :
    OutputStreamBase(edge_layer, std::move(network_group_activated_event), status),
    m_device(&device),
    m_channel_index(channel_index),
    is_stream_activated(false),
    m_transfer_timeout(transfer_timeout),
    m_latency_meter(latency_meter),
    m_batch_size(batch_size),
    m_transfer_size(get_transfer_size(m_stream_info))
{
    // Check status for base class c'tor
    if (HAILO_SUCCESS != status) {
        return;
    }

    status = config_stream();
    if (HAILO_SUCCESS != status) {
        return;
    }

    status = HAILO_SUCCESS;
}

VdmaOutputStream::VdmaOutputStream(VdmaOutputStream &&other) :
    OutputStreamBase(std::move(other)),
    m_device(std::move(other.m_device)),
    m_channel_index(std::move(other.m_channel_index)),
    m_channel(std::move(other.m_channel)),
    is_stream_activated(std::exchange(other.is_stream_activated, false)),
    m_transfer_timeout(std::move(other.m_transfer_timeout)),
    m_latency_meter(std::move(other.m_latency_meter)),
    m_batch_size(other.m_batch_size),
    m_transfer_size(other.m_transfer_size)
{}

VdmaOutputStream::~VdmaOutputStream()
{
    // We want to stop the vdma channel before closing the stream in the firmware
    // because sending data to a closed stream may terminate the dma engine
    auto status = HAILO_UNINITIALIZED;

    if (this->is_stream_activated) {
        status = VdmaOutputStream::deactivate_stream();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to deactivate stream with error status {}", status);
        }
    }
}

hailo_status VdmaOutputStream::set_timeout(std::chrono::milliseconds timeout)
{
    this->m_transfer_timeout = timeout;
    return HAILO_SUCCESS;
}

std::chrono::milliseconds VdmaOutputStream::get_timeout() const
{
    return this->m_transfer_timeout;
}

hailo_status VdmaOutputStream::abort()
{
    return m_channel->abort();
}

hailo_status VdmaOutputStream::clear_abort()
{
    return m_channel->clear_abort();
}

hailo_status VdmaOutputStream::activate_stream()
{
    auto status = m_channel->start_allocated_channel(m_transfer_size);
    CHECK_SUCCESS(status);

    this->is_stream_activated = true;

    return HAILO_SUCCESS;
}

hailo_status VdmaOutputStream::deactivate_stream()
{
    if (!is_stream_activated) {
        return HAILO_SUCCESS;
    }

    auto status = m_channel->stop_channel();
    CHECK_SUCCESS(status);

    this->is_stream_activated = false;
    return HAILO_SUCCESS;
}

Expected<size_t> VdmaOutputStream::sync_read_raw_buffer(MemoryView &buffer)
{
    hailo_status status = HAILO_UNINITIALIZED;

    status = m_channel->wait(buffer.size(), m_transfer_timeout);
    if ((HAILO_STREAM_INTERNAL_ABORT == status) || (HAILO_STREAM_NOT_ACTIVATED == status)) {
        return make_unexpected(status);
    }
    CHECK_SUCCESS_AS_EXPECTED(status);

    status = m_channel->transfer(buffer.data(), buffer.size());
    CHECK_SUCCESS_AS_EXPECTED(status);

    return buffer.size();
}

hailo_status VdmaOutputStream::config_stream()
{
    const uint32_t min_active_trans = MIN_ACTIVE_TRANSFERS_SCALE * m_batch_size;
    const uint32_t max_active_trans = MAX_ACTIVE_TRANSFERS_SCALE * m_batch_size;
    assert(min_active_trans <= max_active_trans);

    CHECK(IS_FIT_IN_UINT16(min_active_trans), HAILO_INVALID_ARGUMENT, 
        "calculated min_active_trans for vdma descriptor list is out of UINT16 range");

    CHECK(IS_FIT_IN_UINT16(max_active_trans), HAILO_INVALID_ARGUMENT, 
        "calculated min_active_trans for vdma descriptor list is out of UINT16 range");

    auto desc_sizes_pair = VdmaDescriptorList::get_desc_buffer_sizes_for_single_transfer(m_device->get_driver(),
        static_cast<uint16_t>(min_active_trans), static_cast<uint16_t>(max_active_trans), m_transfer_size);
    CHECK_EXPECTED_AS_STATUS(desc_sizes_pair);
    
    const auto page_size = desc_sizes_pair->first;
    auto channel = VdmaChannel::create(m_channel_index, VdmaChannel::Direction::D2H, m_device->get_driver(), page_size,
        m_stream_info.index, m_latency_meter, m_batch_size);

    m_channel = make_unique_nothrow<VdmaChannel>(channel.release());
    CHECK(nullptr != m_channel, HAILO_OUT_OF_HOST_MEMORY);

    const auto descs_count = desc_sizes_pair->second;
    const auto status = m_channel->allocate_resources(descs_count);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status VdmaOutputStream::read_all(MemoryView &buffer)
{
    CHECK((buffer.size() % HailoRTCommon::HW_DATA_ALIGNMENT) == 0, HAILO_INVALID_ARGUMENT, 
        "Size must be aligned to {} (got {})", HailoRTCommon::HW_DATA_ALIGNMENT, buffer.size());

    return sync_read_raw_buffer(buffer).status();
}

uint32_t VdmaOutputStream::get_transfer_size(const hailo_stream_info_t &stream_info)
{
    // The ppu outputs one bbox per vdma buffer in the case of nms
    return (HAILO_FORMAT_ORDER_HAILO_NMS == stream_info.format.order) ?
        stream_info.nms_info.bbox_size : stream_info.hw_frame_size;
}

} /* namespace hailort */
