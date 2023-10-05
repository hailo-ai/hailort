/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file boundary_channel.hpp
 * @brief BoundaryChannel - vdma boundary channel
 **/

#ifndef _HAILO_VDMA_BOUNDARY_CHANNEL_HPP_
#define _HAILO_VDMA_BOUNDARY_CHANNEL_HPP_

#include "vdma/channel/vdma_channel_regs.hpp"
#include "vdma/channel/channel_id.hpp"
#include "vdma/memory/descriptor_list.hpp"
#include "stream_common/transfer_common.hpp"

#include "common/latency_meter.hpp"

#include "context_switch_defs.h"

#include <memory>


namespace hailort {
namespace vdma {

struct OngoingTransfer {
    TransferRequest request;
    uint16_t last_desc;
    uint16_t latency_measure_desc;
};

class BoundaryChannel;
using BoundaryChannelPtr = std::shared_ptr<BoundaryChannel>;
class BoundaryChannel final
{
public:
    using Direction = HailoRTDriver::DmaDirection;

    static Expected<BoundaryChannelPtr> create(vdma::ChannelId channel_id, Direction direction, HailoRTDriver &driver,
        uint32_t descs_count, uint16_t desc_page_size, const std::string &stream_name = "", LatencyMeterPtr latency_meter = nullptr);

    BoundaryChannel(vdma::ChannelId channel_id, Direction direction, HailoRTDriver &driver, uint32_t descs_count,
        uint16_t desc_page_size, const std::string &stream_name, LatencyMeterPtr latency_meter,
        hailo_status &status);
    BoundaryChannel(const BoundaryChannel &other) = delete;
    BoundaryChannel &operator=(const BoundaryChannel &other) = delete;
    BoundaryChannel(BoundaryChannel &&other) = delete;
    BoundaryChannel &operator=(BoundaryChannel &&other) = delete;
    virtual ~BoundaryChannel() = default;

    // Called after the FW activated the channel.
    hailo_status activate();

    // Called before the FW deactivated the channel.
    hailo_status deactivate();

    hailo_status trigger_channel_completion(uint16_t hw_num_processed);

    // Calls all pending transfer callbacks (if they exist), marking them as canceled by passing
    // HAILO_STREAM_ABORTED_BY_USER as a status to the callbacks.
    // Note: This function is to be called on a deactivated channel object. Calling on an active channel will lead to
    // unexpected results
    void cancel_pending_transfers();

    // user_owns_buffer is set when the buffer is owned by the user (otherwise we may have some assumtions).
    hailo_status launch_transfer(TransferRequest &&transfer_request, bool user_owns_buffer);

    size_t get_max_ongoing_transfers(size_t transfer_size) const;

    CONTROL_PROTOCOL__host_buffer_info_t get_boundary_buffer_info(uint32_t transfer_size) const;

    vdma::ChannelId get_channel_id() const
    {
        return m_channel_id;
    }

    const std::string &stream_name() const
    {
        return m_stream_name;
    }

    std::shared_ptr<DescriptorList> get_desc_list()
    {
        return m_desc_list;
    }

private:
    static void empty_transfer_done_callback(hailo_status){}

    // Returns the desc index of the last desc whose timestamp was measured in the driver
    Expected<uint16_t> update_latency_meter();

    bool is_transfer_complete(const OngoingTransfer &transfer, uint16_t previous_num_processed,
        uint16_t current_num_processed) const;
    void on_transfer_complete(std::unique_lock<std::mutex> &lock, OngoingTransfer &transfer,
        hailo_status complete_status);
    hailo_status prepare_descriptors(size_t transfer_size, uint16_t starting_desc,
        MappedBufferPtr mapped_buffer, size_t buffer_offset);

    bool is_buffer_already_configured(MappedBufferPtr buffer, size_t buffer_offset_in_descs, size_t starting_desc) const;
    void add_ongoing_transfer(TransferRequest &&transfer_request, uint16_t first_desc, uint16_t last_desc);

    static bool is_desc_between(uint16_t begin, uint16_t end, uint16_t desc);
    uint16_t get_num_available() const;
    hailo_status inc_num_available(uint16_t value);
    hailo_status allocate_descriptor_list(uint32_t descs_count, uint16_t desc_page_size);

    const vdma::ChannelId m_channel_id;
    const Direction m_direction;
    HailoRTDriver &m_driver;
    VdmaChannelRegs m_host_registers;
    std::shared_ptr<DescriptorList> m_desc_list; // Host side descriptor list
    const std::string m_stream_name;
    circbuf_t m_descs;
    LatencyMeterPtr m_latency_meter;
    bool m_is_channel_activated;
    std::mutex m_channel_mutex;
    CircularArray<OngoingTransfer> m_ongoing_transfers;

    // Contains the last num_processed of the last interrupt (only used on latency measurement)
    uint16_t m_last_timestamp_num_processed;

    struct BoundedBuffer {
        MappedBufferPtr buffer;

        // The buffer is bounded starting from this descriptor.
        uint16_t starting_desc;

        // Offset inside the buffer (in desc_page_size granularity) of the "actual start" of the buffer.
        // It implies that:
        //      desc_list[starting_desc] will point to buffer[buffers_desc_offset * desc_page_size].
        uint16_t buffer_offset_in_descs;
    };

    // We store the last bounded buffer as cache in order to avoid unnecessary descriptors list reprogramming.
    // It is good enough to store only the last bounded buffer because we have two modes of execution:
    //      1. User allocated buffers - On each transfer we bind new buffer. Even if the user always uses the same
    //         buffers, due to the circular nature of descriptor list, reprogramming will almost always be needed (So
    //         cacheing won't help).
    //      2. Single circular buffer (internally) - In this case we don't need to bind each time (maybe after the
    //         channel is re-activated). Caching the last bounded buffer is enough.
    BoundedBuffer m_last_bounded_buffer;
};

} /* namespace vdma */
} /* namespace hailort */

#endif  // _HAILO_VDMA_BOUNDARY_CHANNEL_HPP_