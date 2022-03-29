/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file filesystem.cpp
 * @brief Filesystem wrapper for Windows
 **/

#include "common/filesystem.hpp"
#include "common/logger_macros.hpp"
#include "common/utils.hpp"

#include <shlwapi.h>

namespace hailort
{

const char *Filesystem::SEPARATOR = "\\";

Expected<Filesystem::FindFile> Filesystem::FindFile::create(const std::string &dir_path)
{
    static const char *DIR_WALK_WILD_CARD = "*";
    const std::string dir_path_with_sep = has_suffix(dir_path, SEPARATOR) ? dir_path + DIR_WALK_WILD_CARD :
                                                                            dir_path + SEPARATOR + DIR_WALK_WILD_CARD;

    WIN32_FIND_DATAA find_data{};
    auto find_handle = FindFirstFileA(dir_path_with_sep.c_str(), &find_data);
    if (INVALID_HANDLE_VALUE == find_handle) {
        const auto last_error = GetLastError();
        if (last_error == ERROR_FILE_NOT_FOUND) {
            LOGGER__ERROR("No matching files could be found \"{}\"", dir_path_with_sep.c_str());
        } else {
            LOGGER__ERROR("FindFirstFileA(\"{}\") failed with LE={}", dir_path_with_sep.c_str(), last_error);
        }
        return make_unexpected(HAILO_FILE_OPERATION_FAILURE);
    }

    // Note: find_data will be copied into the m_find_data member (it doesn't contain pointers)
    return std::move(FindFile(find_handle, find_data));
}

Filesystem::FindFile::FindFile(HANDLE find_hadle, const WIN32_FIND_DATAA &find_data) :
    m_find_handle(find_hadle),
    m_find_data(find_data)
{}

Filesystem::FindFile::~FindFile()
{
    if (INVALID_HANDLE_VALUE != m_find_handle) {
        const auto result = FindClose(m_find_handle);
        if (0 == result) {
            LOGGER__ERROR("FindClose on handle={} failed with LE={}", m_find_handle, GetLastError());
        }
    }
}

Filesystem::FindFile::FindFile(FindFile &&other) :
    m_find_handle(std::exchange(other.m_find_handle, INVALID_HANDLE_VALUE)),
    m_find_data(other.m_find_data)
{}

Filesystem::FileInfo Filesystem::FindFile::get_cur_file_info()
{
    return {m_find_data.cFileName, m_find_data.dwFileAttributes};
}

hailo_status Filesystem::FindFile::next_file()
{
    const auto result = FindNextFileA(m_find_handle, &m_find_data);
    if (result) {
        return HAILO_SUCCESS;
    }
    const auto last_error = GetLastError();
    if (last_error == ERROR_NO_MORE_FILES) {
        // No more files
        return HAILO_INVALID_OPERATION;
    }
    
    LOGGER__ERROR("FindNextFileA() failed with LE={}", last_error);
    return HAILO_FILE_OPERATION_FAILURE;
}

bool Filesystem::is_regular_or_readonly_file(DWORD attrs)
{
    return (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

Expected<std::vector<std::string>> Filesystem::get_files_in_dir_flat(const std::string &dir_path)
{
    const std::string dir_path_with_sep = has_suffix(dir_path, SEPARATOR) ? dir_path : dir_path + SEPARATOR;
    
    auto dir = FindFile::create(dir_path_with_sep);
    CHECK_EXPECTED(dir);
    
    std::vector<std::string> files;
    auto file_info = dir->get_cur_file_info();
    if (is_regular_or_readonly_file(file_info.attrs)) {
        files.emplace_back(file_info.path);
    }
    
    hailo_status status = HAILO_UNINITIALIZED;
    while (true) {
        status = dir->next_file();
        if (HAILO_INVALID_OPERATION == status) {
            // We're done
            break;
        }
        if (HAILO_SUCCESS != status) {
            // Best effort
            LOGGER__ERROR("next_file failed with status {}; skipping", status);
            continue;
        }

        file_info = dir->get_cur_file_info();
        if (is_regular_or_readonly_file(file_info.attrs)) {
            files.emplace_back(dir_path_with_sep + file_info.path);
        }
    }
    return files;
}

Expected<bool> Filesystem::is_directory(const std::string &path)
{
    if (path.length() > MAX_PATH) {
        LOGGER__ERROR("path is too long (MAX_PATH={}, received length={}", MAX_PATH, path.length());
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }
    return PathIsDirectoryA(path.c_str());
}

} /* namespace hailort */
