/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file channel_base.cpp
 * @brief Base class of Boundary Channel - responsible for all the basic vdma channel functionality that interacts with the
 *  driver and the registers
 *      The hierarchy is as follows:
 *        --------------------------------------------------------------------------------------------------------------
 *        |                    ChannelBase            | (Base class - includes state and buffers)
 *        |                          |                              |
 *        |                  BoundaryChannel          | (handles Boundary channels)
 *        --------------------------------------------------------------------------------------------------------------
 **/
#include "vdma/channel/channel_base.hpp"


namespace hailort {
namespace vdma {

ChannelBase::ChannelBase(vdma::ChannelId channel_id, Direction direction, HailoRTDriver &driver, uint32_t descs_count,
                         uint16_t desc_page_size, const std::string &stream_name, LatencyMeterPtr latency_meter, hailo_status &status) :
    m_channel_id(channel_id),
    m_direction(direction),
    m_driver(driver),
    m_host_registers(driver, channel_id, direction),
    m_desc_list(nullptr),
    m_stream_name(stream_name),
    m_latency_meter(latency_meter)
{
    if (channel_id.channel_index >= VDMA_CHANNELS_PER_ENGINE) {
        LOGGER__ERROR("Invalid DMA channel index {}", channel_id.channel_index);
        status = HAILO_INVALID_ARGUMENT;
        return;
    }

    if (channel_id.engine_index >= driver.dma_engines_count()) {
        LOGGER__ERROR("Invalid DMA engine index {}, max {}", channel_id.engine_index, driver.dma_engines_count());
        status = HAILO_INVALID_ARGUMENT;
        return;
    }

    auto state = VdmaChannelState::create(descs_count, (nullptr != m_latency_meter));
    if(!state) {
        LOGGER__ERROR("Failed to create channel's state");
        status = state.status();
        return;
    }
    m_state = state.release();

    status = allocate_descriptor_list(descs_count, desc_page_size);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to allocate Vdma buffer for channel transfer! status={}", status);
        return;
    }

