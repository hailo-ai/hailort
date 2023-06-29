/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file async_channel.hpp
 * @brief AsyncChannel - Implements the BoundaryChannel interface, allowing for asyc send/recv and zero copy io
 **/

#ifndef _HAILO_ASYNC_CHANNEL_HPP_
#define _HAILO_ASYNC_CHANNEL_HPP_

#include "hailo/hailort.h"

#include "vdma/channel/boundary_channel.hpp"
#include "vdma/channel/channel_state.hpp"

#include <functional>


namespace hailort
{
namespace vdma
{

class AsyncChannel;
using AsyncChannelPtr = std::shared_ptr<AsyncChannel>;

class AsyncChannel : public BoundaryChannel
{
public:
    static Expected<AsyncChannelPtr> create(vdma::ChannelId channel_id, Direction direction, HailoRTDriver &driver,
        uint32_t descs_count, uint16_t desc_page_size, const std::string &stream_name = "", LatencyMeterPtr latency_meter = nullptr,
        uint16_t transfers_per_axi_intr = 1);

    AsyncChannel(vdma::ChannelId channel_id, Direction direction, HailoRTDriver &driver, uint32_t descs_count,
        uint16_t desc_page_size, const std::string &stream_name, LatencyMeterPtr latency_meter, uint16_t transfers_per_axi_intr,
        hailo_status &status);
    AsyncChannel(AsyncChannel &&) = delete;
    AsyncChannel(const AsyncChannel &) = delete;
    AsyncChannel &operator=(AsyncChannel &&) = delete;
    AsyncChannel &operator=(const AsyncChannel &) = delete;
    virtual ~AsyncChannel() = default;

    virtual hailo_status complete_channel_activation(uint32_t transfer_size, bool resume_pending_transfers) override;
    virtual hailo_status complete_channel_deactivation() override;

    virtual hailo_status transfer_async(TransferRequest &&transfer_request) override;
    virtual hailo_status cancel_pending_transfers() override;

    virtual hailo_status transfer_sync(void *buf, size_t count, std::chrono::milliseconds timeout) override;
    // TODO: don't want
    virtual hailo_status write_buffer(const MemoryView &buffer, std::chrono::milliseconds timeout,
        const std::function<bool()> &should_cancel) override;
    // TODO: don't want
    virtual hailo_status send_pending_buffer() override;
    // TODO: don't want
    virtual void notify_all() override;

    // TODO: don't want
    virtual Expected<BoundaryChannel::BufferState> get_buffer_state() override;
    // TODO: don't want
    virtual Expected<size_t> get_h2d_pending_frames_count() override;
    // TODO: don't want
    virtual Expected<size_t> get_d2h_pending_descs_count() override;

private:
    hailo_status transfer_d2h(MappedBufferPtr mapped_buffer, const InternalTransferDoneCallback &user_callback);
    hailo_status transfer_h2d(MappedBufferPtr mapped_buffer, const InternalTransferDoneCallback &user_callback);
    hailo_status prepare_descriptors(MappedBufferPtr mapped_buffer, const InternalTransferDoneCallback &user_callback,
        InterruptsDomain first_desc_interrupts_domain, InterruptsDomain last_desc_interrupts_domain);
};

} /* namespace vdma */
} /* namespace hailort */

#endif /* _HAILO_ASYNC_CHANNEL_HPP_ */
