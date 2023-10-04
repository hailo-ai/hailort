/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_stream.cpp
 **/

#include "hailo/hailort_common.hpp"

#include "vdma/vdma_stream.hpp"
#include "vdma/circular_stream_buffer_pool.hpp"
#include "utils/profiler/tracer_macros.hpp"


namespace hailort
{


/** Input stream **/
Expected<std::shared_ptr<VdmaInputStream>> VdmaInputStream::create(hailo_stream_interface_t interface,
    VdmaDevice &device, vdma::BoundaryChannelPtr channel, const LayerInfo &edge_layer, EventPtr core_op_activated_event)
{
    assert((interface == HAILO_STREAM_INTERFACE_PCIE) || (interface == HAILO_STREAM_INTERFACE_INTEGRATED));

    hailo_status status = HAILO_UNINITIALIZED;
    auto result = make_shared_nothrow<VdmaInputStream>(device, channel, edge_layer,
        core_op_activated_event, interface, status);
    CHECK_NOT_NULL_AS_EXPECTED(result, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return result;
}

VdmaInputStream::VdmaInputStream(VdmaDevice &device, vdma::BoundaryChannelPtr channel,
                                 const LayerInfo &edge_layer, EventPtr core_op_activated_event,
                                 hailo_stream_interface_t stream_interface, hailo_status &status) :
    AsyncInputStreamBase(edge_layer, stream_interface, std::move(core_op_activated_event), status),
    m_device(device),
    m_channel(std::move(channel)),
    m_interface(stream_interface)
{
    // Checking status for base class c'tor
    if (HAILO_SUCCESS != status) {
        return;
    }

    status = HAILO_SUCCESS;
}

VdmaInputStream::~VdmaInputStream()
{
    // We want to stop the vdma channel before closing the stream in the firmware
    // because sending data to a closed stream may terminate the dma engine
    const auto status = m_channel->deactivate();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to deactivate stream with error status {}", status);
    }
}

hailo_stream_interface_t VdmaInputStream::get_interface() const
{
    return m_interface;
}

vdevice_core_op_handle_t VdmaInputStream::get_vdevice_core_op_handle()
{
    return m_core_op_handle;
}

void VdmaInputStream::set_vdevice_core_op_handle(vdevice_core_op_handle_t core_op_handle)
{
    m_core_op_handle = core_op_handle;
}

Expected<std::unique_ptr<StreamBufferPool>> VdmaInputStream::allocate_buffer_pool()
{
    auto circular_pool = CircularStreamBufferPool::create(m_device.get_driver(), HailoRTDriver::DmaDirection::H2D,
        m_channel->get_desc_list()->desc_page_size(), m_channel->get_desc_list()->count(), get_frame_size());
    CHECK_EXPECTED(circular_pool);

    return std::unique_ptr<StreamBufferPool>(circular_pool.release());
}

size_t VdmaInputStream::get_max_ongoing_transfers() const
{
    return m_channel->get_max_ongoing_transfers(get_frame_size());
}

hailo_status VdmaInputStream::write_async_impl(TransferRequest &&transfer_request)
{
    TRACE(InputVdmaDequeueTrace, m_device.get_dev_id(), m_core_op_handle, name());
    const auto user_owns_buffer = (buffer_mode() == StreamBufferMode::NOT_OWNING);
    return m_channel->launch_transfer(std::move(transfer_request), user_owns_buffer);
}

hailo_status VdmaInputStream::activate_stream_impl()
{
    return m_channel->activate();
}

hailo_status VdmaInputStream::deactivate_stream_impl()
{
    return m_channel->deactivate();
}

/** Output stream **/
Expected<std::shared_ptr<VdmaOutputStream>> VdmaOutputStream::create(hailo_stream_interface_t interface,
    VdmaDevice &device, vdma::BoundaryChannelPtr channel, const LayerInfo &edge_layer,
    EventPtr core_op_activated_event)
{
    assert((interface == HAILO_STREAM_INTERFACE_PCIE) || (interface == HAILO_STREAM_INTERFACE_INTEGRATED));

    hailo_status status = HAILO_UNINITIALIZED;
    auto result = make_shared_nothrow<VdmaOutputStream>(device, channel, edge_layer,
        core_op_activated_event, interface, status);
    CHECK_NOT_NULL_AS_EXPECTED(result, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return result;
}

VdmaOutputStream::VdmaOutputStream(VdmaDevice &device, vdma::BoundaryChannelPtr channel, const LayerInfo &edge_layer,
                                   EventPtr core_op_activated_event,
                                   hailo_stream_interface_t interface,
                                   hailo_status &status) :
    AsyncOutputStreamBase(edge_layer, interface, std::move(core_op_activated_event), status),
    m_device(device),
    m_channel(std::move(channel)),
    m_interface(interface),
    m_transfer_size(get_transfer_size(m_stream_info, get_layer_info()))
{
    // Check status for base class c'tor
    if (HAILO_SUCCESS != status) {
        return;
    }

    status = HAILO_SUCCESS;
}

VdmaOutputStream::~VdmaOutputStream()
{
    // We want to stop the vdma channel before closing the stream in the firmware
    // because sending data to a closed stream may terminate the dma engine
    const auto status = m_channel->deactivate();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to deactivate stream with error status {}", status);
    }
}

hailo_stream_interface_t VdmaOutputStream::get_interface() const
{
    return m_interface;
}

Expected<std::unique_ptr<StreamBufferPool>> VdmaOutputStream::allocate_buffer_pool()
{
    auto circular_pool = CircularStreamBufferPool::create(m_device.get_driver(), HailoRTDriver::DmaDirection::D2H,
        m_channel->get_desc_list()->desc_page_size(), m_channel->get_desc_list()->count(), m_transfer_size);
    CHECK_EXPECTED(circular_pool);

    return std::unique_ptr<StreamBufferPool>(circular_pool.release());
}

size_t VdmaOutputStream::get_max_ongoing_transfers() const
{
    return m_channel->get_max_ongoing_transfers(m_transfer_size);
}

hailo_status VdmaOutputStream::read_async_impl(TransferRequest &&transfer_request)
{
    const auto user_owns_buffer = (buffer_mode() == StreamBufferMode::NOT_OWNING);
    return m_channel->launch_transfer(std::move(transfer_request), user_owns_buffer);
}

hailo_status VdmaOutputStream::activate_stream_impl()
{
    return m_channel->activate();
}

hailo_status VdmaOutputStream::deactivate_stream_impl()
{
    return m_channel->deactivate();
}

uint32_t VdmaOutputStream::get_transfer_size(const hailo_stream_info_t &stream_info, const LayerInfo &layer_info)
{
    return LayerInfoUtils::get_stream_transfer_size(stream_info, layer_info);
}

} /* namespace hailort */
