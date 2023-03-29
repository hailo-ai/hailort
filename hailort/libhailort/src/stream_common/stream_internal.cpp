/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file stream_internal.cpp
 * @brief Implementation of InputStreamBase and OutputStreamBase
 **/

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/transform.hpp"

#include "common/utils.hpp"
#include "common/logger_macros.hpp"

#include "stream_common/stream_internal.hpp"


namespace hailort
{

InputStreamBase::InputStreamBase(const hailo_stream_info_t &stream_info,
        const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config, const EventPtr &core_op_activated_event) :
    m_nn_stream_config(nn_stream_config), m_core_op_activated_event(core_op_activated_event)
{
    m_stream_info = stream_info;
}

EventPtr &InputStreamBase::get_core_op_activated_event()
{
    return m_core_op_activated_event;
}

bool InputStreamBase::is_scheduled()
{
    return false;
}

OutputStreamBase::OutputStreamBase(const LayerInfo &layer_info, const hailo_stream_info_t &stream_info,
        const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config, const EventPtr &core_op_activated_event) :
    m_nn_stream_config(nn_stream_config), m_layer_info(layer_info), m_core_op_activated_event(core_op_activated_event)
{
    m_stream_info = stream_info;
}

EventPtr &OutputStreamBase::get_core_op_activated_event()
{
    return m_core_op_activated_event;
}

bool OutputStreamBase::is_scheduled()
{
    return false;
}

} /* namespace hailort */
