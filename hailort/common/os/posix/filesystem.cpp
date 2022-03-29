/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file filesystem.cpp
 * @brief Filesystem wrapper for Linux
 **/

#include "common/filesystem.hpp"
#include "common/logger_macros.hpp"
#include "common/utils.hpp"

#include <errno.h>
#include <sys/stat.h>

namespace hailort
{

const char *Filesystem::SEPARATOR = "/";

Expected<Filesystem::DirWalker> Filesystem::DirWalker::create(const std::string &dir_path)
{
    DIR *dir = opendir(dir_path.c_str());
    CHECK(nullptr != dir, make_unexpected(HAILO_FILE_OPERATION_FAILURE),
        "Could not open directory \"{}\" with errno {}", dir_path, errno);
    return DirWalker(dir, dir_path);
}

Filesystem::DirWalker::DirWalker(DIR *dir, const std::string &dir_path) :
    m_dir(dir),
    m_path_string(dir_path)
{}

Filesystem::DirWalker::~DirWalker()
{
    if (nullptr != m_dir) {
        const auto result = closedir(m_dir);
        if (-1 == result) {
            LOGGER__ERROR("closedir on directory \"{}\" failed with errno {}", m_path_string.c_str(), errno);
        }
    }
}

Filesystem::DirWalker::DirWalker(DirWalker &&other) :
    m_dir(std::exchange(other.m_dir, nullptr)),
    m_path_string(other.m_path_string)
{}
        
dirent* Filesystem::DirWalker::next_file()
{
    return readdir(m_dir);
}

#if defined(__unix__)

Expected<std::vector<std::string>> Filesystem::get_files_in_dir_flat(const std::string &dir_path)
{
    const std::string dir_path_with_sep = has_suffix(dir_path, SEPARATOR) ? dir_path : dir_path + SEPARATOR;
    
    auto dir = DirWalker::create(dir_path_with_sep);
    CHECK_EXPECTED(dir);
    
    std::vector<std::string> files;
    struct dirent *entry = nullptr;
    while ((entry = dir->next_file()) != nullptr) {
        if (entry->d_type != DT_REG) {
            continue;
        }
        const std::string file_name = entry->d_name;
        files.emplace_back(dir_path_with_sep + file_name);
    }

    return files;
}
// QNX
#elif defined(__QNX__)
Expected<std::vector<std::string>> Filesystem::get_files_in_dir_flat(const std::string &dir_path)
{
    (void) dir_path;
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}
// Unsupported Platform
#else
static_assert(false, "Unsupported Platform!");
#endif

Expected<bool> Filesystem::is_directory(const std::string &path)
{
    struct stat path_stat{};
    CHECK(0 == stat(path.c_str(), &path_stat), make_unexpected(HAILO_FILE_OPERATION_FAILURE),
        "stat() on path \"{}\" failed. errno {}", path.c_str(), errno);

   return S_ISDIR(path_stat.st_mode);
}

} /* namespace hailort */
