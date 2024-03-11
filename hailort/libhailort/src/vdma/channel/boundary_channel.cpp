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
#include "vdma/memory/vdma_edge_layer.hpp"

#include <list>
#include <chrono>
#include <thread>
#include <iostream>


namespace hailort {
namespace vdma {


Expected<BoundaryChannelPtr> BoundaryChannel::create(vdma::ChannelId channel_id, Direction direction,
    HailoRTDriver &driver, uint32_t descs_count, uint16_t desc_page_size, const std::string &stream_name,
    LatencyMeterPtr latency_meter)
{
    hailo_status status = HAILO_UNINITIALIZED;
    auto channel_ptr = make_shared_nothrow<BoundaryChannel>(channel_id, direction, driver, descs_count,
        desc_page_size, stream_name, latency_meter, status);
    CHECK_NOT_NULL_AS_EXPECTED(channel_ptr, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status, "Failed creating BoundaryChannel");
    return channel_ptr;
}

BoundaryChannel::BoundaryChannel(vdma::ChannelId channel_id, Direction direction, HailoRTDriver &driver,
                                 uint32_t descs_count, uint16_t desc_page_size, const std::string &stream_name,
                                 LatencyMeterPtr latency_meter, hailo_status &status) :
    m_channel_id(channel_id),
    m_direction(direction),
    m_driver(driver),
    m_desc_list(nullptr),
    m_stream_name(stream_name),
    m_is_channel_activated(false),
    m_ongoing_transfers((latency_meter != nullptr) ? ONGOING_TRANSFERS_SIZE/2 : ONGOING_TRANSFERS_SIZE),
    m_latency_meter(latency_meter),
    m_pending_latency_measurements(ONGOING_TRANSFERS_SIZE) // Make sure there will always be place for latency measure
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

    if (m_latency_meter != nullptr) {
        CHECK_SUCCESS(update_latency_meter());
    }

    while (!m_ongoing_transfers.empty()) {
        // Reading previous_num_processed inside the loop since on_transfer_complete may increase this value.
        const auto previous_num_processed = static_cast<uint16_t>(CB_TAIL(m_descs));
        if (!is_transfer_complete(m_ongoing_transfers.front(), previous_num_processed, hw_num_processed)) {
            break;
        }

        auto transfer = std::move(m_ongoing_transfers.front());
        m_ongoing_transfers.pop_front();

        on_transfer_complete(lock, transfer, HAILO_SUCCESS);
    }

