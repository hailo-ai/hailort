/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file channel_base.hpp
 * @brief Base class of Boundary Channel - responsible for all the basic vdma channel functionality that interacts with the
 *  driver and the registers
 *      The hierarchy is as follows:
 *        --------------------------------------------------------------------------------------------------------------
 *        |                    ChannelBase            | (Base class - includes state and buffers)
 *        |                          |                              |
 *        |                  BoundaryChannel          | (handles Boundary channels)
 *        --------------------------------------------------------------------------------------------------------------
 **/

#ifndef _HAILO_VDMA_CHANNEL_BASE_HPP_
#define _HAILO_VDMA_CHANNEL_BASE_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/buffer.hpp"

#include "common/latency_meter.hpp"

#include "vdma/channel/vdma_channel_regs.hpp"
#include "vdma/memory/sg_buffer.hpp"
#include "vdma/memory/descriptor_list.hpp"
#include "vdma/channel/channel_id.hpp"
#include "vdma/channel/channel_state.hpp"

#include <mutex>
#include <condition_variable>


namespace hailort {
namespace vdma {

class ChannelBase
{
public:
    using Direction = HailoRTDriver::DmaDirection;

    ChannelBase(vdma::ChannelId channel_id, Direction direction, HailoRTDriver &driver, uint32_t descs_count,
        uint16_t desc_page_size, const std::string &stream_name, LatencyMeterPtr latency_meter, hailo_status &status);
    ChannelBase(const ChannelBase &other) = delete;
    ChannelBase &operator=(const ChannelBase &other) = delete;
    ChannelBase(ChannelBase &&other) = delete;
    ChannelBase &operator=(ChannelBase &&other) = delete;
    virtual ~ChannelBase() = default;

    vdma::ChannelId get_channel_id() const
    {
        return m_channel_id;
    }

    uint16_t get_page_size()
    {
        return m_desc_list->desc_page_size();
    }

    const std::string &stream_name() const
    {
        return m_stream_name;
    }

    size_t get_transfers_count_in_buffer(size_t transfer_size);
    size_t get_buffer_size() const;
    uint32_t calculate_descriptors_count(uint32_t buffer_size) const;

    std::shared_ptr<DescriptorList> get_desc_list()
    {
        return m_desc_list;
    }

protected:
    const vdma::ChannelId m_channel_id;
    const Direction m_direction;
    HailoRTDriver &m_driver;
    VdmaChannelRegs m_host_registers;
    std::shared_ptr<DescriptorList> m_desc_list; // Host side descriptor list
    const std::string m_stream_name;
    std::unique_ptr<VdmaChannelState> m_state;
    LatencyMeterPtr m_latency_meter;

    static bool is_desc_between(uint16_t begin, uint16_t end, uint16_t desc);
    // Returns the desc index of the last desc whose timestamp was measured in the driver
    Expected<uint16_t> update_latency_meter();
    Expected<bool> is_aborted();
    hailo_status set_num_avail_value(uint16_t new_value);
    uint16_t get_num_available();
    void set_num_proc_value(uint16_t new_value);
    Expected<uint16_t> get_hw_num_processed();
    hailo_status inc_num_available(uint16_t value);
    static Direction other_direction(const Direction direction);

private:
    hailo_status allocate_descriptor_list(uint32_t descs_count, uint16_t desc_page_size);
};

} /* namespace vdma */
} /* namespace hailort */

#endif /* _HAILO_VDMA_CHANNEL_BASE_HPP_ */