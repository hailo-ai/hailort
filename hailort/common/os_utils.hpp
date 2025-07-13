/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file os_utils.hpp
 * @brief Utilities for OS methods
 **/

#ifndef _HAILO_OS_UTILS_HPP_
#define _HAILO_OS_UTILS_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/hailort_common.hpp"

#include "common/logger_macros.hpp"
#include "common/internal_env_vars.hpp"
#include "common/utils.hpp"

#include <sstream>


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
    static uint32_t get_curr_tid();
    static bool is_pid_alive(uint32_t pid);
    static void set_current_thread_name(const std::string &name);
    static hailo_status set_current_thread_affinity(uint8_t cpu_index);
    static size_t get_page_size();
    static int set_environment_variable(const std::string &name, const std::string &value);

    static size_t get_dma_able_alignment()
    {
        // Note: Following is a HACK to allow transfers aligned to 512 bytes for specific customers. Although DMA should
        // theoretically work as long as transfers are aligned to descriptor-page-size (512 by default), this
        // configuration is NOT TESTED and may cause unexpected behaviour in many cases.
        static size_t alignment = get_page_size();
        static std::once_flag flag;

        std::call_once(flag, [] () {
            if (auto alignment_str = get_env_variable(HAILO_CUSTOM_DMA_ALIGNMENT_ENV_VAR)) {
                std::stringstream ss(alignment_str.release());
                size_t temp_alignment;
                if (!(ss >> temp_alignment)) { // Check 'ss' contains a valid unsigned int.
                    LOGGER__WARNING("Failed to parse custom DMA alignment to size_t");
                    return;
                }
                if (temp_alignment < 64 || !is_powerof2(temp_alignment)) {
                    LOGGER__WARNING("Illegal custom alignment: {}. Must be a power of 2 greater than 64.");
                    return;
                }
                alignment = temp_alignment;
            }
        });

        return alignment;
    }
};

} /* namespace hailort */

#endif /* _HAILO_OS_UTILS_HPP_ */
