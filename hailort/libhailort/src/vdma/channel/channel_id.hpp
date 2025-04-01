/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file channel_index.hpp
 * @brief Struct used for channel identifier - (engine_index, channel_index) pair.
 **/

#ifndef _HAILO_VDMA_CHANNEL_ID_HPP_
#define _HAILO_VDMA_CHANNEL_ID_HPP_

#include "hailo/hailort.h"
#include "common/logger_macros.hpp"
#include <spdlog/fmt/bundled/format.h>
#include <sstream>


namespace hailort {
namespace vdma {

// TODO: HRT-6949 don't use default engine index.
static constexpr uint8_t DEFAULT_ENGINE_INDEX = 0;

/**
 * For each dma engine we have 16 inputs channels and 16 output channels.
 * The amount of engines is determined by the driver.
 */
struct ChannelId
{
    uint8_t engine_index;
    uint8_t channel_index;

    // Allow put `ChannelId` inside std::set
    friend bool operator<(const ChannelId &a, const ChannelId &b)
    {
        return std::make_pair(a.engine_index, a.channel_index) < std::make_pair(b.engine_index, b.channel_index);
    }

    // Allow channel Id's to be compared
    friend bool operator==(const ChannelId &a, const ChannelId &b)
    {
        return ((a.channel_index == b.channel_index) && (a.engine_index == b.engine_index));
    }
};

} /* namespace vdma */
} /* namespace hailort */


template<>
struct fmt::formatter<hailort::vdma::ChannelId> : fmt::formatter<fmt::string_view> {
    template <typename FormatContext>
    auto format(const hailort::vdma::ChannelId &input, FormatContext& ctx) -> decltype(ctx.out()) {
        std::stringstream ss;
        ss << static_cast<uint32_t>(input.engine_index) << ":" 
            << static_cast<uint32_t>(input.channel_index);
        return fmt::formatter<fmt::string_view>::format(ss.str(), ctx);
    }
};

#endif /* _HAILO_VDMA_CHANNEL_ID_HPP_ */
