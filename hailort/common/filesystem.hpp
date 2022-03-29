/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
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

#if defined(__GNUC__)
#include <dirent.h>
#endif

namespace hailort
{

class Filesystem final {
public:
    Filesystem() = delete;

    static Expected<std::vector<std::string>> get_files_in_dir_flat(const std::string &dir_path);
    static Expected<bool> is_directory(const std::string &path);
    static bool has_suffix(const std::string &file_name, const std::string &suffix)
    {
        return (file_name.size() >= suffix.size()) && equal(suffix.rbegin(), suffix.rend(), file_name.rbegin());    
    }

private:
    // OS-specific filesystem directory separator char (i.e. backslash on Windows or forward slash on UNIX)
    static const char *SEPARATOR;

    #if defined(_MSC_VER)

    struct FileInfo {
        std::string path;
        DWORD       attrs;
    };
    
    static bool is_regular_or_readonly_file(DWORD attrs);
    
    // TODO: supoport unicode
    class FindFile final {
    public:
        static Expected<FindFile> create(const std::string &dir_path);
        ~FindFile();
        FindFile(const FindFile &other) = delete;
        FindFile &operator=(const FindFile &other) = delete;
        FindFile &operator=(FindFile &&other) = delete;
        FindFile(FindFile &&other);
        
        Filesystem::FileInfo get_cur_file_info();
        // Will return HAILO_INVALID_OPERATION when the iteration is complete or HAILO_FILE_OPERATION_FAILURE upon failure
        hailo_status next_file();

    private:
        FindFile(HANDLE find_hadle, const WIN32_FIND_DATAA &find_data);

        HANDLE m_find_handle;
        WIN32_FIND_DATAA m_find_data;
    };
    
    #else
    
    class DirWalker final {
    public:
        static Expected<DirWalker> create(const std::string &dir_path);
        ~DirWalker();
        DirWalker(const DirWalker &other) = delete;
        DirWalker &operator=(const DirWalker &other) = delete;
        DirWalker &operator=(DirWalker &&other) = delete;
        DirWalker(DirWalker &&other);
        
        dirent* next_file();

    private:
        DirWalker(DIR *dir, const std::string &dir_path);

        DIR *m_dir;
        const std::string m_path_string;
    };
    
    #endif
};

} /* namespace hailort */

#endif /* _OS_FILESYSTEM_HPP_ */
