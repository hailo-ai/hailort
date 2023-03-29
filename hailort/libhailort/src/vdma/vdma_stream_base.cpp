/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_stream_base.cpp
 **/

#include "hailo/hailort_common.hpp"

#include "vdma/vdma_stream_base.hpp"
#include "vdma/vdma_stream.hpp"
#include "vdma/vdma_async_stream.hpp"


namespace hailort
{

static bool validate_device_interface_compatibility(hailo_stream_interface_t interface, Device::Type type)
{
    bool interface_valid = false;
    switch (type)
    {
    case Device::Type::PCIE:
        interface_valid = (HAILO_STREAM_INTERFACE_PCIE == interface);
        break;
    
    case Device::Type::INTEGRATED:
        interface_valid = (HAILO_STREAM_INTERFACE_INTEGRATED == interface);
        break;

    default:
        LOGGER__ERROR("Invalid device type {}", type);
        return false;
    }

    if (interface_valid) {
        return true;
    }

    LOGGER__ERROR("Invalid interface {} for device of type {}", interface, type);
    return false;
}

Expected<std::shared_ptr<VdmaInputStreamBase>> VdmaInputStreamBase::create(hailo_stream_interface_t interface,
    VdmaDevice &device, vdma::BoundaryChannelPtr channel, const LayerInfo &edge_layer,
    const hailo_stream_parameters_t &stream_params, uint16_t batch_size, EventPtr core_op_activated_event)
{
    CHECK_AS_EXPECTED(validate_device_interface_compatibility(interface, device.get_type()), HAILO_INTERNAL_FAILURE);

    if ((stream_params.flags & HAILO_STREAM_FLAGS_ASYNC) != 0) {
        CHECK_AS_EXPECTED(channel->type() == vdma::BoundaryChannel::Type::ASYNC, HAILO_INVALID_ARGUMENT,
            "Can't create a async vdma stream with a non async channel. Received channel type {}", channel->type());

        hailo_status status = HAILO_UNINITIALIZED;
        auto result = make_shared_nothrow<VdmaAsyncInputStream>(device, channel, edge_layer, core_op_activated_event,
            batch_size, DEFAULT_TRANSFER_TIMEOUT, interface, status);
        CHECK_SUCCESS_AS_EXPECTED(status);
        CHECK_NOT_NULL_AS_EXPECTED(result, HAILO_OUT_OF_HOST_MEMORY);

        return std::static_pointer_cast<VdmaInputStreamBase>(result);
    } else {
        CHECK_AS_EXPECTED(channel->type() == vdma::BoundaryChannel::Type::BUFFERED, HAILO_INVALID_ARGUMENT,
            "Can't create a vdma stream with a non buffered channel. Received channel type {}", channel->type());

        hailo_status status = HAILO_UNINITIALIZED;
        auto result = make_shared_nothrow<VdmaInputStream>(device, channel, edge_layer, core_op_activated_event,
            batch_size, DEFAULT_TRANSFER_TIMEOUT, interface, status);
        CHECK_SUCCESS_AS_EXPECTED(status);
        CHECK_NOT_NULL_AS_EXPECTED(result, HAILO_OUT_OF_HOST_MEMORY);

        return std::static_pointer_cast<VdmaInputStreamBase>(result);
    }
}

VdmaInputStreamBase::VdmaInputStreamBase(VdmaDevice &device, vdma::BoundaryChannelPtr channel,
                                         const LayerInfo &edge_layer, EventPtr core_op_activated_event,
                                         uint16_t batch_size, std::chrono::milliseconds transfer_timeout,
                                         hailo_stream_interface_t stream_interface, hailo_status &status) :
    InputStreamBase(edge_layer, stream_interface, std::move(core_op_activated_event), status),
    m_device(&device),
    m_channel(std::move(channel)),
    m_interface(stream_interface),
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

VdmaInputStreamBase::~VdmaInputStreamBase()
{
    // We want to stop the vdma channel before closing the stream in the firmware
    // because sending data to a closed stream may terminate the dma engine
    if (this->is_stream_activated) {
        const auto status = VdmaInputStreamBase::deactivate_stream();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to deactivate stream with error status {}", status);
        }
    }
}

hailo_stream_interface_t VdmaInputStreamBase::get_interface() const
{
    return m_interface;
}

std::chrono::milliseconds VdmaInputStreamBase::get_timeout() const
{
    return this->m_channel_timeout;
}

hailo_status VdmaInputStreamBase::set_timeout(std::chrono::milliseconds timeout)
{
    this->m_channel_timeout = timeout;
    return HAILO_SUCCESS;
}

