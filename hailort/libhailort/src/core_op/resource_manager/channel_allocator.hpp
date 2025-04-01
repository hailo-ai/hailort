/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file channel_allocator.hpp
 * @brief Allocates vdma channel indexes, allows reusing non-boundary channels between contextes.
 **/

#ifndef _HAILO_CHANNEL_ALLOCATOR_HPP_
#define _HAILO_CHANNEL_ALLOCATOR_HPP_

#include "hailo/hailort.h"

#include "vdma/memory/descriptor_list.hpp"
#include "vdma/channel/channel_id.hpp"
#include "hef/layer_info.hpp"

#include <array>


namespace hailort
{

class ChannelAllocator final
{
public:
    explicit ChannelAllocator(size_t max_engines_count);
    ChannelAllocator(ChannelAllocator &&other) = default;

    Expected<vdma::ChannelId> get_available_channel_id(const LayerIdentifier &layer_identifier,
        HailoRTDriver::DmaDirection direction, uint8_t engine_index, bool use_enhanced_channel = false);
    hailo_status free_channel_index(const LayerIdentifier &layer_identifier);

private:
    void insert_new_channel_id(const LayerIdentifier &layer_identifier, const vdma::ChannelId &channel_id);

    const size_t m_max_engines_count;

    // Contains all channels that are currently used. This channels are released in the free_channel_index.
    std::map<LayerIdentifier, vdma::ChannelId> m_allocated_channels;

    // Contains all channels id allocated for the network group. This channels are never released.
    std::set<vdma::ChannelId> m_boundary_channel_ids;
    std::set<vdma::ChannelId> m_internal_channel_ids;
};

} /* namespace hailort */

#endif /* _HAILO_CHANNEL_ALLOCATOR_HPP_ */