    status = HAILO_SUCCESS;
}

hailo_status ChannelBase::set_num_avail_value(uint16_t new_value)
{
    // TODO - HRT-7885 : add check in driver
    CHECK(m_state->m_is_channel_activated, HAILO_STREAM_NOT_ACTIVATED,
        "Error, can't set num available when stream is not activated");

    auto status = m_host_registers.set_num_available(new_value);
    CHECK_SUCCESS(status, "Fail to write vdma num available register");

#ifndef NDEBUG
    // Validate synchronization with HW
    auto hw_num_avail = m_host_registers.get_num_available();
    assert(hw_num_avail);
    assert(hw_num_avail.value() == new_value);
#endif
    return HAILO_SUCCESS;
}

hailo_status ChannelBase::inc_num_available(uint16_t value)
{
    //TODO: validate that count is added.
    int num_available = get_num_available();
    int num_processed = CB_TAIL(m_state->m_descs);
    int num_free = CB_AVAIL(m_state->m_descs, num_available, num_processed);
    if (value > num_free) {
        return HAILO_OUT_OF_DESCRIPTORS;
    }

    CB_ENQUEUE(m_state->m_descs, value);
    num_available = (num_available + value) & m_state->m_descs.size_mask;
    return set_num_avail_value(static_cast<uint16_t>(num_available));
}

bool ChannelBase::is_desc_between(uint16_t begin, uint16_t end, uint16_t desc)
{
    if (begin == end) {
        // There is nothing between
        return false;
    }
    if (begin < end) {
        // desc needs to be in [begin, end)
        return (begin <= desc) && (desc < end);
    }
    else {
        // desc needs to be in [0, end) or [begin, m_state->m_descs.size()-1]
        return (desc < end) || (begin <= desc);
    }
}

uint16_t ChannelBase::get_num_available()
{
    uint16_t num_available = (uint16_t)CB_HEAD(m_state->m_descs);

#ifndef NDEBUG
    // Validate synchronization with HW
    auto hw_num_avail = m_host_registers.get_num_available();
    assert(hw_num_avail);

    // On case of channel aborted, the num_available is set to 0 (so we don't accept sync)
    auto is_aborted_exp = m_host_registers.is_aborted();
    assert(is_aborted_exp);

    if (m_state->m_is_channel_activated && !is_aborted_exp.value()) {
        assert(hw_num_avail.value() == num_available);
    }
#endif
    return num_available;
}

void ChannelBase::set_num_proc_value(uint16_t new_value)
{
    assert(new_value < m_state->m_descs.size);
    _CB_SET(m_state->m_descs.tail, new_value);
}

Expected<uint16_t> ChannelBase::get_hw_num_processed()
{
    auto hw_num_processed = m_host_registers.get_num_processed();
    CHECK_EXPECTED(hw_num_processed, "Fail to read vdma num processed register");

    // Although the hw_num_processed should be a number between 0 and m_descs.size-1, if
    // m_desc.size < 0x10000 (the maximum desc size), the actual hw_num_processed is a number
    // between 1 and m_descs.size. Therefore the value can be m_descs.size, in this case we change it
    // to zero.
    return static_cast<uint16_t>(hw_num_processed.value() & m_state->m_descs.size_mask);
}

ChannelBase::Direction ChannelBase::other_direction(Direction direction)
{
    return (Direction::H2D == direction) ? Direction::D2H : Direction::H2D;
}

hailo_status ChannelBase::allocate_descriptor_list(uint32_t descs_count, uint16_t desc_page_size)
{
    static const bool CIRCULAR = true;
    auto desc_list_exp = DescriptorList::create(descs_count, desc_page_size, CIRCULAR, m_driver);
    CHECK_EXPECTED_AS_STATUS(desc_list_exp);

    m_desc_list = make_shared_nothrow<DescriptorList>(desc_list_exp.release());
    CHECK_NOT_NULL(m_desc_list, HAILO_OUT_OF_HOST_MEMORY);

    return HAILO_SUCCESS;
}

size_t ChannelBase::get_transfers_count_in_buffer(size_t transfer_size)
{
    const auto descs_in_transfer = m_desc_list->descriptors_in_buffer(transfer_size);
    const auto descs_count = CB_SIZE(m_state->m_descs);
    return (descs_count - 1) / descs_in_transfer;
}

Expected<uint16_t> ChannelBase::update_latency_meter()
{
    uint16_t last_num_processed = m_state->m_last_timestamp_num_processed;

    auto timestamp_list = m_driver.vdma_interrupts_read_timestamps(m_channel_id);
    CHECK_EXPECTED(timestamp_list);

    if (0 == timestamp_list->count) {
        // No new timestamps for this channel, return the previous result
        return Expected<uint16_t>(last_num_processed);
    }

    // TODO: now we have more iterations than we need. We know that the pending buffers + the timestamp list
    // are ordered. If pending_buffer[i] is not in any of the timestamps_list[0, 1, ... k], then also pending_buffer[i+1,i+2,...]
    // not in those timestamps

    for (const auto &pending_buffer : m_state->m_pending_buffers) {
        uint16_t latency_desc = static_cast<uint16_t>(pending_buffer.latency_measure_desc);
        for (size_t i = 0; i < timestamp_list->count; i++) {
            const auto &irq_timestamp = timestamp_list->timestamp_list[i];
            const auto desc_num_processed = static_cast<uint16_t>(irq_timestamp.desc_num_processed & m_state->m_descs.size_mask);
            if (is_desc_between(last_num_processed, desc_num_processed, latency_desc)) {
                if (m_direction == Direction::H2D) {
                    m_latency_meter->add_start_sample(irq_timestamp.timestamp);
                }
                else {
                    m_latency_meter->add_end_sample(m_stream_name, irq_timestamp.timestamp);
                }
                break;
            }
        }
    }

    m_state->m_last_timestamp_num_processed = static_cast<uint16_t>(
        timestamp_list->timestamp_list[timestamp_list->count-1].desc_num_processed & m_state->m_descs.size_mask);
    return Expected<uint16_t>(m_state->m_last_timestamp_num_processed);
}

uint32_t ChannelBase::calculate_descriptors_count(uint32_t buffer_size) const
{
    return DescriptorList::calculate_descriptors_count(buffer_size, 1, m_desc_list->desc_page_size());
}

} /* namespace vdma */
} /* namespace hailort */
