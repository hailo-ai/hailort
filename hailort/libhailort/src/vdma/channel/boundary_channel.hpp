/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file boundary_channel.hpp
 * @brief BoundaryChannel - vdma boundary channel
 **/

#ifndef _HAILO_VDMA_BOUNDARY_CHANNEL_HPP_
#define _HAILO_VDMA_BOUNDARY_CHANNEL_HPP_

#include "transfer_splitter.hpp"
#include "vdma/channel/channel_id.hpp"
#include "vdma/memory/descriptor_list.hpp"

#include "common/latency_meter.hpp"

#include <memory>


namespace hailort {
namespace vdma {

class BoundaryChannel;
using BoundaryChannelPtr = std::shared_ptr<BoundaryChannel>;
class BoundaryChannel final
{
public:
    using Direction = HailoRTDriver::DmaDirection;

    static Expected<BoundaryChannelPtr> create(HailoRTDriver &driver, vdma::ChannelId channel_id, Direction direction,
        vdma::DescriptorList &&desc_list, size_t queue_size, bool should_split_buffers = false,
        const std::string &stream_name = "", LatencyMeterPtr latency_meter = nullptr);

    BoundaryChannel(HailoRTDriver &driver, vdma::ChannelId channel_id, Direction direction, DescriptorList &&desc_list,
        size_t queue_size, bool should_split_buffers, const std::string &stream_name, LatencyMeterPtr latency_meter);

    virtual ~BoundaryChannel() = default;

    BoundaryChannel(const BoundaryChannel &other) = delete;
    BoundaryChannel &operator=(const BoundaryChannel &other) = delete;
    BoundaryChannel(BoundaryChannel &&other) = delete;
    BoundaryChannel &operator=(BoundaryChannel &&other) = delete;

    // Assumes the vDMA channel registers are already in activated state.
    hailo_status activate();

    // Aassumes the vDMA channel registers are already in deactivated state.
    void deactivate();

    // Assumes channel is deactivated. Calling on an active channel will lead to undefined behaviour.
    void cancel_pending_transfers(hailo_status status = HAILO_STREAM_ABORT);

    hailo_status launch_transfer(TransferRequest &&transfer_request);
    hailo_status complete_transfers(hailo_status status, size_t transfers_completed);

    // To avoid buffer bindings, one can call this function to statically bind a full buffer to the channel. The buffer
    // size should be exactly desc_page_size() * descs_count() of current descriptors list.
    hailo_status bind_cyclic_buffer(MappedBufferPtr buffer);

    hailo_status prepare_transfer(TransferRequest &&transfer_request);
    hailo_status cancel_prepared_transfers();

    // TODO: HRT-19716
    // These functions are BROKEN as they calculate based on transfer-size rather than number of descriptors.
    size_t get_max_ongoing_transfers(size_t transfer_size) const;
    bool is_ready(size_t transfer_size) const;

    vdma::ChannelId get_channel_id() const
    {
        return m_channel_id;
    }

    const std::string &stream_name() const
    {
        return m_stream_name;
    }

    DescriptorList &get_desc_list()
    {
        return m_desc_list;
    }

private:
    void on_request_complete(std::unique_lock<std::mutex> &lock, TransferChunk &chunk, hailo_status status);

    hailo_status launch_transfer_internal(TransferChunk &chunk);

    const vdma::ChannelId m_channel_id;
    const Direction m_direction;
    HailoRTDriver &m_driver;
    TransferSplitter m_transfer_splitter;
    DescriptorList m_desc_list;
    const std::string m_stream_name;
    uint32_t m_free_descs;
    bool m_is_activated;
    std::mutex m_mutex;
    CircularArray<TransferChunk, IsNotPow2Tag> m_pending;
    CircularArray<TransferChunk, IsPow2Tag> m_ongoing;
    LatencyMeterPtr m_latency_meter;
    bool m_is_cyclic;
};

} /* namespace vdma */
} /* namespace hailort */

#endif  // _HAILO_VDMA_BOUNDARY_CHANNEL_HPP_
