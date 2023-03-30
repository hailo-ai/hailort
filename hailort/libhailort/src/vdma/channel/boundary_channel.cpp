/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file boundary_channel.cpp
 * @brief BoundaryChannel - Base class functionality
 **/

#include "hailo/hailort_common.hpp"

#include "common/os_utils.hpp"

#include "vdma/channel/boundary_channel.hpp"
#include "vdma/channel/buffered_channel.hpp"
#include "vdma/channel/async_channel.hpp"

#include <list>
#include <chrono>
#include <thread>
#include <iostream>


namespace hailort {
namespace vdma {


Expected<BoundaryChannelPtr> BoundaryChannel::create(vdma::ChannelId channel_id, Direction direction,
    HailoRTDriver &driver, uint32_t descs_count, uint16_t desc_page_size, const std::string &stream_name,
    LatencyMeterPtr latency_meter, uint16_t transfers_per_axi_intr, Type type)
{
    switch (type)
    {
    case Type::BUFFERED:
        {
            auto buffered_channel = BufferedChannel::create(channel_id, direction, driver, descs_count, desc_page_size,
                stream_name, latency_meter, transfers_per_axi_intr);
            CHECK_EXPECTED(buffered_channel);

            // Upcasting
            return std::static_pointer_cast<BoundaryChannel>(buffered_channel.value());
        }
    
    case Type::ASYNC:
        {
            auto async_channel = AsyncChannel::create(channel_id, direction, driver, descs_count, desc_page_size,
                stream_name, latency_meter, transfers_per_axi_intr);
            CHECK_EXPECTED(async_channel);

            // Upcasting
            return std::static_pointer_cast<BoundaryChannel>(async_channel.value());
        }
    }

    // Shouldn't get here
    return make_unexpected(HAILO_INVALID_ARGUMENT);
}

BoundaryChannel::BoundaryChannel(Type type, vdma::ChannelId channel_id, Direction direction, HailoRTDriver &driver,
                                 uint32_t descs_count, uint16_t desc_page_size, const std::string &stream_name,
                                 LatencyMeterPtr latency_meter, uint16_t transfers_per_axi_intr, hailo_status &status) :
    ChannelBase(channel_id, direction, driver, descs_count, desc_page_size, stream_name, latency_meter, status),
    m_type(type),
    m_user_interrupt_callback(ignore_processing_complete),
    m_transfers_per_axi_intr(transfers_per_axi_intr)
{
    // Check that base constructor was successful
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed building vdma channel base class");
        return;
    }

    if (Direction::BOTH == direction) {
        LOGGER__ERROR("Boundary channels must be unidirectional");
        status = HAILO_INVALID_ARGUMENT;
        return;
    }

    if (m_transfers_per_axi_intr == 0) {
        LOGGER__ERROR("Invalid transfers per axi interrupt");
        status = HAILO_INVALID_ARGUMENT;
        return;
    }

