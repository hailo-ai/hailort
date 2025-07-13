/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file filesystem.cpp
 * @brief Filesystem wrapper for Linux
 **/

#include "common/filesystem.hpp"
#include "common/logger_macros.hpp"
#include "common/utils.hpp"

#include <iostream>
#include <errno.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <pwd.h>
#include <fstream>

namespace hailort
{

const char *Filesystem::SEPARATOR = "/";
const std::string UNIQUE_TMP_FILE_SUFFIX = "XXXXXX\0";

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

#if defined(__linux__)

Expected<std::vector<std::string>> Filesystem::get_files_in_dir_flat(const std::string &dir_path)
{
    const std::string dir_path_with_sep = has_suffix(dir_path, SEPARATOR) ? dir_path : dir_path + SEPARATOR;
    
    TRY(auto dir, DirWalker::create(dir_path_with_sep));
    
    std::vector<std::string> files;
    struct dirent *entry = nullptr;
    while ((entry = dir.next_file()) != nullptr) {
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

Expected<time_t> Filesystem::get_file_modified_time(const std::string &file_path)
{
    struct stat attr;
    auto res = stat(file_path.c_str(), &attr);
    CHECK(0 == res, HAILO_INTERNAL_FAILURE, "stat() failed on file {}, with errno {}", file_path, errno);
    auto last_modification_time = attr.st_mtime;
    return last_modification_time;
}

Expected<size_t> Filesystem::get_file_size(const std::string &file_path)
{
    std::ifstream file(file_path, std::ios::binary | std::ios::ate);
    CHECK(file.is_open(), HAILO_FILE_OPERATION_FAILURE, "Failed to open file {} with errno {}", file_path, errno);

    return static_cast<size_t>(file.tellg());
}

#if defined(__linux__)

Expected<std::vector<std::string>> Filesystem::get_latest_files_in_dir_flat(const std::string &dir_path,
    std::chrono::milliseconds time_interval)
{
    std::time_t curr_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    const std::string dir_path_with_sep = has_suffix(dir_path, SEPARATOR) ? dir_path : dir_path + SEPARATOR;
    
    TRY(auto dir, DirWalker::create(dir_path_with_sep));
    
    std::vector<std::string> files;
    struct dirent *entry = nullptr;
    while ((entry = dir.next_file()) != nullptr) {
        if (entry->d_type != DT_REG) {
            continue;
        }

        const std::string file_path = dir_path_with_sep + std::string(entry->d_name);
        TRY(const auto file_modified_time, get_file_modified_time(file_path));

        auto time_diff_sec = std::difftime(curr_time, file_modified_time);
        auto time_diff_millisec = time_diff_sec * 1000;
        if (time_diff_millisec <= static_cast<double>(time_interval.count())) {
            files.emplace_back(file_path);
        }
    }

    return files;
}

#elif defined(__QNX__)
Expected<std::vector<std::string>> Filesystem::get_latest_files_in_dir_flat(const std::string &dir_path,
    std::chrono::milliseconds time_interval)
{
    // TODO: HRT-7643
    (void)dir_path;
    (void)time_interval;
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}
// Unsupported Platform
#else
static_assert(false, "Unsupported Platform!");
#endif // __linux__

Expected<bool> Filesystem::is_directory(const std::string &path)
{
    struct stat path_stat{};
    auto ret_Val = stat(path.c_str(), &path_stat);
    if (ret_Val != 0 && (errno == ENOENT)) {
        // Directory path does not exist
        return false;
    }
    CHECK(0 == ret_Val, make_unexpected(HAILO_FILE_OPERATION_FAILURE),
        "stat() on path \"{}\" failed. errno {}", path.c_str(), errno);

   return S_ISDIR(path_stat.st_mode);
}

hailo_status Filesystem::create_directory(const std::string &dir_path)
{
    auto ret_val = mkdir(dir_path.c_str(), S_IRWXU | S_IRWXG | S_IRWXO );
    CHECK((ret_val == 0) || (errno == EEXIST), HAILO_FILE_OPERATION_FAILURE, "Failed to create directory {}", dir_path);
    return HAILO_SUCCESS;
}

hailo_status Filesystem::remove_directory(const std::string &dir_path)
{
    auto ret_val = rmdir(dir_path.c_str());
    CHECK(0 == ret_val, HAILO_FILE_OPERATION_FAILURE, "Failed to remove directory {}", dir_path);
    return HAILO_SUCCESS;
}

Expected<std::string> Filesystem::get_current_dir()
{
    char cwd[PATH_MAX];
    auto ret_val = getcwd(cwd, sizeof(cwd));
    CHECK_AS_EXPECTED(nullptr != ret_val, HAILO_FILE_OPERATION_FAILURE, "Failed to get current directory path with errno {}", errno);

    return std::string(cwd);
}

std::string Filesystem::get_home_directory()
{
    const char *homedir = getenv("HOME");
    if (NULL == homedir) {
        homedir = getpwuid(getuid())->pw_dir;
    }

#ifdef __QNX__
    const std::string root_dir = "/";
    std::string homedir_str = std::string(homedir);
    if (homedir_str == root_dir) {
        return homedir_str + "home";
    }
#endif

    return homedir;
}

bool Filesystem::is_path_accesible(const std::string &path)
{
    auto ret = access(path.c_str(), W_OK);
    if (ret == 0) {
        return true;
    }
    else if (EACCES == errno) {
        return false;
    } else {
        std::cerr << "Failed checking path " << path << " access permissions, errno = " << errno << std::endl;
        return false;
    }
}

bool Filesystem::does_file_exists(const std::string &path)
{
    // From https://stackoverflow.com/a/12774387
    struct stat buffer;
    return (0 == stat(path.c_str(), &buffer));
}

Expected<std::string> Filesystem::get_temp_path()
{
    return std::string("/tmp/");
}

Expected<TempFile> TempFile::create(const std::string &file_name, const std::string &file_directory)
{
    if (!file_directory.empty()) {
        auto status = Filesystem::create_directory(file_directory);
        CHECK_SUCCESS_AS_EXPECTED(status);
    }

    std::string file_path = file_directory + file_name + UNIQUE_TMP_FILE_SUFFIX;
    std::vector<char> fname(file_path.begin(), file_path.end());
    fname.push_back('\0');

    std::vector<char> dirname(file_directory.begin(), file_directory.end());
    dirname.push_back('\0');

    int fd = mkstemp(fname.data());
    CHECK_AS_EXPECTED((-1 != fd), HAILO_FILE_OPERATION_FAILURE, "Failed to create tmp file {}, with errno {}", file_path, errno);
    close(fd);

    return TempFile(fname.data(), dirname.data());

}

TempFile::TempFile(const char *file_path, const char *dir_path) :
    m_file_path(file_path), m_dir_path(dir_path)
{}

TempFile::~TempFile()
{
    // TODO: Guarantee file deletion upon unexpected program termination. 
    std::remove(m_file_path.c_str());
}

std::string TempFile::name() const
{
    return m_file_path;
}


std::string TempFile::dir() const
{
    return m_dir_path;
}

Expected<LockedFile> LockedFile::create(const std::string &file_path, const std::string &mode)
{
    auto fp = fopen(file_path.c_str(), mode.c_str());
    CHECK_AS_EXPECTED((nullptr != fp), HAILO_OPEN_FILE_FAILURE, "Failed opening file: {}, with errno: {}", file_path, errno);

    int fd = fileno(fp);
    int done = flock(fd, LOCK_EX | LOCK_NB);
    if (-1 == done) {
        LOGGER__ERROR("Failed to flock file: {}, with errno: {}", file_path, errno);
        fclose(fp);
        return make_unexpected(HAILO_FILE_OPERATION_FAILURE);
    }

    return LockedFile(fp, fd);
}

LockedFile::LockedFile(FILE *fp, int fd) : m_fp(fp), m_fd(fd)
{}

LockedFile::~LockedFile()
{
    if (m_fp != nullptr) {
        // The lock is released when all descriptors are closed.
        // Since we use LOCK_EX, this is the only fd open and the lock will be release after d'tor.
        fclose(m_fp);
    }
}

LockedFile::LockedFile(LockedFile &&other) :
    m_fp(std::exchange(other.m_fp, nullptr)),
    m_fd(other.m_fd)
{}

int LockedFile::get_fd() const
{
    return m_fd;
}

} /* namespace hailort */
