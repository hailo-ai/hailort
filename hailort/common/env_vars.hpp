/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file env_vars.hpp
 * @brief: defines a set of environment variables used in the HailoRT
 * **/

#ifndef HAILO_ENV_VARS_HPP_
#define HAILO_ENV_VARS_HPP_


namespace hailort
{

#define HAILORT_LOGGER_PATH_ENV_VAR ("HAILORT_LOGGER_PATH")

#define HAILORT_CONSOLE_LOGGER_LEVEL_ENV_VAR ("HAILORT_CONSOLE_LOGGER_LEVEL")

#define SCHEDULER_MON_ENV_VAR ("HAILO_MONITOR")
#define SCHEDULER_MON_TIME_INTERVAL_IN_MILLISECONDS_ENV_VAR ("HAILO_MONITOR_TIME_INTERVAL")

#define TRACE_ENV_VAR ("HAILO_TRACE")
#define TRACE_ENV_VAR_VALUE ("scheduler")
#define TRACE_ENV_VAR_TIME_IN_SECONDS_BOUNDED_DUMP ("HAILO_TRACE_TIME_IN_SECONDS_BOUNDED_DUMP")
#define TRACE_ENV_VAR_SIZE_IN_KB_BOUNDED_DUMP ("HAILO_TRACE_SIZE_IN_KB_BOUNDED_DUMP")

#define PERFETTO_TRACE_ENV_VAR ("HAILO_PERFETTO_TRACE")

#define PROFILER_FILE_ENV_VAR ("HAILO_TRACE_PATH")

} /* namespace hailort */

#endif /* HAILO_ENV_VARS_HPP_ */