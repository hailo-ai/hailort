/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file tracer.cpp
 * @brief: Tracing mechanism for HailoRT + FW events
 **/

#include "common/utils.hpp"
#include "common/env_vars.hpp"
#include "utils/profiler/tracer.hpp"


namespace hailort
{

Tracer::Tracer()
{
    init_scheduler_profiler_handler();
    init_monitor_handler();
}

void Tracer::init_scheduler_profiler_handler()
{
    m_should_trace = is_env_variable_on(TRACE_ENV_VAR, TRACE_ENV_VAR_VALUE);
    if (m_should_trace) {
        auto profiler_time_bounded = get_env_variable(TRACE_ENV_VAR_TIME_IN_SECONDS_BOUNDED_DUMP);
        auto profiler_time_bounded_time_in_seconds = (profiler_time_bounded) ? std::stoull(profiler_time_bounded.value()) : 0;
        auto profiler_size_bounded = get_env_variable(TRACE_ENV_VAR_SIZE_IN_KB_BOUNDED_DUMP);
        auto profiler_size_bounded_size_in_kb = (profiler_size_bounded) ? std::stoull(profiler_size_bounded.value()) : 0;

        if ((0 != profiler_time_bounded_time_in_seconds) && (0 != profiler_size_bounded_size_in_kb)) {
            LOGGER__WARNING("Scheduler profiler cannot be initialized. Both {} and {} are set. Only one can be set at a time",
                TRACE_ENV_VAR_TIME_IN_SECONDS_BOUNDED_DUMP,
                TRACE_ENV_VAR_SIZE_IN_KB_BOUNDED_DUMP);
        } else {
            m_handlers.push_back(std::make_unique<SchedulerProfilerHandler>(
                profiler_time_bounded_time_in_seconds, profiler_size_bounded_size_in_kb));
        }
    }
}

void Tracer::init_monitor_handler()
{
    m_should_monitor = is_env_variable_on(SCHEDULER_MON_ENV_VAR, SCHEDULER_MON_ENV_VAR_VALUE);
    if (m_should_monitor) {
        m_handlers.push_back(std::make_unique<MonitorHandler>());
    }
}

}
