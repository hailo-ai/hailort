/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file filesystem.hpp
 * @brief File system API
 **/

#ifndef _OS_FILESYSTEM_HPP_
#define _OS_FILESYSTEM_HPP_

#include "hailo/hailort.h"
#include "hailo/platform.h"

#include "hailo/expected.hpp"
#include <vector>
#include <string>
#include <chrono>

#if defined(__GNUC__)
#include <dirent.h>
#endif

#if defined(_MSC_VER)
#include <minwinbase.h>
#endif

namespace hailort
{

class Filesystem final {
public:
    Filesystem() = delete;

    static Expected<std::vector<std::string>> get_files_in_dir_flat(const std::string &dir_path);
    static Expected<std::vector<std::string>> get_latest_files_in_dir_flat(const std::string &dir_path, std::chrono::milliseconds time_interval);
    static Expected<bool> is_directory(const std::string &path);
    static hailo_status create_directory(const std::string &dir_path);
    static hailo_status remove_directory(const std::string &dir_path);
    static Expected<std::string> get_current_dir();
    static bool does_file_exists(const std::string &path);

    /**
     * Gets the path to the temporary directory.
     *
     * @return Upon success, returns Expected of the temporary directory path string, ending with / on posix systems
     * or with \ on windows systems. Otherwise, returns Unexpected of ::hailo_status error.
     */
    static Expected<std::string> get_temp_path();

    static bool has_suffix(const std::string &file_name, const std::string &suffix)
    {
        return (file_name.size() >= suffix.size()) && equal(suffix.rbegin(), suffix.rend(), file_name.rbegin());
    }

    static std::string remove_suffix(const std::string &file_name, const std::string &suffix)
    {
        if (!has_suffix(file_name, suffix)) {
            return file_name;
        }

        return file_name.substr(0, file_name.length() - suffix.length());
    }

    // Emulates https://docs.python.org/3/library/os.path.html#os.path.basename
    static std::string basename(const std::string &file_name);
};

} /* namespace hailort */

#endif /* _OS_FILESYSTEM_HPP_ */
