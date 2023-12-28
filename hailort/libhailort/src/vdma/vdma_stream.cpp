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
#include "common/os_utils.hpp"


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

std::unique_ptr<StreamBufferPool> VdmaInputStream::init_dma_bounce_buffer_pool(
    vdma::BoundaryChannelPtr channel, const LayerInfo &edge_layer, hailo_status &status)
{
    const auto dma_able_alignment = OsUtils::get_dma_able_alignment();
    const auto dma_bounce_buffer_pool_size = channel->get_max_ongoing_transfers(
        LayerInfoUtils::get_layer_transfer_size(edge_layer));

    // Checking status for base class c'tor
    if (HAILO_SUCCESS != status) {
        return nullptr;
    }

    // Initialize dma buffer pool for support for non-aligned user buffers
    auto dma_queued_pool = QueuedStreamBufferPool::create(dma_bounce_buffer_pool_size, dma_able_alignment,
        BufferStorageParams::create_dma());
    if (dma_queued_pool.status() != HAILO_SUCCESS) {
        LOGGER__ERROR("Failed creating DMA bounce buffer pool with status {}", dma_queued_pool.status());
        status = dma_queued_pool.status();
        return nullptr;
    }

    return std::unique_ptr<StreamBufferPool>(dma_queued_pool.release());
}

VdmaInputStream::VdmaInputStream(VdmaDevice &device, vdma::BoundaryChannelPtr channel,
                                 const LayerInfo &edge_layer, EventPtr core_op_activated_event,
                                 hailo_stream_interface_t stream_interface, hailo_status &status) :
    AsyncInputStreamBase(edge_layer, std::move(core_op_activated_event), status),
    m_device(device),
    m_dma_bounce_buffer_pool(init_dma_bounce_buffer_pool(channel, edge_layer, status)),
    m_channel(std::move(channel)),
    m_interface(stream_interface),
    m_core_op_handle(INVALID_CORE_OP_HANDLE)
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

void VdmaInputStream::set_vdevice_core_op_handle(vdevice_core_op_handle_t core_op_handle)
{
    m_core_op_handle = core_op_handle;
}

Expected<std::unique_ptr<StreamBufferPool>> VdmaInputStream::allocate_buffer_pool()
{
    auto circular_pool = CircularStreamBufferPool::create(m_device, HailoRTDriver::DmaDirection::H2D,
        m_channel->get_desc_list()->desc_page_size(), m_channel->get_desc_list()->count(), get_frame_size());
    CHECK_EXPECTED(circular_pool);

    return std::unique_ptr<StreamBufferPool>(circular_pool.release());
}

size_t VdmaInputStream::get_max_ongoing_transfers() const
{
    return m_channel->get_max_ongoing_transfers(get_frame_size());
}

Expected<TransferRequest> VdmaInputStream::align_transfer_request(TransferRequest &&transfer_request)
{
    const auto dma_alignment = OsUtils::get_dma_able_alignment();
    std::vector<TransferBuffer> transfer_buffers;
    TransferBuffer dma_able_bounce_buffer;
    const auto buffer_address = transfer_request.transfer_buffers[0].base_buffer()->data();
    const auto buffer_size = transfer_request.transfer_buffers[0].size();

    {
        std::unique_lock<std::mutex> lock(m_dma_pool_mutex);
        // Initialize dma able bounce buffer the size of alignment size to read pre alignment data
        auto dma_able_bounce_buffer_exp = m_dma_bounce_buffer_pool->dequeue();
        CHECK_EXPECTED(dma_able_bounce_buffer_exp);
        dma_able_bounce_buffer = dma_able_bounce_buffer_exp.release();
    }

    // If buffer size is larger than alignment size - will create bounce buffer for non aligned buffer part and then use
    // User's buffer from aligned address - otherwise will create bounce buffer size of user buffer and copy whole frame
    if (buffer_size > dma_alignment) {
        transfer_buffers.reserve(2);

        // Get first aligned address in user buffer
        const auto aligned_user_buffer_addr = HailoRTCommon::align_to(reinterpret_cast<size_t>(buffer_address), dma_alignment);
        const auto bounce_buffer_exact_size = aligned_user_buffer_addr - reinterpret_cast<size_t>(buffer_address);
        const auto user_buffer_size = buffer_size - bounce_buffer_exact_size;

        // Create another transfer buffer with same base address but exact size for actual transfer
        auto dma_able_exact_bounce_buffer = TransferBuffer(dma_able_bounce_buffer.base_buffer(), bounce_buffer_exact_size, 0);
        memcpy((dma_able_exact_bounce_buffer.base_buffer())->data(), buffer_address, bounce_buffer_exact_size);
        transfer_buffers.emplace_back(dma_able_exact_bounce_buffer);

        auto dma_able_user_buffer = DmaStorage::create_dma_able_buffer_from_user_size(
            reinterpret_cast<uint8_t*>(aligned_user_buffer_addr), user_buffer_size);
        CHECK_EXPECTED(dma_able_user_buffer);
        transfer_buffers.emplace_back(dma_able_user_buffer.release());
    } else {
        auto dma_able_exact_bounce_buffer = TransferBuffer(dma_able_bounce_buffer.base_buffer(), buffer_size, 0);
        memcpy((dma_able_exact_bounce_buffer.base_buffer())->data(), buffer_address, buffer_size);
        transfer_buffers.emplace_back(dma_able_exact_bounce_buffer);
    }

    auto wrapped_callback = [user_callback=transfer_request.callback, dma_able_bounce_buffer, this](hailo_status callback_status) {
        {
            std::unique_lock<std::mutex> lock(m_dma_pool_mutex);
            m_dma_bounce_buffer_pool->enqueue(TransferBuffer{dma_able_bounce_buffer});
        }
        user_callback(callback_status);
    };

    return TransferRequest(std::move(transfer_buffers), wrapped_callback);
}

