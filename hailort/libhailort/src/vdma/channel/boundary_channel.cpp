/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file boundary_channel.cpp
 * @brief BoundaryChannel - Base class functionality
 **/

#include "hailo/hailort_common.hpp"

#include "common/os_utils.hpp"

#include "vdma/channel/boundary_channel.hpp"

#include <list>
#include <chrono>
#include <thread>
#include <iostream>
#include "utils.h"


namespace hailort {
namespace vdma {

Expected<BoundaryChannelPtr> BoundaryChannel::create(HailoRTDriver &driver, vdma::ChannelId channel_id,
    Direction direction, vdma::DescriptorList &&desc_list, TransferLauncher &transfer_launcher,
    size_t queue_size, bool split_transfer, const std::string &stream_name, LatencyMeterPtr latency_meter)
{
    hailo_status status = HAILO_UNINITIALIZED;
    auto channel_ptr = make_shared_nothrow<BoundaryChannel>(driver, channel_id, direction, std::move(desc_list),
        transfer_launcher, queue_size, split_transfer, stream_name, latency_meter, status);
    CHECK_NOT_NULL_AS_EXPECTED(channel_ptr, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status, "Failed creating BoundaryChannel");
    return channel_ptr;
}

size_t BoundaryChannel::get_chunk_size() const
{
    return static_cast<size_t>(MAX_SG_DESCS_COUNT * m_desc_list.desc_page_size() / OPTIMAL_CHUNKS_DIVISION_FACTOR);
}

BoundaryChannel::BoundaryChannel(HailoRTDriver &driver, vdma::ChannelId channel_id, Direction direction,
                                 DescriptorList &&desc_list, TransferLauncher &transfer_launcher,
                                 size_t queue_size,
                                 bool split_transfer, const std::string &stream_name,
                                 LatencyMeterPtr latency_meter, hailo_status &status) :
    m_channel_id(channel_id),
    m_direction(direction),
    m_driver(driver),
    m_transfer_launcher(transfer_launcher),
    m_desc_list(std::move(desc_list)),
    m_stream_name(stream_name),
    m_descs(m_desc_list.count()),
    m_is_channel_activated(false),
    m_channel_mutex(),
    // When measuring latency, we use 2 interrupts per transfer, so we have half the space for ongoing transfers
    m_ongoing_transfers((latency_meter == nullptr ) ? ONGOING_TRANSFERS_SIZE : ONGOING_TRANSFERS_SIZE / 2),
    // CircularArrays with storage_size x can store x-1 elements, hence the +1
    m_pending_transfers(queue_size + 1),
    m_latency_meter(latency_meter),
    m_pending_latency_measurements(ONGOING_TRANSFERS_SIZE), // Make sure there will always be place for latency measure
    m_last_timestamp_num_processed(0),
    m_bounded_buffer(nullptr),
    m_split_transfer(split_transfer)
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

