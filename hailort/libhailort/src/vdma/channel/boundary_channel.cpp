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
#include "vdma/memory/vdma_buffer.hpp"

#include <list>
#include <chrono>
#include <thread>
#include <iostream>


namespace hailort {
namespace vdma {


Expected<BoundaryChannelPtr> BoundaryChannel::create(vdma::ChannelId channel_id, Direction direction,
    VdmaDevice &vdma_device, uint32_t descs_count, uint16_t desc_page_size, const std::string &stream_name,
    LatencyMeterPtr latency_meter)
{
    hailo_status status = HAILO_UNINITIALIZED;
    auto channel_ptr = make_shared_nothrow<BoundaryChannel>(channel_id, direction, vdma_device, descs_count,
        desc_page_size, stream_name, latency_meter, status);
    CHECK_NOT_NULL_AS_EXPECTED(channel_ptr, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status, "Failed creating BoundaryChannel");
    return channel_ptr;
}

BoundaryChannel::BoundaryChannel(vdma::ChannelId channel_id, Direction direction, VdmaDevice &vdma_device,
                                 uint32_t descs_count, uint16_t desc_page_size, const std::string &stream_name,
                                 LatencyMeterPtr latency_meter, hailo_status &status) :
    m_channel_id(channel_id),
    m_direction(direction),
    m_vdma_device(vdma_device),
    m_driver(vdma_device.get_driver()),
    m_host_registers(vdma_device.get_driver(), channel_id, direction),
    m_desc_list(nullptr),
    m_stream_name(stream_name),
    m_latency_meter(latency_meter),
    m_is_channel_activated(false),
    m_ongoing_transfers((latency_meter != nullptr) ? ONGOING_TRANSFERS_SIZE/2 : ONGOING_TRANSFERS_SIZE),
    m_last_bounded_buffer(BoundedBuffer{nullptr, 0, 0})
{
    if (Direction::BOTH == direction) {
        LOGGER__ERROR("Boundary channels must be unidirectional");
        status = HAILO_INVALID_ARGUMENT;
        return;
    }

    if (channel_id.channel_index >= VDMA_CHANNELS_PER_ENGINE) {
        LOGGER__ERROR("Invalid DMA channel index {}", channel_id.channel_index);
        status = HAILO_INVALID_ARGUMENT;
        return;
    }

    if (channel_id.engine_index >= m_driver.dma_engines_count()) {
        LOGGER__ERROR("Invalid DMA engine index {}, max {}", channel_id.engine_index, m_driver.dma_engines_count());
        status = HAILO_INVALID_ARGUMENT;
        return;
    }

    CB_INIT(m_descs, descs_count);

    status = allocate_descriptor_list(descs_count, desc_page_size);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to allocate Vdma buffer for channel transfer! status={}", status);
        return;
    }

