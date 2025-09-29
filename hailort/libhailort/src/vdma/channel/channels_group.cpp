/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file channels_group.cpp
 **/

#include "channels_group.hpp"

namespace hailort {
namespace vdma {

ChannelsGroup::ChannelsGroup(std::initializer_list<BoundaryChannelPtr> channels)
{
    for (const auto &channel : channels) {
        add_channel(channel);
    }
}

void ChannelsGroup::add_channel(BoundaryChannelPtr channel)
{
    const auto id = channel->get_channel_id();
    assert(nullptr == m_channels[id.engine_index][id.channel_index]);
    m_channels[id.engine_index][id.channel_index] = channel;
}

ChannelsBitmap ChannelsGroup::bitmap() const
{
    ChannelsBitmap bitmap{};
    for (size_t i = 0; i < m_channels.size(); i++) {
        for (size_t j = 0; j < m_channels[i].size(); j++) {
            if (m_channels[i][j]) {
                bitmap[i] |= (1ULL << j);
            }
        }
    }
    return bitmap;
}

bool ChannelsGroup::should_measure_timestamp() const
{
    for (const auto &engine : m_channels) {
        for (const auto &channel : engine) {
            if (channel && channel->should_measure_timestamp()) {
                return true;
            }
        }
    }
    return false;
}

Expected<BoundaryChannelPtr> ChannelsGroup::get_by_id(vdma::ChannelId channel_id)
{
    auto channel = m_channels[channel_id.engine_index][channel_id.channel_index];
    if (!channel) {
        return make_unexpected(HAILO_NOT_FOUND);
    }
    return channel;
}

Expected<BoundaryChannelPtr> ChannelsGroup::get_by_name(const std::string &stream_name)
{
    for (const auto &engine : m_channels) {
        for (const auto &channel : engine) {
            if (channel && (channel->stream_name() == stream_name)) {
                return BoundaryChannelPtr{channel};
            }
        }
    }
    return make_unexpected(HAILO_NOT_FOUND);
}

void ChannelsGroup::process_interrupts(IrqData &&irq_data)
{
    assert(irq_data.channels_count <= ARRAY_ENTRIES(irq_data.channels_irq_data));
    for (uint8_t irq_index = 0; irq_index < irq_data.channels_count; irq_index++) {
        const auto &channel_irq_data = irq_data.channels_irq_data[irq_index];
        auto status = process_channel_interrupt(channel_irq_data);
        if ((status != HAILO_SUCCESS) && (status != HAILO_STREAM_NOT_ACTIVATED)) {
            LOGGER__ERROR("Trigger channel completion failed on channel {} with status {}", channel_irq_data.channel_id, status);
        }
    }
}

// Function that based off the irq data returns the status to be sent to the callbak functions
static hailo_status get_callback_status(vdma::ChannelId channel_id, const ChannelIrqData &irq_data)
{
    hailo_status status = HAILO_UNINITIALIZED;
    if (!irq_data.is_active) {
        status = HAILO_STREAM_ABORT;
    } else if (!irq_data.validation_success) {
        LOGGER__WARNING("Channel {} validation failed", channel_id);
        status = HAILO_INTERNAL_FAILURE;
    } else {
        status = HAILO_SUCCESS;
    }
    return status;
}

hailo_status ChannelsGroup::process_channel_interrupt(const ChannelIrqData &channel_irq_data)
{
    TRY(auto channel, get_by_id(channel_irq_data.channel_id), "Channel {} not found", channel_irq_data.channel_id);
    auto callback_status = get_callback_status(channel_irq_data.channel_id, channel_irq_data);
    if (HAILO_SUCCESS == callback_status) {
        return channel->trigger_channel_completion(channel_irq_data.transfers_completed);
    } else {
        channel->trigger_channel_error(callback_status);
        return HAILO_SUCCESS;
    }
}

} /* namespace vdma */
} /* namespace hailort */
