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

using ProcessingCompleteCallback = std::function<void()>;

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
    hailo_status set_transfers_per_axi_intr(uint16_t transfers_per_axi_intr);

    void clear_pending_buffers_descriptors();
    hailo_status trigger_channel_completion(uint16_t hw_num_processed);

    // Register some new interrupt callback (and reset previous).
    // Note - when reseting an old callback, it may still be called (until interrupts are stopped).
    void register_interrupt_callback(const ProcessingCompleteCallback &callback);

    CONTROL_PROTOCOL__host_buffer_info_t get_boundary_buffer_info(uint32_t transfer_size);
    virtual hailo_status abort();
    virtual hailo_status clear_abort();

    // For D2H channels, we don't buffer data
    // Hence there's nothing to be "flushed" and the function will return with HAILO_SUCCESS
    virtual hailo_status flush(const std::chrono::milliseconds &timeout);

    // Blocks until buffer_size bytes can transferred to/from the channel or until timeout has elapsed.
    // If stop_if_deactivated is true, this function will return HAILO_STREAM_NOT_ACTIVATED after deactivate()
    // is called. Otherwise, this function can be used to access the buffer while the channel is not active.
    hailo_status wait(size_t buffer_size, std::chrono::milliseconds timeout, bool stop_if_deactivated=false);

    // Transfers count bytes to/from buf via the channel.
    // Blocks until the transfer can be registered or timeout has elapsed. Hence, calling 'wait(buffer_size, timeout)'
    // prior to 'transfer(buf, buffer_size)' is redundant.
    virtual hailo_status transfer_sync(void *buf, size_t count, std::chrono::milliseconds timeout) = 0;

    // TODO: can write_buffer + send_pending_buffer move to BufferedChannel? (HRT-9105)
    // Either write_buffer + send_pending_buffer or transfer (h2d) should be used on a given channel, not both
    virtual hailo_status write_buffer(const MemoryView &buffer, std::chrono::milliseconds timeout,
        const std::function<bool()> &should_cancel) = 0;
    virtual hailo_status send_pending_buffer() = 0;

    // When the transfer is complete (i.e. data is written to/from buffer with a D2H/H2D channel) callback is called
    // transfer_request.buffer can't be freed/changed until callback is called.
    virtual hailo_status transfer_async(TransferRequest &&transfer_request) = 0;

    // Calls all pending transfer callbacks (if they exist), marking them as canceled by passing
    // HAILO_STREAM_ABORTED_BY_USER as a status to the callbacks.
    // Note: This function is to be called on a deactivated channel object. Calling on an active channel will lead to
    // unexpected results
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
    static void ignore_processing_complete() {}
    void stop_interrupts_thread(std::unique_lock<RecursiveSharedMutex> &lock);
    virtual bool is_ready_for_transfer_h2d(size_t buffer_size);
    virtual bool is_ready_for_transfer_d2h(size_t buffer_size);

    // Called after activate/deactivate with the state mutex held
    virtual hailo_status complete_channel_activation(uint32_t transfer_size, bool resume_pending_transfers) = 0;
    virtual hailo_status complete_channel_deactivation() = 0;

    const Type m_type;
    ProcessingCompleteCallback m_user_interrupt_callback;
    uint16_t m_transfers_per_axi_intr;

private:
    bool has_room_in_desc_list(size_t buffer_size);
    bool is_complete(const PendingBuffer &pending_buffer, uint16_t previous_num_processed,
        uint16_t current_num_processed);
    void on_pending_buffer_irq(PendingBuffer &buffer);
};

} /* namespace vdma */
} /* namespace hailort */

#endif  // _HAILO_VDMA_BOUNDARY_CHANNEL_HPP_