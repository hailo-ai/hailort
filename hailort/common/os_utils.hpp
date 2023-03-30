/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file os_utils.hpp
 * @brief Utilities for OS methods
 **/

#ifndef _HAILO_OS_UTILS_HPP_
#define _HAILO_OS_UTILS_HPP_

#include "hailo/hailort.h"

#include "common/logger_macros.hpp"


namespace hailort
{

class HailoRTOSLogger final
{
public:
    static HailoRTOSLogger& get_instance()
    {
        static HailoRTOSLogger instance;
        return instance;
    }

    std::shared_ptr<spdlog::logger> logger()
    {
        return m_hailort_os_logger;
    }

private:
    HailoRTOSLogger();
    std::shared_ptr<spdlog::logger> m_hailort_os_logger;
};

class CursorAdjustment final
{
public:
    CursorAdjustment();
    ~CursorAdjustment();
private:
#if defined(_WIN32)
    unsigned int m_previous_output_buffer_mode;
#endif /* _WIN32 */
};

#define _HAILORT_OS_LOG(level, ...)  SPDLOG_LOGGER_CALL(hailort::HailoRTOSLogger::get_instance().logger(), level, __VA_ARGS__)
#define HAILORT_OS_LOG_INFO(...)  _HAILORT_OS_LOG(spdlog::level::info, __VA_ARGS__)
#define HAILORT_OS_LOG_WARNNING(...)  _HAILORT_OS_LOG(spdlog::level::warn, __VA_ARGS__)
#define HAILORT_OS_LOG_ERROR(...)  _HAILORT_OS_LOG(spdlog::level::err, __VA_ARGS__)

class OsUtils final
{
public:
    OsUtils() = delete;
    static uint32_t get_curr_pid();

    static void set_current_thread_name(const std::string &name)
    {
        (void)name;
#ifndef NDEBUG
#ifndef _WIN32
    // pthread_setname_np name size is limited to 16 chars (including null terminator)
    assert(name.size() < 16);
    pthread_setname_np(pthread_self(), name.c_str());
#else
// TODO: implement for windows
#endif /* _WIN32 */
#endif /* NDEBUG */
    }

};

} /* namespace hailort */

#endif /* _HAILO_OS_UTILS_HPP_ */
