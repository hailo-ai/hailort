/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file boundary_channel.cpp
 * @brief BoundaryChannel - Base class functionality
 **/

#include "common/os_utils.hpp"
#include "hailo/hailort_common.hpp"
#include "utils.h"

#include "vdma/channel/boundary_channel.hpp"


namespace hailort {
namespace vdma {

BoundaryChannel::BoundaryChannel(HailoRTDriver &driver, vdma::ChannelId channel_id, Direction direction,
    DescriptorList &&desc_list, size_t queue_size, bool should_split_buffers, const std::string &stream_name,
    LatencyMeterPtr latency_meter) :
    m_channel_id(channel_id),
    m_direction(direction),
    m_driver(driver),
    m_transfer_splitter(desc_list.desc_page_size(), desc_list.count(), OsUtils::get_dma_able_alignment(), should_split_buffers),
    m_desc_list(std::move(desc_list)),
    m_stream_name(stream_name),
    m_free_descs(m_desc_list.count()),
    m_is_activated(false),
    m_pending(queue_size + 1), // CircularArrays with storage-size x+1 can store x elements.
    m_ongoing(ONGOING_TRANSFERS_SIZE),
    m_latency_meter(latency_meter),
    m_is_cyclic(false)
{}

Expected<BoundaryChannelPtr> BoundaryChannel::create(HailoRTDriver &driver, vdma::ChannelId channel_id,
    Direction direction, vdma::DescriptorList &&desc_list, size_t queue_size, bool should_split_buffers,
    const std::string &stream_name, LatencyMeterPtr latency_meter)
{
    CHECK(channel_id.engine_index < driver.dma_engines_count(), HAILO_INVALID_ARGUMENT, "Invalid DMA engine index");
    CHECK(channel_id.channel_index < VDMA_CHANNELS_PER_ENGINE, HAILO_INVALID_ARGUMENT, "Invalid DMA channel index");
    CHECK(direction != Direction::BOTH, HAILO_INVALID_ARGUMENT, "Boundary channels must be unidirectional");

    auto channel_ptr = make_shared_nothrow<BoundaryChannel>(driver, channel_id, direction, std::move(desc_list),
        queue_size, should_split_buffers, stream_name, latency_meter);
    CHECK_NOT_NULL(channel_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return channel_ptr;
}

hailo_status BoundaryChannel::complete_transfers(hailo_status status, size_t transfers_completed)
{
    if (HAILO_SUCCESS != status) {
        deactivate();
        cancel_pending_transfers(status);
        return HAILO_SUCCESS;
    }

    std::unique_lock<std::mutex> lock(m_mutex);

    if (!m_is_activated) {
        return HAILO_STREAM_NOT_ACTIVATED;
    }

    CHECK(transfers_completed <= m_ongoing.size(), HAILO_INTERNAL_FAILURE, "Invalid number of completed transfers.");

    for (size_t i = 0; i < transfers_completed; i++) {
        TransferChunk &chunk = m_ongoing.front();

        if ((m_latency_meter) && (Direction::D2H == m_direction) && (chunk.is_last)) {
            m_latency_meter->add_end_sample(m_stream_name);
        }

        m_free_descs += chunk.num_descs;
        on_request_complete(lock, chunk, HAILO_SUCCESS);

        m_ongoing.pop_front();
    }

    while (!m_pending.empty() && !m_ongoing.full()) {
        TransferChunk &chunk = m_pending.front();
        if (m_free_descs <= chunk.num_descs) {
            break;
        }

        status = launch_transfer_internal(chunk);
        if (HAILO_SUCCESS != status) {
            on_request_complete(lock, chunk, status);
        } else {
            m_ongoing.push_back(std::move(chunk));
        }

        m_pending.pop_front();
    }

    return HAILO_SUCCESS;
}

hailo_status BoundaryChannel::activate()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    CHECK(!m_is_activated, HAILO_INTERNAL_FAILURE, "Vdma channel {} is already activated", m_channel_id);
    m_is_activated = true;
    m_free_descs = m_desc_list.count();

    return HAILO_SUCCESS;
}

void BoundaryChannel::deactivate()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_is_activated = false;
}

hailo_status BoundaryChannel::launch_transfer(TransferRequest &&transfer_request)
{
    std::unique_lock<std::mutex> lock(m_mutex);

    CHECK(m_is_activated, HAILO_STREAM_NOT_ACTIVATED);

    TRY(auto transfer_chunks, m_transfer_splitter.split(std::move(transfer_request)));

    for (auto &chunk : transfer_chunks) {
        if ((m_pending.size() > 0) || (chunk.num_descs >= m_free_descs) || (m_ongoing.full())) {
            // There are pending transfers or not engough room: add transfer to the back of the queue.
            CHECK(!m_pending.full(), HAILO_QUEUE_IS_FULL);
            m_pending.push_back(std::move(chunk));
            continue;
        }

        // There is room and no transfers are pending: launch now.
        auto status = launch_transfer_internal(chunk);
        CHECK_SUCCESS_WITH_ACCEPTABLE_STATUS(HAILO_STREAM_ABORT, status);
        m_ongoing.push_back(std::move(chunk));
    }

    return HAILO_SUCCESS;
}

hailo_status BoundaryChannel::prepare_transfer(TransferRequest &&request)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    CHECK(!m_is_activated, HAILO_INTERNAL_FAILURE, "Failed prepare-transfer: channel is already activated");

