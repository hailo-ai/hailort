/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file buffered_channel.hpp
 * @brief BufferedChannel - Implements the BoundaryChannel interface, allowing for buffering of frames
 *                          by managing a vdma buffer
 **/

#ifndef _HAILO_VDMA_BUFFERED_CHANNEL_HPP_
#define _HAILO_VDMA_BUFFERED_CHANNEL_HPP_

#include "hailo/hailort.h"
#include "hailo/dma_mapped_buffer.hpp"

#include "vdma/channel/boundary_channel.hpp"


namespace hailort {
namespace vdma {

class BufferedChannel;
using BufferedChannelPtr = std::shared_ptr<BufferedChannel>;

class BufferedChannel : public BoundaryChannel
{
public:
    static Expected<BufferedChannelPtr> create(vdma::ChannelId channel_id, Direction direction, HailoRTDriver &driver,
        uint32_t descs_count, uint16_t desc_page_size, const std::string &stream_name = "", LatencyMeterPtr latency_meter = nullptr,
        uint16_t transfers_per_axi_intr = 1);

    BufferedChannel(vdma::ChannelId channel_id, Direction direction, HailoRTDriver &driver, uint32_t descs_count,
        uint16_t desc_page_size, const std::string &stream_name, LatencyMeterPtr latency_meter, uint16_t transfers_per_axi_intr, hailo_status &status);
    BufferedChannel(const BufferedChannel &other) = delete;
    BufferedChannel &operator=(const BufferedChannel &other) = delete;
    BufferedChannel(BufferedChannel &&other) = delete;
    BufferedChannel &operator=(BufferedChannel &&other) = delete;
    virtual ~BufferedChannel() = default;

    virtual hailo_status transfer(void *buf, size_t count) override;
    // Either write_buffer + send_pending_buffer or transfer (h2d) should be used on a given channel, not both
    virtual hailo_status write_buffer(const MemoryView &buffer, std::chrono::milliseconds timeout,
        const std::function<bool()> &should_cancel) override;
    virtual hailo_status send_pending_buffer() override;
    virtual hailo_status transfer(std::shared_ptr<DmaMappedBuffer>, const TransferDoneCallback &, void *) override;
    virtual hailo_status cancel_pending_transfers() override;
    virtual hailo_status complete_channel_activation(uint32_t transfer_size, bool resume_pending_transfers) override;
    virtual hailo_status complete_channel_deactivation() override;

    // Assumes that the channel is idle; doesn't block changes to the channel
    // To be used for debugging purposes
    virtual Expected<BoundaryChannel::BufferState> get_buffer_state() override;
    virtual Expected<size_t> get_h2d_pending_frames_count() override;
    virtual Expected<size_t> get_d2h_pending_descs_count() override;

    virtual void notify_all() override;

private:
    static Expected<std::shared_ptr<DmaMappedBuffer>> create_mapped_buffer(uint32_t descs_count, uint16_t desc_page_size,
        Direction direction, HailoRTDriver &driver);

    hailo_status transfer_h2d(void *buf, size_t count);
    hailo_status write_buffer_impl(const MemoryView &buffer);
    hailo_status write_to_channel_buffer_cyclic(const MemoryView &buffer, size_t channel_buffer_write_offset);
    hailo_status read_from_channel_buffer_cyclic(uint8_t *dest_buffer, size_t read_size, size_t channel_buffer_read_offset);
    hailo_status send_pending_buffer_impl();
    hailo_status transfer_d2h(void *buf, size_t count);
    hailo_status prepare_descriptors(size_t transfer_size, InterruptsDomain first_desc_interrupts_domain,
        InterruptsDomain last_desc_interrupts_domain);
    hailo_status prepare_d2h_pending_descriptors(uint32_t transfer_size, uint32_t transfers_count);
    bool is_ready_for_write(const uint16_t desired_desc_num);
    virtual bool is_ready_for_transfer_d2h(size_t buffer_size) override;
    hailo_status store_channel_buffer_state();

    // TODO: m_channel_buffer gets bound to ChannelBase::m_desc_list meaning the desc in that list point to dma addrs
    //       that back m_channel_buffer. Because ChannelBase gets dtor'd after BufferedChannel, m_channel_buffer ChannelBase::m_desc_list
    //       will point to a freed buffer. This is ok because the channel objects only get dtor'd after they are deactivated by the fw.
    //       Might want to enforce this in hailort as well (e.g. desc lists can hold shared_ptrs to DmaMappedBuffer while they are bound).
    //       (HRT-9110)
    std::shared_ptr<DmaMappedBuffer> m_channel_buffer;
    // Using CircularArray because it won't allocate or free memory wile pushing and popping. The fact that it is circular is not relevant here
    CircularArray<size_t> m_pending_buffers_sizes;
    std::atomic_uint16_t m_pending_num_avail_offset;
};

} /* namespace vdma */
} /* namespace hailort */

#endif  // _HAILO_VDMA_BUFFERED_CHANNEL_HPP_