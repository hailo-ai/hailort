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
#include <tuple>
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
    const auto max_sg_descs_count = m_driver.get_sg_desc_params().max_descs_count;
    return static_cast<size_t>(max_sg_descs_count * m_desc_list.desc_page_size() / OPTIMAL_CHUNKS_DIVISION_FACTOR);
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
    m_num_launched(0),
    m_num_programmed(0),
    m_num_free_descs(m_desc_list.count()), // Initially all descriptors are free
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

hailo_status BoundaryChannel::trigger_channel_completion(size_t transfers_completed)
{
    std::unique_lock<std::mutex> lock(m_channel_mutex);

    if (!m_is_channel_activated) {
        return HAILO_STREAM_NOT_ACTIVATED;
    }

    if (m_latency_meter != nullptr) {
        CHECK_SUCCESS(update_latency_meter());
    }

    CHECK(transfers_completed <= m_ongoing_transfers.size(), HAILO_INTERNAL_FAILURE,
        "Invalid amount of completed transfers {} max {}", transfers_completed, m_ongoing_transfers.size());

    size_t i = 0;
    while (i < transfers_completed) {
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
        m_num_free_descs += transfer.total_descs;

        if (!m_pending_transfers.empty()) {
            m_transfer_launcher.enqueue_transfer([this]() {
                std::unique_lock<std::mutex> lock(m_channel_mutex);

                // Check under lock to make sure the channel is still activated
                if (!m_is_channel_activated) {
                    return;
                }

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
                if (desired_desc_num < m_num_free_descs) {
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
        on_request_complete(lock, transfer.request, HAILO_SUCCESS);
        i++;
    }

    return HAILO_SUCCESS;
}

void BoundaryChannel::trigger_channel_error(hailo_status status)
{
    assert(status != HAILO_SUCCESS);

    std::unique_lock<std::mutex> lock(m_channel_mutex);
    m_is_channel_activated = false;

    while (!m_ongoing_transfers.empty()) {
        auto transfer = std::move(m_ongoing_transfers.front());
        m_ongoing_transfers.pop_front();
        on_request_complete(lock, transfer.request, status);
    }

    while (!m_pending_transfers.empty()) {
        auto pending_transfer = std::move(m_pending_transfers.front());
        m_pending_transfers.pop_front();
        on_request_complete(lock, pending_transfer, status);
    }
}

hailo_status BoundaryChannel::activate()
{
    std::lock_guard<std::mutex> lock(m_channel_mutex);

    CHECK(!m_is_channel_activated, HAILO_INTERNAL_FAILURE,
        "Vdma channel {} is already activated", m_channel_id);
    m_is_channel_activated = true;
    assert(m_ongoing_transfers.empty());
    m_last_timestamp_num_processed = 0;
    m_num_programmed = 0;
    m_num_launched = 0;
    m_num_free_descs = m_desc_list.count();

    return HAILO_SUCCESS;
}

void BoundaryChannel::deactivate()
{
    std::unique_lock<std::mutex> lock(m_channel_mutex);
    m_is_channel_activated = false;
}

Expected<std::vector<TransferRequest>> BoundaryChannel::split_messages(TransferRequest &&original_request)
{
    if (!m_split_transfer || (original_request.transfer_buffers.at(0).type() != TransferBufferType::MEMORYVIEW)) {
        // Split not supported
        CHECK(original_request.transfer_buffers.size() <= MAX_TRANSFER_BUFFERS_IN_REQUEST, HAILO_INVALID_ARGUMENT,
            "too many buffers in transfer request: {}, max is: {}",
            original_request.transfer_buffers.size(), MAX_TRANSFER_BUFFERS_IN_REQUEST);
        return std::vector<TransferRequest>{original_request};
    }

    // Will split a single request into a vector of requests. The size of each request will be as large as possible, but
    // no greater than `chunk_size`. Buffers within the original request may be split but they will remain dma-aligned.
    // Assumes TransferBuffers are pointers (not dmabufs), that they are dma-aligned and that they have zero offset.
    const auto alignment = OsUtils::get_dma_able_alignment();
    const auto chunk_size = get_chunk_size();
    std::vector<TransferRequest> transfer_request_split;
    std::vector<TransferBuffer> current_request_buffers;
    size_t bytes_used = 0;
    size_t idx = 0;
    // We reserve two extra transfer requests: one because of floor-division and the second because
    // DMA-alignment may force us to add an additional request.
    transfer_request_split.reserve((original_request.get_total_transfer_size() / chunk_size) + 2);
    current_request_buffers.reserve(original_request.transfer_buffers.size());

    while (idx < original_request.transfer_buffers.size()) {

        // Add buffer to request only if we have space.
        if (current_request_buffers.size() < MAX_TRANSFER_BUFFERS_IN_REQUEST) {
            auto &buffer = original_request.transfer_buffers[idx];

            // Take next buffer so long as we use less than chunk_size bytes.
            if (bytes_used + buffer.size() <= chunk_size) {
                idx++;
                bytes_used += buffer.size();
                current_request_buffers.push_back(std::move(buffer));
                continue;
            }

            // If we got here we have a buffer that needs to be split.
            const size_t aligned_offset = chunk_size - HailoRTCommon::align_to(bytes_used, alignment);
            if (aligned_offset > 0) {
                CHECK(TransferBufferType::MEMORYVIEW == buffer.type(), HAILO_INVALID_OPERATION, "cannot split dmabuf");
                CHECK(0 == buffer.offset(), HAILO_INVALID_OPERATION, "cannot split buffer with non-zero offset");

                TRY(auto base_buffer, buffer.base_buffer());

                // The first part of the buffer is used in the new request.
                current_request_buffers.emplace_back(MemoryView(base_buffer.data(), aligned_offset));

                // The tail of the buffer is added back to the original vector to be used next time.
                original_request.transfer_buffers[idx] =
                    MemoryView(base_buffer.data() + aligned_offset, buffer.size() - aligned_offset);
            }
        }

        // Add new TransferRequest to split, reset and start again.
        transfer_request_split.emplace_back(std::move(current_request_buffers), [](hailo_status){});
        current_request_buffers.clear();
        bytes_used = 0;
    }

    // Setting the original callback for the last transfer
    transfer_request_split.emplace_back(std::move(current_request_buffers), original_request.callback);

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
            (m_pending_transfers.size() == 0) && (desired_desc_num < m_num_free_descs)) {
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
    auto total_descs = launch_transfer_impl(transfer_request);
    if (!total_descs) {
        if (queue_failed_transfer) {
            m_ongoing_transfers.push_back(OngoingTransfer{std::move(transfer_request), 0, total_descs.status()});
        }
        return total_descs.status();
    }
    m_ongoing_transfers.push_back(OngoingTransfer{std::move(transfer_request), total_descs.value()});

    return HAILO_SUCCESS;
}

InterruptsDomain BoundaryChannel::get_first_interrupts_domain() const
{
    if ((nullptr != m_latency_meter) && (m_direction == Direction::H2D)) {
        return InterruptsDomain::HOST;
    }
    // If the transfer is not H2D, we don't need to return an interrupts domain
    return InterruptsDomain::NONE;
}

Expected<std::tuple<std::vector<HailoRTDriver::TransferBuffer>, uint32_t>> BoundaryChannel::prepare_driver_transfer(TransferRequest &transfer_request)
{
    std::vector<HailoRTDriver::TransferBuffer> driver_transfer_buffers;
    uint32_t total_descs = 0;
    for (auto &transfer_buffer : transfer_request.transfer_buffers) {
        TRY(auto driver_buffers, transfer_buffer.to_driver_buffers());
        driver_transfer_buffers.insert(driver_transfer_buffers.end(), driver_buffers.begin(), driver_buffers.end());

        const auto desired_desc_num = m_desc_list.descriptors_in_buffer(transfer_buffer.size());
        const auto max_sg_descs_count = m_driver.get_sg_desc_params().max_descs_count;
        CHECK(desired_desc_num <= max_sg_descs_count, HAILO_INTERNAL_FAILURE);
        const uint16_t desc_num = static_cast<uint16_t>(desired_desc_num);
        assert(total_descs + desc_num < max_sg_descs_count);
        total_descs = static_cast<uint16_t>(total_descs + desc_num);
    }
    return std::make_tuple(driver_transfer_buffers, total_descs);
}

hailo_status BoundaryChannel::prepare_transfer(TransferRequest &&transfer_request)
{
    std::lock_guard<std::mutex> lock(m_channel_mutex);

    std::vector<HailoRTDriver::TransferBuffer> driver_transfer_buffers;
    uint32_t total_descs = 0;
    TRY(std::tie(driver_transfer_buffers, total_descs), prepare_driver_transfer(transfer_request));
    if (total_descs > m_num_free_descs) {
        return HAILO_SUCCESS;
    }
    uint32_t descs_size = m_desc_list.count();
    m_num_programmed =  static_cast<uint16_t>((m_num_programmed + total_descs) % descs_size);
    m_num_free_descs -= total_descs;
    auto status = m_driver.hailo_vdma_prepare_transfer(
        m_channel_id, m_desc_list.handle(), driver_transfer_buffers,
        get_first_interrupts_domain(), InterruptsDomain::HOST);

    CHECK_SUCCESS_WITH_ACCEPTABLE_STATUS(HAILO_STREAM_ABORT, status);
    return HAILO_SUCCESS;
}

// Assumes:
// * m_channel_mutex is locked
// * m_ongoing_transfers.size() < m_ongoing_transfers.capacity()
Expected<uint32_t> BoundaryChannel::launch_transfer_impl(TransferRequest &transfer_request)
{
    assert(m_ongoing_transfers.size() < m_ongoing_transfers.capacity());
    if (!m_is_channel_activated) {
        return make_unexpected(HAILO_STREAM_NOT_ACTIVATED);
    }
    std::vector<HailoRTDriver::TransferBuffer> driver_transfer_buffers;
    uint32_t total_descs = 0;
    TRY(std::tie(driver_transfer_buffers, total_descs), prepare_driver_transfer(transfer_request));

    if (m_num_launched >= m_num_programmed) {
        if (total_descs > m_num_free_descs) {
            return make_unexpected(HAILO_OUT_OF_DESCRIPTORS);
        }
        m_num_free_descs -= total_descs;
    }
    const uint16_t first_desc = m_num_launched;
    uint32_t descs_size = m_desc_list.count();
    uint16_t last_desc = static_cast<uint16_t>((m_num_launched + total_descs - 1) % descs_size);
    m_num_launched = static_cast<uint16_t>((m_num_launched + total_descs) % descs_size);
    if (m_latency_meter) {
        assert(!m_pending_latency_measurements.full());
        m_pending_latency_measurements.push_back(m_direction == Direction::H2D ? first_desc : last_desc);
    }
    auto status = m_driver.launch_transfer(
        m_channel_id,
        m_desc_list.handle(),
        driver_transfer_buffers,
        is_cyclic_buffer(),
        get_first_interrupts_domain(),
        InterruptsDomain::HOST);
    CHECK_SUCCESS_WITH_ACCEPTABLE_STATUS(HAILO_STREAM_ABORT, status);
    return total_descs;
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
    m_num_programmed = 0;
    m_num_launched = 0;
    m_num_free_descs = m_desc_list.count();
}

hailo_status BoundaryChannel::cancel_prepared_transfers()
{
    std::unique_lock<std::mutex> lock(m_channel_mutex);
    auto status = m_driver.cancel_prepared_transfers(m_desc_list.handle());
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed resetting prepared transfers on desc list {} (channel {}), status {}",
            m_desc_list.handle(), m_channel_id, status);
        return status;
    }
    
    m_num_programmed = 0;
    m_num_launched = 0;
    m_num_free_descs = m_desc_list.count();
    return HAILO_SUCCESS;
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
        if (!is_cyclic_buffer()) {
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

bool BoundaryChannel::is_cyclic_buffer() {
    if (nullptr == m_bounded_buffer) {
        return false;
    }

    return (static_cast<size_t>(m_desc_list.count() * m_desc_list.desc_page_size()) == m_bounded_buffer->size());
}

} /* namespace vdma */
} /* namespace hailort */
