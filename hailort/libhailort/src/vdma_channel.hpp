/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_channel.hpp
 * @brief Cordinator of everything related to one channel of Vdma
 *
 * <doc>
 **/

#ifndef _HAILO_VDMA_CHANNEL_HPP_
#define _HAILO_VDMA_CHANNEL_HPP_

#include "hailo/hailort.h"
#include "common/circular_buffer.hpp"
#include "common/latency_meter.hpp"
#include "vdma_channel_regs.hpp"
#include "hailo/expected.hpp"
#include "os/hailort_driver.hpp"
#include "vdma/sg_buffer.hpp"
#include "vdma_descriptor_list.hpp"
#include "vdma/channel_id.hpp"
#include "hailo/buffer.hpp"

#include <mutex>
#include <array>
#include <condition_variable>

namespace hailort
{

class VdmaChannel;
class PendingBufferState final
{
public:
    PendingBufferState(VdmaChannel &vdma_channel, size_t next_buffer_desc_num) : m_vdma_channel(vdma_channel),
        m_next_buffer_desc_num(next_buffer_desc_num) {}
    hailo_status finish(std::chrono::milliseconds timeout, std::unique_lock<std::mutex> &lock, uint16_t max_batch_size);

private:
    VdmaChannel &m_vdma_channel;
    size_t m_next_buffer_desc_num;
};

class VdmaChannel final
{
public:
    using Direction = HailoRTDriver::DmaDirection;

    static Expected<VdmaChannel> create(vdma::ChannelId channel_id, Direction direction, HailoRTDriver &driver,
        uint16_t requested_desc_page_size, uint32_t stream_index = 0, LatencyMeterPtr latency_meter = nullptr, 
        uint16_t transfers_per_axi_intr = 1);
    ~VdmaChannel();

    /**
     * Waits until the channel is ready for transfer `buffer_size` bytes.
     * For now only supported in H2D stream.
     * TODO: SDK-15831 support D2H
     *
     * @param[in] buffer_size
     * @param[in] timeout
     */
    hailo_status wait(size_t buffer_size, std::chrono::milliseconds timeout);

    hailo_status transfer(void *buf, size_t count);
    hailo_status write_buffer(const MemoryView &buffer, std::chrono::milliseconds timeout);
    Expected<PendingBufferState> send_pending_buffer();
    hailo_status trigger_channel_completion(uint16_t hw_num_processed, const std::function<void(uint32_t)> &callback);
    hailo_status allocate_resources(uint32_t descs_count);
    // Call for boundary channels, after the fw has activted them (via ResourcesManager::enable_state_machine)
    hailo_status complete_channel_activation(uint32_t transfer_size);
    // Libhailort registers the channels to the driver and the FW is responsible for opening and closing them
    hailo_status register_fw_controlled_channel();
    hailo_status unregister_fw_controlled_channel();
    void mark_d2h_callbacks_for_shutdown();
    void unmark_d2h_callbacks_for_shutdown();
    // For D2H channels, we don't buffer data
    // Hence there's nothing to be "flushed" and the function will return with HAILO_SUCCESS
    hailo_status flush(const std::chrono::milliseconds &timeout);
    hailo_status set_num_avail_value(uint16_t new_value);
    hailo_status set_transfers_per_axi_intr(uint16_t transfers_per_axi_intr);
    hailo_status inc_num_available_for_ddr(uint16_t value, uint32_t size_mask);
    Expected<uint16_t> get_hw_num_processed_ddr(uint32_t size_mask);
    
    hailo_status stop_channel();
    uint16_t get_page_size();
    Expected<CONTROL_PROTOCOL__host_buffer_info_t> get_boundary_buffer_info(uint32_t transfer_size);

    hailo_status abort();
    hailo_status clear_abort();

    vdma::ChannelId get_channel_id() const
    {
        return m_channel_id;
    }

    size_t get_transfers_count_in_buffer(size_t transfer_size);
    size_t get_buffer_size() const;
    Expected<size_t> get_h2d_pending_frames_count();
    Expected<size_t> get_d2h_pending_descs_count();

    hailo_status reset_offset_of_pending_frames();

    VdmaChannel(const VdmaChannel &other) = delete;
    VdmaChannel &operator=(const VdmaChannel &other) = delete;
    VdmaChannel(VdmaChannel &&other) noexcept;
    VdmaChannel &operator=(VdmaChannel &&other) = delete;

    static uint32_t calculate_buffer_size(const HailoRTDriver &driver, uint32_t transfer_size, uint32_t transfers_count,
        uint16_t requested_desc_page_size);

    hailo_status register_for_d2h_interrupts(const std::function<void(uint32_t)> &callback);

    friend class PendingBufferState;

private:
    struct PendingBuffer {
        uint32_t last_desc;
        uint32_t latency_measure_desc;
    };

    // TODO (HRT-3762) : Move channel's state to driver to avoid using shared memory
    class State {
    public:

        void lock();
        void unlock();

#ifndef _MSC_VER
        pthread_mutex_t m_state_lock;
#else
        CRITICAL_SECTION m_state_lock;
#endif
        std::array<PendingBuffer, PENDING_BUFFERS_SIZE> m_pending_buffers;
        circbuf_t m_buffers;
        // TODO: describe why we must have our own num_available and num_proc.
        // it's not just for efficiency but its critical to avoid a potential bug - see Avigail email.
        // TODO: Consider C11 stdatomic
        circbuf_t m_descs;
        int m_d2h_read_desc_index;
        // Contains the last num_processed of the last interrupt (only used on latency measurement)
        uint16_t m_last_timestamp_num_processed;
        size_t m_accumulated_transfers;
        bool m_channel_is_active;
    };

    hailo_status register_channel_to_driver();
    hailo_status unregister_for_d2h_interrupts(std::unique_lock<State> &lock);

    VdmaChannel(vdma::ChannelId channel_id, Direction direction, HailoRTDriver &driver, uint32_t stream_index,
        LatencyMeterPtr latency_meter, uint16_t desc_page_size, uint16_t transfers_per_axi_intr, hailo_status &status);

    hailo_status allocate_buffer(const uint32_t buffer_size);
    void clear_descriptor_list();
    hailo_status release_buffer();
    static Direction other_direction(const Direction direction);
    hailo_status transfer_h2d(void *buf, size_t count);
    hailo_status write_buffer_impl(const MemoryView &buffer);
    hailo_status send_pending_buffer_impl();
    uint16_t get_num_available();
    Expected<uint16_t> get_hw_num_processed();
    void add_pending_buffer(uint32_t first_desc, uint32_t last_desc);
    hailo_status inc_num_available(uint16_t value);
    hailo_status transfer_d2h(void *buf, size_t count);
    bool is_ready_for_transfer_h2d(size_t buffer_size);
    bool is_ready_for_transfer_d2h(size_t buffer_size);
    hailo_status prepare_descriptors(size_t transfer_size, VdmaInterruptsDomain first_desc_interrupts_domain,
        VdmaInterruptsDomain last_desc_interrupts_domain);
    hailo_status prepare_d2h_pending_descriptors(uint32_t transfer_size);
    void reset_internal_counters();
    hailo_status wait_for_channel_completion(std::chrono::milliseconds timeout, const std::function<void(uint32_t)> &callback = [](uint32_t) { return; });

    uint32_t calculate_descriptors_count(uint32_t buffer_size);

    hailo_status wait_for_condition(std::function<bool()> condition, std::chrono::milliseconds timeout);

    void wait_d2h_callback(const std::function<void(uint32_t)> &callback);
    std::unique_ptr<std::thread> m_d2h_callback_thread;

    /**
     * Returns the new hw num_processed of the irq
     */
    Expected<uint16_t> wait_interrupts(std::chrono::milliseconds timeout);

    /**
     * Returns the new hw num processed. 
     */
    Expected<uint16_t> update_latency_meter(const ChannelInterruptTimestampList &timestamp_list);
    static bool is_desc_between(uint16_t begin, uint16_t end, uint16_t desc);
    Expected<bool> is_aborted();

    const vdma::ChannelId m_channel_id;
    Direction m_direction;
    HailoRTDriver &m_driver;
    VdmaChannelRegs m_host_registers;
    VdmaChannelRegs m_device_registers;

    // TODO: use m_descriptors_buffer.desc_page_size()
    const uint16_t m_desc_page_size;

    // TODO: remove the unique_ptr, instead allocate the buffer in the ctor (needs to move ddr channel to
    // other class)
    std::unique_ptr<vdma::SgBuffer> m_buffer;
    uint32_t m_stream_index;
    LatencyMeterPtr m_latency_meter;

    MmapBuffer<State> m_state;
    // Unique channel handle, may be changed between registration to driver. This object is shared
    // because multiple processes can enable/disable vdma channel (which changes the channel)
    MmapBuffer<HailoRTDriver::VdmaChannelHandle> m_channel_handle;

    // TODO: Remove after HRT-7838
    Buffer m_buffer_for_frames_shift;

    bool m_channel_enabled;
    
    uint16_t m_transfers_per_axi_intr;
    // Using CircularArray because it won't allocate or free memory wile pushing and poping. The fact that it is circural is not relevant here
    CircularArray<size_t> m_pending_buffers_sizes;
    std::atomic_uint16_t m_pending_num_avail_offset;
    std::condition_variable_any m_can_write_buffer_cv;
    std::condition_variable_any m_can_read_buffer_cv;
    std::atomic_bool m_is_waiting_for_channel_completion;
    std::atomic_bool m_is_aborted_by_internal_source;
    std::atomic_bool m_d2h_callbacks_marked_for_shutdown;
};

} /* namespace hailort */

#endif  // _HAILO_VDMA_CHANNEL_HPP_