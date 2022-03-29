/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
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

class CliCommon final
{
public:
    CliCommon() = delete;

    static std::string duration_to_string(std::chrono::seconds secs);
    static Expected<std::string> current_time_to_string();
    static void reset_cursor(size_t number_of_lines);
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

#endif /* _HAILO_HAILORTCLI_COMMON_HPP_ */