/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file stream_internal.cpp
 * @brief Implementation of InputStreamBase and OutputStreamBase
 **/

#include "stream_internal.hpp"
#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "common/logger_macros.hpp"
#include "hailo/transform.hpp"
#include "common/utils.hpp"

namespace hailort
{

InputStreamBase::InputStreamBase(const hailo_stream_info_t &stream_info,
        const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config, const EventPtr &network_group_activated_event) :
    m_nn_stream_config(nn_stream_config), m_network_group_activated_event(network_group_activated_event)
{
    m_stream_info = stream_info;
}

EventPtr &InputStreamBase::get_network_group_activated_event()
{
    return m_network_group_activated_event;
}

bool InputStreamBase::is_scheduled()
{
    return false;
}

OutputStreamBase::OutputStreamBase(const LayerInfo &layer_info, const hailo_stream_info_t &stream_info,
        const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config, const EventPtr &network_group_activated_event) :
    m_nn_stream_config(nn_stream_config), m_layer_info(layer_info), m_network_group_activated_event(network_group_activated_event)
{
    m_stream_info = stream_info;
}

EventPtr &OutputStreamBase::get_network_group_activated_event()
{
    return m_network_group_activated_event;
}

bool OutputStreamBase::is_scheduled()
{
    return false;
}

} /* namespace hailort */
