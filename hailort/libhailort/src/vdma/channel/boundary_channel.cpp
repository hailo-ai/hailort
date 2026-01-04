/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file boundary_channel.cpp
 * @brief BoundaryChannel - Base class functionality
 **/

#include "hailo/hailort_common.hpp"
#include "vdma/channel/boundary_channel.hpp"
#include "common/os_utils.hpp"
#include "utils.h"


namespace hailort {
namespace vdma {

Expected<BoundaryChannelPtr> BoundaryChannel::create(HailoRTDriver &driver, vdma::ChannelId channel_id,
    Direction direction, vdma::DescriptorList &&desc_list, TransferLauncher &transfer_launcher,
    size_t queue_size, bool split_transfer, const std::string &stream_name, LatencyMeterPtr latency_meter)
{
    CHECK(Direction::BOTH != direction, HAILO_INVALID_ARGUMENT,
          "Boundary channels must be unidirectional");

    CHECK(channel_id.channel_index < VDMA_CHANNELS_PER_ENGINE, HAILO_INVALID_ARGUMENT,
          "Invalid DMA channel index {}", channel_id.channel_index);

    CHECK(channel_id.engine_index < driver.dma_engines_count(), HAILO_INVALID_ARGUMENT,
          "Invalid DMA engine index {}, max {}", channel_id.engine_index, driver.dma_engines_count());

    auto channel_ptr = make_shared_nothrow<BoundaryChannel>(driver, channel_id, direction, std::move(desc_list),
        transfer_launcher, queue_size, split_transfer, stream_name, latency_meter);
    CHECK_NOT_NULL(channel_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return channel_ptr;
}

BoundaryChannel::BoundaryChannel(HailoRTDriver &driver, vdma::ChannelId channel_id, Direction direction,
    DescriptorList &&desc_list, TransferLauncher &transfer_launcher, size_t queue_size, bool split_transfer,
    const std::string &stream_name, LatencyMeterPtr latency_meter) :
    m_channel_id(channel_id),
    m_direction(direction),
    m_driver(driver),
    m_transfer_launcher(transfer_launcher),
    m_desc_list(std::move(desc_list)),
    m_stream_name(stream_name),
    m_num_launched(0),
    m_free_descs(m_desc_list.count()),
    m_is_channel_activated(false),
    // CircularArrays with storage_size x can store x-1 elements, hence the +1
    m_pending(queue_size + 1),
    // When measuring latency, we use 2 interrupts per transfer, so we have half the space for ongoing transfers
    m_ongoing((latency_meter == nullptr ) ? ONGOING_TRANSFERS_SIZE : ONGOING_TRANSFERS_SIZE / 2),
    m_latency_meter(latency_meter),
    m_pending_latency_measurements(ONGOING_TRANSFERS_SIZE),
    m_last_timestamp_num_processed(0),
    m_is_cyclic(false),
    m_should_split_transfer(split_transfer)
{}

size_t BoundaryChannel::get_chunk_size() const
{
    const auto max_sg_descs_count = m_driver.get_sg_desc_params().max_descs_count;
    return static_cast<size_t>(max_sg_descs_count * m_desc_list.desc_page_size() / OPTIMAL_CHUNKS_DIVISION_FACTOR);
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

    CHECK(transfers_completed <= m_ongoing.size(), HAILO_INTERNAL_FAILURE,
        "Invalid amount of completed transfers {} max {}", transfers_completed, m_ongoing.size());

    for (size_t i = 0; i < transfers_completed; i++) {
        auto transfer = std::move(m_ongoing.front());
        m_ongoing.pop_front();

        m_free_descs += transfer.num_descs;

        if (!m_pending.empty()) {
            m_transfer_launcher.enqueue_transfer([this]() {
                std::unique_lock<std::mutex> lock(m_channel_mutex);

                if (!m_is_channel_activated) {
                    return;
                }

                if (m_pending.empty() || m_ongoing.full()) {
                    return;
                }

                if (m_pending.front().num_descs >= m_free_descs) {
                    return;
                }

                auto transfer = std::move(m_pending.front());
                m_pending.pop_front();
                auto status = launch_transfer_impl(transfer);
                if (HAILO_SUCCESS != status) {
                    on_request_complete(lock, transfer.request, status);
                    return;
                }
                m_ongoing.push_back(std::move(transfer));
            });
        }

        on_request_complete(lock, transfer.request, HAILO_SUCCESS);
    }

    return HAILO_SUCCESS;
}

void BoundaryChannel::trigger_channel_error(hailo_status status)
{
    std::unique_lock<std::mutex> lock(m_channel_mutex);
    m_is_channel_activated = false;

    while (!m_ongoing.empty()) {
        auto transfer = std::move(m_ongoing.front());
        m_ongoing.pop_front();
        on_request_complete(lock, transfer.request, status);
    }

    while (!m_pending.empty()) {
        auto pending_transfer = std::move(m_pending.front());
        m_pending.pop_front();
        on_request_complete(lock, pending_transfer.request, status);
    }
}

hailo_status BoundaryChannel::activate()
{
    std::lock_guard<std::mutex> lock(m_channel_mutex);

    CHECK(!m_is_channel_activated, HAILO_INTERNAL_FAILURE, "Vdma channel {} is already activated", m_channel_id);

    m_is_channel_activated = true;
    m_last_timestamp_num_processed = 0;
    m_num_launched = 0;
    m_free_descs = m_desc_list.count();

    return HAILO_SUCCESS;
}

void BoundaryChannel::deactivate()
{
    std::unique_lock<std::mutex> lock(m_channel_mutex);
    m_is_channel_activated = false;
}

static std::vector<TransferBuffer> create_transfer_chunk(const std::vector<TransferBuffer> &request_buffers, uint32_t offset)
{
    const auto size = std::min(request_buffers.size() - offset, MAX_TRANSFER_BUFFERS_IN_REQUEST);
    std::vector<TransferBuffer> transfer_buffers;
    transfer_buffers.reserve(size);
    for (size_t i = offset; i < offset + size; i++) {
        transfer_buffers.push_back(std::move(request_buffers[i]));
    }
    return transfer_buffers;
}

Expected<std::vector<TransferChunk>> BoundaryChannel::split_transfer(TransferRequest &&request)
{
    if (!m_should_split_transfer) {
        // Split not supported
        CHECK(request.transfer_buffers.size() <= MAX_TRANSFER_BUFFERS_IN_REQUEST,
              HAILO_INVALID_ARGUMENT, "too many buffers in transfer request: {}, max is: {}",
              request.transfer_buffers.size(), MAX_TRANSFER_BUFFERS_IN_REQUEST);

        return std::vector<TransferChunk>{
            TransferChunk(std::move(request.transfer_buffers), std::move(request.callback), m_desc_list.desc_page_size())
        };
    }

    if (request.transfer_buffers.at(0).is_dmabuf()) {
        std::vector<TransferChunk> transfer_chunks;
        const auto split_count = DIV_ROUND_UP(request.transfer_buffers.size(), MAX_TRANSFER_BUFFERS_IN_REQUEST);
        transfer_chunks.reserve(split_count);

        for (uint32_t i = 0; i < split_count; i++) {
            uint32_t offset = i * static_cast<uint32_t>(MAX_TRANSFER_BUFFERS_IN_REQUEST);
            bool is_last_chunk = (i == static_cast<uint32_t>(split_count - 1));
            transfer_chunks.emplace_back(create_transfer_chunk(request.transfer_buffers, offset),
                                          is_last_chunk ? request.callback : [](hailo_status){},
                                          m_desc_list.desc_page_size());
        }

        return transfer_chunks;
    }

    // Will split a single request into a vector of TransferChunks. The size of each chunk will be as large as possible,
    // but no greater than `chunk_size`. Buffers within the original request may be split but they will remain
    // dma-aligned.
    // Assumes TransferBuffers are pointers (not dmabufs), that they are dma-aligned and that they have zero offset.
    const auto alignment = OsUtils::get_dma_able_alignment();
    const auto chunk_size = get_chunk_size();
    std::vector<TransferChunk> transfer_chunks;
    std::vector<TransferBuffer> current_request_buffers;
    size_t bytes_used = 0;
    size_t idx = 0;
    // We reserve two extra transfer requests: one because of floor-division and the second because
    // DMA-alignment may force us to add an additional request.
    transfer_chunks.reserve((request.get_total_transfer_size() / chunk_size) + 2);
    current_request_buffers.reserve(MAX_TRANSFER_BUFFERS_IN_REQUEST);

    while (idx < request.transfer_buffers.size()) {

        // Add buffer to request only if we have space.
        if (current_request_buffers.size() < MAX_TRANSFER_BUFFERS_IN_REQUEST) {
            auto &buffer = request.transfer_buffers[idx];

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
                CHECK(!buffer.is_dmabuf(), HAILO_INVALID_OPERATION, "cannot split dmabuf");
                CHECK(0 == buffer.offset(), HAILO_INVALID_OPERATION, "cannot split buffer with non-zero offset");

                TRY(auto base_buffer, buffer.base_buffer());

                // The first chunk of the buffer is used in the new request.
                current_request_buffers.emplace_back(MemoryView(base_buffer.data(), aligned_offset));

                // The tail of the buffer is added back to the original vector to be used next time.
                request.transfer_buffers[idx] =
                    MemoryView(base_buffer.data() + aligned_offset, buffer.size() - aligned_offset);
            }
        }

        // Add new OngoingTransfer to split, reset and start again.
        transfer_chunks.emplace_back(std::move(current_request_buffers),
                                      [](hailo_status){},
                                      m_desc_list.desc_page_size());

        current_request_buffers.clear();
        bytes_used = 0;
    }

    // Setting the original callback for the last transfer
    transfer_chunks.emplace_back(std::move(current_request_buffers),
                                  std::move(request.callback),
                                  m_desc_list.desc_page_size());

    return transfer_chunks;
}

hailo_status BoundaryChannel::launch_transfer(TransferRequest &&transfer_request)
{
    std::unique_lock<std::mutex> lock(m_channel_mutex);

    CHECK(m_is_channel_activated, HAILO_STREAM_NOT_ACTIVATED);

    TRY(auto transfer_chunks, split_transfer(std::move(transfer_request)));
    for (auto &transfer : transfer_chunks) {
        if ((m_pending.size() > 0) || (transfer.num_descs >= m_free_descs) || (m_ongoing.full())) {
            // There are pending transfers or not engough room: add transfer to the back of the queue.
            CHECK(!m_pending.full(), HAILO_QUEUE_IS_FULL);
            m_pending.push_back(std::move(transfer));
            continue;
        }

        // There is room and no transfers are pending: launch now.
        auto status = launch_transfer_impl(transfer);
        CHECK_SUCCESS_WITH_ACCEPTABLE_STATUS(HAILO_STREAM_ABORT, status);
        m_ongoing.push_back(std::move(transfer));
    }

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

hailo_status BoundaryChannel::prepare_transfer(TransferRequest &&transfer_request)
{
    std::lock_guard<std::mutex> lock(m_channel_mutex);

    CHECK(!m_is_channel_activated, HAILO_INTERNAL_FAILURE, "Failed prepare-transfer: channel is already activated");

    uint32_t total_descs = 0;
    for (auto &buffer : transfer_request.transfer_buffers) {
        total_descs += m_desc_list.descriptors_in_buffer(buffer.size());
    }
    if (total_descs >= m_free_descs) {
        // Best effort. If there is no room in the descriptor-list just skip prepare.
        return HAILO_SUCCESS;
    }

    auto status = m_driver.hailo_vdma_prepare_transfer(m_channel_id, m_desc_list.handle(),
        transfer_request.transfer_buffers, get_first_interrupts_domain(), InterruptsDomain::HOST);
    CHECK_SUCCESS_WITH_ACCEPTABLE_STATUS(HAILO_STREAM_ABORT, status);

    m_free_descs -= total_descs;

    return HAILO_SUCCESS;
}

// Assumes:
// * m_channel_mutex is locked
// * m_is_channel_activated is true
// * transfer.total_descs < m_num_free_descs
hailo_status BoundaryChannel::launch_transfer_impl(TransferChunk &chunk)
{
    const auto total_descs = chunk.num_descs;
    const uint16_t first_desc = static_cast<uint16_t>(m_num_launched);
    const uint16_t last_desc = static_cast<uint16_t>((m_num_launched + total_descs - 1) % m_desc_list.count());
    if (m_latency_meter) {
        m_pending_latency_measurements.push_back(m_direction == Direction::H2D ? first_desc : last_desc);
    }

    auto status = m_driver.launch_transfer(m_channel_id, m_desc_list.handle(), chunk.request.transfer_buffers,
        m_is_cyclic, get_first_interrupts_domain(), InterruptsDomain::HOST);
    CHECK_SUCCESS_WITH_ACCEPTABLE_STATUS(HAILO_STREAM_ABORT, status);

    m_free_descs -= total_descs;
    m_num_launched = (m_num_launched + total_descs) % m_desc_list.count();

    return HAILO_SUCCESS;
}

hailo_status BoundaryChannel::bind_cyclic_buffer(MappedBufferPtr buffer)
{
    std::lock_guard<std::mutex> lock(m_channel_mutex);

    CHECK(!m_is_cyclic, HAILO_INTERNAL_FAILURE, "Cyclic buffer is already bound to channel {}", m_channel_id);

    const auto expected_size = static_cast<size_t>(m_desc_list.desc_page_size()) * m_desc_list.count();
    CHECK(buffer->size() == expected_size, HAILO_INVALID_ARGUMENT,
        "Buffer size {} does not fit in desc list - descs count {} desc page size {}",
        buffer->size(), m_desc_list.count(), m_desc_list.desc_page_size());

    static const size_t DEFAULT_BUFFER_OFFSET = 0;
    CHECK_SUCCESS(m_desc_list.program(buffer, buffer->size(), DEFAULT_BUFFER_OFFSET, m_channel_id));

    m_is_cyclic = true;

    return HAILO_SUCCESS;
}

void BoundaryChannel::cancel_pending_transfers()
{
    std::unique_lock<std::mutex> lock(m_channel_mutex);

    // Cancel all ongoing transfers
    while (!m_ongoing.empty()) {
        auto transfer = std::move(m_ongoing.front());
        m_ongoing.pop_front();
        on_request_complete(lock, transfer.request, HAILO_STREAM_ABORT);
    }

    // Then cancel all pending transfers (which were to happen after the ongoing transfers are done)
    while (!m_pending.empty()) {
        auto pending_transfer = std::move(m_pending.front());
        m_pending.pop_front();
        on_request_complete(lock, pending_transfer.request, HAILO_STREAM_ABORT);
    }
    m_num_launched = 0;
    m_free_descs = m_desc_list.count();
}

hailo_status BoundaryChannel::cancel_prepared_transfers()
{
    std::unique_lock<std::mutex> lock(m_channel_mutex);
    auto status = m_driver.cancel_prepared_transfers(m_desc_list.handle());
    CHECK_SUCCESS(status, "Failed cancelling prepared transfers for channel {}", m_channel_id);

    m_num_launched = 0;
    m_free_descs = m_desc_list.count();
    return HAILO_SUCCESS;
}

size_t BoundaryChannel::get_max_ongoing_transfers(size_t transfer_size) const
{
    size_t divide_factor = m_should_split_transfer ? DIV_ROUND_UP(transfer_size, get_chunk_size()) : 1;
    return m_pending.capacity() / divide_factor;
}

bool BoundaryChannel::is_ready(size_t transfer_size) const
{
    return DIV_ROUND_UP(transfer_size, get_chunk_size()) < (m_pending.capacity() - m_pending.size());
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

} /* namespace vdma */
} /* namespace hailort */