hailo_status VdmaInputStreamBase::abort()
{
    return m_channel->abort();
}

hailo_status VdmaInputStreamBase::clear_abort()
{
    return m_channel->clear_abort();
}

hailo_status VdmaInputStreamBase::flush()
{
    const auto dynamic_batch_size = (CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE == m_dynamic_batch_size) ? 
        1 : m_dynamic_batch_size;
    return m_channel->flush(m_channel_timeout * dynamic_batch_size);
}

hailo_status VdmaInputStreamBase::activate_stream(uint16_t dynamic_batch_size, bool resume_pending_stream_transfers)
{
    auto status = set_dynamic_batch_size(dynamic_batch_size);
    CHECK_SUCCESS(status);

    status = m_channel->activate(0, resume_pending_stream_transfers);
    CHECK_SUCCESS(status);

    this->is_stream_activated = true;

    return HAILO_SUCCESS;
}

hailo_status VdmaInputStreamBase::deactivate_stream()
{
    if (!is_stream_activated) {
        return HAILO_SUCCESS;
    }

    // Flush is best effort
    auto status = m_channel->flush(VDMA_FLUSH_TIMEOUT);
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        LOGGER__INFO("Flush input_channel is not needed because channel was aborted. (channel {})", m_channel->get_channel_id());
        status = HAILO_SUCCESS;
    } else if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to flush input_channel. (status {} channel {})", status, m_channel->get_channel_id());
    }

    status = m_channel->deactivate();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to stop channel with status {}", status);
    }

    this->is_stream_activated = false;
    return status;
}

uint16_t VdmaInputStreamBase::get_dynamic_batch_size() const
{
    return std::max(m_dynamic_batch_size, static_cast<uint16_t>(1));
}

const char* VdmaInputStreamBase::get_dev_id() const
{
    return m_device->get_dev_id();
}

Expected<vdma::BoundaryChannel::BufferState> VdmaInputStreamBase::get_buffer_state()
{
    return m_channel->get_buffer_state();
}

Expected<size_t> VdmaInputStreamBase::get_buffer_frames_size() const
{
    return m_channel->get_transfers_count_in_buffer(m_stream_info.hw_frame_size);
}

Expected<size_t> VdmaInputStreamBase::get_pending_frames_count() const
{
    return m_channel->get_h2d_pending_frames_count();
}

hailo_status VdmaInputStreamBase::register_interrupt_callback(const vdma::ProcessingCompleteCallback &callback)
{
    return m_channel->register_interrupt_callback(callback);
}

hailo_status VdmaInputStreamBase::set_dynamic_batch_size(uint16_t dynamic_batch_size)
{
    // TODO: use std::max in the configure stage
    if (CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE == m_max_batch_size) {
        LOGGER__TRACE("max_batch_size is CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE; "
                      "Ignoring value of dynamic_batch_size {}", m_dynamic_batch_size);
        return HAILO_SUCCESS;
    }

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
Expected<std::shared_ptr<VdmaOutputStreamBase>> VdmaOutputStreamBase::create(hailo_stream_interface_t interface,
    VdmaDevice &device, vdma::BoundaryChannelPtr channel, const LayerInfo &edge_layer, uint16_t batch_size,
    const hailo_stream_parameters_t &stream_params, EventPtr core_op_activated_event)
{
    CHECK_AS_EXPECTED(validate_device_interface_compatibility(interface, device.get_type()), HAILO_INTERNAL_FAILURE);

    if ((stream_params.flags & HAILO_STREAM_FLAGS_ASYNC) != 0) {
        CHECK_AS_EXPECTED(channel->type() == vdma::BoundaryChannel::Type::ASYNC, HAILO_INVALID_ARGUMENT,
            "Can't create a async vdma stream with a non async channel. Received channel type {}", channel->type());

        hailo_status status = HAILO_UNINITIALIZED;
        auto result = make_shared_nothrow<VdmaAsyncOutputStream>(device, channel, edge_layer, core_op_activated_event,
            batch_size, DEFAULT_TRANSFER_TIMEOUT, interface, status);
        CHECK_SUCCESS_AS_EXPECTED(status);
        CHECK_NOT_NULL_AS_EXPECTED(result, HAILO_OUT_OF_HOST_MEMORY);

        return std::static_pointer_cast<VdmaOutputStreamBase>(result);
    } else {
        CHECK_AS_EXPECTED(channel->type() == vdma::BoundaryChannel::Type::BUFFERED, HAILO_INVALID_ARGUMENT,
            "Can't create a vdma stream with a non buffered channel. Received channel type {}", channel->type());

        hailo_status status = HAILO_UNINITIALIZED;
        auto result = make_shared_nothrow<VdmaOutputStream>(device, channel, edge_layer, core_op_activated_event,
            batch_size, DEFAULT_TRANSFER_TIMEOUT, interface, status);
        CHECK_SUCCESS_AS_EXPECTED(status);
        CHECK_NOT_NULL_AS_EXPECTED(result, HAILO_OUT_OF_HOST_MEMORY);

        return std::static_pointer_cast<VdmaOutputStreamBase>(result);
    }
}

VdmaOutputStreamBase::VdmaOutputStreamBase(VdmaDevice &device, vdma::BoundaryChannelPtr channel, const LayerInfo &edge_layer,
                                           EventPtr core_op_activated_event, uint16_t batch_size,
                                           std::chrono::milliseconds transfer_timeout, hailo_stream_interface_t interface,
                                           hailo_status &status) :
    OutputStreamBase(edge_layer, std::move(core_op_activated_event), status),
    m_device(&device),
    m_channel(std::move(channel)),
    m_interface(interface),
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

VdmaOutputStreamBase::~VdmaOutputStreamBase()
{
    // We want to stop the vdma channel before closing the stream in the firmware
    // because sending data to a closed stream may terminate the dma engine
    if (this->is_stream_activated) {
        const auto status = VdmaOutputStreamBase::deactivate_stream();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to deactivate stream with error status {}", status);
        }
    }
}

