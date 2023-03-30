/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file tracer_macros.hpp
 * @brief Macros for tracing mechanism for HailoRT + FW events
 **/

#ifndef _HAILO_TRACER_MACROS_HPP_
#define _HAILO_TRACER_MACROS_HPP_

#if defined HAILO_ENABLE_PROFILER_BUILD
#include "tracer.hpp"
#endif

namespace hailort
{

struct VoidAll {
    template<typename... Args> VoidAll(Args const& ...) {}
};

#if defined HAILO_ENABLE_PROFILER_BUILD
#define TRACE(type, ...) (Tracer::trace<type>(__VA_ARGS__))
#else
#define TRACE(type, ...) {VoidAll temporary_name{__VA_ARGS__};}
#endif

}

#endif // _HAILO_TRACER_MACROS_HPP_