/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file logger_macros.hpp
 * @brief Declares logger used by hailort.
 **/

#ifndef _HAILORT_LOGGER_HPP_
#define _HAILORT_LOGGER_HPP_


#include <string.h>
#include <stdint.h>
#include <ctype.h>

#include "hailo/hailort.h"
#include "common/logger_macros.hpp"
#include "common/utils.hpp"

namespace hailort
{

class HailoRTLogger {
public:
#ifdef NDEBUG
    static std::unique_ptr<HailoRTLogger> &get_instance(spdlog::level::level_enum console_level = spdlog::level::warn,
        spdlog::level::level_enum file_level = spdlog::level::info, spdlog::level::level_enum flush_level = spdlog::level::warn)
#else
    static std::unique_ptr<HailoRTLogger> &get_instance(spdlog::level::level_enum console_level = spdlog::level::warn,
        spdlog::level::level_enum file_level = spdlog::level::debug, spdlog::level::level_enum flush_level = spdlog::level::debug)
#endif
    {
        static std::unique_ptr<HailoRTLogger> instance = nullptr;
        if (nullptr == instance) {
            instance = make_unique_nothrow<HailoRTLogger>(console_level, file_level, flush_level);
        }
        return instance;
    }

    HailoRTLogger(spdlog::level::level_enum console_level, spdlog::level::level_enum file_level, spdlog::level::level_enum flush_level);
    ~HailoRTLogger() = default;
    HailoRTLogger(HailoRTLogger const&) = delete;
    void operator=(HailoRTLogger const&) = delete;

    static std::string get_log_path(const std::string &path_env_var);
    static bool should_flush_every_print(const std::string &flush_every_print_env_var);
    static std::string get_main_log_path();
    static std::shared_ptr<spdlog::sinks::sink> create_file_sink(const std::string &dir_path, const std::string &filename, bool rotate);

private:
    static std::string parse_log_path(const char *log_path);
    void set_levels(spdlog::level::level_enum console_level, spdlog::level::level_enum file_level, spdlog::level::level_enum flush_level);

    std::shared_ptr<spdlog::sinks::sink> m_console_sink;

    // The main log will written to a centralized directory (home directory)
    // The local log will be written to the local directory or to the path the user has chosen (via $HAILORT_LOGGER_PATH)
    std::shared_ptr<spdlog::sinks::sink> m_main_log_file_sink;
    std::shared_ptr<spdlog::sinks::sink> m_local_log_file_sink;
    std::shared_ptr<spdlog::logger> m_hailort_logger;
};


} /* namespace hailort */

#endif /* _HAILORT_LOGGER_HPP_ */
