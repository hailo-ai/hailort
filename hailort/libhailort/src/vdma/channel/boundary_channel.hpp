/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file boundary_channel.hpp
 * @brief BoundaryChannel - vdma boundary channel
 **/

#ifndef _HAILO_VDMA_BOUNDARY_CHANNEL_HPP_
#define _HAILO_VDMA_BOUNDARY_CHANNEL_HPP_

#include "vdma/transfer_common.hpp"
#include "vdma/channel/channel_id.hpp"
#include "vdma/channel/transfer_launcher.hpp"
#include "vdma/memory/descriptor_list.hpp"

#include "common/latency_meter.hpp"

#include <memory>


namespace hailort {
namespace vdma {

struct TransferChunk {
    TransferRequest request;
    uint32_t num_descs;

    TransferChunk() = default;

    TransferChunk(std::vector<TransferBuffer> &&buffers, TransferDoneCallback &&callback, uint16_t desc_page_size)
        : request(std::move(buffers), std::move(callback))
    {
        num_descs = 0;
        for (auto &buffer : request.transfer_buffers) {
            num_descs += DescriptorList::descriptors_in_buffer(buffer.size(), desc_page_size);
        }
    }
};

class BoundaryChannel;
using BoundaryChannelPtr = std::shared_ptr<BoundaryChannel>;
class BoundaryChannel final
{
public:
    using Direction = HailoRTDriver::DmaDirection;

    static Expected<BoundaryChannelPtr> create(HailoRTDriver &driver, vdma::ChannelId channel_id, Direction direction,
        vdma::DescriptorList &&desc_list, TransferLauncher &transfer_launcher, size_t queue_size,
        bool split_transfer = false, const std::string &stream_name = "", LatencyMeterPtr latency_meter = nullptr);

    BoundaryChannel(HailoRTDriver &driver, vdma::ChannelId channel_id, Direction direction, DescriptorList &&desc_list,
        TransferLauncher &transfer_launcher, size_t queue_size, bool split_transfer, const std::string &stream_name,
        LatencyMeterPtr latency_meter);

    BoundaryChannel(const BoundaryChannel &other) = delete;
    BoundaryChannel &operator=(const BoundaryChannel &other) = delete;
    BoundaryChannel(BoundaryChannel &&other) = delete;
    BoundaryChannel &operator=(BoundaryChannel &&other) = delete;
    virtual ~BoundaryChannel() = default;

    /**
     * Activates the channel object, assume the vDMA channel registers are already in activated state.
     */
    hailo_status activate();

    /**
     * Deactivates the channel object, assume the vDMA channel registers are already in deactivated state.
     */
    void deactivate();

    // Calls all pending transfer callbacks (if they exist), marking them as canceled by passing
    // HAILO_STREAM_ABORT as a status to the callbacks.
    // Note: This function is to be called on a deactivated channel object. Calling on an active channel will lead to
    // unexpected results
    void cancel_pending_transfers();

    hailo_status cancel_prepared_transfers();

    /**
     * Called when some transfer (or transfers) is completed.
     */
    hailo_status trigger_channel_completion(size_t transfers_completed);

    /**
     * Called on interrupt error. Assumes channel won't work after this.
     */
    void trigger_channel_error(hailo_status status);

    hailo_status launch_transfer(TransferRequest &&transfer_request);

    // To avoid buffer bindings, one can call this function to statically bind a full buffer to the channel. The buffer
    // size should be exactly desc_page_size() * descs_count() of current descriptors list.
    hailo_status bind_cyclic_buffer(MappedBufferPtr buffer);
    hailo_status prepare_transfer(TransferRequest &&transfer_request);

    // TODO: rename BoundaryChannel::get_max_ongoing_transfers to BoundaryChannel::get_max_parallel_transfers (HRT-13513)
    size_t get_max_ongoing_transfers(size_t transfer_size) const;

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

    bool should_measure_timestamp() const { return m_latency_meter != nullptr; }

    /**
     * Checks if the channel is ready to accept a new transfer of the given size.
     */
    bool is_ready(size_t transfer_size) const;

private:
    hailo_status update_latency_meter();

    void on_request_complete(std::unique_lock<std::mutex> &lock, TransferRequest &request, hailo_status complete_status);

    hailo_status launch_transfer_impl(TransferChunk &chunk);

    static bool is_desc_between(uint16_t begin, uint16_t end, uint16_t desc);

    Expected<std::vector<TransferChunk>> split_transfer(TransferRequest &&request);

    size_t get_chunk_size() const;
    InterruptsDomain get_first_interrupts_domain() const;

    const vdma::ChannelId m_channel_id;
    const Direction m_direction;
    HailoRTDriver &m_driver;
    TransferLauncher &m_transfer_launcher;
    DescriptorList m_desc_list;
    const std::string m_stream_name;
    uint32_t m_num_launched;
    uint32_t m_free_descs;
    bool m_is_channel_activated;
    std::mutex m_channel_mutex;
    CircularArray<TransferChunk, IsNotPow2Tag> m_pending;
    CircularArray<TransferChunk, IsNotPow2Tag> m_ongoing;

    // About HW latency measurements:
    //  - For each ongoing transfer, we push some num-proc value to the pending_latency_measurements array. When this
    //    descriptor is processed, we can add a sample to the latency meter.
    //  - On H2D, the descriptor is the first descriptor on each transfer, so we start the measure after the first
    //    vdma descriptor is processed. We don't measure on launch_transfer since the hw may be busy processing
    //    requests. When the first descriptor is processed, we can be sure the hw has really started processing the
    //    frame.
    //  - On D2H, the descriptor is the last descriptor on each transfer, so we end the measure after the transfer is
    //    processed.
    //  - To get the timestamp, the read_timestamps ioctl is called. This ioctl returns pairs of num-processed and
    //    and their interrupt timestamp, then, using m_last_timestamp_num_processed, we can check if some
    //    pending_latency_measurement is done.
    //  - We don't use m_ongoing_transfers to store the latency measurements because we to finish an ongoing transfer
    //    we use hw num processed given by trigger_channel_completion, which may be different that the hw num processed
    //    returned from read_timestamps_ioctl (one is measured in the ioctl and the other is measured in the interrupt).
    LatencyMeterPtr m_latency_meter;
    CircularArray<uint16_t> m_pending_latency_measurements;
    uint16_t m_last_timestamp_num_processed;

    bool m_is_cyclic;
    bool m_should_split_transfer;

    static constexpr uint32_t OPTIMAL_CHUNKS_DIVISION_FACTOR = 4;
};

} /* namespace vdma */
} /* namespace hailort */

#endif  // _HAILO_VDMA_BOUNDARY_CHANNEL_HPP_
