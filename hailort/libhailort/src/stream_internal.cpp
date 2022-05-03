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

EventPtr &InputStreamBase::get_network_group_activated_event()
{
    return m_network_group_activated_event;
}

bool InputStreamBase::is_scheduled()
{
    return false;
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
