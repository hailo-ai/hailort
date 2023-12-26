/**
 * Copyright (c) 2020-2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file profiler_utils.hpp
 * @brief Utils for profiling mechanism for HailoRT + FW events
 **/

#ifndef _HAILO_PROFILER_UTILS_HPP_
#define _HAILO_PROFILER_UTILS_HPP_

#include "utils/hailort_logger.hpp"

#if defined(__linux__)
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#endif

namespace hailort
{

struct ProfilerTime {
    uint32_t year;
    uint32_t month;
    uint32_t day;
    uint32_t hour;
    uint32_t min;
    int64_t time_since_epoch;
};

#if defined(__linux__)
std::string os_name()
{
    struct utsname uts;
    if (uname(&uts) != 0) {
        LOGGER__ERROR("Failed to fetch os name.");
        return "";
    }
    return uts.sysname;
}

std::string os_ver()
{
    struct utsname uts;
    if (uname(&uts) != 0) {
        LOGGER__ERROR("Failed to fetch os ver.");
        return "";
    }
    return uts.version;
}

std::string cpu_arch()
{
    struct utsname uts;
    if (uname(&uts) != 0) {
        LOGGER__ERROR("Failed to fetch cpu architecture.");
        return "";
    }
    return uts.machine;
}

std::uint64_t system_ram_size()
{
    struct sysinfo sys_info;

    if (sysinfo(&sys_info) != 0) {
        LOGGER__ERROR("Failed to fetch system ram size.");
        return 1;
    }

    return sys_info.totalram;
}
#endif

ProfilerTime get_curr_time()
{
    ProfilerTime curr_time = {};
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    struct std::tm t_time = *std::localtime(&time);

    curr_time.day = t_time.tm_mday;
    // Months in std::tm are 0-based
    curr_time.month = t_time.tm_mon + 1;
    // Years since 1900
    curr_time.year = t_time.tm_year + 1900;
    curr_time.hour = t_time.tm_hour;
    curr_time.min = t_time.tm_min;
    curr_time.time_since_epoch = std::chrono::duration_cast<std::chrono::nanoseconds>
        (std::chrono::high_resolution_clock::now().time_since_epoch()).count();

    return curr_time;
}

std::string get_libhailort_version_representation()
{
    std::string result = "";
    hailo_version_t libhailort_version = {};
    auto status = hailo_get_library_version(&libhailort_version);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to fetch libhailort version");
        return result;
    }

    result = result + std::to_string(libhailort_version.major) + "." + std::to_string(libhailort_version.minor) + "." +
        std::to_string(libhailort_version.revision);
    return result;
}

}

#endif // _HAILO_PROFILER_UTILS_HPP_