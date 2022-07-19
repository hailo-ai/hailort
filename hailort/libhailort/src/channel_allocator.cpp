/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file channel_allocator.cpp
 * @brief Allocates vdma channel indexes, allows reusing non-boundary channels between contextes.
 **/

#include "channel_allocator.hpp"


namespace hailort
{

hailo_status ChannelAllocator::set_number_of_cfg_channels(const uint8_t number_of_cfg_channels)
{
    // TODO HRT-7198: Currently we assume cfg_index==vdma_channel_index. Needs to allocate the channels before.

    CHECK(number_of_cfg_channels <= CONTROL_PROTOCOL__MAX_CFG_CHANNELS, HAILO_INVALID_HEF, "Too many cfg channels");
    size_t channels_count = 0;
    for (uint8_t index = MIN_H2D_CHANNEL_INDEX; index <= MAX_H2D_CHANNEL_INDEX; ++index) {
        // use the empty channel if avaialble
        if (!m_channels_info[index].is_used()) {
            m_channels_info[index].set_type(ChannelInfo::Type::CFG);
            channels_count++;
        }
        if (number_of_cfg_channels == channels_count) {
            return HAILO_SUCCESS;
        }
    }

    LOGGER__ERROR("Failed to set cfg channels");
    return HAILO_INTERNAL_FAILURE;
}

Expected<uint8_t> ChannelAllocator::get_available_channel_index(std::set<uint8_t> &blacklist,
    ChannelInfo::Type required_type, VdmaChannel::Direction direction, const std::string &layer_name)
{
    uint8_t min_channel_index =
        (direction == VdmaChannel::Direction::H2D) ? MIN_H2D_CHANNEL_INDEX : MIN_D2H_CHANNEL_INDEX;
    uint8_t max_channel_index =
        (direction == VdmaChannel::Direction::H2D) ? MAX_H2D_CHANNEL_INDEX : MAX_D2H_CHANNEL_INDEX;

    for (uint8_t index = min_channel_index; index <= max_channel_index; ++index) {
        // Skip index that are on the blacklist
        if (contains(blacklist, index)) {
            continue;
        }

        // In preliminary_run_asap, channels are reused across contexts (same channels in the preliminary
        // context and first dynamic context).
        const bool is_preliminary_run_asap = m_supported_features.preliminary_run_asap;
        if (is_preliminary_run_asap) {
            if (m_channels_info[index].is_used() &&
            (m_channels_info[index].get_layer_name() == layer_name) &&
            (m_channels_info[index].is_type(required_type))) {
                LOGGER__TRACE("Reusing channel {} for layer {} (running in preliminary_run_asap mode)",
                    index, layer_name);
                return index;
            }
        }

        // Use the empty channel if available
        if (!m_channels_info[index].is_used()) {
            m_channels_info[index].set_type(required_type);
            m_channels_info[index].set_layer_name(layer_name);
            return index;
        }

        if (((ChannelInfo::Type::BOUNDARY != required_type) && (ChannelInfo::Type::CFG != required_type)) &&
            ((m_channels_info[index].is_type(ChannelInfo::Type::DDR)) || (m_channels_info[index].is_type(ChannelInfo::Type::INTER_CONTEXT)))) {
            m_channels_info[index].set_type(required_type);
            m_channels_info[index].set_layer_name(layer_name);
            return index;
        }
    }

    LOGGER__ERROR("Failed to get available channel_index");
    return make_unexpected(HAILO_INTERNAL_FAILURE);
}

Expected<std::reference_wrapper<ChannelInfo>> ChannelAllocator::get_channel_info(uint8_t index)
{
    CHECK_AS_EXPECTED(index < m_channels_info.max_size(), HAILO_INVALID_ARGUMENT);
    return std::ref(m_channels_info[index]);
}

std::vector<vdma::ChannelId> ChannelAllocator::get_boundary_channel_ids() const
{
    std::vector<vdma::ChannelId> fw_managed_channel_ids;

    for (uint8_t i = 0; i < m_channels_info.max_size(); ++i) {
        const vdma::ChannelId channel_id = {vdma::DEFAULT_ENGINE_INDEX, i};
        if (m_channels_info[i].is_type(ChannelInfo::Type::BOUNDARY)) {
            fw_managed_channel_ids.push_back(channel_id);
        }
    }

    return fw_managed_channel_ids;
}

std::vector<vdma::ChannelId> ChannelAllocator::get_fw_managed_channel_ids() const
{
    std::vector<vdma::ChannelId> fw_managed_channel_ids;

    for (uint8_t i = 0; i < m_channels_info.max_size(); ++i) {
        const vdma::ChannelId channel_id = {vdma::DEFAULT_ENGINE_INDEX, i};
        if ((m_channels_info[i].is_type(ChannelInfo::Type::INTER_CONTEXT)) ||
            m_channels_info[i].is_type(ChannelInfo::Type::CFG) ||
            m_channels_info[i].is_type(ChannelInfo::Type::DDR)) {
            fw_managed_channel_ids.push_back(channel_id);
        }
    }

    return fw_managed_channel_ids;
}

} /* namespace hailort */
