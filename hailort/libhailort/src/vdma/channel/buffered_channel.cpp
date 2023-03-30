/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file buffered_channel.cpp
 * @brief Implementation of the BufferedChannel class
 **/

#include "hailo/hailort_common.hpp"

#include "common/logger_macros.hpp"

#include "vdma/channel/buffered_channel.hpp"
#include "vdma/memory/mapped_buffer_factory.hpp"
#include "vdma/memory/mapped_buffer_impl.hpp"
#include "hw_consts.hpp"

#include <list>
#include <chrono>
#include <thread>
#include <iostream>


namespace hailort {
namespace vdma {

Expected<BufferedChannelPtr> BufferedChannel::create(vdma::ChannelId channel_id, Direction direction,
    HailoRTDriver &driver, uint32_t descs_count, uint16_t desc_page_size, const std::string &stream_name,
    LatencyMeterPtr latency_meter, uint16_t transfers_per_axi_intr)
{
    hailo_status status = HAILO_UNINITIALIZED;
    auto channel_ptr = make_shared_nothrow<vdma::BufferedChannel>(channel_id, direction, driver, descs_count,
        desc_page_size, stream_name, latency_meter, transfers_per_axi_intr, status);
    CHECK_NOT_NULL_AS_EXPECTED(channel_ptr, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status, "Failed creating BufferedChannel");

    return channel_ptr;
}

BufferedChannel::BufferedChannel(vdma::ChannelId channel_id, Direction direction, HailoRTDriver &driver,
                                 uint32_t descs_count, uint16_t desc_page_size, const std::string &stream_name,
                                 LatencyMeterPtr latency_meter, uint16_t transfers_per_axi_intr, hailo_status &status) :
    BoundaryChannel(BoundaryChannel::Type::BUFFERED, channel_id, direction, driver, descs_count, desc_page_size,
                    stream_name, latency_meter, transfers_per_axi_intr, status),
    m_channel_buffer(nullptr),
    m_pending_buffers_sizes(0),
    m_pending_num_avail_offset(0)
{
    // Check that base constructor was successful
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed building boundary channel base class");
        return;
    }

    auto mapped_buffer = create_mapped_buffer(descs_count, desc_page_size, direction, driver);
    if (!mapped_buffer) {
        LOGGER__ERROR("Failed building mapped vdma buffer");
        status = mapped_buffer.status();
        return;
    }
    m_channel_buffer = mapped_buffer.release();

    status = m_desc_list->configure_to_use_buffer(*m_channel_buffer, channel_id, 0);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed binding vdma buffer to desc list");
        return;
    }

    m_pending_buffers_sizes = CircularArray<size_t>(descs_count);

    status = HAILO_SUCCESS;
}

Expected<std::shared_ptr<DmaMappedBuffer>> BufferedChannel::create_mapped_buffer(uint32_t descs_count, uint16_t desc_page_size,
    Direction direction, HailoRTDriver &driver)
{
    auto desc_page_size_value = driver.calc_desc_page_size(desc_page_size);
    CHECK_AS_EXPECTED(is_powerof2(desc_page_size_value), HAILO_INVALID_ARGUMENT, "Descriptor page_size must be a power of two.");

    auto mapped_buffer_exp = MappedBufferFactory::create_mapped_buffer(descs_count * desc_page_size_value, direction, driver);
    CHECK_EXPECTED(mapped_buffer_exp);

    auto mapped_buffer = make_shared_nothrow<DmaMappedBuffer>(mapped_buffer_exp.release());
    CHECK_NOT_NULL_AS_EXPECTED(mapped_buffer, HAILO_OUT_OF_HOST_MEMORY);

    return mapped_buffer;
}

hailo_status BufferedChannel::complete_channel_deactivation()
{
    const auto status = store_channel_buffer_state();
    CHECK_SUCCESS(status);

    if (Direction::H2D == m_direction) {
        clear_pending_buffers_descriptors();
        // For H2D channels we reset counters as we want to allow writes to the start of the buffer while the channel is stopped
        m_state->reset_counters();
    }

    return HAILO_SUCCESS;
}

