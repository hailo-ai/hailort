/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_stream.cpp
 **/

#include "vdma_stream.hpp"
#include "pcie_stream.hpp"
#include "core_stream.hpp"

namespace hailort
{

Expected<std::unique_ptr<VdmaInputStream>> VdmaInputStream::create(VdmaDevice &device,
    std::shared_ptr<VdmaChannel> channel, const LayerInfo &edge_layer, uint16_t batch_size, 
    EventPtr network_group_activated_event)
{
    switch (device.get_type()) {
    case Device::Type::PCIE:
    {
        auto local_stream = PcieInputStream::create(device, channel, edge_layer, batch_size,
            network_group_activated_event);
        CHECK_EXPECTED(local_stream);
        return std::unique_ptr<VdmaInputStream>(local_stream.release());
    }
    case Device::Type::CORE:
    {
        auto local_stream = CoreInputStream::create(device, channel, edge_layer, batch_size,
            network_group_activated_event);
        CHECK_EXPECTED(local_stream);
        return std::unique_ptr<VdmaInputStream>(local_stream.release());
    }
    default:
        assert(false);
        LOGGER__ERROR("Invalid device type {}", static_cast<uint8_t>(device.get_type()));
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }
}

VdmaInputStream::VdmaInputStream(VdmaDevice &device, std::shared_ptr<VdmaChannel> channel,
                                 const LayerInfo &edge_layer, EventPtr network_group_activated_event, uint16_t batch_size,
                                 std::chrono::milliseconds transfer_timeout,
                                 hailo_stream_interface_t stream_interface, hailo_status &status) :
    InputStreamBase(edge_layer, stream_interface, std::move(network_group_activated_event), status),
    m_device(&device),
    m_channel(std::move(channel)),
    is_stream_activated(false),
    m_channel_timeout(transfer_timeout),
    m_max_batch_size(batch_size),
    m_dynamic_batch_size(batch_size)
{
    // Checking status for base class c'tor
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
    m_channel(std::move(other.m_channel)),
    is_stream_activated(std::exchange(other.is_stream_activated, false)),
    m_channel_timeout(std::move(other.m_channel_timeout)),
    m_max_batch_size(other.m_max_batch_size),
    m_dynamic_batch_size(other.m_dynamic_batch_size)
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
    return m_channel->flush((m_channel_timeout * m_dynamic_batch_size));
}

hailo_status VdmaInputStream::activate_stream(uint16_t dynamic_batch_size)
{
    auto status = set_dynamic_batch_size(dynamic_batch_size);
    CHECK_SUCCESS(status);

    status = m_channel->start_allocated_channel(0);
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
        LOGGER__INFO("Flush input_channel is not needed because channel was aborted. (channel {})", m_channel->get_channel_id());
        status = HAILO_SUCCESS;
    } else if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to flush input_channel. (status {} channel {})", status, m_channel->get_channel_id());
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
    if ((status == HAILO_STREAM_INTERNAL_ABORT) || (status == HAILO_STREAM_NOT_ACTIVATED)) {
        return make_unexpected(status);
    }
    CHECK_SUCCESS_AS_EXPECTED(status);

    status = m_channel->transfer((void*)buffer.data(), buffer.size());
    if ((status == HAILO_STREAM_INTERNAL_ABORT) || (status == HAILO_STREAM_NOT_ACTIVATED)) {
        return make_unexpected(status);
    }
    CHECK_SUCCESS_AS_EXPECTED(status);

    return buffer.size();
}

hailo_status VdmaInputStream::write_buffer_only(const MemoryView &buffer)
{
    std::unique_lock<std::mutex> lock(m_write_only_mutex);
    return m_channel->write_buffer(buffer, m_channel_timeout);
}

Expected<PendingBufferState> VdmaInputStream::send_pending_buffer()
{
    std::unique_lock<std::mutex> lock(m_send_pending_mutex);
    hailo_status status = m_channel->wait(get_frame_size(), m_channel_timeout);
    if ((HAILO_STREAM_INTERNAL_ABORT == status) || (HAILO_STREAM_NOT_ACTIVATED == status)) {
        return make_unexpected(status);
    }
    CHECK_SUCCESS_AS_EXPECTED(status);

    return m_channel->send_pending_buffer();
}

