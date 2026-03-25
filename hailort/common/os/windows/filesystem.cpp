/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file filesystem.cpp
 * @brief Filesystem wrapper std::filesystem POSIX implementation
 **/

#include "common/filesystem.hpp"

namespace hailort
{

Expected<TempFile> TempFile::create(const std::string &, const std::string &)
{
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

TempFile::TempFile(const char *file_path, const char *dir_path) :
    m_file_path(file_path), m_dir_path(dir_path)
{}

TempFile::~TempFile()
{
}

std::string TempFile::path() const
{
    return m_file_path;
}

std::string TempFile::dir() const
{
    return m_dir_path;
}

Expected<LockedFile> LockedFile::create(const std::string &, const std::string &)
{
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

int LockedFile::get_fd() const
{
    return -1;
}

const std::string &LockedFile::path() const
{
    return m_file_path;
}

hailo_status LockedFile::lock()
{
    return HAILO_NOT_IMPLEMENTED;
}

hailo_status LockedFile::try_lock_for(std::chrono::milliseconds)
{
    return HAILO_NOT_IMPLEMENTED;
}

hailo_status LockedFile::unlock()
{
    return HAILO_NOT_IMPLEMENTED;
}

} /* namespace hailort */
