/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_stream.cpp
 **/

#include "hailo/hailort_common.hpp"

#include "vdma/vdma_stream.hpp"
#include "vdma/circular_stream_buffer_pool.hpp"
#include "utils/profiler/tracer_macros.hpp"
#include "utils/buffer_storage.hpp"


namespace hailort
{


/** Input stream **/
Expected<std::shared_ptr<VdmaInputStream>> VdmaInputStream::create(hailo_stream_interface_t interface,
    VdmaDevice &device, vdma::BoundaryChannelPtr channel, const LayerInfo &edge_layer, EventPtr core_op_activated_event)
{
    assert((interface == HAILO_STREAM_INTERFACE_PCIE) || (interface == HAILO_STREAM_INTERFACE_INTEGRATED));

    TRY(auto bounce_buffers_pool, init_dma_bounce_buffer_pool(device, channel, edge_layer));

    hailo_status status = HAILO_UNINITIALIZED;
    auto result = make_shared_nothrow<VdmaInputStream>(device, channel, edge_layer,
        core_op_activated_event, interface, std::move(bounce_buffers_pool), status);
    CHECK_NOT_NULL_AS_EXPECTED(result, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return result;
}

Expected<BounceBufferQueuePtr> VdmaInputStream::init_dma_bounce_buffer_pool(
    VdmaDevice &device, vdma::BoundaryChannelPtr channel, const LayerInfo &edge_layer)
{
    const auto dma_able_alignment = OsUtils::get_dma_able_alignment();
    const auto dma_bounce_buffer_pool_size = channel->get_max_ongoing_transfers(
        LayerInfoUtils::get_layer_transfer_size(edge_layer));

    const auto bounce_buffer_size = std::min(
        static_cast<uint32_t>(dma_able_alignment), LayerInfoUtils::get_layer_transfer_size(edge_layer));

    auto bounce_buffers_pool = make_unique_nothrow<BounceBufferQueue>(dma_bounce_buffer_pool_size);
    CHECK_NOT_NULL(bounce_buffers_pool, HAILO_OUT_OF_HOST_MEMORY);

    for (size_t i = 0; i < dma_bounce_buffer_pool_size; i++) {
        TRY(auto dma_able_buffer, vdma::DmaAbleBuffer::create_by_allocation(bounce_buffer_size));

        auto dma_storage = make_shared_nothrow<DmaStorage>(std::move(dma_able_buffer));
        CHECK_NOT_NULL(dma_storage, HAILO_OUT_OF_HOST_MEMORY);

        TRY(auto buffer, Buffer::create(std::move(dma_storage)));
        TRY(auto mapping, DmaMappedBuffer::create(device, buffer.data(), buffer.size(), HAILO_DMA_BUFFER_DIRECTION_H2D));

        auto bounce_buffer = make_shared_nothrow<BounceBuffer>(BounceBuffer{std::move(buffer), std::move(mapping)});
        CHECK_NOT_NULL(bounce_buffer, HAILO_OUT_OF_HOST_MEMORY);

        CHECK_SUCCESS(bounce_buffers_pool->enqueue(std::move(bounce_buffer)));
    }
    return bounce_buffers_pool;
}

VdmaInputStream::VdmaInputStream(VdmaDevice &device, vdma::BoundaryChannelPtr channel,
                                 const LayerInfo &edge_layer, EventPtr core_op_activated_event,
                                 hailo_stream_interface_t stream_interface, BounceBufferQueuePtr &&bounce_buffers_pool,
                                 hailo_status &status) :
    AsyncInputStreamBase(edge_layer, std::move(core_op_activated_event), status),
    m_device(device),
    m_bounce_buffers_pool(std::move(bounce_buffers_pool)),
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
    m_channel->deactivate();
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
    const auto frame_size = get_frame_size();

    // Since this calc if for aligned transfers, we don't need to factor in the bounce buffer
    constexpr auto NO_BOUNCE_BUFFER = false;
    const auto max_transfers_in_desc_list = m_channel->get_desc_list().max_transfers(static_cast<uint32_t>(frame_size), NO_BOUNCE_BUFFER);

    const auto max_ongoing_transfers = m_channel->get_max_ongoing_transfers(frame_size);
    if (max_transfers_in_desc_list < max_ongoing_transfers) {
        // In this case we don't bind, since the descriptor list isn't big enough to hold all the buffers.
        // (otherwise pending_transfers_queue_in_use would be false)
        TRY(auto stream_buffer_pool, QueuedStreamBufferPool::create(max_ongoing_transfers, frame_size,
            BufferStorageParams::create_dma()));

        return std::unique_ptr<StreamBufferPool>(std::move(stream_buffer_pool));
    } else {
        // We can fit all the buffers in the descriptor list, so we can bind them statically.
        TRY(auto circular_pool, CircularStreamBufferPool::create(m_device, HAILO_DMA_BUFFER_DIRECTION_H2D,
            m_channel->get_desc_list().desc_page_size(), m_channel->get_desc_list().count(), frame_size));

        // Bind the buffer to the channel to avoid the need to do it on every transfer.
        CHECK_SUCCESS(m_channel->bind_buffer(circular_pool->get_base_buffer()));

        return std::unique_ptr<StreamBufferPool>(std::move(circular_pool));
    }
}

size_t VdmaInputStream::get_max_ongoing_transfers() const
{
    return m_channel->get_max_ongoing_transfers(get_frame_size());
}

Expected<TransferRequest> VdmaInputStream::align_transfer_request(TransferRequest &&transfer_request)
{
    const auto dma_alignment = OsUtils::get_dma_able_alignment();
    std::vector<TransferBuffer> transfer_buffers;
    TRY(auto base_buffer, transfer_request.transfer_buffers[0].base_buffer());
    const auto buffer_address = base_buffer.data();
    const auto buffer_size = transfer_request.transfer_buffers[0].size();

    TRY(const auto dma_able_bounce_buffer, m_bounce_buffers_pool->dequeue());

    // If buffer size is larger than alignment size - will create bounce buffer for non aligned buffer part and then use
    // User's buffer from aligned address - otherwise will create bounce buffer size of user buffer and copy whole frame
    if (buffer_size > dma_alignment) {
        transfer_buffers.reserve(2);

        // Get first aligned address in user buffer
        const auto aligned_user_buffer_addr = HailoRTCommon::align_to(reinterpret_cast<size_t>(buffer_address), dma_alignment);
        const auto bounce_buffer_exact_size = aligned_user_buffer_addr - reinterpret_cast<size_t>(buffer_address);
        const auto user_buffer_size = buffer_size - bounce_buffer_exact_size;

        // Create another transfer buffer with same base address but exact size for actual transfer
        auto dma_able_exact_bounce_buffer = TransferBuffer(MemoryView(dma_able_bounce_buffer->buffer_storage),
            bounce_buffer_exact_size, 0);
        dma_able_exact_bounce_buffer.copy_from(MemoryView(buffer_address, bounce_buffer_exact_size));
        transfer_buffers.emplace_back(dma_able_exact_bounce_buffer);
        transfer_buffers.emplace_back(MemoryView(reinterpret_cast<uint8_t*>(aligned_user_buffer_addr), user_buffer_size));
    } else {
        auto dma_able_exact_bounce_buffer = TransferBuffer(MemoryView(dma_able_bounce_buffer->buffer_storage), buffer_size, 0);
        dma_able_exact_bounce_buffer.copy_from(MemoryView(buffer_address, buffer_size));
        transfer_buffers.emplace_back(dma_able_exact_bounce_buffer);
    }

    auto wrapped_callback = [user_callback=transfer_request.callback,
                             dma_able_bounce_buffer=std::move(dma_able_bounce_buffer), this](hailo_status callback_status) mutable {
        m_bounce_buffers_pool->enqueue(std::move(dma_able_bounce_buffer));
        user_callback(callback_status);
    };

    return TransferRequest(std::move(transfer_buffers), wrapped_callback);
}

hailo_status VdmaInputStream::bind_buffer(TransferRequest &&transfer_request)
{
    m_channel->remove_buffer_binding();
    if (TransferBufferType::MEMORYVIEW == transfer_request.transfer_buffers[0].type()) {
        const auto is_request_aligned = transfer_request.transfer_buffers[0].is_aligned_for_dma();
        if (!is_request_aligned) {
            // Best effort, if buffer is not aligned - will program descriptors later
            return HAILO_SUCCESS;
        }
    }

    return m_channel->map_and_bind_buffer(transfer_request.transfer_buffers[0]);
}

hailo_status VdmaInputStream::write_async_impl(TransferRequest &&transfer_request)
{
    TRACE(FrameDequeueH2DTrace, m_device.get_dev_id(), m_core_op_handle, name());

    if (transfer_request.transfer_buffers[0].type() == TransferBufferType::DMABUF) {
        return m_channel->launch_transfer(std::move(transfer_request));
    } else {
        const auto is_request_aligned = transfer_request.transfer_buffers[0].is_aligned_for_dma();
        if (is_request_aligned) {
            return m_channel->launch_transfer(std::move(transfer_request));
        } else {
            auto realigned_transfer_request = align_transfer_request(std::move(transfer_request));
            CHECK_EXPECTED_AS_STATUS(realigned_transfer_request);
            return m_channel->launch_transfer(realigned_transfer_request.release());
        }
    }
}

hailo_status VdmaInputStream::activate_stream_impl()
{
    return m_channel->activate();
}

hailo_status VdmaInputStream::deactivate_stream_impl()
{
    m_channel->deactivate();
    return HAILO_SUCCESS;
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
    m_core_op_handle(INVALID_CORE_OP_HANDLE),
    m_d2h_callback(default_d2h_callback)
{
    // Check status for base class c'tor
    if (HAILO_SUCCESS != status) {
        return;
    }

    status = HAILO_SUCCESS;
}

VdmaOutputStream::~VdmaOutputStream()
{
    m_channel->deactivate();
}

hailo_stream_interface_t VdmaOutputStream::get_interface() const
{
    return m_interface;
}

Expected<std::unique_ptr<StreamBufferPool>> VdmaOutputStream::allocate_buffer_pool()
{
    // Since this calc if for aligned transfers, we don't need to factor in the bounce buffer
    constexpr auto NO_BOUNCE_BUFFER = false;
    const auto max_transfers_in_desc_list = m_channel->get_desc_list().max_transfers(m_transfer_size, NO_BOUNCE_BUFFER);

    const auto max_ongoing_transfers = m_channel->get_max_ongoing_transfers(m_transfer_size);
    if (max_transfers_in_desc_list < max_ongoing_transfers) {
        // In this case we don't bind, since the descriptor list isn't big enough to hold all the buffers.
        // (otherwise pending_transfers_queue_in_use would be false)
        TRY(auto stream_buffer_pool, QueuedStreamBufferPool::create(max_ongoing_transfers, m_transfer_size,
            BufferStorageParams::create_dma()));

        return std::unique_ptr<StreamBufferPool>(std::move(stream_buffer_pool));
    } else  {
        // We can fit all the buffers in the descriptor list, so we can bind them statically.
        TRY(auto circular_pool, CircularStreamBufferPool::create(m_device, HAILO_DMA_BUFFER_DIRECTION_D2H,
            m_channel->get_desc_list().desc_page_size(), m_channel->get_desc_list().count(), m_transfer_size));

        // Bind the buffer to the channel to avoid the need to do it on every transfer.
        CHECK_SUCCESS(m_channel->bind_buffer(circular_pool->get_base_buffer()));

        return std::unique_ptr<StreamBufferPool>(std::move(circular_pool));
    }
}

size_t VdmaOutputStream::get_max_ongoing_transfers() const
{
    return m_channel->get_max_ongoing_transfers(m_transfer_size);
}

Expected<TransferRequest> VdmaOutputStream::align_transfer_request_only_end(TransferRequest &&transfer_request)
{
    // Create 2 buffers - one for the aligned part and one for the unaligned part.
    // The unaligned part will be copied to the bounce buffer and the aligned part will be transferred directly.
    // The bounce buffer will be kept alive and copied back to the user buffer in the callback.
    std::vector<TransferBuffer> transfer_buffers;
    TRY(auto base_buffer, transfer_request.transfer_buffers[0].base_buffer());
    auto user_buffer_size = transfer_request.transfer_buffers[0].size();
    auto bounce_buffer_size = user_buffer_size % OsUtils::get_dma_able_alignment();
    CHECK(bounce_buffer_size > 0, HAILO_INVALID_ARGUMENT, "Buffer size is already aligned");
    auto aligned_buffer_size = user_buffer_size - bounce_buffer_size;

    if (0 != aligned_buffer_size) {
        transfer_buffers.emplace_back(MemoryView(base_buffer.data(), aligned_buffer_size));
    }

    TRY(auto bounce_buffer, Buffer::create_shared(bounce_buffer_size, BufferStorageParams::create_dma())); // TODO: HRT-15660
    transfer_buffers.emplace_back(MemoryView(bounce_buffer->data(), bounce_buffer_size));

    auto callback = [
        dst = base_buffer,
        src = bounce_buffer,
        user_callback = transfer_request.callback,
        offset = user_buffer_size - bounce_buffer_size](hailo_status callback_status) mutable {
            if (HAILO_SUCCESS == callback_status) {
                memcpy(dst.data() + offset, src->data(), src->size());
            }
            user_callback(callback_status);
    };

    return TransferRequest(std::move(transfer_buffers), callback);
}

Expected<TransferRequest> VdmaOutputStream::align_transfer_request(TransferRequest &&transfer_request)
{
    // Allocate a bounce buffer and store it inside the lambda to keep it alive until not needed.
    auto bounce_buffer_exp = Buffer::create_shared(transfer_request.transfer_buffers[0].size(), BufferStorageParams::create_dma());
    CHECK_EXPECTED(bounce_buffer_exp);
    auto bounce_buffer = bounce_buffer_exp.release();

    TRY(auto base_buffer, transfer_request.transfer_buffers[0].base_buffer()); // TODO: HRT-15660
    auto wrapped_callback = [unaligned_user_buffer = base_buffer,
            bounce_buffer=bounce_buffer, user_callback=transfer_request.callback](hailo_status callback_status) {
        memcpy(const_cast<uint8_t*>(unaligned_user_buffer.data()), bounce_buffer->data(), unaligned_user_buffer.size());
        user_callback(callback_status);
    };

    return TransferRequest(MemoryView(bounce_buffer->data(), bounce_buffer->size()), wrapped_callback);
}

hailo_status VdmaOutputStream::read_async_impl(TransferRequest &&transfer_request)
{
    if (HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP != m_stream_info.format.order) {
        // On NMS stream we trace EnqueueD2H inside nms_stream
        transfer_request.callback = [original_callback=transfer_request.callback, d2h_callback=m_d2h_callback, this](hailo_status status) {
            if ((HAILO_SUCCESS == status) && (INVALID_CORE_OP_HANDLE != m_core_op_handle)) {
                TRACE(FrameEnqueueD2HTrace, m_device.get_dev_id(), m_core_op_handle, name());
            }
            d2h_callback(status);
            original_callback(status);
        };
    }
    if (transfer_request.transfer_buffers[0].type() == TransferBufferType::DMABUF) {
        return m_channel->launch_transfer(std::move(transfer_request));
    } else {
        const auto is_request_aligned = transfer_request.transfer_buffers[0].is_aligned_for_dma();
        if (is_request_aligned) {
            bool can_skip_alignment = true; // TODO :change back to false when HRT-15741 is resolved, then implement HRT-15731
            bool owned_by_user = (StreamBufferMode::OWNING != buffer_mode()); // Buffers owned by HRT are always aligned
            TRY(auto is_request_end_aligned, transfer_request.is_request_end_aligned());
            if (owned_by_user && !can_skip_alignment && !is_request_end_aligned) {
                TRY(transfer_request, align_transfer_request_only_end(std::move(transfer_request)));
            }
            return m_channel->launch_transfer(std::move(transfer_request));
        } else {
            // In case of read unaligned - don't support using users buffer - so well allocate complete new buffer size of user's buffer
            TRY(auto base_buffer, transfer_request.transfer_buffers[0].base_buffer());
            LOGGER__WARNING("read_async() was provided an unaligned buffer (address=0x{:x}), which causes performance degradation. Use buffers algined to {} bytes for optimal performance",
                reinterpret_cast<size_t>(base_buffer.data()), OsUtils::get_dma_able_alignment());

            auto realigned_transfer_request = align_transfer_request(std::move(transfer_request));
            CHECK_EXPECTED_AS_STATUS(realigned_transfer_request);
            return m_channel->launch_transfer(realigned_transfer_request.release());
        }
    }
}

hailo_status VdmaOutputStream::bind_buffer(TransferRequest &&transfer_request)
{
    m_channel->remove_buffer_binding();
    if (TransferBufferType::MEMORYVIEW == transfer_request.transfer_buffers[0].type()) {
        const auto is_request_aligned = transfer_request.transfer_buffers[0].is_aligned_for_dma();
        TRY(auto is_request_end_aligned, transfer_request.is_request_end_aligned());
        if (!is_request_aligned || !is_request_end_aligned) {
            // Best effort, if buffer is not aligned - will program descriptors later
            return HAILO_SUCCESS;
        }
    }

    return m_channel->map_and_bind_buffer(transfer_request.transfer_buffers[0]);
}

hailo_status VdmaOutputStream::activate_stream_impl()
{
    return m_channel->activate();
}

hailo_status VdmaOutputStream::deactivate_stream_impl()
{
    m_channel->deactivate();
    return HAILO_SUCCESS;
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

void VdmaOutputStream::set_d2h_callback(std::function<void(hailo_status)> callback)
{
    m_d2h_callback = callback;
}

} /* namespace hailort */