    return HAILO_SUCCESS;
}

CONTROL_PROTOCOL__host_buffer_info_t BoundaryChannel::get_boundary_buffer_info(uint32_t transfer_size) const
{
    // Boundary channels always have scatter gather buffers
    return VdmaEdgeLayer::get_host_buffer_info(VdmaEdgeLayer::Type::SCATTER_GATHER, m_desc_list->dma_address(), 
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

hailo_status BoundaryChannel::launch_transfer(TransferRequest &&transfer_request)
{
    std::unique_lock<std::mutex> lock(m_channel_mutex);
    if (!m_is_channel_activated) {
        return HAILO_STREAM_NOT_ACTIVATED;
    }

    if (m_ongoing_transfers.size() >= get_max_ongoing_transfers(transfer_request.get_total_transfer_size())) {
        return HAILO_QUEUE_IS_FULL;
    }

    auto num_available = static_cast<uint16_t>(CB_HEAD(m_descs));
    const uint16_t first_desc = num_available;
    uint16_t last_desc = std::numeric_limits<uint16_t>::max();
    uint16_t total_descs_count = 0;

    const bool should_bind = !m_bounded_buffer;
    if (!should_bind) {
        CHECK_SUCCESS(validate_bound_buffer(transfer_request));
    }

    std::vector<HailoRTDriver::TransferBuffer> driver_transfer_buffers;

    auto current_num_available = num_available;
    for (auto &transfer_buffer : transfer_request.transfer_buffers) {
        TRY(auto mapped_buffer, transfer_buffer.map_buffer(m_driver, m_direction));
        driver_transfer_buffers.emplace_back(HailoRTDriver::TransferBuffer{
            mapped_buffer->handle(),
            transfer_buffer.offset(),
            transfer_buffer.size()
        });

        const auto desired_desc_num = m_desc_list->descriptors_in_buffer(transfer_buffer.size());
        CHECK(desired_desc_num <= MAX_SG_DESCS_COUNT, HAILO_INTERNAL_FAILURE);
        const uint16_t desc_num = static_cast<uint16_t>(desired_desc_num);
        assert(total_descs_count + desc_num < MAX_SG_DESCS_COUNT);
        total_descs_count = static_cast<uint16_t>(total_descs_count + desc_num);

        last_desc = static_cast<uint16_t>((current_num_available + desc_num - 1) & m_descs.size_mask);
        current_num_available = static_cast<uint16_t>((last_desc + 1) & m_descs.size_mask);
    }

    auto first_desc_interrupts = InterruptsDomain::NONE;
    if ((nullptr != m_latency_meter) && (m_direction == Direction::H2D)) {
        // If we measure latency, we need an interrupt on the first descriptor for each H2D channel.
        first_desc_interrupts = InterruptsDomain::HOST;
    }
    const auto last_desc_interrupts = InterruptsDomain::HOST;

    int num_processed = CB_TAIL(m_descs);
    int num_free = CB_AVAIL(m_descs, num_available, num_processed);
    if (total_descs_count > num_free) {
        return HAILO_OUT_OF_DESCRIPTORS;
    }

    m_ongoing_transfers.push_back(OngoingTransfer{std::move(transfer_request), last_desc});
    if (m_latency_meter) {
        assert(!m_pending_latency_measurements.full());
        m_pending_latency_measurements.push_back(m_direction == Direction::H2D ? first_desc : last_desc);
    }
    CB_ENQUEUE(m_descs, total_descs_count);

    TRY(const auto desc_programmed, m_driver.launch_transfer(
        m_channel_id,
        m_desc_list->handle(),
        num_available,
        driver_transfer_buffers,
        should_bind,
        first_desc_interrupts,
        last_desc_interrupts
        ));
    CHECK(total_descs_count == desc_programmed, HAILO_INTERNAL_FAILURE,
        "Inconsistent desc programed expecting {} got {}", total_descs_count, desc_programmed);

    return HAILO_SUCCESS;
}

hailo_status BoundaryChannel::bind_buffer(MappedBufferPtr buffer)
{
    CHECK(m_bounded_buffer == nullptr, HAILO_INTERNAL_FAILURE,
        "Buffer is already bound to channel {}", m_channel_id);
    const auto expected_size = static_cast<size_t>(m_desc_list->desc_page_size()) * m_desc_list->count();
    CHECK(buffer->size() == expected_size, HAILO_INVALID_ARGUMENT,
        "Buffer size {} does not feet in desc list - descs count {} desc page size {}", buffer->size(),
        m_desc_list->count(), m_desc_list->desc_page_size());
    static const size_t DEFAULT_BUFFER_OFFSET = 0;
    CHECK_SUCCESS(m_desc_list->configure_to_use_buffer(*buffer, buffer->size(), DEFAULT_BUFFER_OFFSET, m_channel_id));
    m_bounded_buffer = buffer;
    return HAILO_SUCCESS;
}

void BoundaryChannel::cancel_pending_transfers()
{
    std::unique_lock<std::mutex> lock(m_channel_mutex);
    while (!m_ongoing_transfers.empty()) {
        auto transfer = std::move(m_ongoing_transfers.front());
        m_ongoing_transfers.pop_front();

        on_transfer_complete(lock, transfer, HAILO_STREAM_ABORT);
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

hailo_status BoundaryChannel::update_latency_meter()
{
    TRY(auto timestamp_list, m_driver.vdma_interrupts_read_timestamps(m_channel_id));
    if (0 == timestamp_list.count) {
        // No new timestamps for this channel.
        return HAILO_SUCCESS;
    }

    // TODO: now we have more iterations than we need. We know that the pending buffers + the timestamp list
    // are ordered. If ongoing_transfers[i] is not in any of the timestamps_list[0, 1, ... k], then
    // also ongoing_transfers[i+1,i+2,...]
    // not in those timestamps

    auto find_timestamp = [&](uint16_t latency_desc) -> Expected<std::chrono::nanoseconds> {
        for (size_t i = 0; i < timestamp_list.count; i++) {
            const auto &irq_timestamp = timestamp_list.timestamp_list[i];
            if (is_desc_between(m_last_timestamp_num_processed, irq_timestamp.desc_num_processed, latency_desc)) {
                return std::chrono::nanoseconds{irq_timestamp.timestamp};
            }
        }
        return make_unexpected(HAILO_NOT_FOUND);
    };

    while (!m_pending_latency_measurements.empty()) {
        auto timestamp = find_timestamp(m_pending_latency_measurements.front());
        if (!timestamp) {
            break;
        }

        if (m_direction == Direction::H2D) {
            m_latency_meter->add_start_sample(*timestamp);
        } else {
            m_latency_meter->add_end_sample(m_stream_name, *timestamp);
        }
        m_pending_latency_measurements.pop_front();
    }

    m_last_timestamp_num_processed = timestamp_list.timestamp_list[timestamp_list.count-1].desc_num_processed;
    return HAILO_SUCCESS;
}

bool BoundaryChannel::is_transfer_complete(const OngoingTransfer &transfer, uint16_t previous_num_processed,
    uint16_t current_num_processed) const
{
    // Transfer is complete if its last descriptor is in [previous_num_processed, current_num_processed) or
    // the the buffer is empty (previous_num_processed == CB_HEAD(m_descs))
    return is_desc_between(previous_num_processed, current_num_processed, transfer.last_desc) ||
        (current_num_processed == CB_HEAD(m_descs));
}

void BoundaryChannel::on_transfer_complete(std::unique_lock<std::mutex> &lock,
    OngoingTransfer &transfer, hailo_status complete_status)
{
    // We increase desc num_proc (can happen only in this flow). After it is increased -
    //  1. On D2H channels - the output can be read by the user.
    //  2. On H2D channels - new input can be written to the buffer.
    _CB_SET(m_descs.tail, (transfer.last_desc + 1) & m_descs.size_mask);

    // Finally, we notify user callbacks registered with the transfer.
    // We want to make sure that the callbacks are called after the descriptors can be reused (So the user will
    // be able to start new transfer).
    lock.unlock();
    transfer.request.callback(complete_status);
    lock.lock();
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

hailo_status BoundaryChannel::allocate_descriptor_list(uint32_t descs_count, uint16_t desc_page_size)
{
    static const bool CIRCULAR = true;
    auto desc_list_exp = DescriptorList::create(descs_count, desc_page_size, CIRCULAR, m_driver);
    CHECK_EXPECTED_AS_STATUS(desc_list_exp);

    m_desc_list = make_shared_nothrow<DescriptorList>(desc_list_exp.release());
    CHECK_NOT_NULL(m_desc_list, HAILO_OUT_OF_HOST_MEMORY);

    return HAILO_SUCCESS;
}

hailo_status BoundaryChannel::validate_bound_buffer(TransferRequest &transfer_request)
{
    assert(m_bounded_buffer);
    CHECK(transfer_request.transfer_buffers.size() == 1, HAILO_INTERNAL_FAILURE,
        "When bound buffer is used, transfer request must contain only one buffer");

    auto &transfer_buffer = transfer_request.transfer_buffers[0];
    const auto num_available = CB_HEAD(m_descs);
    const auto expected_offset = static_cast<size_t>(m_desc_list->desc_page_size()) * num_available;
    CHECK(transfer_buffer.offset() == expected_offset, HAILO_INTERNAL_FAILURE,
        "Unexpected buffer offset, expected {} actual {}", expected_offset, transfer_buffer.offset());
    CHECK(transfer_buffer.base_buffer().data() == reinterpret_cast<const uint8_t*>(m_bounded_buffer->user_address()), HAILO_INTERNAL_FAILURE,
        "Got the wrong buffer");
    CHECK(transfer_buffer.base_buffer().size() == m_bounded_buffer->size(), HAILO_INTERNAL_FAILURE,
        "Got invalid buffer size {}, expected {}", transfer_buffer.base_buffer().size(), m_bounded_buffer->size());
    return HAILO_SUCCESS;
}

} /* namespace vdma */
} /* namespace hailort */
