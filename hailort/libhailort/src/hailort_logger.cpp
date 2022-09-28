/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailort_logger.cpp
 * @brief Implements logger used by hailort.
 **/

#include "hailort_logger.hpp"
#include "common/utils.hpp"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/android_sink.h>
#include <spdlog/sinks/null_sink.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#ifdef _WIN32
#include <io.h>
#include <AclAPI.h>
#endif

namespace hailort
{

#define MAX_LOG_FILE_SIZE (1024 * 1024) // 1MB

#define HAILORT_NAME "HailoRT"
#define HAILORT_LOGGER_FILENAME "hailort.log"
#define HAILORT_MAX_NUMBER_OF_LOG_FILES (1) // There will be 2 log files - 1 spare
#define HAILORT_CONSOLE_LOGGER_PATTERN "[%n] [%^%l%$] %v" // Console logger will print: [hailort logger file name] [log level] msg
#define HAILORT_FILE_LOGGER_PATTERN "[%Y-%m-%d %X.%e] [%n] [%l] [%s:%#] [%!] %v" //File logger will print: [timestamp] [hailort] [log level] [source file:line number] [function name] msg
#define HAILORT_ANDROID_LOGGER_PATTERN "%v"               // Android logger will print only message (additional info are built-in)

#define HAILORT_LOGGER_PATH "HAILORT_LOGGER_PATH"

#ifdef _WIN32
#define PATH_SEPARATOR "\\"

bool is_dir_accesible(const std::string &dir)
{
    // The code is based on examples from: https://cpp.hotexamples.com/examples/-/-/AccessCheck/cpp-accesscheck-function-examples.html
    bool return_val = false;
    SECURITY_INFORMATION security_Info = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION;
    PSECURITY_DESCRIPTOR security_desc = NULL;
    DWORD access_mask = GENERIC_WRITE;
    GENERIC_MAPPING mapping = {0xFFFFFFFF};
    mapping.GenericRead = FILE_GENERIC_READ;
    mapping.GenericWrite = FILE_GENERIC_WRITE;
    mapping.GenericExecute = FILE_GENERIC_EXECUTE;
    mapping.GenericAll = FILE_ALL_ACCESS;
    HANDLE h_token = NULL;
    HANDLE h_impersonated_token = NULL;
    PRIVILEGE_SET privilege_set = {0};
    DWORD privilege_set_size = sizeof(privilege_set);
    DWORD granted_access = 0;
    BOOL access_status = FALSE;

    // Retrieves a copy of the security descriptor for current dir.
    DWORD result = GetNamedSecurityInfo(dir.c_str(), SE_FILE_OBJECT, security_Info, NULL, NULL, NULL, NULL, &security_desc);
    if (result != ERROR_SUCCESS) {
        std::cerr << "Failed to get security information for local directory with error = " << result << std::endl;
        return_val = false;
        goto l_exit;
    }

    MapGenericMask(&access_mask, &mapping);
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &h_token) == 0) {
        return_val = false;
        std::cerr << "OpenProcessToken() Failed. Cannot check directory's access permissions, last_error = " << GetLastError() << std::endl;
        goto l_release_security_desc;
    }

    // Getting a handle to an impersonation token. It will represent the client that is attempting to gain access. 
    if (DuplicateToken(h_token, SecurityImpersonation, &h_impersonated_token) == 0) {
        std::cerr << "DuplicateToken() Failed. Cannot check directory's access permissions, last_error = " << GetLastError() << std::endl;
        return_val = false;
        goto l_close_token;
    }

    if (AccessCheck(security_desc, h_impersonated_token, access_mask, &mapping, &privilege_set, &privilege_set_size, &granted_access, &access_status) == 0) {
        std::cerr << "AccessCheck Failed. Cannot check directory's access permissions, last_error = " << GetLastError() << std::endl;
        return_val = false;
        goto l_close_impersonated_token;
    }

    return_val = (access_status == TRUE);

l_close_impersonated_token:
    if (NULL != h_impersonated_token) {
        (void)CloseHandle(h_impersonated_token);
    }

l_close_token:
    if (NULL != h_token) {
        (void)CloseHandle(h_token);
    }

l_release_security_desc:
    if (NULL != security_desc) {
	    (void)LocalFree(security_desc);
    }    
l_exit:
    return return_val;
}
#else
#define PATH_SEPARATOR "/"

bool is_dir_accesible(const std::string &dir)
{
    auto ret = access(dir.c_str(), W_OK);
    if (ret == 0) {
        return true;
    }
    else if (EACCES == errno) {
        return false;
    }
    else {
        std::cerr << "Failed checking directory's access permissions, errno = " << errno << std::endl;
        return false;
    }
}
#endif

std::shared_ptr<spdlog::sinks::sink> HailoRTLogger::create_file_sink()
{
    auto dir_env_var = std::getenv(HAILORT_LOGGER_PATH);
    std::string dir = ((nullptr == dir_env_var) || (0 == std::strlen(dir_env_var))) ? "." : dir_env_var; // defaulted to current directory
    if (dir == "NONE") {
        return make_shared_nothrow<spdlog::sinks::null_sink_st>();
    } else if (is_dir_accesible(dir)) {
        return make_shared_nothrow<spdlog::sinks::rotating_file_sink_mt>((dir + PATH_SEPARATOR + HAILORT_LOGGER_FILENAME), MAX_LOG_FILE_SIZE,
            HAILORT_MAX_NUMBER_OF_LOG_FILES);
    } else {
        std::cerr << "HailoRT warning: Cannot create HailoRT log file! Please check the current directory's write permissions." << std::endl;
        // Create null sink instead (Will throw away its log)
        return make_shared_nothrow<spdlog::sinks::null_sink_st>();
    }
}

HailoRTLogger::HailoRTLogger() :
    m_console_sink(make_shared_nothrow<spdlog::sinks::stderr_color_sink_mt>()),
#ifdef __ANDROID__
    m_file_sink(make_shared_nothrow<spdlog::sinks::android_sink_mt>(HAILORT_NAME))
#else
    m_file_sink(create_file_sink())
#endif
{

#ifdef __ANDROID__
    m_file_sink->set_pattern(HAILORT_ANDROID_LOGGER_PATTERN);
#else
    m_file_sink->set_pattern(HAILORT_FILE_LOGGER_PATTERN);
#endif

    // TODO: Handle null pointers for logger and sinks
    m_console_sink->set_pattern(HAILORT_CONSOLE_LOGGER_PATTERN);
    spdlog::sinks_init_list sink_list = { m_console_sink, m_file_sink };
    m_hailort_logger = make_shared_nothrow<spdlog::logger>(HAILORT_NAME, sink_list.begin(), sink_list.end());

#ifdef NDEBUG
    set_levels(spdlog::level::warn, spdlog::level::info, spdlog::level::warn);
#else
    set_levels(spdlog::level::warn, spdlog::level::debug, spdlog::level::debug);
#endif
    spdlog::set_default_logger(m_hailort_logger);
}

std::shared_ptr<spdlog::logger> HailoRTLogger::logger()
{
    return m_hailort_logger;
}

void HailoRTLogger::set_levels(spdlog::level::level_enum console_level,
    spdlog::level::level_enum file_level, spdlog::level::level_enum flush_level)
{
    m_console_sink->set_level(console_level);
    m_file_sink->set_level(file_level);
    m_hailort_logger->flush_on(flush_level);
}

} /* namespace hailort */
