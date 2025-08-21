/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file logger_macros.hpp
 * @brief Declares logger macros used by hailort components.
 *        Assumes spdlog::set_default_logger was called (otherwise uses spdlog default logger)
 **/

#ifndef _LOGGER_MACROS_HPP_
#define _LOGGER_MACROS_HPP_

#include "hailo/hailort.h"

#define SPDLOG_NO_EXCEPTIONS

/* Minimum log level availble at compile time */
#ifndef SPDLOG_ACTIVE_LEVEL
#ifndef NDEBUG
#define SPDLOG_ACTIVE_LEVEL (SPDLOG_LEVEL_TRACE)
#else
#define SPDLOG_ACTIVE_LEVEL (SPDLOG_LEVEL_DEBUG)
#endif
#endif

#if defined(__linux__) && !defined(__ANDROID__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>
#if defined(__linux__) && !defined(__ANDROID__)
#pragma GCC diagnostic pop
#endif

inline std::ostream& operator<<(std::ostream& os, const hailo_status& status)
{
    auto status_str = hailo_get_status_message(status);
    if (status_str == nullptr) {
        return os << "<Invalid(" << static_cast<int>(status) << ")>";
    }
    return os << status_str << "(" << static_cast<int>(status) << ")";
}

template <> struct fmt::formatter<hailo_status> : fmt::ostream_formatter {};

namespace hailort
{

// Makes sure during compilation time that all strings in LOGGER__X macros are not in printf format, but in fmtlib format.
constexpr bool string_not_printf_format(char const * str) {
    int i = 0;

    while (str[i] != '\0') {
        if (str[i] == '%' && ((str[i+1] >= 'a' && str[i+1] <= 'z') || (str[i+1] >= 'A' && str[i+1] <= 'Z'))) {
            return false;
        }
        i++;
    }

    return true;
}

#define EXPAND(x) x
#define ASSERT_NOT_PRINTF_FORMAT(fmt, ...) static_assert(string_not_printf_format(fmt), "Error - Log string is in printf format and not in fmtlib format!")

#define LOGGER_TO_SPDLOG(level, ...)\
do{\
    EXPAND(ASSERT_NOT_PRINTF_FORMAT(__VA_ARGS__));\
    level(__VA_ARGS__);\
} while(0) // NOLINT: clang complains about this code never executing

#define LOGGER__TRACE(...)  LOGGER_TO_SPDLOG(SPDLOG_TRACE, __VA_ARGS__)
#define LOGGER__DEBUG(...)  LOGGER_TO_SPDLOG(SPDLOG_DEBUG, __VA_ARGS__)
#define LOGGER__INFO(...)  LOGGER_TO_SPDLOG(SPDLOG_INFO, __VA_ARGS__)
#define LOGGER__WARN(...)  LOGGER_TO_SPDLOG(SPDLOG_WARN, __VA_ARGS__)
#define LOGGER__WARNING  LOGGER__WARN
#define LOGGER__ERROR(...)  LOGGER_TO_SPDLOG(SPDLOG_ERROR, __VA_ARGS__)
#define LOGGER__CRITICAL(...)  LOGGER_TO_SPDLOG(SPDLOG_CRITICAL, __VA_ARGS__)

// special macros for GenAI stats collection
static inline size_t current_timestamp()
{
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(now).count();
}

#define LOGGER__GENAI_STATS_START(...)  LOGGER_TO_SPDLOG(SPDLOG_DEBUG, "[STATS][{}][START] " __VA_ARGS__, current_timestamp())
#define LOGGER__GENAI_STATS_END(...)  LOGGER_TO_SPDLOG(SPDLOG_DEBUG, "[STATS][{}][END] " __VA_ARGS__, current_timestamp())

} /* namespace hailort */

#endif /* _LOGGER_MACROS_HPP_ */