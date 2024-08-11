/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file tracer.cpp
 * @brief: Tracing mechanism for HailoRT + FW events
 **/

#include "common/utils.hpp"
#include "utils/profiler/tracer.hpp"

constexpr auto TRACE_ENV_VAR ("HAILO_TRACE");
constexpr auto TRACE_ENV_VAR_VALUE ("scheduler");
constexpr auto TRACE_ENV_VAR_TIME_IN_SECONDS_BOUNDED_DUMP("HAILO_TRACE_TIME_IN_SECONDS_BOUNDED_DUMP");
constexpr auto TRACE_ENV_VAR_SIZE_IN_KB_BOUNDED_DUMP("HAILO_TRACE_SIZE_IN_KB_BOUNDED_DUMP");

namespace hailort
{

Tracer::Tracer()
{
    init_scheduler_profiler_handler();
    init_monitor_handler();
}

void Tracer::init_scheduler_profiler_handler()
{
    const char* env_var_name = TRACE_ENV_VAR;
    m_should_trace = is_env_variable_on(env_var_name, TRACE_ENV_VAR_VALUE);
    if (m_should_trace) {
        auto profiler_time_bounded = std::getenv(TRACE_ENV_VAR_TIME_IN_SECONDS_BOUNDED_DUMP);
        auto profiler_size_bounded = std::getenv(TRACE_ENV_VAR_SIZE_IN_KB_BOUNDED_DUMP);
        auto profiler_time_bounded_time_in_seconds = (profiler_time_bounded == nullptr) ? 0 : std::stoull(profiler_time_bounded);
        auto profiler_size_bounded_size_in_kb = (profiler_size_bounded == nullptr) ? 0 : std::stoull(profiler_size_bounded);

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
    const char* env_var_name = SCHEDULER_MON_ENV_VAR;
    m_should_monitor = is_env_variable_on(env_var_name, SCHEDULER_MON_ENV_VAR_VALUE);
    if (m_should_monitor) {
        m_handlers.push_back(std::make_unique<MonitorHandler>());
    }
}

}