uint16_t VdmaInputStream::get_dynamic_batch_size() const
{
    return m_dynamic_batch_size;
}

const char* VdmaInputStream::get_dev_id() const
{
    return m_device->get_dev_id();
}

Expected<size_t> VdmaInputStream::get_buffer_frames_size() const
{
    return m_channel->get_transfers_count_in_buffer(m_stream_info.hw_frame_size);
}

Expected<size_t> VdmaInputStream::get_pending_frames_count() const
{
    return m_channel->get_h2d_pending_frames_count();
}

hailo_status VdmaInputStream::sync_write_all_raw_buffer_no_transform_impl(void *buffer, size_t offset, size_t size)
{
    ASSERT(NULL != buffer);

    return sync_write_raw_buffer(MemoryView(static_cast<uint8_t*>(buffer) + offset, size)).status();
}

hailo_status VdmaInputStream::set_dynamic_batch_size(uint16_t dynamic_batch_size)
{
    CHECK(dynamic_batch_size <= m_max_batch_size, HAILO_INVALID_ARGUMENT,
        "Dynamic batch size ({}) must be <= than the configured batch size ({})",
        dynamic_batch_size, m_max_batch_size);
    
    if (CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE == dynamic_batch_size) {
        LOGGER__TRACE("Received CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE == dynamic_batch_size; "
                      "Leaving previously set value of {}", m_dynamic_batch_size);
    } else {
        LOGGER__TRACE("Setting stream's dynamic_batch_size to {}", dynamic_batch_size);
        m_dynamic_batch_size = dynamic_batch_size;

        const auto status = m_channel->set_transfers_per_axi_intr(m_dynamic_batch_size);
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

/** Output stream **/
Expected<std::unique_ptr<VdmaOutputStream>> VdmaOutputStream::create(VdmaDevice &device,
    std::shared_ptr<VdmaChannel> channel, const LayerInfo &edge_layer, uint16_t batch_size, 
    EventPtr network_group_activated_event)
{
    switch (device.get_type()) {
    case Device::Type::PCIE:
    {
        auto local_stream = PcieOutputStream::create(device, channel, edge_layer, batch_size,
            network_group_activated_event);
        CHECK_EXPECTED(local_stream);
        return std::unique_ptr<VdmaOutputStream>(local_stream.release());
    }
    case Device::Type::CORE:
    {
        auto local_stream = CoreOutputStream::create(device, channel, edge_layer, batch_size,
            network_group_activated_event);
        CHECK_EXPECTED(local_stream);
        return std::unique_ptr<VdmaOutputStream>(local_stream.release());
    }
    default:
        assert(false);
        LOGGER__ERROR("Invalid device type {}", static_cast<uint8_t>(device.get_type()));
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }
}

VdmaOutputStream::VdmaOutputStream(VdmaDevice &device, std::shared_ptr<VdmaChannel> channel,
                                   const LayerInfo &edge_layer, EventPtr network_group_activated_event, uint16_t batch_size,
                                   std::chrono::milliseconds transfer_timeout, hailo_status &status) :
    OutputStreamBase(edge_layer, std::move(network_group_activated_event), status),
    m_device(&device),
    m_channel(std::move(channel)),
    is_stream_activated(false),
    m_transfer_timeout(transfer_timeout),
    m_max_batch_size(batch_size),
    m_dynamic_batch_size(batch_size),
    m_transfer_size(get_transfer_size(m_stream_info))
{
    // Check status for base class c'tor
    if (HAILO_SUCCESS != status) {
        return;
    }

    status = HAILO_SUCCESS;
}

VdmaOutputStream::VdmaOutputStream(VdmaOutputStream &&other) :
    OutputStreamBase(std::move(other)),
    m_device(std::move(other.m_device)),
    m_channel(std::move(other.m_channel)),
    is_stream_activated(std::exchange(other.is_stream_activated, false)),
    m_transfer_timeout(std::move(other.m_transfer_timeout)),
    m_max_batch_size(other.m_max_batch_size),
    m_dynamic_batch_size(other.m_dynamic_batch_size),
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

uint16_t VdmaOutputStream::get_dynamic_batch_size() const
{
    return m_dynamic_batch_size;
}

const char* VdmaOutputStream::get_dev_id() const
{
    return m_device->get_dev_id();
}

hailo_status VdmaOutputStream::activate_stream(uint16_t dynamic_batch_size)
{
    auto status = set_dynamic_batch_size(dynamic_batch_size);
    CHECK_SUCCESS(status);
    
    status = m_channel->start_allocated_channel(m_transfer_size);
    CHECK_SUCCESS(status);

    this->is_stream_activated = true;

    return HAILO_SUCCESS;
}

hailo_status VdmaOutputStream::register_for_d2h_interrupts(const std::function<void(uint32_t)> &callback)
{
    return m_channel->register_for_d2h_interrupts(callback);
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
    if ((status == HAILO_STREAM_INTERNAL_ABORT) || (status == HAILO_STREAM_NOT_ACTIVATED)) {
        return make_unexpected(status);
    }
    CHECK_SUCCESS_AS_EXPECTED(status);

    status = m_channel->transfer(buffer.data(), buffer.size());
    if ((status == HAILO_STREAM_NOT_ACTIVATED) || (status == HAILO_STREAM_INTERNAL_ABORT)) {
        return make_unexpected(status);
    }
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

uint32_t VdmaOutputStream::get_transfer_size(const hailo_stream_info_t &stream_info)
{
    // The ppu outputs one bbox per vdma buffer in the case of nms
    return (HAILO_FORMAT_ORDER_HAILO_NMS == stream_info.format.order) ?
        stream_info.nms_info.bbox_size : stream_info.hw_frame_size;
}

hailo_status VdmaOutputStream::set_dynamic_batch_size(uint16_t dynamic_batch_size)
{
    CHECK(dynamic_batch_size <= m_max_batch_size, HAILO_INVALID_ARGUMENT,
        "Dynamic batch size ({}) must be <= than the configured batch size ({})",
        dynamic_batch_size, m_max_batch_size);
    
    if (CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE == dynamic_batch_size) {
        LOGGER__TRACE("Received CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE == dynamic_batch_size; "
                      "Leaving previously set value of {}", m_dynamic_batch_size);
    } else {
        LOGGER__TRACE("Setting stream's dynamic_batch_size to {}", dynamic_batch_size);
        m_dynamic_batch_size = dynamic_batch_size;

        const auto status = m_channel->set_transfers_per_axi_intr(m_dynamic_batch_size);
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

Expected<size_t> VdmaOutputStream::get_buffer_frames_size() const
{
    if (HAILO_FORMAT_ORDER_HAILO_NMS == m_stream_info.format.order) {
        // In NMS, each output frame has different size depending on the number of bboxes found for each class
        // and m_stream_info.hw_frame_size is the max frame size. To know the actual frame size and
        // calculate the number of frames we need to read the content of the buffer (and finding the delimiter for each class in each frame).
        LOGGER__INFO("NMS is not supported in function get_buffer_frames_size()");
        return make_unexpected(HAILO_NOT_AVAILABLE);
    }

    return m_channel->get_transfers_count_in_buffer(m_stream_info.hw_frame_size);
}

Expected<size_t> VdmaOutputStream::get_pending_frames_count() const
{
    if (HAILO_FORMAT_ORDER_HAILO_NMS == m_stream_info.format.order) {
        // In NMS, each output frame has different size depending on the number of bboxes found for each class
        // and m_stream_info.hw_frame_size is the max frame size. To know the actual frame size and
        // calculate the number of frames we need to read the content of the buffer (and finding the delimiter for each class in each frame).
        LOGGER__INFO("NMS is not supported in function get_pending_frames_count()");
        return make_unexpected(HAILO_NOT_AVAILABLE);
    }

    auto pending_descs_count = m_channel->get_d2h_pending_descs_count();
    CHECK_EXPECTED(pending_descs_count);

    auto channel_page_size = m_channel->get_page_size();
    uint32_t descs_per_frame = (0 == (m_stream_info.hw_frame_size % channel_page_size)) ? (m_stream_info.hw_frame_size / channel_page_size) :
        ((m_stream_info.hw_frame_size / channel_page_size) + 1);

    return static_cast<size_t>(std::floor(pending_descs_count.value() / descs_per_frame));
}

} /* namespace hailort */
