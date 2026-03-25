/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file filesystem.hpp
 * @brief File system API
 **/

#ifndef _OS_FILESYSTEM_HPP_
#define _OS_FILESYSTEM_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"

#include <vector>
#include <string>
#include <chrono>
#include <unordered_map>
#include <mutex>
#include <memory>

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

// TODO: HRT-7304 - Add support for windows
class TempFile {
public:
    static Expected<TempFile> create(const std::string &file_name, const std::string &file_directory = "");
    ~TempFile();

    std::string path() const;
    std::string dir() const;

private:
    TempFile(const char *file_path, const char *dir_path);

    std::string m_file_path;
    std::string m_dir_path;
};

class LockedFile {
public:
    // The mode param is the string containing the file access mode, compatible with `fopen` function.
    static Expected<LockedFile> create(const std::string &file_path, const std::string &mode);
    virtual ~LockedFile();

    LockedFile(const LockedFile &other) = delete;
    LockedFile &operator=(const LockedFile &other) = delete;
    LockedFile &operator=(LockedFile &&other) noexcept = default;
    LockedFile(LockedFile &&other) noexcept = default;

    int get_fd() const;
    const std::string &path() const;
    hailo_status lock();
    hailo_status try_lock_for(std::chrono::milliseconds timeout);
    hailo_status unlock();

private:
    struct FcloseDeleter {
        void operator()(FILE *p) const noexcept { if (p) { (void)fclose(p); } }
    };

    LockedFile(std::unique_ptr<FILE, FcloseDeleter> &&fp, const std::string &file_path, std::shared_ptr<std::timed_mutex> mutex)
        : m_fp(std::move(fp)), m_file_path(file_path), m_mutex(mutex) {}

    std::unique_ptr<FILE, FcloseDeleter> m_fp;
    std::string m_file_path;

    // Stores mutex per file path - needed for thread safety
    static std::unordered_map<std::string, std::shared_ptr<std::timed_mutex>> m_shared_mutexes;
    static std::mutex m_map_mutex;
    std::shared_ptr<std::timed_mutex> m_mutex; // This comes from the static shared mutexes map - needed for thread safety
};

} /* namespace hailort */

#endif /* _OS_FILESYSTEM_HPP_ */
