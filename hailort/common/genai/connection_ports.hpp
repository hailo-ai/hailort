/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file connection_ports.hpp
 * @brief: Defines the connection ports for each hrt genai server
 **/

#ifndef _HAILO_CONNECTION_PORTS_HPP_
#define _HAILO_CONNECTION_PORTS_HPP_

#include "hailo/hailort.h"
#include "hrpc/connection_context.hpp"

namespace hailort
{
namespace genai
{

static constexpr uint16_t DEFAULT_LLM_CONNECTION_PORT = 12145;
static constexpr uint16_t DEFAULT_VLM_CONNECTION_PORT = 12147;
static constexpr uint16_t DEFAULT_SPEECH2TEXT_CONNECTION_PORT = 12149;

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_CONNECTION_PORTS_HPP_ */
