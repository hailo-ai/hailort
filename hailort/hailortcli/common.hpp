/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file common.hpp
 * @brief Common functions.
 **/

#ifndef _HAILO_HAILORTCLI_COMMON_HPP_
#define _HAILO_HAILORTCLI_COMMON_HPP_

#include "CLI/CLI.hpp"

#include "common/filesystem.hpp"

#include <chrono>
#include <sstream>

using namespace hailort;

// http://www.climagic.org/mirrors/VT100_Escape_Codes.html
#define FORMAT_CLEAR_LINE "\033[2K\r"
#define FORMAT_CURSOR_UP_LINE "\033[F"
#define FORMAT_CLEAR_TERMINAL_CURSOR_FIRST_LINE "\033[2J\033[1;1H"
#define FORMAT_RESET_TERMINAL_CURSOR_FIRST_LINE "\033[H\033[J"
#define FORMAT_ENTER_ALTERNATIVE_SCREEN "\033[?1049h"
#define FORMAT_EXIT_ALTERNATIVE_SCREEN "\033[?1049l"
#define FORMAT_GREEN_PRINT "\x1B[1;32m"
#define FORMAT_NORMAL_PRINT "\x1B[0m"

class CliCommon final
{
public:
    CliCommon() = delete;

    static std::string duration_to_string(std::chrono::seconds secs);
    static Expected<std::string> current_time_to_string();
    static void reset_cursor(size_t number_of_lines);
    static bool is_positive_number(const std::string &s);
    static bool is_non_negative_number(const std::string &s);
    static void clear_terminal();
};

// Validators
struct FileSuffixValidator : public CLI::Validator {
    FileSuffixValidator(const std::string &suffix) {
        name_ = "FILE_SUFFIX";
        func_ = [suffix](const std::string &filename) {
            if (!Filesystem::has_suffix(filename, suffix)) {
                std::stringstream error_message;
                error_message << "File '" << filename << "' does not end with suffix '"
                    << suffix << "'." << std::endl;
                return error_message.str();
            }
            // Success
            return std::string();
        };
    }
};

// This class is an RAII for running in alternative terminal
class AlternativeTerminal final
{
public:
    AlternativeTerminal();
    ~AlternativeTerminal();
};

// Based on NLOHMANN_JSON_SERIALIZE_ENUM (json/include/nlohmann/json.hpp)
// Accepts a static array instead of building one in the function
#define NLOHMANN_JSON_SERIALIZE_ENUM2(ENUM_TYPE, _pair_arr)\
    template<typename BasicJsonType>                                                            \
    inline void to_json(BasicJsonType& j, const ENUM_TYPE& e)                                   \
    {                                                                                           \
        static_assert(std::is_enum<ENUM_TYPE>::value, #ENUM_TYPE " must be an enum!");          \
        auto it = std::find_if(std::begin(_pair_arr), std::end(_pair_arr),                      \
                               [e](const std::pair<ENUM_TYPE, BasicJsonType>& ej_pair) -> bool  \
        {                                                                                       \
            return ej_pair.first == e;                                                          \
        });                                                                                     \
        j = ((it != std::end(_pair_arr)) ? it : std::begin(_pair_arr))->second;                 \
    }                                                                                           \
    template<typename BasicJsonType>                                                            \
    inline void from_json(const BasicJsonType& j, ENUM_TYPE& e)                                 \
    {                                                                                           \
        static_assert(std::is_enum<ENUM_TYPE>::value, #ENUM_TYPE " must be an enum!");          \
        auto it = std::find_if(std::begin(_pair_arr), std::end(_pair_arr),                      \
                               [&j](const std::pair<ENUM_TYPE, BasicJsonType>& ej_pair) -> bool \
        {                                                                                       \
            return ej_pair.second == j;                                                         \
        });                                                                                     \
        e = ((it != std::end(_pair_arr)) ? it : std::begin(_pair_arr))->first;                  \
    }

#endif /* _HAILO_HAILORTCLI_COMMON_HPP_ */