    uint32_t total_descs = 0;
    for (auto &buffer : request.transfer_buffers) {
        total_descs += m_desc_list.descriptors_in_buffer(buffer.size());
    }
    if (total_descs >= m_free_descs) {
        // Best effort. If there is no room in the descriptor-list just skip prepare.
        return HAILO_SUCCESS;
    }

    auto status = m_driver.hailo_vdma_prepare_transfer(m_channel_id, m_desc_list.handle(), request.transfer_buffers);
    CHECK_SUCCESS_WITH_ACCEPTABLE_STATUS(HAILO_STREAM_ABORT, status);

    m_free_descs -= total_descs;

    return HAILO_SUCCESS;
}

// Assumes channel is activated, locked, and that `chunk.num_descs < m_num_free_descs`.
hailo_status BoundaryChannel::launch_transfer_internal(TransferChunk &chunk)
{
    auto status = m_driver.launch_transfer(m_channel_id, m_desc_list.handle(), chunk.buffers, m_is_cyclic);
    CHECK_SUCCESS_WITH_ACCEPTABLE_STATUS(HAILO_STREAM_ABORT, status);

    if ((m_latency_meter) && (Direction::H2D == m_direction) && (chunk.is_first)) {
        m_latency_meter->add_start_sample();
    }

    m_free_descs -= chunk.num_descs;

    return HAILO_SUCCESS;
}

hailo_status BoundaryChannel::bind_cyclic_buffer(MappedBufferPtr buffer)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    CHECK(!m_is_cyclic, HAILO_INTERNAL_FAILURE, "Cyclic buffer is already bound to channel {}", m_channel_id);

    const size_t expected_size = m_desc_list.desc_page_size() * m_desc_list.count();
    CHECK(buffer->size() == expected_size, HAILO_INVALID_ARGUMENT, "Cyclic buffer too large for desc list");

    static const size_t DEFAULT_BUFFER_OFFSET = 0;
    auto status = m_desc_list.program(buffer, buffer->size(), DEFAULT_BUFFER_OFFSET, m_channel_id);
    CHECK_SUCCESS(status);

    m_is_cyclic = true;

    return HAILO_SUCCESS;
}

void BoundaryChannel::cancel_pending_transfers(hailo_status status)
{
    std::unique_lock<std::mutex> lock(m_mutex);

    while (!m_ongoing.empty()) {
        auto ongoing_chunk = std::move(m_ongoing.front());
        m_ongoing.pop_front();
        on_request_complete(lock, ongoing_chunk, status);
    }

    while (!m_pending.empty()) {
        auto pending_chunk = std::move(m_pending.front());
        m_pending.pop_front();
        on_request_complete(lock, pending_chunk, status);
    }

    m_free_descs = m_desc_list.count();
}

hailo_status BoundaryChannel::cancel_prepared_transfers()
{
    std::unique_lock<std::mutex> lock(m_mutex);

    auto status = m_driver.cancel_prepared_transfers(m_desc_list.handle());
    CHECK_SUCCESS(status, "Failed cancelling prepared transfers for channel {}", m_channel_id);

    m_free_descs = m_desc_list.count();

    return HAILO_SUCCESS;
}

size_t BoundaryChannel::get_max_ongoing_transfers(size_t transfer_size) const
{
    const size_t chunk_size_bytes = m_transfer_splitter.descs_per_chunk() * m_desc_list.desc_page_size();
    const size_t chunks_in_transfer = DIV_ROUND_UP(transfer_size, chunk_size_bytes);
    return m_pending.capacity() / chunks_in_transfer;
}

bool BoundaryChannel::is_ready(size_t transfer_size) const
{
    const size_t chunk_size_bytes = m_transfer_splitter.descs_per_chunk() * m_desc_list.desc_page_size();
    const size_t chunks_in_transfer = DIV_ROUND_UP(transfer_size, chunk_size_bytes);
    return chunks_in_transfer < (m_pending.capacity() - m_pending.size());
}

void BoundaryChannel::on_request_complete(std::unique_lock<std::mutex> &lock, TransferChunk &chunk, hailo_status status)
{
    lock.unlock();
    chunk.callback(status);
    lock.lock();
}

} /* namespace vdma */
} /* namespace hailort */
