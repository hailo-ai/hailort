/**
 * Copyright (c) 2019-2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file channel_allocator.cpp
 * @brief Allocates vdma channel indexes, allows reusing non-boundary channels between contextes.
 **/

#include "core_op/resource_manager/channel_allocator.hpp"


namespace hailort
{

ChannelAllocator::ChannelAllocator(size_t max_engines_count) :
    m_max_engines_count(max_engines_count)
{}

Expected<vdma::ChannelId> ChannelAllocator::get_available_channel_id(const LayerIdentifier &layer_identifier,
    HailoRTDriver::DmaDirection direction, uint8_t engine_index)
{
    CHECK_AS_EXPECTED(engine_index < m_max_engines_count, HAILO_INVALID_ARGUMENT,
        "Invalid engine index {}, max is {}", engine_index, m_max_engines_count);

    const auto found_channel = m_allocated_channels.find(layer_identifier);
    if (found_channel != m_allocated_channels.end()) {
        CHECK_AS_EXPECTED(found_channel->second.engine_index == engine_index, HAILO_INTERNAL_FAILURE,
            "Mismatch engine index");
        return Expected<vdma::ChannelId>(found_channel->second);
    }

    // If we reach here, we need to allocate channel index for that layer.
    std::set<vdma::ChannelId> currently_used_channel_indexes;
    for (auto channel_id_pair : m_allocated_channels) {
        currently_used_channel_indexes.insert(channel_id_pair.second);
    }

    uint8_t min_channel_index =
        (direction == HailoRTDriver::DmaDirection::H2D) ? MIN_H2D_CHANNEL_INDEX : MIN_D2H_CHANNEL_INDEX;
    uint8_t max_channel_index =
        (direction == HailoRTDriver::DmaDirection::H2D) ? MAX_H2D_CHANNEL_INDEX : MAX_D2H_CHANNEL_INDEX;

    for (uint8_t index = min_channel_index; index <= max_channel_index; ++index) {
        const vdma::ChannelId channel_id = {engine_index, index};

        // Check that the channel is not currently in use.
        if (contains(currently_used_channel_indexes, channel_id)) {
            continue;
        }

        // In the case of boundary channels, if the channel id was used in previous context as an internal channel (and
        // it was freed, so it doesn't appear in `currently_used_channel_index`), we can't reuse it.
        if (std::get<0>(layer_identifier) == LayerType::BOUNDARY) {
            if (contains(m_internal_channel_ids, channel_id)) {
                continue;
            }
        }

        // Found it
        insert_new_channel_id(layer_identifier, channel_id);
        return Expected<vdma::ChannelId>(channel_id);
    }

    LOGGER__ERROR("Failed to get available channel_index");
    return make_unexpected(HAILO_INTERNAL_FAILURE);
}

hailo_status ChannelAllocator::free_channel_index(const LayerIdentifier &layer_identifier)
{
    auto layer_channel_pair = m_allocated_channels.find(layer_identifier);
    CHECK(m_allocated_channels.end() != layer_channel_pair, HAILO_INTERNAL_FAILURE, "Failed to free channel");
    CHECK(std::get<0>(layer_channel_pair->first) != LayerType::BOUNDARY, HAILO_INTERNAL_FAILURE,
        "Can't free boundary channels");

    m_allocated_channels.erase(layer_channel_pair);
    return HAILO_SUCCESS;
}

void ChannelAllocator::insert_new_channel_id(const LayerIdentifier &layer_identifier, const vdma::ChannelId &channel_id)
{
    if (LayerType::BOUNDARY == std::get<0>(layer_identifier)) {
        m_boundary_channel_ids.insert(channel_id);
    } else {
        m_internal_channel_ids.insert(channel_id);
    }

    m_allocated_channels.emplace(layer_identifier, channel_id);
}

} /* namespace hailort */
