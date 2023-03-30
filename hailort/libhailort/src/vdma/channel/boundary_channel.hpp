/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file boundary_channel.hpp
 * @brief BoundaryChannel - vdma boundary channel interface
 *      The hierarchy is as follows:
 *        -------------------------------------------------------------------------
 *        |             ChannelBase               | (Base class - includes state) |
 *        |                   |                   |                               |
 *        |           BoundaryChannel             | (Boundary interface)          |
 *        |            /           \              |                               |
 *        | AsyncChannel       BufferedChannel    | (Impls)                       |
 *        -------------------------------------------------------------------------
 **/

#ifndef _HAILO_VDMA_BOUNDARY_CHANNEL_HPP_
#define _HAILO_VDMA_BOUNDARY_CHANNEL_HPP_

#include "hailo/hailort.h"
#include "hailo/stream.hpp"

#include "vdma/channel/channel_base.hpp"

#include <memory>


namespace hailort {
namespace vdma {

class BoundaryChannel;
using BoundaryChannelPtr = std::shared_ptr<BoundaryChannel>;

using ProcessingCompleteCallback = std::function<void(uint32_t frames_processed)>;

class BoundaryChannel : public ChannelBase
{
public:
    enum class Type
    {
        BUFFERED = 0,
        ASYNC
    };

    static Expected<BoundaryChannelPtr> create(vdma::ChannelId channel_id, Direction direction, HailoRTDriver &driver,
        uint32_t descs_count, uint16_t desc_page_size, const std::string &stream_name = "", LatencyMeterPtr latency_meter = nullptr,
        uint16_t transfers_per_axi_intr = 1, Type type = Type::BUFFERED);

    BoundaryChannel(Type type, vdma::ChannelId channel_id, Direction direction, HailoRTDriver &driver, uint32_t descs_count,
        uint16_t desc_page_size, const std::string &stream_name, LatencyMeterPtr latency_meter, uint16_t transfers_per_axi_intr,
        hailo_status &status);
    BoundaryChannel(const BoundaryChannel &other) = delete;
    BoundaryChannel &operator=(const BoundaryChannel &other) = delete;
    BoundaryChannel(BoundaryChannel &&other) = delete;
    BoundaryChannel &operator=(BoundaryChannel &&other) = delete;
    virtual ~BoundaryChannel() = default;

    // Called after the FW activated the channel.
    hailo_status activate(uint32_t transfer_size, bool resume_pending_transfers);

    // Called before the FW deactivated the channel.
    hailo_status deactivate();

    Type type() const;

    void clear_pending_buffers_descriptors();
    hailo_status trigger_channel_completion(uint16_t hw_num_processed);
    virtual hailo_status register_interrupt_callback(const ProcessingCompleteCallback &callback);
    CONTROL_PROTOCOL__host_buffer_info_t get_boundary_buffer_info(uint32_t transfer_size);
    virtual hailo_status abort();
    virtual hailo_status clear_abort();

    // For D2H channels, we don't buffer data
    // Hence there's nothing to be "flushed" and the function will return with HAILO_SUCCESS
    virtual hailo_status flush(const std::chrono::milliseconds &timeout);
    virtual hailo_status wait(size_t buffer_size, std::chrono::milliseconds timeout);
    hailo_status set_transfers_per_axi_intr(uint16_t transfers_per_axi_intr);

    virtual hailo_status transfer(void *buf, size_t count) = 0;
    // TODO: can write_buffer + send_pending_buffer move to BufferedChannel? (HRT-9105)
    // Either write_buffer + send_pending_buffer or transfer (h2d) should be used on a given channel, not both
    virtual hailo_status write_buffer(const MemoryView &buffer, std::chrono::milliseconds timeout,
        const std::function<bool()> &should_cancel) = 0;
    virtual hailo_status send_pending_buffer() = 0;
    
    // TODO: move buffer?
    // TODO: If the same callback is used for different buffers we need a way to tell the transfers appart
    //       - Passing buffer to callback could do the trick. However, what will happen if the same buffer has been transferred twice?
    //       - Maybe add a unique transfer_id? At least unique in the context of the maximum number of ongoing transfers
    // TODO: What if there's no more room in desc list so the transfer can't be programmed? Should the function block
    //       - Maybe define that if more than max_concurrent_transfers() (based on a param passed to create) the function will return a failure?
    // When the transfer is complete (i.e. data is written to/from buffer with a D2H/H2D channel) callback is called
    // buffer can't be freed until callback is called
    virtual hailo_status transfer(std::shared_ptr<DmaMappedBuffer> buffer, const TransferDoneCallback &user_callback, void *opaque) = 0;

    // Calls all pending transfer callbacks (if they exist), marking them as canceled by passing hailo_async_transfer_completion_info_t{HAILO_STREAM_NOT_ACTIVATED}.
    // Note: This function is to be called on a deactivated channel object. Calling on an active channel will lead to unexpected results
    virtual hailo_status cancel_pending_transfers() = 0;

    virtual void notify_all() = 0;

    class BufferState {
    public:
        std::vector<std::pair<uint16_t, Buffer>> desc_buffer_pairing;
        uint16_t num_avail;
        uint16_t num_processed;
        uint16_t hw_num_avail;
        uint16_t hw_num_processed;
    };

    // Assumes that the channel is idle; doesn't block changes to the channel
    // To be used for debugging purposes
    // TODO: these will move to BufferedChannel (HRT-9105)
    virtual Expected<BufferState> get_buffer_state() = 0;
    virtual Expected<size_t> get_h2d_pending_frames_count() = 0;
    virtual Expected<size_t> get_d2h_pending_descs_count() = 0;

protected:
    static void ignore_processing_complete(uint32_t) {}
    void stop_interrupts_thread(std::unique_lock<RecursiveSharedMutex> &lock);
    virtual bool is_ready_for_transfer_h2d(size_t buffer_size);
    virtual bool is_ready_for_transfer_d2h(size_t buffer_size);

    // Called after activate/deactivate with the state mutex held
    virtual hailo_status complete_channel_activation(uint32_t transfer_size, bool resume_pending_transfers) = 0;
    virtual hailo_status complete_channel_deactivation() = 0;

    const Type m_type;
    TransferDoneCallback m_transfer_done_callback;
    ProcessingCompleteCallback m_user_interrupt_callback;
    uint16_t m_transfers_per_axi_intr;

private:
    bool has_room_in_desc_list(size_t buffer_size);
};

} /* namespace vdma */
} /* namespace hailort */

#endif  // _HAILO_VDMA_BOUNDARY_CHANNEL_HPP_