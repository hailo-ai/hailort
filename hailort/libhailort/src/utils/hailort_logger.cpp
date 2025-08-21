/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailort_logger.cpp
 * @brief Implements logger used by hailort.
 **/

#include "common/utils.hpp"
#include "common/filesystem.hpp"
#include "common/internal_env_vars.hpp"
#include "common/env_vars.hpp"

#include "utils/hailort_logger.hpp"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/android_sink.h>
#include <spdlog/sinks/null_sink.h>
#ifdef __unix__
#include <spdlog/sinks/syslog_sink.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <iomanip>
#ifdef _WIN32
#include <shlwapi.h>
#include <shlobj.h>
#endif


namespace hailort
{


#define MAX_LOG_FILE_SIZE (1024 * 1024) // 1MB

#define HAILORT_NAME ("HailoRT")
#define HAILORT_LOGGER_FILENAME ("hailort.log")
#define HAILORT_MAX_NUMBER_OF_LOG_FILES (1) // There will be 2 log files - 1 spare
#ifdef NDEBUG
#define HAILORT_CONSOLE_LOGGER_PATTERN ("[%n] [%^%l%$] %v") // Console logger will print: [hailort] [log level] msg
#else
#define HAILORT_CONSOLE_LOGGER_PATTERN ("[%Y-%m-%d %X.%e] [%P] [%t] [%n] [%^%l%$] [%s:%#] [%!] %v") // Console logger will print: [timestamp] [PID] [TID] [hailort] [log level] [source file:line number] [function name] msg
#endif
#define HAILORT_MAIN_FILE_LOGGER_PATTERN ("[%Y-%m-%d %X.%e] [%P] [%t] [%n] [%l] [%s:%#] [%!] %v") // File logger will print: [timestamp] [PID] [TID] [hailort] [log level] [source file:line number] [function name] msg
#define HAILORT_LOCAL_FILE_LOGGER_PATTERN ("[%Y-%m-%d %X.%e] [%t] [%n] [%l] [%s:%#] [%!] %v") // File logger will print: [timestamp] [TID] [hailort] [log level] [source file:line number] [function name] msg
#define HAILORT_ANDROID_LOGGER_PATTERN ("%v")               // Android logger will print only message (additional info are built-in)

#define PERIODIC_FLUSH_INTERVAL_IN_SECONDS (5)


std::string HailoRTLogger::parse_log_path(const char *log_path)
{
    if ((nullptr == log_path) || (std::strlen(log_path) == 0)) {
        return ".";
    }

    std::string log_path_str(log_path);
    if (log_path_str == "NONE") {
        return "";
    }

    return log_path_str;
}

std::string HailoRTLogger::get_log_path(const std::string &path_env_var)
{
    auto log_path_c_str_exp = get_env_variable(path_env_var.c_str());
    std::string log_path_c_str = (log_path_c_str_exp) ? log_path_c_str_exp.value() : "";
    return parse_log_path(log_path_c_str.c_str());
}

std::string HailoRTLogger::get_main_log_path()
{
    std::string local_log_path = get_log_path(HAILORT_LOGGER_PATH_ENV_VAR);
    if (local_log_path.length() == 0) {
        return "";
    }

#ifdef _WIN32
    // See https://stackoverflow.com/questions/2899013/how-do-i-get-the-application-data-path-in-windows-using-c
    TCHAR local_app_data_path[MAX_PATH];
    auto result = SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, local_app_data_path);
    if (!SUCCEEDED(result)) {
        std::cerr << "Cannot resolve Local Application Data directory path" << std::endl;
        return "";
    }
    
    const auto hailo_dir_path = std::string(local_app_data_path) + PATH_SEPARATOR + "Hailo";
    const auto full_path = hailo_dir_path + PATH_SEPARATOR + "HailoRT";
#else
    const auto hailo_dir_path = Filesystem::get_home_directory() + PATH_SEPARATOR + ".hailo";
    const auto full_path = hailo_dir_path + PATH_SEPARATOR + "hailort";
#endif

    auto status = Filesystem::create_directory(hailo_dir_path);
    if (HAILO_SUCCESS != status) {
        std::cerr << "Cannot create directory at path " << hailo_dir_path << std::endl;
        return "";
    }

    status = Filesystem::create_directory(full_path);
    if (HAILO_SUCCESS != status) {
        std::cerr << "Cannot create directory at path " << full_path << std::endl;
        return "";
    }

    return full_path;
}

std::shared_ptr<spdlog::sinks::sink> HailoRTLogger::create_file_sink(const std::string &dir_path, const std::string &filename, bool rotate)
{
    if ("" == dir_path) {
        return make_shared_nothrow<spdlog::sinks::null_sink_st>();
    }

    auto is_dir = Filesystem::is_directory(dir_path);
    if (!is_dir) {
        std::cerr << "HailoRT warning: Cannot create log file " << filename << "! Path " << dir_path << " is not valid." << std::endl;
        return make_shared_nothrow<spdlog::sinks::null_sink_st>();
    }
    if (!is_dir.value()) {
        auto status = Filesystem::create_directory(dir_path);
        if (status != HAILO_SUCCESS) {
            std::cerr << "HailoRT warning: Cannot create log file " << filename << "! Path " << dir_path << " is not valid." << std::endl;
            return make_shared_nothrow<spdlog::sinks::null_sink_st>();
        }
    }

    if (!Filesystem::is_path_accesible(dir_path)) {
        std::cerr << "HailoRT warning: Cannot create log file " << filename << "! Please check the directory " << dir_path << " write permissions." << std::endl;
        return make_shared_nothrow<spdlog::sinks::null_sink_st>();
    }

    const auto file_path = dir_path + PATH_SEPARATOR + filename;
    if (Filesystem::does_file_exists(file_path) && !Filesystem::is_path_accesible(file_path)) {
        std::cerr << "HailoRT warning: Cannot create log file " << filename << "! Please check the file " << file_path << " write permissions." << std::endl;
        return make_shared_nothrow<spdlog::sinks::null_sink_st>();
    }

    if (rotate) {
        return make_shared_nothrow<spdlog::sinks::rotating_file_sink_mt>(file_path, MAX_LOG_FILE_SIZE, HAILORT_MAX_NUMBER_OF_LOG_FILES);
    }

    return make_shared_nothrow<spdlog::sinks::basic_file_sink_mt>(file_path);
}

