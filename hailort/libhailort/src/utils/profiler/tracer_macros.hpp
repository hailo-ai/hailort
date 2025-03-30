/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file tracer_macros.hpp
 * @brief Macros for tracing mechanism for HailoRT + FW events
 **/

#ifndef _HAILO_TRACER_MACROS_HPP_
#define _HAILO_TRACER_MACROS_HPP_

#include "tracer.hpp"

namespace hailort
{

#define TRACE(type, ...) (Tracer::trace<type>(__VA_ARGS__))

}

#endif // _HAILO_TRACER_MACROS_HPP_