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
#include "vdma_buffer.hpp"
#include "vdma_descriptor_list.hpp"

#include <mutex>
#include <array>

namespace hailort
{

class VdmaChannel final
{
public:
    using Direction = HailoRTDriver::DmaDirection;

    static Expected<VdmaChannel> create(uint8_t channel_index, Direction direction, HailoRTDriver &driver,
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
    hailo_status wait(size_t buffer_size, const std::chrono::milliseconds &timeout);

    hailo_status transfer(void *buf, size_t count);
    hailo_status trigger_channel_completion(uint16_t hw_num_processed);
    hailo_status allocate_resources(uint32_t descs_count);
    /* For channels controlled by the HailoRT, the HailoRT needs to use this function to start the channel (it registers the channel to driver 
       and starts the vDMA channel. Used for boundary channels */
    hailo_status start_allocated_channel(uint32_t transfer_size);
    hailo_status register_fw_controlled_channel();
    hailo_status unregister_fw_controlled_channel();
    hailo_status flush(const std::chrono::milliseconds &timeout);
    hailo_status set_num_avail_value(uint16_t new_value);
    hailo_status inc_num_available_for_ddr(uint16_t value, uint32_t size_mask);
    hailo_status wait_channel_interrupts_for_ddr(const std::chrono::milliseconds &timeout);
    Expected<uint16_t> get_hw_num_processed_ddr(uint32_t size_mask);
    /*Used for DDR channels only. TODO - remove */
    hailo_status start_channel(VdmaDescriptorList &desc_list);
    /* For channels controlled by the FW (inter context and cfg channels), the hailort needs only to register the channel to the driver.
       The FW would be responsible to open and close the channel */
    hailo_status register_channel_to_driver(uintptr_t desc_list_handle);
    hailo_status stop_channel();
    uint16_t get_page_size();

    hailo_status abort();
    hailo_status clear_abort();

    VdmaChannel(const VdmaChannel &other) = delete;
    VdmaChannel &operator=(const VdmaChannel &other) = delete;
    VdmaChannel(VdmaChannel &&other) noexcept;
    VdmaChannel &operator=(VdmaChannel &&other) = delete;

    static uint32_t calculate_buffer_size(const HailoRTDriver &driver, uint32_t transfer_size, uint32_t transfers_count,
        uint16_t requested_desc_page_size);


    const uint8_t channel_index;

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
    };

    VdmaChannel(uint8_t channel_index, Direction direction, HailoRTDriver &driver, uint32_t stream_index,
        LatencyMeterPtr latency_meter, uint16_t desc_page_size, uint16_t transfers_per_axi_intr, hailo_status &status);

    hailo_status allocate_buffer(const size_t buffer_size);
    void clear_descriptor_list();
    hailo_status release_buffer();
    static Direction other_direction(const Direction direction);
    hailo_status transfer_h2d(void *buf, size_t count);
    uint16_t get_num_available();
    Expected<uint16_t> get_hw_num_processed();
    void add_pending_buffer(uint32_t first_desc, uint32_t last_desc);
    hailo_status inc_num_available(uint16_t value);
    hailo_status transfer_d2h(void *buf, size_t count);
    bool is_ready_for_transfer_h2d(size_t buffer_size);
    bool is_ready_for_transfer_d2h(size_t buffer_size);
    hailo_status prepare_descriptors(size_t transfer_size, VdmaInterruptsDomain first_desc_interrupts_domain,
        VdmaInterruptsDomain last_desc_interrupts_domain);
    hailo_status prepare_d2h_pending_descriptors(uint32_t descs_count, uint32_t transfer_size);
    void reset_internal_counters();

    uint32_t calculate_descriptors_count(uint32_t buffer_size);

    hailo_status wait_for_condition(std::function<bool()> condition, std::chrono::milliseconds timeout);

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

    Direction m_direction;
    HailoRTDriver &m_driver;
    VdmaChannelRegs m_host_registers;
    VdmaChannelRegs m_device_registers;

    // TODO: use m_descriptors_buffer.desc_page_size()
    const uint16_t m_desc_page_size;

    // TODO: remove the unique_ptr, instead allocate the buffer in the ctor (needs to move ddr channel to
    // other class)
    std::unique_ptr<VdmaBuffer> m_mapped_user_buffer;
    std::unique_ptr<VdmaDescriptorList> m_descriptors_buffer;
    uint32_t m_stream_index;
    LatencyMeterPtr m_latency_meter;

    MmapBuffer<State> m_state;
    // Unique channel handle, may be changed between registration to driver. This object is shared
    // because multiple processes can enable/disable vdma channel (which changes the channel)
    MmapBuffer<HailoRTDriver::VdmaChannelHandle> m_channel_handle;

    bool m_channel_enabled;
    
    uint16_t m_transfers_per_axi_intr;
};

} /* namespace hailort */

#endif  // _HAILO_VDMA_CHANNEL_HPP_