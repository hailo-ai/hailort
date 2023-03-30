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

hailo_status AsyncChannel::transfer(std::shared_ptr<DmaMappedBuffer> buffer, const TransferDoneCallback &user_callback, void *opaque)
{
    CHECK_NOT_NULL(buffer, HAILO_INVALID_ARGUMENT);
    CHECK(0 != buffer->size(), HAILO_INVALID_ARGUMENT);

    std::lock_guard<RecursiveSharedMutex> state_guard(m_state->mutex());
    if (m_state->m_is_aborted) {
        LOGGER__INFO("Tried to write to aborted channel {}", m_channel_id);
        return HAILO_STREAM_ABORTED_BY_USER;
    }

    hailo_status status = HAILO_UNINITIALIZED;
    if (Direction::H2D == m_direction) {
        status = transfer_h2d(buffer, user_callback, opaque);
    } else {
        status = transfer_d2h(buffer, user_callback, opaque);
    }

    if (HAILO_STREAM_NOT_ACTIVATED == status) {
        LOGGER__INFO("Transfer failed because Channel {} is not activated", m_channel_id);
        return HAILO_STREAM_NOT_ACTIVATED;
    } 
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Transfer failed for channel {} with status {}", m_channel_id, status);
        return status;
    }

    return HAILO_SUCCESS;
}

hailo_status AsyncChannel::cancel_pending_transfers()
{
    std::lock_guard<RecursiveSharedMutex> state_guard(m_state->mutex());
    for (auto &pending_buffer_info : m_state->m_pending_buffers) {
        if (pending_buffer_info.on_transfer_done) {
            pending_buffer_info.on_transfer_done(pending_buffer_info.buffer,
                hailo_async_transfer_completion_info_t{HAILO_STREAM_NOT_ACTIVATED},
                pending_buffer_info.opaque);
            // Release our references to user buffer, callback and opaque
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

hailo_status AsyncChannel::transfer(void */* buf */, size_t /* count */)
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

hailo_status AsyncChannel::transfer_d2h(std::shared_ptr<DmaMappedBuffer> buffer, const TransferDoneCallback &user_callback, void *opaque)
{
    InterruptsDomain first_desc_interrupts_domain = InterruptsDomain::NONE;
    // Provide FW interrupt only in the end of the last transfer in the batch
    InterruptsDomain last_desc_interrupts_domain = (m_state->m_accumulated_transfers + 1 == m_transfers_per_axi_intr) ? 
        InterruptsDomain::BOTH : InterruptsDomain::HOST;

    const auto status = prepare_descriptors(buffer, user_callback, opaque, first_desc_interrupts_domain, last_desc_interrupts_domain);
    CHECK_SUCCESS(status);

    m_state->m_accumulated_transfers = (m_state->m_accumulated_transfers + 1) % m_transfers_per_axi_intr;

    return HAILO_SUCCESS;
}

hailo_status AsyncChannel::transfer_h2d(std::shared_ptr<DmaMappedBuffer> buffer, const TransferDoneCallback &user_callback, void *opaque)
{
    // For h2d, only the host need to get transfer done interrupts
    InterruptsDomain last_desc_interrupts_domain = InterruptsDomain::HOST;
    // If we measure latency, we need interrupt on the first descriptor
    InterruptsDomain first_desc_interrupts_domain = (m_latency_meter != nullptr) ?
        InterruptsDomain::HOST : InterruptsDomain::NONE;

    return prepare_descriptors(buffer, user_callback, opaque, first_desc_interrupts_domain, last_desc_interrupts_domain);
}

hailo_status AsyncChannel::prepare_descriptors(std::shared_ptr<DmaMappedBuffer> buffer, const TransferDoneCallback &user_callback,
    void *opaque, InterruptsDomain first_desc_interrupts_domain, InterruptsDomain last_desc_interrupts_domain)
{
    const auto desired_desc_num = m_desc_list->descriptors_in_buffer(buffer->size());
    CHECK(desired_desc_num <= MAX_DESCS_COUNT, HAILO_INTERNAL_FAILURE);
    const uint16_t desc_num = static_cast<uint16_t>(desired_desc_num);

    int num_available = get_num_available();
    int num_processed = CB_TAIL(m_state->m_descs);
    int num_free = CB_AVAIL(m_state->m_descs, num_available, num_processed);
    if (num_free < desc_num) {
        // TODO: do we want to block here?
        return HAILO_OUT_OF_DESCRIPTORS;
    }

    const auto status = m_desc_list->configure_to_use_buffer(*buffer, m_channel_id, num_available);
    CHECK_SUCCESS(status);
    if (nullptr != m_latency_meter) {
        // Program first descriptor
        m_desc_list->program_single_descriptor((*m_desc_list)[num_available], m_desc_list->desc_page_size(),
            first_desc_interrupts_domain);
    }
    auto actual_desc_count = m_desc_list->program_last_descriptor(buffer->size(), last_desc_interrupts_domain,
        num_available, true);
    CHECK_EXPECTED_AS_STATUS(actual_desc_count, "Failed to program desc_list for channel {}", m_channel_id);
    assert (actual_desc_count.value() == desc_num);
    int last_desc_avail = ((num_available + desc_num - 1) & m_state->m_descs.size_mask);

    const auto callback = [this, user_callback](std::shared_ptr<DmaMappedBuffer> buffer, const hailo_async_transfer_completion_info_t &status, void *opaque) {
        user_callback(buffer, status, opaque);

        // opaque is only for the user callback
        static constexpr void *NO_CONTEXT = nullptr;
        m_transfer_done_callback(buffer, status, NO_CONTEXT);
    };

    m_state->add_pending_buffer(num_available, last_desc_avail, m_direction, callback, buffer, opaque);
    return inc_num_available(desc_num);
}

} /* namespace vdma */
} /* namespace hailort */