hailo_status VdmaInputStream::write_async_impl(TransferRequest &&transfer_request)
{
    TRACE(FrameDequeueH2DTrace, m_device.get_dev_id(), m_core_op_handle, name());
    const auto user_owns_buffer = (buffer_mode() == StreamBufferMode::NOT_OWNING);

    const auto dma_able_alignment = OsUtils::get_dma_able_alignment();
    if (reinterpret_cast<size_t>(transfer_request.transfer_buffers[0].base_buffer()->data()) % dma_able_alignment == 0) {
        return m_channel->launch_transfer(std::move(transfer_request), user_owns_buffer);
    } else {
        auto unaligned_transfer_request = align_transfer_request(std::move(transfer_request));
        CHECK_EXPECTED_AS_STATUS(unaligned_transfer_request);
        return m_channel->launch_transfer(unaligned_transfer_request.release(), user_owns_buffer);
    }

    return HAILO_INTERNAL_FAILURE;
}

hailo_status VdmaInputStream::activate_stream_impl()
{
    return m_channel->activate();
}

hailo_status VdmaInputStream::deactivate_stream_impl()
{
    return m_channel->deactivate();
}

hailo_status VdmaInputStream::cancel_pending_transfers()
{
    m_channel->cancel_pending_transfers();

    return HAILO_SUCCESS;
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
    AsyncOutputStreamBase(edge_layer, std::move(core_op_activated_event), status),
    m_device(device),
    m_channel(std::move(channel)),
    m_interface(interface),
    m_transfer_size(get_transfer_size(m_stream_info, get_layer_info())),
    m_core_op_handle(INVALID_CORE_OP_HANDLE)
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
    auto circular_pool = CircularStreamBufferPool::create(m_device, HailoRTDriver::DmaDirection::D2H,
        m_channel->get_desc_list()->desc_page_size(), m_channel->get_desc_list()->count(), m_transfer_size);
    CHECK_EXPECTED(circular_pool);

    return std::unique_ptr<StreamBufferPool>(circular_pool.release());
}

size_t VdmaOutputStream::get_max_ongoing_transfers() const
{
    return m_channel->get_max_ongoing_transfers(m_transfer_size);
}

Expected<TransferRequest> VdmaOutputStream::align_transfer_request(TransferRequest &&transfer_request)
{
    auto aligned_bounce_buffer_exp = DmaStorage::create_dma_able_buffer_from_user_size(nullptr,
        transfer_request.transfer_buffers[0].size());
    CHECK_EXPECTED(aligned_bounce_buffer_exp);
    auto aligned_bounce_buffer = aligned_bounce_buffer_exp.release();

    auto wrapped_callback = [unaligned_user_buffer = transfer_request.transfer_buffers[0].base_buffer(),
            aligned_bounce_buffer, user_callback = transfer_request.callback](hailo_status callback_status) {
        memcpy(const_cast<uint8_t*>(unaligned_user_buffer->data()), aligned_bounce_buffer->data(), unaligned_user_buffer->size());
        user_callback(callback_status);
    };

    return TransferRequest(std::move(aligned_bounce_buffer), wrapped_callback);
}

hailo_status VdmaOutputStream::read_async_impl(TransferRequest &&transfer_request)
{
    if ((INVALID_CORE_OP_HANDLE != m_core_op_handle) && (HAILO_FORMAT_ORDER_HAILO_NMS != m_stream_info.format.order)) {
        // On NMS stream we trace EnqueueD2H inside nms_stream
        transfer_request.callback = [original_callback=transfer_request.callback, this](hailo_status status) {
            if (HAILO_SUCCESS == status) {
                TRACE(FrameEnqueueD2HTrace, m_device.get_dev_id(), m_core_op_handle, name());
            }
            original_callback(status);
        };
    }
    const auto user_owns_buffer = (buffer_mode() == StreamBufferMode::NOT_OWNING);
    const auto dma_able_alignment = OsUtils::get_dma_able_alignment();
    if (reinterpret_cast<size_t>(transfer_request.transfer_buffers[0].base_buffer()->data()) % dma_able_alignment == 0) {
        return m_channel->launch_transfer(std::move(transfer_request), user_owns_buffer);
    } else {
        // In case of read unaligned - currently doesnt support using users buffer - so allocate complete new buffer size of user's buffer
        LOGGER__WARNING("read_async() was provided an unaligned buffer (address=0x{:x}), which causes performance degradation. Use buffers algined to {} bytes for optimal performance",
            reinterpret_cast<size_t>(transfer_request.transfer_buffers[0].base_buffer()->data()), dma_able_alignment);

        auto realigned_transfer_request = align_transfer_request(std::move(transfer_request));
        CHECK_EXPECTED_AS_STATUS(realigned_transfer_request);
        return m_channel->launch_transfer(realigned_transfer_request.release(), user_owns_buffer);
    }
}

hailo_status VdmaOutputStream::activate_stream_impl()
{
    return m_channel->activate();
}

hailo_status VdmaOutputStream::deactivate_stream_impl()
{
    return m_channel->deactivate();
}

void VdmaOutputStream::set_vdevice_core_op_handle(vdevice_core_op_handle_t core_op_handle)
{
    m_core_op_handle = core_op_handle;
}

uint32_t VdmaOutputStream::get_transfer_size(const hailo_stream_info_t &stream_info, const LayerInfo &layer_info)
{
    return LayerInfoUtils::get_stream_transfer_size(stream_info, layer_info);
}

hailo_status VdmaOutputStream::cancel_pending_transfers()
{
    m_channel->cancel_pending_transfers();

    return HAILO_SUCCESS;
}

} /* namespace hailort */
