/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file channels_group.hpp
 * @brief Contains a group of channels that are used together.
 **/

#ifndef _HAILO_CHANNELS_GROUP_HPP_
#define _HAILO_CHANNELS_GROUP_HPP_

#include "vdma/channel/boundary_channel.hpp"

namespace hailort {
namespace vdma {

class ChannelsGroup final {
public:
    ChannelsGroup() = default;
    ChannelsGroup(std::initializer_list<BoundaryChannelPtr> channels);

    void add_channel(BoundaryChannelPtr channel);

    ChannelsBitmap bitmap() const;
    bool should_measure_timestamp() const;
    Expected<BoundaryChannelPtr> get_by_id(vdma::ChannelId channel_id);
    Expected<BoundaryChannelPtr> get_by_name(const std::string &stream_name);

    void process_interrupts(IrqData &&irq_data);

private:

    hailo_status process_channel_interrupt(const ChannelIrqData &irq_data);

    std::array<
        std::array<BoundaryChannelPtr, MAX_VDMA_CHANNELS_COUNT>,
        MAX_VDMA_ENGINES_COUNT
    > m_channels;
};

} /* namespace vdma */
} /* namespace hailort */

#endif /* _HAILO_CHANNELS_GROUP_HPP_ */