hailo_stream_interface_t VdmaOutputStreamBase::get_interface() const
{
    return m_interface;
}

hailo_status VdmaOutputStreamBase::set_timeout(std::chrono::milliseconds timeout)
{
    this->m_transfer_timeout = timeout;
    return HAILO_SUCCESS;
}

std::chrono::milliseconds VdmaOutputStreamBase::get_timeout() const
{
    return this->m_transfer_timeout;
}

hailo_status VdmaOutputStreamBase::abort()
{
    return m_channel->abort();
}

hailo_status VdmaOutputStreamBase::clear_abort()
{
    return m_channel->clear_abort();
}

uint16_t VdmaOutputStreamBase::get_dynamic_batch_size() const
{
    return std::max(m_dynamic_batch_size, static_cast<uint16_t>(1));
}

const char* VdmaOutputStreamBase::get_dev_id() const
{
    return m_device->get_dev_id();
}

Expected<vdma::BoundaryChannel::BufferState> VdmaOutputStreamBase::get_buffer_state()
{
    return m_channel->get_buffer_state();
}

hailo_status VdmaOutputStreamBase::activate_stream(uint16_t dynamic_batch_size, bool resume_pending_stream_transfers)
{
    auto status = set_dynamic_batch_size(dynamic_batch_size);
    CHECK_SUCCESS(status);

    status = m_channel->activate(m_transfer_size, resume_pending_stream_transfers);
    CHECK_SUCCESS(status);

    this->is_stream_activated = true;

    return HAILO_SUCCESS;
}

hailo_status VdmaOutputStreamBase::register_interrupt_callback(const vdma::ProcessingCompleteCallback &callback)
{
    return m_channel->register_interrupt_callback(callback);
}

hailo_status VdmaOutputStreamBase::deactivate_stream()
{
    if (!is_stream_activated) {
        return HAILO_SUCCESS;
    }

    auto status = m_channel->deactivate();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to stop channel with status {}", status);
    }

    this->is_stream_activated = false;
    return HAILO_SUCCESS;
}

uint32_t VdmaOutputStreamBase::get_transfer_size(const hailo_stream_info_t &stream_info)
{
    // The ppu outputs one bbox per vdma buffer in the case of nms
    return (HAILO_FORMAT_ORDER_HAILO_NMS == stream_info.format.order) ?
        stream_info.nms_info.bbox_size : stream_info.hw_frame_size;
}

hailo_status VdmaOutputStreamBase::set_dynamic_batch_size(uint16_t dynamic_batch_size)
{
    // TODO: use std::max in the configure stage
    if (CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE == m_max_batch_size) {
        LOGGER__TRACE("max_batch_size is CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE; "
                      "Ignoring value of dynamic_batch_size {}", m_dynamic_batch_size);
        return HAILO_SUCCESS;
    }

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

Expected<size_t> VdmaOutputStreamBase::get_buffer_frames_size() const
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

Expected<size_t> VdmaOutputStreamBase::get_pending_frames_count() const
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
