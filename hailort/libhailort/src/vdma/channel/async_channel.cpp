/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file async_channel.cpp
 * @brief Implementation of the AsyncChannel class
 **/

#include "async_channel.hpp"
#include "hailo/hailort.h"
#include "hailo/hailort_common.hpp"

namespace hailort
{
namespace vdma
{

Expected<AsyncChannelPtr> AsyncChannel::create(vdma::ChannelId channel_id, Direction direction,
    HailoRTDriver &driver, uint32_t descs_count, uint16_t desc_page_size, const std::string &stream_name,
    LatencyMeterPtr latency_meter, uint16_t transfers_per_axi_intr)
{
    hailo_status status = HAILO_UNINITIALIZED;
    auto channel_ptr = make_shared_nothrow<vdma::AsyncChannel>(channel_id, direction, driver, descs_count,
        desc_page_size, stream_name, latency_meter, transfers_per_axi_intr, status);
    CHECK_NOT_NULL_AS_EXPECTED(channel_ptr, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status, "Failed creating AsyncChannel");
    return channel_ptr;
}

AsyncChannel::AsyncChannel(vdma::ChannelId channel_id, Direction direction, HailoRTDriver &driver,
                           uint32_t descs_count, uint16_t desc_page_size, const std::string &stream_name,
                           LatencyMeterPtr latency_meter, uint16_t transfers_per_axi_intr,
                           hailo_status &status) :
    BoundaryChannel(BoundaryChannel::Type::ASYNC, channel_id, direction, driver, descs_count, desc_page_size,
                    stream_name, latency_meter, transfers_per_axi_intr, status)
{
    // Check that base constructor was successful
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed building Vdma Channel base class");
        return;
    }