hailo_status BufferedChannel::store_channel_buffer_state()
{
    // TODO: If a D2H channel is deactivated before all of it's pending frames have recv'd ints
    //       we'll store a tail value that won't be up to date when the channel is activated again.
    //       Potentially, we might overwrite frames in that situation. Note that we can't flush() in the case
    //       of D2H channels (as we can with H2D channels), because num_avail may be greater than the number of frames
    //       that will be recv'd on a given channel. E.g., upon channel activation for the first time we call
    //       prepare_d2h_pending_descriptors with the maximum number of descs possible for this channel, which will
    //       accommodate X frames. If the usert only sends Y < X frames on the input channel, only Y output frames will
    //       be recv'd (assuming one output frame per input frame). Hence, flush() won't return (we won't dequeue all
    //       pending buffers). This needs to be handled by the sched that uses this feature. (HRT-9456)
    auto tail = get_hw_num_processed();
    CHECK_EXPECTED_AS_STATUS(tail);

    const auto temp = m_state->m_previous_tail;
    m_state->m_previous_tail = (tail.value() + m_state->m_previous_tail) & m_state->m_descs.size_mask;
    m_state->m_desc_list_delta = temp - m_state->m_previous_tail;

    return HAILO_SUCCESS;
}

Expected<size_t> BufferedChannel::get_h2d_pending_frames_count()
{
    return m_pending_buffers_sizes.size();
}

Expected<size_t> BufferedChannel::get_d2h_pending_descs_count()
{
    std::lock_guard<RecursiveSharedMutex> state_guard(m_state->mutex());

    int num_proc = CB_TAIL(m_state->m_descs);
    int desc_num_ready = CB_PROG(m_state->m_descs, num_proc, m_state->m_d2h_read_desc_index);

    return desc_num_ready;
}

hailo_status BufferedChannel::prepare_d2h_pending_descriptors(uint32_t transfer_size, uint32_t transfers_count)
{
    // on D2H no need for interrupt of first descriptor
    const auto first_desc_interrupts_domain = InterruptsDomain::NONE;
    for (uint32_t i = 0; i < transfers_count; i++) {
        // Provide FW interrupt only in the end of the last transfer in the batch
        auto last_desc_interrutps_domain =
            (static_cast<uint32_t>(m_transfers_per_axi_intr - 1) == (i % m_transfers_per_axi_intr)) ?
                InterruptsDomain::BOTH : InterruptsDomain::HOST;
        auto status = prepare_descriptors(transfer_size, first_desc_interrupts_domain, last_desc_interrutps_domain);
        if (HAILO_STREAM_NOT_ACTIVATED == status) {
            LOGGER__INFO("preparing descriptors failed because channel is not activated");
            return status;
        }
        CHECK_SUCCESS(status, "Failed prepare desc status={}", status);
    }

    // We assume each output transfer is in the same size
    m_state->m_accumulated_transfers += ((m_state->m_accumulated_transfers + transfers_count) % m_transfers_per_axi_intr);

    return HAILO_SUCCESS;
}

