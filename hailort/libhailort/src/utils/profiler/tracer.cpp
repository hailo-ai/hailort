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

#define PROFILER_ENV_VAR ("HAILO_TRACE")
#define PROFILER_ENV_VAR_VALUE ("scheduler")

namespace hailort
{

Tracer::Tracer()
{
    init_scheduler_profiler_handler();
    init_monitor_handler();
}

void Tracer::init_scheduler_profiler_handler()
{
    const char* env_var_name = PROFILER_ENV_VAR;
    m_should_trace = is_env_variable_on(env_var_name, PROFILER_ENV_VAR_VALUE, sizeof(PROFILER_ENV_VAR_VALUE));
    if (m_should_trace) {
        m_start_time = std::chrono::high_resolution_clock::now();
        int64_t time_since_epoch = std::chrono::duration_cast<std::chrono::nanoseconds>(m_start_time.time_since_epoch()).count();
        m_handlers.push_back(std::make_unique<SchedulerProfilerHandler>(time_since_epoch));
    }
}

void Tracer::init_monitor_handler()
{
    const char* env_var_name = SCHEDULER_MON_ENV_VAR;
    m_should_monitor = is_env_variable_on(env_var_name, SCHEDULER_MON_ENV_VAR_VALUE, sizeof(SCHEDULER_MON_ENV_VAR_VALUE));
    if (m_should_monitor) {
        m_handlers.push_back(std::make_unique<MonitorHandler>());
    }
}

}
