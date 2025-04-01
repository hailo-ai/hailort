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

#ifdef HAILO_SUPPORT_MULTI_PROCESS
    TCHAR program_data_path[MAX_PATH];
    auto ret_val = SHGetFolderPath(NULL, CSIDL_COMMON_APPDATA, NULL, 0, program_data_path);
    if (!SUCCEEDED(ret_val)) {
        std::cerr << "Cannot resolve ProgramData directory path" << std::endl;
        return "";
    }

    const auto hailort_service_dir_path = std::string(program_data_path) + PATH_SEPARATOR + "HailoRT_Service";
    auto create_status = Filesystem::create_directory(hailort_service_dir_path);
    if (HAILO_SUCCESS != create_status) {
        std::cerr << "Cannot create directory at path " << hailort_service_dir_path << std::endl;
        return "";
    }

    const auto hailort_service_full_path = std::string(program_data_path) + PATH_SEPARATOR + "HailoRT_Service" + PATH_SEPARATOR + "logs";
    create_status = Filesystem::create_directory(hailort_service_full_path);
    if (HAILO_SUCCESS != create_status) {
        std::cerr << "Cannot create directory at path " << hailort_service_full_path << std::endl;
        return "";
    }
#endif

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
    spdlog::sinks_init_list sink_list = { m_console_sink, m_main_log_file_sink, m_local_log_file_sink };
    m_hailort_logger = make_shared_nothrow<spdlog::logger>(HAILORT_NAME, sink_list.begin(), sink_list.end());
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

    if (is_env_variable_on(HAILORT_LOGGER_FLUSH_EVERY_PRINT_ENV_VAR)) {
        m_hailort_logger->flush_on(spdlog::level::trace);
        std::cerr << "HailoRT warning: Flushing log file on every print. May reduce HailoRT performance!" << std::endl;
    } else {
        m_hailort_logger->flush_on(flush_level);
    }
    spdlog::flush_every(std::chrono::seconds(PERIODIC_FLUSH_INTERVAL_IN_SECONDS));
}

Expected<spdlog::level::level_enum> HailoRTLogger::get_console_logger_level_from_string(const std::string &user_console_logger_level)
{
    static const std::unordered_map<std::string, spdlog::level::level_enum> log_level_map = {
        {"info", spdlog::level::info},
        {"warning", spdlog::level::warn},
        {"error", spdlog::level::err},
        {"critical", spdlog::level::critical}
    };
    if(log_level_map.find(user_console_logger_level) != log_level_map.end()) {
        return Expected<spdlog::level::level_enum>(log_level_map.at(user_console_logger_level));
    }
    return make_unexpected(HAILO_INVALID_ARGUMENT);
}

} /* namespace hailort */
