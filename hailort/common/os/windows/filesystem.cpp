/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file filesystem.cpp
 * @brief Filesystem wrapper for Windows
 **/

#include "common/filesystem.hpp"
#include "common/logger_macros.hpp"
#include "common/utils.hpp"

#include <iostream>
#include <io.h>
#include <AclAPI.h>
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

Filesystem::FileInfo Filesystem::FindFile::get_cur_file_info() const
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
    
    TRY(auto dir, FindFile::create(dir_path_with_sep));
    
    std::vector<std::string> files;
    auto file_info = dir.get_cur_file_info();
    if (is_regular_or_readonly_file(file_info.attrs)) {
        files.emplace_back(file_info.path);
    }
    
    hailo_status status = HAILO_UNINITIALIZED;
    while (true) {
        status = dir.next_file();
        if (HAILO_INVALID_OPERATION == status) {
            // We're done
            break;
        }
        if (HAILO_SUCCESS != status) {
            // Best effort
            LOGGER__ERROR("next_file failed with status {}; skipping", status);
            continue;
        }

        file_info = dir.get_cur_file_info();
        if (is_regular_or_readonly_file(file_info.attrs)) {
            files.emplace_back(dir_path_with_sep + file_info.path);
        }
    }
    return files;
}


Expected<std::vector<std::string>> Filesystem::get_latest_files_in_dir_flat(const std::string &dir_path, std::chrono::milliseconds time_interval)
{
    // TODO: HRT-7304
    (void)dir_path;
    (void)time_interval;
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

Expected<time_t> Filesystem::get_file_modified_time(const std::string &file_path)
{
    // TODO: HRT-7304
    (void)file_path;
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

Expected<bool> Filesystem::is_directory(const std::string &path)
{
    if (path.length() > MAX_PATH) {
        LOGGER__ERROR("path is too long (MAX_PATH={}, received length={}", MAX_PATH, path.length());
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }
    return PathIsDirectoryA(path.c_str());
}

Expected<std::string> Filesystem::get_current_dir()
{
    char cwd[MAX_PATH];
    auto ret_val = GetCurrentDirectoryA(MAX_PATH, cwd);
    CHECK_AS_EXPECTED(0 != ret_val, HAILO_FILE_OPERATION_FAILURE, "Failed to get current directory path with error: {}", WSAGetLastError());

    return std::string(cwd);
}

hailo_status Filesystem::create_directory(const std::string &dir_path)
{
    auto ret_val = CreateDirectory(dir_path.c_str(), nullptr);
    CHECK((0 != ret_val) || (GetLastError() == ERROR_ALREADY_EXISTS), HAILO_FILE_OPERATION_FAILURE, "Failed to create directory {}", dir_path);
    return HAILO_SUCCESS;
}

hailo_status Filesystem::remove_directory(const std::string &dir_path)
{
    bool was_removed = RemoveDirectoryA(dir_path.c_str());
    CHECK(was_removed, HAILO_FILE_OPERATION_FAILURE, "Failed to remove directory {}", dir_path);
    return HAILO_SUCCESS;
}

bool Filesystem::is_path_accesible(const std::string &path)
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

    // Retrieves a copy of the security descriptor for the path
    DWORD result = GetNamedSecurityInfo(path.c_str(), SE_FILE_OBJECT, security_Info, NULL, NULL, NULL, NULL, &security_desc);
    if (result != ERROR_SUCCESS) {
        std::cerr << "Failed to get security information for path " << path << " with error = " << result << std::endl;
        return_val = false;
        goto l_exit;
    }

    MapGenericMask(&access_mask, &mapping);
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &h_token) == 0) {
        return_val = false;
        std::cerr << "OpenProcessToken() Failed. Cannot check path " << path << " access permissions, last_error = " << GetLastError() << std::endl;
        goto l_release_security_desc;
    }

    // Getting a handle to an impersonation token. It will represent the client that is attempting to gain access.
    if (DuplicateToken(h_token, SecurityImpersonation, &h_impersonated_token) == 0) {
        std::cerr << "DuplicateToken() Failed. Cannot check path " << path << " access permissions, last_error = " << GetLastError() << std::endl;
        return_val = false;
        goto l_close_token;
    }

    if (AccessCheck(security_desc, h_impersonated_token, access_mask, &mapping, &privilege_set, &privilege_set_size, &granted_access, &access_status) == 0) {
        std::cerr << "AccessCheck Failed. Cannot check path " << path << " access permissions, last_error = " << GetLastError() << std::endl;
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

bool Filesystem::does_file_exists(const std::string &path)
{
    // From https://stackoverflow.com/a/2112304
    return ((GetFileAttributes(path.c_str()) != INVALID_FILE_ATTRIBUTES) && (GetLastError() == ERROR_FILE_NOT_FOUND));
}

Expected<std::string> Filesystem::get_temp_path()
{
    char temp_path[MAX_PATH + 1];
    DWORD path_len = GetTempPathA(MAX_PATH + 1, temp_path);
    CHECK_AS_EXPECTED(path_len > 0 && path_len <= (MAX_PATH + 1), HAILO_FILE_OPERATION_FAILURE,
        "Failed to get temporary directory path");
    return std::string(temp_path);
}

} /* namespace hailort */
