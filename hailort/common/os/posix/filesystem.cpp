/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file filesystem.cpp
 * @brief Filesystem wrapper std::filesystem POSIX implementation
 **/

#include "common/filesystem.hpp"
#include "common/utils.hpp"

#include <sys/file.h>
#include <sys/stat.h>

namespace hailort
{

const std::string UNIQUE_TMP_FILE_SUFFIX = "XXXXXX\0";

Expected<TempFile> TempFile::create(const std::string &file_name, const std::string &file_directory)
{
    if (!file_directory.empty()) {
        auto status = Filesystem::create_directory(file_directory);
        CHECK_SUCCESS(status);
    }

    std::string file_path = file_directory + file_name + UNIQUE_TMP_FILE_SUFFIX;
    std::vector<char> fname(file_path.begin(), file_path.end());
    fname.push_back('\0');

    std::vector<char> dirname(file_directory.begin(), file_directory.end());
    dirname.push_back('\0');

    int fd = mkstemp(fname.data());
    CHECK((-1 != fd), HAILO_FILE_OPERATION_FAILURE, "Failed to create tmp file {}, with errno {}", file_path, errno);
    close(fd);

    return TempFile(fname.data(), dirname.data());

}

TempFile::TempFile(const char *file_path, const char *dir_path) :
    m_file_path(file_path), m_dir_path(dir_path)
{}

TempFile::~TempFile()
{
    // TODO: Guarantee file deletion upon unexpected program termination. HRT-19808
    std::remove(m_file_path.c_str());
}

std::string TempFile::path() const
{
    return m_file_path;
}

std::string TempFile::dir() const
{
    return m_dir_path;
}

std::unordered_map<std::string, std::shared_ptr<std::timed_mutex>> LockedFile::m_shared_mutexes;
std::mutex LockedFile::m_map_mutex;
Expected<LockedFile> LockedFile::create(const std::string &file_path, const std::string &mode)
{
    // Because of the default umask, open first with user permissions and then change to all users permissions
    int permissions = (S_IRUSR | S_IWUSR);

    auto fd = open(file_path.c_str(), O_RDWR | O_CREAT, permissions);
    if ((-1 == fd) && (EACCES == errno)) {
        // We reached a race here between two processes with DIFFERENT PERMISSIONS, trying to create a file with the SAME PATH:
        // The first (higher permissions) created the file, but wasn't able to fchmod it fast enough,
        // so the second process (lower permissions) failed to open the file. The easiest way to solve it is with a small sleep and a retry
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        fd = open(file_path.c_str(), O_RDWR | O_CREAT, permissions);
    }
    CHECK(fd != -1, HAILO_OPEN_FILE_FAILURE, "Failed opening file: {}, with errno: {}", file_path, errno);

    auto ret = fchmod(fd, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    // We allow EPERM because it means the user doesn't have permission to change the file permissions,
    // but we count on the file permissions to be correct so we proceed to open the file
    CHECK((ret != -1) || (EPERM == errno), HAILO_FILE_OPERATION_FAILURE, "Failed changing file permissions: {}, with errno: {}", file_path, errno);

    FILE *fp = fdopen(fd, mode.c_str());
    CHECK(fp != nullptr, HAILO_OPEN_FILE_FAILURE, "Failed opening file: {}, with errno: {}", file_path, errno);

    std::shared_ptr<std::timed_mutex> mutex = nullptr;
    {
        std::unique_lock<std::mutex> lock(m_map_mutex);
        if (contains(m_shared_mutexes, file_path)) {
            mutex = m_shared_mutexes[file_path];
        } else {
            mutex = make_shared_nothrow<std::timed_mutex>();
            CHECK_NOT_NULL(mutex, HAILO_OUT_OF_HOST_MEMORY);

            m_shared_mutexes[file_path] = mutex;
        }
    }

    return LockedFile(std::unique_ptr<FILE, FcloseDeleter>(fp), file_path, mutex);
}

hailo_status LockedFile::lock()
{
    // flock does not work between threads, only between processes, so we need to make it thread safe as well
    m_mutex->lock();
    auto defer_unlock = defer([&]() { m_mutex->unlock(); });

    int ret = flock(get_fd(), LOCK_EX);
    CHECK(0 == ret, HAILO_FILE_OPERATION_FAILURE, "Failed to lock file with errno: {}", errno);

    defer_unlock.release();
    return HAILO_SUCCESS;
}

hailo_status LockedFile::try_lock_for(std::chrono::milliseconds timeout)
{
    auto start_time = std::chrono::steady_clock::now();
    // flock does not work between threads, only between processes, so we need to make it thread safe as well
    bool is_locked = m_mutex->try_lock_for(timeout);
    if (!is_locked) {
        return HAILO_TIMEOUT;
    }
    auto defer_unlock = defer([&]() { m_mutex->unlock(); });

    auto time_passed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time);
    if (time_passed > timeout) {
        timeout = std::chrono::milliseconds(0);
    } else {
        timeout -= time_passed;
    }

    start_time = std::chrono::steady_clock::now();
    auto backoff = std::chrono::microseconds(1);

    // Try atleast once
    do {
        int ret = flock(get_fd(), LOCK_EX | LOCK_NB);
        if (0 == ret) {
            defer_unlock.release();
            return HAILO_SUCCESS;
        }
        CHECK((EWOULDBLOCK == errno) || (EAGAIN == errno) || (EINTR == errno),
            HAILO_FILE_OPERATION_FAILURE, "Failed to lock file with errno: {}", errno);

        std::this_thread::sleep_for(backoff);
        backoff = std::min(backoff * 2, std::chrono::microseconds(1000));
    } while (std::chrono::steady_clock::now() - start_time < timeout);

    return HAILO_TIMEOUT;
}

hailo_status LockedFile::unlock()
{
    hailo_status status = HAILO_SUCCESS;
    int ret = flock(get_fd(), LOCK_UN);
    if (ret != 0) {
        status = HAILO_FILE_OPERATION_FAILURE;
        LOGGER__ERROR("Failed to unlock file with errno: {}", errno);
    }

    m_mutex->unlock();
    return status;
}

LockedFile::~LockedFile()
{
    if (m_fp != nullptr) {
        auto status = unlock();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to unlock file {}, with status: {}", m_file_path, status);
        }
    }
}

int LockedFile::get_fd() const
{
    return fileno(m_fp.get());
}

const std::string &LockedFile::path() const
{
    return m_file_path;
}

} /* namespace hailort */