hailo_status BufferedChannel::complete_channel_activation(uint32_t transfer_size, bool resume_pending_transfers)
{
    auto status = HAILO_UNINITIALIZED;

    // We should have no active transfers now
    if (resume_pending_transfers) {
        // We want the first descriptor (at index zero) to point to where the descriptor at index
        // m_state->m_previous_tail currently points to:
        // * In the case of a D2H channel, m_state->m_previous_tail is the index of the desc where the hw would next
        //   write to (num_proc). Hence, the hw will now write exactly where it left off. Previously unread frames from
        //   the user (pointed to by m_state->m_d2h_read_desc_index) can still be read (the hw won't overwrite them).
        // * In the case of a H2D channel, m_state->m_previous_tail is the index of the desc where the hw would next
        //   read from (num_proc). Hence, the hw will now read exactly where it left off. Previously written frames
        //   from the user (that appear before m_state->m_previous_tail), will not be re-written.
        const uint32_t starting_desc_offset = (m_desc_list->count() - m_state->m_previous_tail) % m_desc_list->count();
        status = m_desc_list->configure_to_use_buffer(*m_channel_buffer, m_channel_id,
            starting_desc_offset);
        CHECK_SUCCESS(status);

        if (Direction::D2H == m_direction) {
            // m_d2h_read_desc_index, which is relative to the first desc, needs to shift by m_desc_list_delta
            m_state->m_d2h_read_desc_index = (m_state->m_d2h_read_desc_index + m_state->m_desc_list_delta) & m_state->m_descs.size_mask;
        }
    } else {
        // We're not resuming pending transfers - clear relevant pointers.
        m_state->reset_previous_state_counters();
    }

    if ((Direction::D2H == m_direction) && (transfer_size != 0)) {
        const auto transfers_in_buffer = get_transfers_count_in_buffer(transfer_size);
        const auto pending_descs = get_d2h_pending_descs_count();
        const auto descs_in_transfer = m_desc_list->descriptors_in_buffer(transfer_size);
        const auto pending_transfers = pending_descs.value() / descs_in_transfer;
        // We prepare descs in advance for D2H channels:
        // (1) The channel's buffer can store up to 'transfers_in_buffer' frames of size transfer_size
        // (2) There are 'pending_transfers' frames from the previous channel activation (we assume that the same
        //     'transfer_size' was used)
        // (3) Hence, we have room for 'transfers_in_buffer - pending_transfers' frames in the buffer currently.
        // (4) However, we can allow at most 'm_state->m_pending_buffers.capacity()' transfers. We can't store more than 
        //     that in the pending buffers circular array.
        // (5) Hence, we'll take the minimum between (3) and (4).
        const auto transfers_count = std::min(transfers_in_buffer - pending_transfers,
            m_state->m_pending_buffers.capacity());
        status = prepare_d2h_pending_descriptors(transfer_size, static_cast<uint32_t>(transfers_count));
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status BufferedChannel::transfer(void *buf, size_t count)
{
    CHECK_NOT_NULL(buf, HAILO_INVALID_ARGUMENT);
    CHECK(0 != count, HAILO_INVALID_ARGUMENT);

    std::lock_guard<RecursiveSharedMutex> state_guard(m_state->mutex());
    if (m_state->m_is_aborted) {
        LOGGER__INFO("Tried to write to aborted channel {}", m_channel_id);
        return HAILO_STREAM_ABORTED_BY_USER;
    }

    hailo_status status = HAILO_UNINITIALIZED;
    if (Direction::H2D == m_direction) {
        status = transfer_h2d(buf, count);
    } else {
        status = transfer_d2h(buf, count);
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

hailo_status BufferedChannel::write_buffer_impl(const MemoryView &buffer)
{
    const uint32_t desired_desc_num = m_desc_list->descriptors_in_buffer(buffer.size());
    const uint32_t desc_avail = (get_num_available() + m_pending_num_avail_offset) & m_state->m_descs.size_mask;
    assert(desired_desc_num <= MAX_DESCS_COUNT);
    assert(CB_AVAIL(m_state->m_descs, desc_avail, CB_TAIL(m_state->m_descs)) >= desired_desc_num);

    const size_t buffer_write_offset = ((desc_avail + m_state->m_previous_tail) & m_state->m_descs.size_mask) * m_desc_list->desc_page_size();
    const auto status = write_to_channel_buffer_cyclic(buffer, buffer_write_offset);
    CHECK_SUCCESS(status);

    m_pending_num_avail_offset = static_cast<uint16_t>(m_pending_num_avail_offset + desired_desc_num);

    CHECK(!m_pending_buffers_sizes.full(), HAILO_INVALID_OPERATION, "Cannot add more pending buffers!");
    m_pending_buffers_sizes.push_back(buffer.size());
    return HAILO_SUCCESS;
}

hailo_status BufferedChannel::write_to_channel_buffer_cyclic(const MemoryView &buffer, size_t channel_buffer_write_offset)
{
    CHECK(buffer.size() <= m_channel_buffer->size(), HAILO_INSUFFICIENT_BUFFER,
        "Can't write {} bytes to channel buffer (channel buffer size {})",
        buffer.size(), m_channel_buffer->size());

    const auto size_to_end = m_channel_buffer->size() - channel_buffer_write_offset;
    const auto first_chunk_size = std::min(size_to_end, buffer.size());
    const auto first_chunk_addr = static_cast<uint8_t *>(m_channel_buffer->user_address()) + channel_buffer_write_offset;

    // Copy from buffer to m_channel_buffer and then synchronize
    std::memcpy(first_chunk_addr, buffer.data(), first_chunk_size);
    auto status = m_channel_buffer->pimpl->synchronize(channel_buffer_write_offset, first_chunk_size);
    CHECK_SUCCESS(status);

    const auto remaining_size = buffer.size() - first_chunk_size;
    if (remaining_size > 0) {
        // Copy the remainder from buffer to m_channel_buffer and then synchronize
        std::memcpy(m_channel_buffer->user_address(), buffer.data() + first_chunk_size, remaining_size);
        status = m_channel_buffer->pimpl->synchronize(0, remaining_size);
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status BufferedChannel::read_from_channel_buffer_cyclic(uint8_t *dest_buffer, size_t read_size, size_t channel_buffer_read_offset)
{
    CHECK(read_size <= m_channel_buffer->size(), HAILO_INSUFFICIENT_BUFFER,
        "Can't read {} bytes from channel buffer (channel buffer size {})",
        read_size, m_channel_buffer->size());

    const auto size_to_end = m_channel_buffer->size() - channel_buffer_read_offset;
    const auto first_chunk_size = std::min(size_to_end, read_size);
    const auto first_chunk_addr = static_cast<uint8_t *>(m_channel_buffer->user_address()) + channel_buffer_read_offset;

    // Synchronize m_channel_buffer and copy to dest_buffer
    auto status = m_channel_buffer->pimpl->synchronize(channel_buffer_read_offset, first_chunk_size);
    CHECK_SUCCESS(status);
    std::memcpy(dest_buffer, first_chunk_addr, first_chunk_size);

    const auto remaining_size = read_size - first_chunk_size;
    if (remaining_size > 0) {
        // Synchronize m_channel_buffer and copy remainder to dest_buffer
        status = m_channel_buffer->pimpl->synchronize(0, remaining_size);
        CHECK_SUCCESS(status);
        std::memcpy(dest_buffer + first_chunk_size, m_channel_buffer->user_address(), remaining_size);
    }

    return HAILO_SUCCESS;
}

Expected<BoundaryChannel::BufferState> BufferedChannel::get_buffer_state()
{
    BoundaryChannel::BufferState result;
    result.num_avail = static_cast<uint16_t>(CB_HEAD(m_state->m_descs));
    result.num_processed = static_cast<uint16_t>(CB_TAIL(m_state->m_descs));
    auto hw_num_avail = m_host_registers.get_num_available();
    CHECK_EXPECTED(hw_num_avail);
    result.hw_num_avail = hw_num_avail.release();
    auto hw_num_processed = get_hw_num_processed();
    CHECK_EXPECTED(hw_num_processed);
    result.hw_num_processed = hw_num_processed.release();

    // Get a snapshot of the channel buffer
    auto channel_buffer_copy = Buffer::create(m_channel_buffer->size());
    CHECK_EXPECTED(channel_buffer_copy);
    const auto status = read_from_channel_buffer_cyclic(channel_buffer_copy->data(), channel_buffer_copy->size(), 0);
    CHECK_SUCCESS_AS_EXPECTED(status);

    for (size_t offset = 0; offset < channel_buffer_copy->size(); offset += m_desc_list->desc_page_size()) {
        auto chunk = Buffer::create(channel_buffer_copy->data() + offset, m_desc_list->desc_page_size());
        CHECK_EXPECTED(chunk);
        const auto abs_index = offset / m_desc_list->desc_page_size();
        const auto desc_num = (abs_index >= static_cast<uint16_t>(m_state->m_previous_tail)) ?
            abs_index - m_state->m_previous_tail :
            m_state->m_descs.size - m_state->m_previous_tail + abs_index;
        result.desc_buffer_pairing.emplace_back(static_cast<uint16_t>(desc_num), chunk.release());
    }

    return result;
}

hailo_status BufferedChannel::write_buffer(const MemoryView &buffer, std::chrono::milliseconds timeout,
    const std::function<bool()> &should_cancel)
{
    std::unique_lock<RecursiveSharedMutex> state_guard(m_state->mutex());

    // Checking in advance so as not to timeout
    CHECK(buffer.size() <= m_channel_buffer->size(), HAILO_INSUFFICIENT_BUFFER,
        "Can't write {} bytes to channel buffer (channel buffer size {})",
        buffer.size(), m_channel_buffer->size());

    size_t desired_desc_num = m_desc_list->descriptors_in_buffer(buffer.size());
    hailo_status channel_completion_status = HAILO_SUCCESS;
    bool was_successful = m_state->transfer_buffer_cv().wait_for(state_guard, timeout, [this, desired_desc_num,
        &should_cancel, &channel_completion_status] () {
        if (m_state->m_is_aborted) {
            return true;
        }

        if (should_cancel()) {
            channel_completion_status = HAILO_STREAM_ABORTED_BY_USER;
            return true;
        }
        // Limit writes to not surpass size of m_pending_buffers
        size_t written_buffers_count = m_pending_buffers_sizes.size();
        size_t sent_buffers_count = m_state->m_pending_buffers.size();
        if (written_buffers_count + sent_buffers_count >= m_state->m_pending_buffers.capacity()) {
            return false;
        }

        return is_ready_for_write(static_cast<uint16_t>(desired_desc_num));
    });
    if (m_state->m_is_aborted || (HAILO_STREAM_ABORTED_BY_USER == channel_completion_status)) {
        LOGGER__INFO("wait_for in write_buffer was aborted!");
        return HAILO_STREAM_ABORTED_BY_USER;
    }
    CHECK(was_successful, HAILO_TIMEOUT, "Got HAILO_TIMEOUT while waiting for descriptors in write_buffer (channel_id={})",
        m_channel_id);
    CHECK_SUCCESS(channel_completion_status);

    return write_buffer_impl(buffer);
}

hailo_status BufferedChannel::send_pending_buffer_impl()
{
    CHECK(!m_pending_buffers_sizes.empty(), HAILO_INVALID_OPERATION, "There are no pending buffers to send!");

    // For h2d, only the host need to get transfer done interrupts
    InterruptsDomain last_desc_interrupts_domain = InterruptsDomain::HOST;
    // If we measure latency, we need interrupt on the first descriptor
    InterruptsDomain first_desc_interrupts_domain = (m_latency_meter != nullptr) ?
        InterruptsDomain::HOST : InterruptsDomain::NONE;

    auto status = prepare_descriptors(m_pending_buffers_sizes.front(), first_desc_interrupts_domain, last_desc_interrupts_domain);
    if (HAILO_STREAM_NOT_ACTIVATED == status) {
        LOGGER__INFO("sending pending buffer failed because stream is not activated");
        // Stream was aborted during transfer - reset pending buffers
        m_pending_num_avail_offset = 0;
        while (m_pending_buffers_sizes.size() > 0) {
            m_pending_buffers_sizes.pop_front();
        }
        return status;
    }
    CHECK_SUCCESS(status);
    m_state->m_accumulated_transfers = (m_state->m_accumulated_transfers + 1) % m_transfers_per_axi_intr;

    size_t desired_desc_num = m_desc_list->descriptors_in_buffer(m_pending_buffers_sizes.front());
    m_pending_num_avail_offset = static_cast<uint16_t>(m_pending_num_avail_offset - desired_desc_num);

    m_pending_buffers_sizes.pop_front();

    return HAILO_SUCCESS;
}

hailo_status BufferedChannel::send_pending_buffer()
{
    {
        std::lock_guard<RecursiveSharedMutex> state_guard(m_state->mutex());

        auto status = send_pending_buffer_impl();
        if (HAILO_STREAM_NOT_ACTIVATED == status) {
            LOGGER__INFO("stream is not activated");
            return HAILO_STREAM_NOT_ACTIVATED;
        } else {
            CHECK_SUCCESS(status);
        }
    }
    m_state->transfer_buffer_cv().notify_one();

    return HAILO_SUCCESS;
}

hailo_status BufferedChannel::transfer(std::shared_ptr<DmaMappedBuffer>, const TransferDoneCallback &, void *)
{
    return HAILO_NOT_IMPLEMENTED;
}

hailo_status BufferedChannel::cancel_pending_transfers()
{
    return HAILO_NOT_IMPLEMENTED;
}

hailo_status BufferedChannel::transfer_h2d(void *buf, size_t count)
{
    auto status = write_buffer_impl(MemoryView(buf, count));
    CHECK_SUCCESS(status);

    status = send_pending_buffer_impl();
    if (HAILO_STREAM_NOT_ACTIVATED == status) {
        return status;
    } else {
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status BufferedChannel::transfer_d2h(void *buf, size_t count)
{
    hailo_status status = HAILO_UNINITIALIZED;
    // Provide FW interrupt only in the end of the last transfer in the batch
    InterruptsDomain first_desc_interrupts_domain = InterruptsDomain::NONE;
    InterruptsDomain last_desc_interrupts_domain = (m_state->m_accumulated_transfers + 1 == m_transfers_per_axi_intr) ?
        InterruptsDomain::BOTH : InterruptsDomain::HOST;

    auto desired_desc_num = m_desc_list->descriptors_in_buffer(count);
    assert(desired_desc_num <= MAX_DESCS_COUNT);
    int desc_num = static_cast<int>(desired_desc_num);

    int num_processed = CB_TAIL(m_state->m_descs);
    int num_ready = CB_PROG(m_state->m_descs, num_processed, m_state->m_d2h_read_desc_index);
    CHECK(num_ready >= desc_num, HAILO_OUT_OF_DESCRIPTORS,
        "{} descriptors desired but only {} available", desc_num, num_ready);

    const auto channel_buffer_read_offset = m_state->m_d2h_read_desc_index_abs * m_desc_list->desc_page_size();
    status = read_from_channel_buffer_cyclic(static_cast<uint8_t *>(buf), count, channel_buffer_read_offset);
    CHECK_SUCCESS(status);

    m_state->m_d2h_read_desc_index = (m_state->m_d2h_read_desc_index + desc_num) & m_state->m_descs.size_mask;
    m_state->m_d2h_read_desc_index_abs = (m_state->m_d2h_read_desc_index_abs + desc_num) & m_state->m_descs.size_mask;

    // prepare descriptors for next recv
    if (m_state->m_is_channel_activated) {
        status = prepare_descriptors(count, first_desc_interrupts_domain, last_desc_interrupts_domain);
        if (HAILO_STREAM_NOT_ACTIVATED == status) {
            LOGGER__INFO("transfer d2h failed because stream is not activated");
            return status;
        }
        CHECK_SUCCESS(status);
    }

    m_state->m_accumulated_transfers = (m_state->m_accumulated_transfers + 1) % m_transfers_per_axi_intr;

    return HAILO_SUCCESS;
}

hailo_status BufferedChannel::prepare_descriptors(size_t transfer_size, InterruptsDomain first_desc_interrupts_domain,
    InterruptsDomain last_desc_interrupts_domain)
{
    if (!m_state->m_is_channel_activated) {
        return HAILO_STREAM_NOT_ACTIVATED;
    }

    // Calculate desired descriptors for the buffer
    size_t desired_desc_num = m_desc_list->descriptors_in_buffer(transfer_size);
    assert(desired_desc_num <= MAX_DESCS_COUNT);
    uint16_t desc_num = static_cast<uint16_t>(desired_desc_num);

    int num_available = get_num_available();
    int num_processed = CB_TAIL(m_state->m_descs);
    int num_free = CB_AVAIL(m_state->m_descs, num_available, num_processed);
    if (num_free < desc_num) {
        return HAILO_OUT_OF_DESCRIPTORS;
    }

    if (nullptr != m_latency_meter) {
        // Program first descriptor
        m_desc_list->program_single_descriptor((*m_desc_list)[num_available], m_desc_list->desc_page_size(),
            first_desc_interrupts_domain);
    }
    auto actual_desc_count = m_desc_list->program_last_descriptor(transfer_size, last_desc_interrupts_domain,
        num_available, true);
    if (!actual_desc_count) {
        LOGGER__ERROR("Failed to program desc_list for channel {}", m_channel_id);
        return actual_desc_count.status();
    }
    assert (actual_desc_count.value() == desc_num);
    int last_desc_avail = ((num_available + desc_num - 1) & m_state->m_descs.size_mask);

    m_state->add_pending_buffer(num_available, last_desc_avail, m_direction, m_transfer_done_callback);
    return inc_num_available(desc_num);
}

bool BufferedChannel::is_ready_for_write(const uint16_t desired_desc_num)
{
    const auto has_space_in_buffers = !m_state->m_pending_buffers.full();
    const uint32_t desc_avail = (get_num_available() + m_pending_num_avail_offset) & m_state->m_descs.size_mask;
    const int num_free = CB_AVAIL(m_state->m_descs, desc_avail, CB_TAIL(m_state->m_descs));
    const auto has_desc_space = (num_free >= desired_desc_num);

    return (has_space_in_buffers && has_desc_space);
}

bool BufferedChannel::is_ready_for_transfer_d2h(size_t buffer_size)
{
    size_t desired_desc_num = m_desc_list->descriptors_in_buffer(buffer_size);
    assert(desired_desc_num <= MAX_DESCS_COUNT);
    int desc_num = static_cast<int>(desired_desc_num);

    if (m_state->m_pending_buffers.full()) {
        return false;
    }

    int num_processed = CB_TAIL(m_state->m_descs);
    int num_ready = CB_PROG(m_state->m_descs, num_processed, m_state->m_d2h_read_desc_index);
    if (num_ready < desc_num) {
        return false;
    }
    return true;
}

void BufferedChannel::notify_all()
{
    {
        // Acquire mutex to make sure the notify_all will wake the blocking threads on the cv
        std::lock_guard<RecursiveSharedMutex> state_guard(m_state->mutex());
    }
    m_state->transfer_buffer_cv().notify_all();
}

} /* namespace vdma */
} /* namespace hailort */
