/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file channel_allocator.hpp
 * @brief Allocates vdma channel indexes, allows reusing non-boundary channels between contextes.
 **/

#ifndef _HAILO_CHANNEL_ALLOCATOR_HPP_
#define _HAILO_CHANNEL_ALLOCATOR_HPP_

#include "hailo/hailort.h"
#include "vdma_descriptor_list.hpp"
#include "vdma/channel_id.hpp"
#include "vdma_channel.hpp"
#include "hef_internal.hpp"

#include <array>

namespace hailort
{

class ChannelInfo
{
public:
    enum class Type : uint8_t 
    {
        NOT_SET = 0,
        BOUNDARY = 1,
        INTER_CONTEXT = 2,
        DDR = 3,
        CFG = 4
    };

    ChannelInfo() : m_type(Type::NOT_SET), m_stream_index(UINT8_MAX), m_layer_name() {}

    void set_type(Type type)
    { 
        m_type = type;
    }

    bool is_type(Type type) const
    {
        return (m_type == type);
    }

    bool is_used() const
    {
        return (m_type != Type::NOT_SET);
    }

    void set_stream_index(uint8_t stream_index)
    {
        m_stream_index = stream_index;
    }

    void set_layer_name(const std::string &name) 
    {
        m_layer_name = name;
    }

    const std::string &get_layer_name() 
    {
        return m_layer_name;
    }

private:
    Type m_type;
    uint8_t m_stream_index;
    std::string m_layer_name;
};

class ChannelAllocator final
{
public:
    ChannelAllocator(const NetworkGroupSupportedFeatures &supported_features) :
        m_supported_features(supported_features)
    {}

    hailo_status set_number_of_cfg_channels(const uint8_t number_of_cfg_channels);

    Expected<uint8_t> get_available_channel_index(std::set<uint8_t> &blacklist,
        ChannelInfo::Type required_type, VdmaChannel::Direction direction, const std::string &layer_name);

    Expected<std::reference_wrapper<ChannelInfo>> get_channel_info(uint8_t index);

    std::vector<vdma::ChannelId> get_boundary_channel_ids() const;
    std::vector<vdma::ChannelId> get_fw_managed_channel_ids() const;

private:
    NetworkGroupSupportedFeatures m_supported_features;
    std::array<ChannelInfo, VDMA_CHANNELS_PER_ENGINE> m_channels_info;
};

} /* namespace hailort */

#endif /* _HAILO_CHANNEL_ALLOCATOR_HPP_ */