    status = HAILO_SUCCESS;
}

hailo_status BoundaryChannel::trigger_channel_completion(uint16_t hw_num_processed)
{
    // NOTE: right now, we can retake the 'completion' descriptor for a new transfer before handling the interrupt.
    //      we should have our own pointers indicating whats free instead of reading from HW.

    std::unique_lock<std::mutex> lock(m_channel_mutex);

    if (!m_is_channel_activated) {
        return HAILO_STREAM_NOT_ACTIVATED;
    }

    // Although the hw_num_processed should be a number between 0 and m_descs.size-1, if m_desc.size < 0x10000
    // (the maximum desc size), the actual hw_num_processed is a number between 1 and m_descs.size. Therefore the
    // value can be m_descs.size, in this case we change it to zero.
    hw_num_processed = static_cast<uint16_t>(hw_num_processed & m_descs.size_mask);

    if (m_latency_meter != nullptr) {
        // The latency meter gets an updated hw_num_processed via a call to vdma_interrupts_read_timestamps
        // (the desc index of the last measured timestamp returned from that ioctl). Since update_latency_meter
        // processed m_ongoing_transfers based on this hw_num_processed, and this function (i.e.
        // trigger_channel_completion) also processes m_ongoing_transfers based on the value of hw_num_processed,
        // we want the two to be the same. Hence, we'll use the more up to date num_processed returned by
        // update_latency_meter.
        // TODO: fix update_latency_meter flow (HRT-10284)
        auto latency_meter_hw_num_processed = update_latency_meter();
        CHECK_EXPECTED_AS_STATUS(latency_meter_hw_num_processed);
        hw_num_processed = latency_meter_hw_num_processed.value();
    }

    while (!m_ongoing_transfers.empty()) {
        // Reading previous_num_processed inside the loop since on_transfer_complete may increase this value.
        const auto previous_num_processed = static_cast<uint16_t>(CB_TAIL(m_descs));

        if (!is_transfer_complete(m_ongoing_transfers.front(), previous_num_processed, hw_num_processed)) {
            break;
        }

        auto transfer = std::move(m_ongoing_transfers.front());
        m_ongoing_transfers.pop_front();

        hailo_status complete_status = HAILO_SUCCESS;

        #ifndef NDEBUG
            assert(!transfer.last_descs.empty());
            auto &last_desc = (*m_desc_list)[transfer.last_descs.back()];
            if (!last_desc.is_done() || last_desc.is_error()) {
                LOGGER__ERROR("Error while processing descriptor {} of DMA {} on device {} DESC_STATUS=0x{:x}.",
                    transfer.last_descs.back(), m_channel_id, m_driver.device_id(), last_desc.status());
                complete_status = HAILO_INTERNAL_FAILURE;
            }
        #endif

        on_transfer_complete(lock, transfer, complete_status);
    }

    return HAILO_SUCCESS;
}

CONTROL_PROTOCOL__host_buffer_info_t BoundaryChannel::get_boundary_buffer_info(uint32_t transfer_size) const
{
    // Boundary channels always have scatter gather buffers
    return VdmaBuffer::get_host_buffer_info(VdmaBuffer::Type::SCATTER_GATHER, m_desc_list->dma_address(), 
        m_desc_list->desc_page_size(), m_desc_list->count(), transfer_size);
}

hailo_status BoundaryChannel::activate()
{
    std::lock_guard<std::mutex> lock(m_channel_mutex);

    CHECK(!m_is_channel_activated, HAILO_INTERNAL_FAILURE,
        "Vdma channel {} is already activated", m_channel_id);
    m_is_channel_activated = true;
    assert(m_ongoing_transfers.empty());
    m_last_timestamp_num_processed = 0;
    CB_RESET(m_descs);

    return HAILO_SUCCESS;
}

hailo_status BoundaryChannel::deactivate()
{
    std::unique_lock<std::mutex> lock(m_channel_mutex);
    m_is_channel_activated = false;

    // Note: OngoingTransfers held by m_ongoing_transfers may still hold copies of the current callback
    // which in turn holds a reference to *this. Since we deactivate the channel there's no risk that
    // these callbacks will be called and we don't need to reset this callback.

    return HAILO_SUCCESS;
}

hailo_status BoundaryChannel::launch_transfer(TransferRequest &&transfer_request, bool user_owns_buffer)
{
    std::unique_lock<std::mutex> lock(m_channel_mutex);
    if (!m_is_channel_activated) {
        return HAILO_STREAM_NOT_ACTIVATED;
    }

    if (m_ongoing_transfers.size() >= get_max_ongoing_transfers(transfer_request.get_total_transfer_size())) {
        return HAILO_QUEUE_IS_FULL;
    }

    auto num_available = get_num_available();
    const uint16_t first_desc = num_available;
    std::vector<uint16_t> transfer_last_descs;
    uint16_t total_descs_count = 0;

    for (size_t i = 0; i < transfer_request.transfer_buffers.size(); i++) {
        auto mapped_buffer_exp = transfer_request.transfer_buffers[i].map_buffer(m_vdma_device, m_direction);
        CHECK_EXPECTED_AS_STATUS(mapped_buffer_exp);
        auto mapped_buffer = mapped_buffer_exp.release();

        // Syncing the buffer to device change its ownership from host to the device.
        // We sync on D2H as well if the user owns the buffer since the buffer might have been changed by
        // the host between the time it was mapped and the current async transfer. If the buffer is not owned by the user,
        // it won't be accessed for write.
        if ((Direction::H2D == m_direction) || user_owns_buffer) {
            auto status = transfer_request.transfer_buffers[i].synchronize(m_vdma_device, HailoRTDriver::DmaSyncDirection::TO_DEVICE);
            CHECK_SUCCESS(status);
        }

        const auto desired_desc_num = m_desc_list->descriptors_in_buffer(transfer_request.transfer_buffers[i].size());
        CHECK(desired_desc_num <= MAX_DESCS_COUNT, HAILO_INTERNAL_FAILURE);
        const uint16_t desc_num = static_cast<uint16_t>(desired_desc_num);
        assert(total_descs_count + desc_num < MAX_DESCS_COUNT);
        total_descs_count = static_cast<uint16_t>(total_descs_count + desc_num);

        const auto last_desc_avail = static_cast<uint16_t>((num_available + desc_num - 1) & m_descs.size_mask);

        transfer_last_descs.emplace_back(last_desc_avail);

        // Raise interrupt on last buffer
        const auto should_buffer_raise_int = (i == (transfer_request.transfer_buffers.size() - 1));
        auto status = prepare_descriptors(transfer_request.transfer_buffers[i].size(), num_available, mapped_buffer,
            transfer_request.transfer_buffers[i].offset(), should_buffer_raise_int);
        CHECK_SUCCESS(status);

        num_available = static_cast<uint16_t>((last_desc_avail + 1) & m_descs.size_mask);
    }

    if ((nullptr != m_latency_meter) && (m_direction == Direction::H2D)) {
        // If we measure latency, we need an interrupt on the first descriptor for each H2D channel.
        m_desc_list->program_single_descriptor((*m_desc_list)[first_desc], m_desc_list->desc_page_size(),
            InterruptsDomain::HOST);
    }

    add_ongoing_transfer(std::move(transfer_request), first_desc, std::move(transfer_last_descs));

    auto status = inc_num_available(total_descs_count);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

void BoundaryChannel::cancel_pending_transfers()
{
    std::unique_lock<std::mutex> lock(m_channel_mutex);
    while (!m_ongoing_transfers.empty()) {
        auto transfer = std::move(m_ongoing_transfers.front());
        m_ongoing_transfers.pop_front();

        on_transfer_complete(lock, transfer, HAILO_STREAM_ABORTED_BY_USER);
    }
}

size_t BoundaryChannel::get_max_ongoing_transfers(size_t transfer_size) const
{
    // Add desc for boundary channel because might need extra for non aligned async API
    const auto descs_in_transfer = m_desc_list->descriptors_in_buffer(transfer_size) + 1;
    const auto descs_count = CB_SIZE(m_descs);
    size_t max_transfers_in_buffer = (descs_count - 1) / descs_in_transfer;

    return std::min(max_transfers_in_buffer, m_ongoing_transfers.capacity());
}

Expected<uint16_t> BoundaryChannel::update_latency_meter()
{
    uint16_t last_num_processed = m_last_timestamp_num_processed;

    auto timestamp_list = m_driver.vdma_interrupts_read_timestamps(m_channel_id);
    CHECK_EXPECTED(timestamp_list);

    if (0 == timestamp_list->count) {
        // No new timestamps for this channel, return the previous result
        return Expected<uint16_t>(last_num_processed);
    }

    // TODO: now we have more iterations than we need. We know that the pending buffers + the timestamp list
    // are ordered. If ongoing_transfers[i] is not in any of the timestamps_list[0, 1, ... k], then
    // also ongoing_transfers[i+1,i+2,...]
    // not in those timestamps

    for (const auto &transfer : m_ongoing_transfers) {
        uint16_t latency_desc = static_cast<uint16_t>(transfer.latency_measure_desc);
        for (size_t i = 0; i < timestamp_list->count; i++) {
            const auto &irq_timestamp = timestamp_list->timestamp_list[i];
            const auto desc_num_processed = static_cast<uint16_t>(irq_timestamp.desc_num_processed & m_descs.size_mask);
            if (is_desc_between(last_num_processed, desc_num_processed, latency_desc)) {
                if (m_direction == Direction::H2D) {
                    m_latency_meter->add_start_sample(irq_timestamp.timestamp);
                }
                else {
                    m_latency_meter->add_end_sample(m_stream_name, irq_timestamp.timestamp);
                }
                break;
            }
        }
    }

    m_last_timestamp_num_processed = static_cast<uint16_t>(
        timestamp_list->timestamp_list[timestamp_list->count-1].desc_num_processed & m_descs.size_mask);
    return Expected<uint16_t>(m_last_timestamp_num_processed);
}

bool BoundaryChannel::is_transfer_complete(const OngoingTransfer &transfer, uint16_t previous_num_processed,
    uint16_t current_num_processed) const
{
    // Transfer is complete if its last descriptor is in [previous_num_processed, current_num_processed) or
    // the the buffer is empty (previous_num_processed == get_num_available())
    assert(!transfer.last_descs.empty());
    return is_desc_between(previous_num_processed, current_num_processed, transfer.last_descs.back()) ||
        (current_num_processed == get_num_available());
}

void BoundaryChannel::on_transfer_complete(std::unique_lock<std::mutex> &lock,
    OngoingTransfer &transfer, hailo_status complete_status)
{
    // Clear relevant descriptors from previous transfer
    if (nullptr != m_latency_meter) {
        m_desc_list->clear_descriptor(transfer.latency_measure_desc);
    }

    assert(!transfer.last_descs.empty());
    for (const auto& last_desc : transfer.last_descs) {
        m_desc_list->clear_descriptor(last_desc);
    }

    // We increase desc num_proc (can happen only in this flow). After it is increased -
    //  1. On D2H channels - the output can be read by the user.
    //  2. On H2D channels - new input can be written to the buffer.
    _CB_SET(m_descs.tail, (transfer.last_descs.back() + 1) & m_descs.size_mask);

    // Finally, we notify user callbacks registered with the transfer.
    // We want to make sure that the callbacks are called after the descriptors can be reused (So the user will
    // be able to start new transfer).
    lock.unlock();

    if (Direction::D2H == m_direction) {
        for (auto& transfer_buffer : transfer.request.transfer_buffers) {
            auto sync_status = transfer_buffer.synchronize(m_vdma_device, HailoRTDriver::DmaSyncDirection::TO_HOST);
            if (HAILO_SUCCESS != sync_status) {
                LOGGER__ERROR("Failed to sync buffer for output channel {} device {}", m_channel_id, m_driver.device_id());
                if (HAILO_SUCCESS != complete_status) {
                    complete_status = sync_status;
                }
            }
        }
    }

    transfer.request.callback(complete_status);
    lock.lock();
}

hailo_status BoundaryChannel::prepare_descriptors(size_t transfer_size, uint16_t starting_desc,
    MappedBufferPtr mapped_buffer, size_t buffer_offset, bool raise_interrupt)
{
    if (mapped_buffer != nullptr) {
        CHECK((buffer_offset % m_desc_list->desc_page_size()) == 0, HAILO_INTERNAL_FAILURE,
            "Buffer offset {} must be desc page size aligned {}", buffer_offset, m_desc_list->desc_page_size());
        const size_t buffer_offset_in_descs = buffer_offset / m_desc_list->desc_page_size();
        if (!is_buffer_already_configured(mapped_buffer, buffer_offset_in_descs, starting_desc)) {
            // We need to configure the buffer now.

            // First, store information on the buffer.
            m_last_bounded_buffer.buffer = mapped_buffer;
            m_last_bounded_buffer.starting_desc = starting_desc;
            m_last_bounded_buffer.buffer_offset_in_descs = static_cast<uint16_t>(buffer_offset_in_descs);

            // Now we want that m_desc_list[starting_desc] will be mapped into mapped_buffer[buffer_offset].
            // The descriptors list configure always starts from buffer_offset=0, so in order to achieve our
            // configuration, we configure the buffer starting from desc=(starting_desc - buffer_offset_in_desc).
            // Then, after configuring buffer_offset bytes from the buffer, the desc_index will be starting desc.
            const int desc_diff = static_cast<int>(starting_desc) - static_cast<int>(buffer_offset_in_descs);
            const auto configure_starting_desc = static_cast<uint16_t>(m_descs.size + desc_diff) % m_descs.size;

            // Finally do the actual configuration.
            auto status = m_desc_list->configure_to_use_buffer(*mapped_buffer, m_channel_id, configure_starting_desc);
            CHECK_SUCCESS(status);
        }
    }

    auto last_desc_interrupts_domain = raise_interrupt ? InterruptsDomain::HOST : InterruptsDomain::NONE;
    // TODO: HRT-11188 - fix starting_desc parameter
    auto actual_desc_count = m_desc_list->program_last_descriptor(transfer_size, last_desc_interrupts_domain,
        starting_desc);
    CHECK_EXPECTED_AS_STATUS(actual_desc_count, "Failed to program desc_list for channel {}", m_channel_id);

    return HAILO_SUCCESS;
}

bool BoundaryChannel::is_buffer_already_configured(MappedBufferPtr buffer, size_t buffer_offset_in_descs,
    size_t starting_desc) const
{
    if (m_last_bounded_buffer.buffer != buffer) {
        // Last buffer is nullptr or not the same as the given.
        return false;
    }

    // If the diff between starting_desc and m_last_bounded_buffer.starting_desc and the diff between
    // buffer_offset_in_descs - m_last_bounded_buffer.buffer_offset_in_descs are equal, it means that the buffer is
    // already configured.
    // Note that we don't afraid of overflow since buffer_offset_in_descs * desc_page_size() must fit inside the buffer.
    const auto starting_desc_diff = (starting_desc - m_last_bounded_buffer.starting_desc) % m_descs.size;
    const auto buffer_offset_diff_in_descs = (buffer_offset_in_descs - m_last_bounded_buffer.buffer_offset_in_descs) % m_descs.size;
    return starting_desc_diff == buffer_offset_diff_in_descs;
}

void BoundaryChannel::add_ongoing_transfer(TransferRequest &&transfer_request, uint16_t first_desc,
        std::vector<uint16_t> &&last_descs)
{
    OngoingTransfer transfer{};
    transfer.request = std::move(transfer_request);
    transfer.last_descs = std::move(last_descs);
    transfer.latency_measure_desc = (m_direction == HailoRTDriver::DmaDirection::H2D) ? first_desc :
        transfer.last_descs.back();
    m_ongoing_transfers.push_back(std::move(transfer));
}

hailo_status BoundaryChannel::inc_num_available(uint16_t value)
{
    int num_available = get_num_available();
    int num_processed = CB_TAIL(m_descs);
    int num_free = CB_AVAIL(m_descs, num_available, num_processed);
    if (value > num_free) {
        return HAILO_OUT_OF_DESCRIPTORS;
    }

    CB_ENQUEUE(m_descs, value);
    num_available = (num_available + value) & m_descs.size_mask;

    return m_host_registers.set_num_available(static_cast<uint16_t>(num_available));
}

bool BoundaryChannel::is_desc_between(uint16_t begin, uint16_t end, uint16_t desc)
{
    if (begin == end) {
        // There is nothing between
        return false;
    }
    if (begin < end) {
        // desc needs to be in [begin, end)
        return (begin <= desc) && (desc < end);
    }
    else {
        // desc needs to be in [0, end) or [begin, m_descs.size()-1]
        return (desc < end) || (begin <= desc);
    }
}

uint16_t BoundaryChannel::get_num_available() const
{
    uint16_t num_available = (uint16_t)CB_HEAD(m_descs);

#ifndef NDEBUG
    // Validate synchronization with HW
    auto hw_num_avail = m_host_registers.get_num_available();
    assert(hw_num_avail);

    // On case of channel aborted, the num_available is set to 0 (so we don't accept sync)
    auto is_aborted_exp = m_host_registers.is_aborted();
    assert(is_aborted_exp);

    if (!is_aborted_exp.value()) {
        assert(hw_num_avail.value() == num_available);
    }
#endif
    return num_available;
}

hailo_status BoundaryChannel::allocate_descriptor_list(uint32_t descs_count, uint16_t desc_page_size)
{
    static const bool CIRCULAR = true;
    auto desc_list_exp = DescriptorList::create(descs_count, desc_page_size, CIRCULAR, m_driver);
    CHECK_EXPECTED_AS_STATUS(desc_list_exp);

    m_desc_list = make_shared_nothrow<DescriptorList>(desc_list_exp.release());
    CHECK_NOT_NULL(m_desc_list, HAILO_OUT_OF_HOST_MEMORY);

    return HAILO_SUCCESS;
}

} /* namespace vdma */
} /* namespace hailort */