HailoRTLogger::HailoRTLogger(spdlog::level::level_enum console_level, spdlog::level::level_enum file_level, spdlog::level::level_enum flush_level) :
    m_console_sink(make_shared_nothrow<spdlog::sinks::stderr_color_sink_mt>()),
#ifdef __ANDROID__
    m_main_log_file_sink(make_shared_nothrow<spdlog::sinks::android_sink_mt>(HAILORT_NAME)),
    m_local_log_file_sink(make_shared_nothrow<spdlog::sinks::null_sink_mt>())
#else
    m_main_log_file_sink(create_file_sink(get_main_log_path(), HAILORT_LOGGER_FILENAME, true)),
    m_local_log_file_sink(create_file_sink(get_log_path(HAILORT_LOGGER_PATH_ENV_VAR), HAILORT_LOGGER_FILENAME, true))
#endif
{
    if ((nullptr == m_console_sink) || (nullptr == m_main_log_file_sink) || (nullptr == m_local_log_file_sink)) {
        std::cerr << "Allocating memory on heap for logger sinks has failed! Please check if this host has enough memory. Writing to log will result in a SEGFAULT!" << std::endl;
        return;
    }

#ifdef __ANDROID__
    m_main_log_file_sink->set_pattern(HAILORT_ANDROID_LOGGER_PATTERN);
#else
    m_main_log_file_sink->set_pattern(HAILORT_MAIN_FILE_LOGGER_PATTERN);
    m_local_log_file_sink->set_pattern(HAILORT_LOCAL_FILE_LOGGER_PATTERN);
#endif

    m_console_sink->set_pattern(HAILORT_CONSOLE_LOGGER_PATTERN);
    std::vector<std::shared_ptr<spdlog::sinks::sink>> sink_vector = { m_console_sink, m_main_log_file_sink, m_local_log_file_sink };

#ifdef __unix__
    m_should_print_to_syslog = is_env_variable_on(HAILORT_LOGGER_PRINT_TO_SYSLOG_ENV_VAR, HAILORT_LOGGER_PRINT_TO_SYSLOG_ENV_VAR_VALUE);
    if (m_should_print_to_syslog) {
        m_syslog_sink = make_shared_nothrow<spdlog::sinks::syslog_sink_mt>("HailoRT", 0, LOG_USER, true);
        m_syslog_sink->set_pattern(HAILORT_SYSLOG_LOGGER_PATTERN);
        sink_vector.push_back(m_syslog_sink);
    }
#endif

    m_hailort_logger = make_shared_nothrow<spdlog::logger>(HAILORT_NAME, sink_vector.begin(), sink_vector.end());
    if (nullptr == m_hailort_logger) {
        std::cerr << "Allocating memory on heap for HailoRT logger has failed! Please check if this host has enough memory. Writing to log will result in a SEGFAULT!" << std::endl;
        return;
    }

    set_levels(console_level, file_level, flush_level);
    spdlog::set_default_logger(m_hailort_logger);
}

void HailoRTLogger::set_levels(spdlog::level::level_enum console_level, spdlog::level::level_enum file_level,
    spdlog::level::level_enum flush_level)
{
    m_console_sink->set_level(console_level);
    m_main_log_file_sink->set_level(file_level);
    m_local_log_file_sink->set_level(file_level);
    if (m_should_print_to_syslog) {
        auto is_env_var_set = get_env_variable(HAILORT_SYSLOG_LOGGER_LEVEL_ENV_VAR);
        if (is_env_var_set) {
            auto syslog_level = HailoRTLogger::get_console_logger_level_from_string(is_env_var_set.value());
            if (syslog_level) {
                m_syslog_sink->set_level(syslog_level.value());
            }
        } else {
            m_syslog_sink->set_level(file_level);
        }
    }

    if (is_env_variable_on(HAILORT_LOGGER_FLUSH_EVERY_PRINT_ENV_VAR)) {
        m_hailort_logger->flush_on(spdlog::level::trace);
        std::cerr << "HailoRT warning: Flushing log file on every print. May reduce HailoRT performance!" << std::endl;
    } else {
        m_hailort_logger->flush_on(flush_level);
    }

    // Setting loggr level to min active level, as traces will only show if the sink level is set to their level
    m_hailort_logger->set_level(static_cast<spdlog::level::level_enum>(SPDLOG_ACTIVE_LEVEL));
    spdlog::flush_every(std::chrono::seconds(PERIODIC_FLUSH_INTERVAL_IN_SECONDS));
}

} /* namespace hailort */