    status = HAILO_SUCCESS;
}

// Function that based off the irq data returns the status to be sent to the callbak functions
static hailo_status get_callback_status(vdma::ChannelId channel_id, const ChannelIrqData &irq_data)
{
    hailo_status status = HAILO_UNINITIALIZED;
    if (!irq_data.is_active) {
        status = HAILO_STREAM_ABORT;
    } else if (!irq_data.validation_success) {
        LOGGER__WARNING("Channel {} validation failed", channel_id);
        status = HAILO_INTERNAL_FAILURE;
    } else {
        status = HAILO_SUCCESS;
    }
    return status;
}

hailo_status BoundaryChannel::trigger_channel_completion(const ChannelIrqData &irq_data)
{
    std::unique_lock<std::mutex> lock(m_channel_mutex);

    if (!m_is_channel_activated) {
        return HAILO_STREAM_NOT_ACTIVATED;
    }

    if (m_latency_meter != nullptr) {
        CHECK_SUCCESS(update_latency_meter());
    }

    CHECK(irq_data.transfers_completed <= m_ongoing_transfers.size(), HAILO_INTERNAL_FAILURE,
        "Invalid amount of completed transfers {} max {}", irq_data.transfers_completed, m_ongoing_transfers.size());

    auto callback_status = get_callback_status(m_channel_id, irq_data);

    // If channel is no longer active - all transfers should be completed
    const size_t num_transfers_to_trigger = (HAILO_SUCCESS == callback_status) ? irq_data.transfers_completed :
        m_ongoing_transfers.size();
    size_t i = 0;
    while (i < num_transfers_to_trigger) {
        auto transfer = std::move(m_ongoing_transfers.front());
        m_ongoing_transfers.pop_front();
        if (HAILO_SUCCESS != transfer.launch_status) {
            // The transfer failed to launch to begin with. We need to call the callback with the failure status.
            on_request_complete(lock, transfer.request, transfer.launch_status);

            // Continue to the next transfer, without counting it, since num_transfers_to_trigger is the number of
            // completed transfers that were launched to begin with.
            continue;
        }

        // We increase desc num_proc (can happen only in this flow). After it is increased -
        //  1. On D2H channels - the output can be read by the user.
        //  2. On H2D channels - new input can be written to the buffer.
        m_descs.set_tail((transfer.last_desc + 1) & m_descs.size_mask());

        if (!m_pending_transfers.empty()) {
            m_transfer_launcher.enqueue_transfer([this]() {
                std::unique_lock<std::mutex> lock(m_channel_mutex);
                // There can be more transfers deferred to the m_transfer_launcher than need to be launched.
                // E.g. If num_transfers_to_trigger is 2, but only one transfer is pending in m_pending_transfers.
                //      (we still need to handle the transfers via the m_transfer_launcher to keep their order).
                if (m_pending_transfers.empty()) {
                    return;
                }
                // If at a given moment there's no room for new m_ongoing_transfers, we'll leave the pending transfers
                // in the m_pending_transfers queue, and they will be launched when there's room.
                if (m_ongoing_transfers.full()) {
                    return;
                }
                // Note: We don't check the return value of launch_and_enqueue_transfer, since failed transfers will be queued
                //       to m_ongoing_transfers (due to QUEUE_FAILED_TRANSFER being true). This is needed to keep the
                //       callback order consistent.
                static const auto QUEUE_FAILED_TRANSFER = true;
                const auto desired_desc_num = m_desc_list.descriptors_in_buffer(m_pending_transfers.front().get_total_transfer_size());
                if (desired_desc_num < free_descs()) {
                    auto transfer_request = std::move(m_pending_transfers.front());
                    m_pending_transfers.pop_front();
                    (void) launch_and_enqueue_transfer(std::move(transfer_request), QUEUE_FAILED_TRANSFER);
                } else {
                    assert(m_ongoing_transfers.size() > 0);
                }
            });
        }

        // Call the user callback
        // We want to do this after launching transfers queued in m_pending_transfers, in order to keep the
        // callback order consistent.
        // Also, we want to make sure that the callbacks are called after the descriptors can be reused (so the user
        // will be able to start new transfer).
        on_request_complete(lock, transfer.request, callback_status);
        i++;
    }

    return HAILO_SUCCESS;
}

hailo_status BoundaryChannel::activate()
{
    std::lock_guard<std::mutex> lock(m_channel_mutex);

    CHECK(!m_is_channel_activated, HAILO_INTERNAL_FAILURE,
        "Vdma channel {} is already activated", m_channel_id);
    m_is_channel_activated = true;
    assert(m_ongoing_transfers.empty());
    m_last_timestamp_num_processed = 0;
    m_descs.reset();

    return HAILO_SUCCESS;
}

void BoundaryChannel::deactivate()
{
    std::unique_lock<std::mutex> lock(m_channel_mutex);
    m_is_channel_activated = false;
}

uint16_t BoundaryChannel::free_descs()
{
    const auto num_available = m_descs.head();
    const auto num_processed = m_descs.tail();
    const auto num_free = m_descs.avail(num_available, num_processed);

    return static_cast<uint16_t>(num_free);
}

Expected<std::vector<TransferRequest>> BoundaryChannel::split_messages(TransferRequest &&original_request)
{
    const auto chunk_size = get_chunk_size();

    if (!m_split_transfer || (original_request.transfer_buffers.at(0).type() != TransferBufferType::MEMORYVIEW)) {
        // Split not supported
        return std::vector<TransferRequest>{original_request};
    }

    // From original_request, create a vector of several TransferRequests.
    // Each TransferRequest may be splitted into serveral buffers, but the total size of the buffers in each
    // TransferRequest will not exceed chunk_size (which is the optimal amount of bytes for single transfer).
    // In addition, each TransferRequest should hold no more than MAX_TRANSFER_BUFFERS_IN_REQUEST buffers.
    // Notice that each new transfer will consume a full descriptor in bytes (even if the size is smaller than
    // descriptors size).
    std::vector<TransferRequest> transfer_request_split;
    TransferRequest current_transfer{};
    size_t current_transfer_consumed_bytes = 0;

    for (auto &buffer : original_request.transfer_buffers) {
        size_t bytes_processed = 0;
        while (bytes_processed < buffer.size()) {
            assert(chunk_size > current_transfer_consumed_bytes);
            const auto size_left_in_transfer = chunk_size - current_transfer_consumed_bytes;
            size_t amount_to_transfer = std::min(buffer.size() - bytes_processed, size_left_in_transfer);
            assert(amount_to_transfer > 0);

            TRY(auto base_buffer, buffer.base_buffer());
            auto sub_buffer = MemoryView(base_buffer.data() + bytes_processed, amount_to_transfer);
            bytes_processed += amount_to_transfer;
            current_transfer.transfer_buffers.push_back(TransferBuffer{sub_buffer});
            const auto desc_consumed = m_desc_list.descriptors_in_buffer(amount_to_transfer);
            current_transfer_consumed_bytes += desc_consumed * m_desc_list.desc_page_size();

            // Start a new trasnfer if reach the limit.
            if ((current_transfer_consumed_bytes >= chunk_size) ||
                current_transfer.transfer_buffers.size() >= MAX_TRANSFER_BUFFERS_IN_REQUEST) {
                transfer_request_split.emplace_back(std::move(current_transfer));
                current_transfer = TransferRequest{};
                current_transfer_consumed_bytes = 0;
            }
        }
    }

    if (current_transfer.get_total_transfer_size() > 0) {
        transfer_request_split.emplace_back(std::move(current_transfer));
    }

    // Setting the original callback for the last transfer
    transfer_request_split.back().callback = original_request.callback;

    // Removing previous bounded buffers since transfer now is split
    m_bounded_buffer = nullptr;

    return transfer_request_split;
}

hailo_status BoundaryChannel::launch_transfer(TransferRequest &&transfer_request)
{
    std::unique_lock<std::mutex> lock(m_channel_mutex);
    if (!m_is_channel_activated) {
        return HAILO_STREAM_NOT_ACTIVATED;
    }

    TRY(auto transfer_request_split, split_messages(std::move(transfer_request)));
    for (auto &transfer : transfer_request_split) {
        const auto desired_desc_num = m_desc_list.descriptors_in_buffer(transfer.get_total_transfer_size());
        if ((m_ongoing_transfers.size() < m_ongoing_transfers.capacity()) &&
            (m_pending_transfers.size() == 0) && (desired_desc_num < static_cast<uint32_t>(free_descs()))) {
            // There's room in the desc list and there are no pending transfers or callbacks => execute on user's thread
            // We can't use the user thread to launch the transfer if there are pending transfers, because we need to
            // preserve the order of the transfers.
            auto status = launch_and_enqueue_transfer(std::move(transfer));
            CHECK_SUCCESS(status);
        } else if (m_pending_transfers.size() >= m_pending_transfers.capacity()) {
            return HAILO_QUEUE_IS_FULL;
        } else {
            // Defer to the transfer launcher
            m_pending_transfers.push_back(std::move(transfer));
        }
    }
    return HAILO_SUCCESS;
}

// Assumes:
// * m_channel_mutex is locked
// * m_ongoing_transfers.size() < m_ongoing_transfers.capacity()
hailo_status BoundaryChannel::launch_and_enqueue_transfer(TransferRequest &&transfer_request, bool queue_failed_transfer)
{
    auto last_desc = launch_transfer_impl(transfer_request);
    if (!last_desc) {
        if (queue_failed_transfer) {
            m_ongoing_transfers.push_back(OngoingTransfer{std::move(transfer_request), 0, last_desc.status()});
        }
        return last_desc.status();
    }
    m_ongoing_transfers.push_back(OngoingTransfer{std::move(transfer_request), last_desc.value()});

    return HAILO_SUCCESS;
}

// Assumes:
// * m_channel_mutex is locked
// * m_ongoing_transfers.size() < m_ongoing_transfers.capacity()
Expected<uint16_t> BoundaryChannel::launch_transfer_impl(TransferRequest &transfer_request)
{
    assert(m_ongoing_transfers.size() < m_ongoing_transfers.capacity());
    if (!m_is_channel_activated) {
        return make_unexpected(HAILO_STREAM_NOT_ACTIVATED);
    }

    auto num_available = static_cast<uint16_t>(m_descs.head());
    const uint16_t first_desc = num_available;
    uint16_t last_desc = std::numeric_limits<uint16_t>::max();
    uint16_t total_descs_count = 0;

    TRY(const bool should_bind, should_bind_buffer(transfer_request));
    if (should_bind) {
        m_bounded_buffer = nullptr;
    } else {
        CHECK_SUCCESS(validate_bound_buffer(transfer_request));
    }

    std::vector<HailoRTDriver::TransferBuffer> driver_transfer_buffers;

    auto current_num_available = num_available;
    for (auto &transfer_buffer : transfer_request.transfer_buffers) {
        TRY(auto mapped_buffer, transfer_buffer.map_buffer(m_driver, m_direction));
        TRY(auto driver_buffers, transfer_buffer.to_driver_buffers());
        driver_transfer_buffers.insert(driver_transfer_buffers.end(), driver_buffers.begin(), driver_buffers.end());

        const auto desired_desc_num = m_desc_list.descriptors_in_buffer(transfer_buffer.size());
        CHECK(desired_desc_num <= MAX_SG_DESCS_COUNT, HAILO_INTERNAL_FAILURE);
        const uint16_t desc_num = static_cast<uint16_t>(desired_desc_num);
        assert(total_descs_count + desc_num < MAX_SG_DESCS_COUNT);
        total_descs_count = static_cast<uint16_t>(total_descs_count + desc_num);

        last_desc = static_cast<uint16_t>((current_num_available + desc_num - 1) & m_descs.size_mask());
        current_num_available = static_cast<uint16_t>((last_desc + 1) & m_descs.size_mask());
    }

    auto first_desc_interrupts = InterruptsDomain::NONE;
    if ((nullptr != m_latency_meter) && (m_direction == Direction::H2D)) {
        // If we measure latency, we need an interrupt on the first descriptor for each H2D channel.
        first_desc_interrupts = InterruptsDomain::HOST;
    }
    const auto last_desc_interrupts = InterruptsDomain::HOST;

    if (total_descs_count > static_cast<uint16_t>(free_descs())) {
        return make_unexpected(HAILO_OUT_OF_DESCRIPTORS);
    }

    if (m_latency_meter) {
        assert(!m_pending_latency_measurements.full());
        m_pending_latency_measurements.push_back(m_direction == Direction::H2D ? first_desc : last_desc);
    }
    m_descs.enqueue(total_descs_count);

    auto status = m_driver.launch_transfer(
        m_channel_id,
        m_desc_list.handle(),
        num_available,
        driver_transfer_buffers,
        should_bind,
        first_desc_interrupts,
        last_desc_interrupts);
    CHECK_SUCCESS_WITH_ACCEPTABLE_STATUS(HAILO_STREAM_ABORT, status);

    return last_desc;
}

hailo_status BoundaryChannel::bind_buffer(MappedBufferPtr buffer)
{
    std::lock_guard<std::mutex> lock(m_channel_mutex);
    CHECK(m_bounded_buffer == nullptr, HAILO_INTERNAL_FAILURE,
        "Buffer is already bound to channel {}", m_channel_id);
    const auto expected_size = static_cast<size_t>(m_desc_list.desc_page_size()) * m_desc_list.count();
    CHECK(buffer->size() <= expected_size, HAILO_INVALID_ARGUMENT,
        "Buffer size {} does not fit in desc list - descs count {} desc page size {}", buffer->size(),
        m_desc_list.count(), m_desc_list.desc_page_size());
    static const size_t DEFAULT_BUFFER_OFFSET = 0;
    CHECK_SUCCESS(m_desc_list.program(*buffer, buffer->size(), DEFAULT_BUFFER_OFFSET, m_channel_id));
    m_bounded_buffer = buffer;
    return HAILO_SUCCESS;
}

hailo_status BoundaryChannel::map_and_bind_buffer(hailort::TransferBuffer &transfer_request)
{
    TRY(auto mapped_buffer, transfer_request.map_buffer(m_driver, m_direction));
    return(bind_buffer(mapped_buffer));
}

void BoundaryChannel::remove_buffer_binding()
{
    std::lock_guard<std::mutex> lock(m_channel_mutex);
    m_bounded_buffer = nullptr;
}

void BoundaryChannel::cancel_pending_transfers()
{
    std::unique_lock<std::mutex> lock(m_channel_mutex);

    // Cancel all ongoing transfers
    while (!m_ongoing_transfers.empty()) {
        auto transfer = std::move(m_ongoing_transfers.front());
        m_ongoing_transfers.pop_front();

        on_request_complete(lock, transfer.request, HAILO_STREAM_ABORT);
    }

    // Then cancel all pending transfers (which were to happen after the ongoing transfers are done)
    while (!m_pending_transfers.empty()) {
        auto pending_transfer = std::move(m_pending_transfers.front());
        m_pending_transfers.pop_front();

        on_request_complete(lock, pending_transfer, HAILO_STREAM_ABORT);
    }
}

size_t BoundaryChannel::get_max_ongoing_transfers(size_t transfer_size) const
{
    size_t divide_factor = m_split_transfer ? DIV_ROUND_UP(transfer_size, get_chunk_size()) : 1;
    return m_pending_transfers.capacity() / divide_factor;
}

bool BoundaryChannel::is_ready(size_t transfer_size) const
{
    return DIV_ROUND_UP(transfer_size, get_chunk_size()) < (m_pending_transfers.capacity() - m_pending_transfers.size());
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

void BoundaryChannel::on_request_complete(std::unique_lock<std::mutex> &lock, TransferRequest &request,
    hailo_status complete_status)
{
    if (m_bounded_buffer) {
        bool is_cyclic_buffer = (static_cast<size_t>(m_descs.size() * m_desc_list.desc_page_size()) == m_bounded_buffer->size());
        if (!is_cyclic_buffer) {
            // For not cyclic buffer, we need to unmap the buffer (on cyclic the buffer is owned by the stream)
            m_bounded_buffer.reset();
        }
    }

    // On the callback the user can free the buffer so need to verify the buffer is unmapped before calling the callback
    for (auto &transfer_buffer : request.transfer_buffers) {
        transfer_buffer.unmap_buffer();
    }

    lock.unlock();
    request.callback(complete_status);
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

hailo_status BoundaryChannel::validate_bound_buffer(TransferRequest &transfer_request)
{
    assert(m_bounded_buffer);
    CHECK(transfer_request.transfer_buffers.size() == 1, HAILO_INTERNAL_FAILURE,
        "When bound buffer is used, transfer request must contain only one buffer");

    auto &transfer_buffer = transfer_request.transfer_buffers[0];
    const auto num_available = m_descs.head();
    const auto expected_offset = static_cast<size_t>(m_desc_list.desc_page_size()) * num_available;
    CHECK(transfer_buffer.offset() == expected_offset, HAILO_INTERNAL_FAILURE,
        "Unexpected buffer offset, expected {} actual {}", expected_offset, transfer_buffer.offset());
    TRY(auto is_same_buffer, is_same_buffer(m_bounded_buffer, transfer_buffer));
    if (!is_same_buffer) {
        LOGGER__ERROR("Got diff in buffers");
        return HAILO_INTERNAL_FAILURE;
    }

    return HAILO_SUCCESS;
}

Expected<bool> BoundaryChannel::is_same_buffer(MappedBufferPtr mapped_buff, TransferBuffer &transfer_buffer)
{
    if (transfer_buffer.type() == TransferBufferType::DMABUF) {
        TRY(auto buf_fd, transfer_buffer.dmabuf_fd());
        TRY(auto mapped_buf_fd, mapped_buff->fd());
        return ((buf_fd == mapped_buf_fd) && (transfer_buffer.size() == mapped_buff->size()));
    } else {
        auto base_buffer = transfer_buffer.base_buffer();
        return ((base_buffer.value().data() == mapped_buff->user_address()) &&
            (base_buffer.value().size() == mapped_buff->size()));
    }
}

Expected<bool> BoundaryChannel::should_bind_buffer(TransferRequest &transfer_request)
{
    if ((nullptr == m_bounded_buffer) || (1 < transfer_request.transfer_buffers.size())) {
        return true;
    }

    bool is_cyclic_buffer = (static_cast<size_t>(m_descs.size() * m_desc_list.desc_page_size()) == m_bounded_buffer->size());
    /* If the buffer is cyclic, sync api is used, means no bind needed.
        Checking if the bounded buffer points correctly to the received buffer
        and the descriptors are pointing to the beginning of the buffer. */
    if (!is_cyclic_buffer) {
        TRY(auto is_same_buffer, is_same_buffer(m_bounded_buffer, transfer_request.transfer_buffers[0]));
        return !(is_same_buffer && (0 == m_descs.head()));
    }

    return false;
}

} /* namespace vdma */
} /* namespace hailort */