    m_transfer_done_callback = [this](std::shared_ptr<DmaMappedBuffer>, const hailo_async_transfer_completion_info_t &, void *) {
        m_user_interrupt_callback(1);
    };
}

void BoundaryChannel::clear_pending_buffers_descriptors()
{
    for (const auto &pending_buffer : m_state->m_pending_buffers) {
        const auto last_desc_index = pending_buffer.last_desc;

        // Clear relevant descriptors from previous transfer
        if (nullptr != m_latency_meter) {
            const auto latency_desc_index = pending_buffer.latency_measure_desc;
            m_desc_list->clear_descriptor(latency_desc_index);
        }
        m_desc_list->clear_descriptor(last_desc_index);
    }
}

hailo_status BoundaryChannel::trigger_channel_completion(uint16_t hw_num_processed)
{
    size_t processed_no = 0;

    {
        // NOTE: right now, we can retake the 'completion' descriptor for a new transfer before handling the interrupt.
        //      we should have our own pointers indicating whats free instead of reading from HW.
        // TODO: consider calculating the last descriptor using the src_desc_avail and src_desc_proc instead of using
        // status?
        // TODO: we might free a pending buffer which we didn't get an interrupt for yet. we should still handle this
        // situation correctly.

        std::lock_guard<RecursiveSharedMutex> state_guard(m_state->mutex());

        // Although the hw_num_processed should be a number between 0 and m_descs.size-1, if m_desc.size < 0x10000
        // (the maximum desc size), the actual hw_num_processed is a number between 1 and m_descs.size. Therefore the
        // value can be m_descs.size, in this case we change it to zero.
        hw_num_processed = static_cast<uint16_t>(hw_num_processed & m_state->m_descs.size_mask);

        if (m_state->m_is_aborted) {
            return HAILO_STREAM_ABORTED_BY_USER;
        }

        if (!m_state->m_is_channel_activated) {
            return HAILO_STREAM_NOT_ACTIVATED;
        }

        if (m_latency_meter != nullptr) {
            // The latency meter gets an updated hw_num_processed via a call to vdma_interrupts_read_timestamps
            // (the desc index of the last measured timestamp returned from that ioctl). Since update_latency_meter
            // processed m_pending_buffers based on this hw_num_processed, and this function (i.e.
            // trigger_channel_completion) also processes m_pending_buffers based on the value of hw_num_processed,
            // we want the two to be the same. Hence, we'll use the more up to date num_processed returned by
            // update_latency_meter.
            // TODO: fix update_latency_meter flow (HRT-10284)
            auto latency_meter_hw_num_processed = update_latency_meter();
            CHECK_EXPECTED_AS_STATUS(latency_meter_hw_num_processed);
            hw_num_processed = latency_meter_hw_num_processed.value();
        }

        const auto last_num_processed = static_cast<uint16_t>(CB_TAIL(m_state->m_descs));

        // Calculate pending_buffers_count before iteration, because the iteration removes done transfers
        const auto pending_buffers_count = m_state->m_pending_buffers.size();
        for (size_t i = 0; i < pending_buffers_count; i++) {
            auto &last_pending_buffer_info = m_state->m_pending_buffers.front();
            const auto last_desc_index = static_cast<uint16_t>(last_pending_buffer_info.last_desc);
            // Transfer is complete if its last descriptor is in [last_num_processed, hw_num_processed) or
            // the the buffer is empty (hw_num_processed == get_num_available())
            const bool is_complete = is_desc_between(last_num_processed, hw_num_processed, last_desc_index) || 
                (hw_num_processed == get_num_available());

    #ifndef NDEBUG
            static constexpr auto STATUS_MASK = 0xFF;
            static constexpr auto ERROR_BIT = 1;
            const auto status = (*m_desc_list)[last_desc_index].RemainingPageSize_Status & STATUS_MASK;
            CHECK(!is_bit_set(status, ERROR_BIT), HAILO_INTERNAL_FAILURE,
                "Error while processing descriptor {} of DMA {} on board {}.",
                last_desc_index, m_channel_id, m_driver.dev_path());

            // status is read after hw_num_processed, so we want is_complete -> (status == 1).
            assert(!is_complete || ((status & 0x1) == 1));
    #endif

            if (!is_complete) {
                break;
            }

            // Clear relevant descriptors from previous transfer
            if (nullptr != m_latency_meter) {
                const auto latency_desc_index = last_pending_buffer_info.latency_measure_desc;
                m_desc_list->clear_descriptor(latency_desc_index);
            }
            m_desc_list->clear_descriptor(last_desc_index);

            _CB_SET(m_state->m_descs.tail, (last_pending_buffer_info.last_desc + 1) & m_state->m_descs.size_mask);
            last_pending_buffer_info.on_transfer_done(last_pending_buffer_info.buffer,
                hailo_async_transfer_completion_info_t{HAILO_SUCCESS}, last_pending_buffer_info.opaque);
            processed_no++;
            m_state->m_pending_buffers.pop_front();
        }
    }

    if (0 < processed_no) {
        m_state->transfer_buffer_cv().notify_all();
    }

    return HAILO_SUCCESS;
}

hailo_status BoundaryChannel::register_interrupt_callback(const ProcessingCompleteCallback &callback)
{
    std::lock_guard<RecursiveSharedMutex> state_guard(m_state->mutex());
    m_user_interrupt_callback = callback;
    return HAILO_SUCCESS;
}

CONTROL_PROTOCOL__host_buffer_info_t BoundaryChannel::get_boundary_buffer_info(uint32_t transfer_size)
{
    // Boundary channels always have scatter gather buffers
    return VdmaBuffer::get_host_buffer_info(VdmaBuffer::Type::SCATTER_GATHER, m_desc_list->dma_address(), 
        m_desc_list->desc_page_size(), m_desc_list->count(), transfer_size);
}

hailo_status BoundaryChannel::abort()
{
    {
        std::lock_guard<RecursiveSharedMutex> state_guard(m_state->mutex());
        m_state->m_is_aborted = true;
    }

    m_state->transfer_buffer_cv().notify_all();

    return HAILO_SUCCESS;
}

hailo_status BoundaryChannel::clear_abort()
{
    std::lock_guard<RecursiveSharedMutex> state_guard(m_state->mutex());
    m_state->m_is_aborted = false;

    return HAILO_SUCCESS;
}

hailo_status BoundaryChannel::activate(uint32_t transfer_size, bool resume_pending_transfers)
{
    std::lock_guard<RecursiveSharedMutex> state_guard(m_state->mutex());

    CHECK(!m_state->m_is_channel_activated, HAILO_INTERNAL_FAILURE,
        "Vdma channel {} is already activated", m_channel_id);
    m_state->m_is_channel_activated = true;
    clear_pending_buffers_descriptors();
    m_state->reset_counters();

    auto status = complete_channel_activation(transfer_size, resume_pending_transfers);
    if (HAILO_SUCCESS != status) {
        m_state->m_is_channel_activated = false;
        return status;
    }

    return HAILO_SUCCESS;
}

hailo_status BoundaryChannel::deactivate()
{
    std::unique_lock<RecursiveSharedMutex> state_guard(m_state->mutex());

    CHECK(m_state->m_is_channel_activated, HAILO_INTERNAL_FAILURE,
        "Vdma channel {} is not activated", m_channel_id);
    m_state->m_is_channel_activated = false;

    // Reset the user callback, so as not to keep objects provided by the user alive (they may lead to a chain of refs
    // back to this channel causing it to be leaked).
    // Note: PendingBuffers held by m_pending_buffers may still hold copies of the current m_transfer_done_callback,
    //       which in turn holds a reference to *this. Since we stop the m_wait_interrupts_thread there's no risk that
    //       these callbacks will be called and we don't need to reset this callback.
    m_user_interrupt_callback = ignore_processing_complete;

    auto status = complete_channel_deactivation();
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

BoundaryChannel::Type BoundaryChannel::type() const
{
    return m_type;
}

hailo_status BoundaryChannel::flush(const std::chrono::milliseconds &timeout)
{
    if (Direction::D2H == m_direction) {
        // We are not buffering user data
        return HAILO_SUCCESS;
    }

    std::unique_lock<RecursiveSharedMutex> state_guard(m_state->mutex());
    hailo_status status = HAILO_SUCCESS; // Best effort
    bool was_successful = m_state->transfer_buffer_cv().wait_for(state_guard, timeout, [this, &status] () {
        if (m_state->m_is_aborted) {
            status = HAILO_STREAM_ABORTED_BY_USER;
            return true; // return true so that the wait will finish
        }
        return m_state->m_pending_buffers.empty();
    });
    CHECK(was_successful, HAILO_TIMEOUT, "Got HAILO_TIMEOUT while waiting for channel {} interrupts on flush", m_channel_id);
    return status;
}

bool BoundaryChannel::is_ready_for_transfer_h2d(size_t buffer_size)
{
    return has_room_in_desc_list(buffer_size);
}

bool BoundaryChannel::is_ready_for_transfer_d2h(size_t buffer_size)
{
    return has_room_in_desc_list(buffer_size);
}

bool BoundaryChannel::has_room_in_desc_list(size_t buffer_size)
{
    size_t desired_desc_num = m_desc_list->descriptors_in_buffer(buffer_size);
    assert(desired_desc_num <= MAX_DESCS_COUNT);
    int desc_num = static_cast<int>(desired_desc_num);

    if (m_state->m_pending_buffers.full()) {
        return false;
    }

    int num_available = get_num_available();
    int num_processed = CB_TAIL(m_state->m_descs);

    if (desc_num == m_state->m_descs.size) {
        // Special case when the checking if the buffer is empty
        return num_available == num_processed; 
    }

    int num_free = CB_AVAIL(m_state->m_descs, num_available, num_processed);
    if (num_free < desc_num) {
        return false;
    }

    return true;
}

hailo_status BoundaryChannel::wait(size_t buffer_size, std::chrono::milliseconds timeout)
{
    const auto max_transfer_size = m_desc_list->desc_page_size() * m_desc_list->count();
    CHECK(buffer_size < max_transfer_size, HAILO_INVALID_ARGUMENT,
        "Requested transfer size ({}) must be smaller than ({})", buffer_size, max_transfer_size);

    auto is_ready_for_transfer = (Direction::H2D == m_direction) ?
        std::bind(&BoundaryChannel::is_ready_for_transfer_h2d, this, buffer_size) :
        std::bind(&BoundaryChannel::is_ready_for_transfer_d2h, this, buffer_size);

    std::unique_lock<RecursiveSharedMutex> state_guard(m_state->mutex());
    hailo_status status = HAILO_SUCCESS; // Best effort
    bool was_successful = m_state->transfer_buffer_cv().wait_for(state_guard, timeout, [this, is_ready_for_transfer, &status] () {
        if (m_state->m_is_aborted) {
            status = HAILO_STREAM_ABORTED_BY_USER;
            return true; // return true so that the wait will finish
        }

        return is_ready_for_transfer();
    });
    CHECK(was_successful, HAILO_TIMEOUT, "Got HAILO_TIMEOUT while waiting for channel {} interrupts", m_channel_id);
    return status;
}

hailo_status BoundaryChannel::set_transfers_per_axi_intr(uint16_t transfers_per_axi_intr)
{
    CHECK(0 != transfers_per_axi_intr, HAILO_INVALID_ARGUMENT, "Invalid transfers per axi interrupt");
    m_transfers_per_axi_intr = transfers_per_axi_intr;
    return HAILO_SUCCESS;
}

} /* namespace vdma */
} /* namespace hailort */