    status = HAILO_SUCCESS;
}

hailo_status AsyncChannel::transfer_async(TransferRequest &&transfer_request)
{
    CHECK_ARG_NOT_NULL(transfer_request.buffer.data());
    CHECK(0 != transfer_request.buffer.size(), HAILO_INVALID_ARGUMENT, "Buffer is empty (size 0)");

    auto is_new_mapping = true;
    MappedBufferPtr mapped_buffer = nullptr;
    if (transfer_request.mapped_buffer != nullptr) {
        assert(transfer_request.buffer.data() == transfer_request.mapped_buffer->data());
        assert(transfer_request.buffer.size() == transfer_request.mapped_buffer->size());
        CHECK(transfer_request.mapped_buffer->storage().type() == BufferStorage::Type::DMA, HAILO_INVALID_ARGUMENT,
            "Buffer must be dma-able (provided buffer type {})", transfer_request.mapped_buffer->storage().type());

        // Map if not already mapped
        const auto mapping_direction = (m_direction == Direction::H2D) ? HAILO_DMA_BUFFER_DIRECTION_H2D : HAILO_DMA_BUFFER_DIRECTION_D2H;
        auto is_new_mapping_exp = transfer_request.mapped_buffer->storage().dma_map(m_driver, mapping_direction);
        CHECK_EXPECTED_AS_STATUS(is_new_mapping_exp);
        is_new_mapping = is_new_mapping_exp.release();

        auto mapped_buffer_exp = transfer_request.mapped_buffer->storage().get_dma_mapped_buffer(m_driver.device_id());
        CHECK_EXPECTED_AS_STATUS(mapped_buffer_exp);
        mapped_buffer = mapped_buffer_exp.release();
    } else {
        auto mapped_buffer_exp = MappedBuffer::create_shared(m_driver, m_direction,
            transfer_request.buffer.size(), transfer_request.buffer.data());
        CHECK_EXPECTED_AS_STATUS(mapped_buffer_exp);
        mapped_buffer = mapped_buffer_exp.release();
    }

    if (!is_new_mapping) {
        // The buffer has been previously mapped, so it needs to be sync'd from host to device.
        // * If the buffer is mapped H2D/BOTH, then synchronize will make sure the device "sees" the most "up to date"
        //   version of the buffer.
        // * If the buffer is mapped D2H, it might have been changed by the host between the time it was mapped and the
        //   current async transfer. Synchronizing will transfer ownership to the device, so that when the transfer is
        //   complete, the host will "see" an "up to date" version of the buffer.
        auto status = mapped_buffer->synchronize(HailoRTDriver::DmaSyncDirection::TO_DEVICE);
        CHECK_SUCCESS(status);
    }

    std::lock_guard<RecursiveSharedMutex> state_guard(m_state->mutex());
    if (!m_state->m_is_channel_activated) {
        return HAILO_STREAM_NOT_ACTIVATED;
    }
    if (m_state->m_is_aborted) {
        LOGGER__INFO("Tried to write to aborted channel {}", m_channel_id);
        return HAILO_STREAM_ABORTED_BY_USER;
    }

    if (Direction::H2D == m_direction) {
        return transfer_h2d(mapped_buffer, transfer_request.callback);
    } else {
        return transfer_d2h(mapped_buffer, transfer_request.callback);
    }
}

hailo_status AsyncChannel::cancel_pending_transfers()
{
    std::lock_guard<RecursiveSharedMutex> state_guard(m_state->mutex());
    for (auto &pending_buffer_info : m_state->m_pending_buffers) {
        if (pending_buffer_info.on_transfer_done) {
            pending_buffer_info.on_transfer_done(HAILO_STREAM_ABORTED_BY_USER);
            // Release our references to user buffer and callback.
            pending_buffer_info = PendingBuffer{};
        } else {
            LOGGER__WARNING("No transfer done callback found for transfer (channel {}); skipping", m_channel_id);
        }
    }

    return HAILO_SUCCESS;
}

hailo_status AsyncChannel::complete_channel_activation(uint32_t /* transfer_size */, bool /* resume_pending_transfers */)
{
    return HAILO_SUCCESS;
}

hailo_status AsyncChannel::complete_channel_deactivation()
{
    // Note: We don't reset channel counters here as the resource manager will signal pending transfers
    //       (i.e. transfers in m_pending_buffers) via cancel_pending_async_transfers.
    //       The counters are reset in the channel activation
    return HAILO_SUCCESS;
}

hailo_status AsyncChannel::transfer_sync(void */* buf */, size_t /* count */, std::chrono::milliseconds /* timeout */)
{
    return HAILO_NOT_IMPLEMENTED;
}

hailo_status AsyncChannel::write_buffer(const MemoryView &/* buffer */, std::chrono::milliseconds /* timeout */,
    const std::function<bool()> &/* should_cancel */)
{
    return HAILO_NOT_IMPLEMENTED;
}

hailo_status AsyncChannel::send_pending_buffer()
{
    return HAILO_NOT_IMPLEMENTED;
}

void AsyncChannel::notify_all()
{}

Expected<BoundaryChannel::BufferState> AsyncChannel::get_buffer_state()
{
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

Expected<size_t> AsyncChannel::get_h2d_pending_frames_count()
{
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

Expected<size_t> AsyncChannel::get_d2h_pending_descs_count()
{
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

hailo_status AsyncChannel::transfer_d2h(MappedBufferPtr mapped_buffer, const InternalTransferDoneCallback &callback)
{
    InterruptsDomain first_desc_interrupts_domain = InterruptsDomain::NONE;
    // Provide FW interrupt only in the end of the last transfer in the batch
    InterruptsDomain last_desc_interrupts_domain = (m_state->m_accumulated_transfers + 1 == m_transfers_per_axi_intr) ?
        InterruptsDomain::BOTH : InterruptsDomain::HOST;

    const auto status = prepare_descriptors(mapped_buffer, callback, first_desc_interrupts_domain,
        last_desc_interrupts_domain);
    if (HAILO_QUEUE_IS_FULL == status) {
        return status;
    }
    CHECK_SUCCESS(status);

    m_state->m_accumulated_transfers = (m_state->m_accumulated_transfers + 1) % m_transfers_per_axi_intr;

    return HAILO_SUCCESS;
}

hailo_status AsyncChannel::transfer_h2d(MappedBufferPtr mapped_buffer, const InternalTransferDoneCallback &callback)
{
    // For h2d, only the host need to get transfer done interrupts
    InterruptsDomain last_desc_interrupts_domain = InterruptsDomain::HOST;
    // If we measure latency, we need interrupt on the first descriptor
    InterruptsDomain first_desc_interrupts_domain = (m_latency_meter != nullptr) ?
        InterruptsDomain::HOST : InterruptsDomain::NONE;

    return prepare_descriptors(mapped_buffer, callback, first_desc_interrupts_domain,
        last_desc_interrupts_domain);
}

hailo_status AsyncChannel::prepare_descriptors(MappedBufferPtr mapped_buffer,
    const InternalTransferDoneCallback &callback, InterruptsDomain first_desc_interrupts_domain,
    InterruptsDomain last_desc_interrupts_domain)
{
    assert(mapped_buffer != nullptr);

    const auto desired_desc_num = m_desc_list->descriptors_in_buffer(mapped_buffer->size());
    CHECK(desired_desc_num <= MAX_DESCS_COUNT, HAILO_INTERNAL_FAILURE);
    const uint16_t desc_num = static_cast<uint16_t>(desired_desc_num);

    const auto num_available = get_num_available();
    const auto num_processed = CB_TAIL(m_state->m_descs);
    const auto num_free = CB_AVAIL(m_state->m_descs, num_available, num_processed);
    if (num_free < desc_num) {
        return HAILO_QUEUE_IS_FULL;
    }

    const auto status = m_desc_list->configure_to_use_buffer(*mapped_buffer, m_channel_id, num_available);
    CHECK_SUCCESS(status);

    if (nullptr != m_latency_meter) {
        // Program first descriptor
        m_desc_list->program_single_descriptor((*m_desc_list)[num_available], m_desc_list->desc_page_size(),
            first_desc_interrupts_domain);
    }
    auto actual_desc_count = m_desc_list->program_last_descriptor(mapped_buffer->size(), last_desc_interrupts_domain,
        num_available);
    CHECK_EXPECTED_AS_STATUS(actual_desc_count, "Failed to program desc_list for channel {}", m_channel_id);
    assert (actual_desc_count.value() == desc_num);
    assert(desc_num > 0);
    const auto last_desc_avail = static_cast<uint16_t>((num_available + desc_num - 1) & m_state->m_descs.size_mask);

    const auto wrapped_callback = [this, mapped_buffer, callback](hailo_status callback_status) {
        if (HAILO_SUCCESS != callback_status) {
            // No need to sync, just forward the callback.
            callback(callback_status);
            return;
        }

        // The device may only change the contents of mapped_buffer, if it was mapped in Direction::D2H
        // (not Direction::BOTH because channels are either D2H or H2D). Hence, we don't need to sync H2D
        // buffers to the host (the host's "view" of the buffer is "up to date").
        if (m_direction == Direction::D2H) {
            auto sync_status = mapped_buffer->synchronize(HailoRTDriver::DmaSyncDirection::TO_HOST);
            if (sync_status != HAILO_SUCCESS) {
                LOGGER__ERROR("Failed to sync buffer to host with status {}", sync_status);
                callback_status = sync_status;
            }
        }

        callback(callback_status);
    };

    m_state->add_pending_buffer(num_available, last_desc_avail, m_direction, wrapped_callback, mapped_buffer);
    return inc_num_available(desc_num);
}

} /* namespace vdma */
} /* namespace hailort */
