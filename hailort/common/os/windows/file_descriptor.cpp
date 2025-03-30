/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file file_descriptor.cpp
 * @brief Wrapper around system file descriptors for Windows
 **/


#include "common/logger_macros.hpp"
#include "common/file_descriptor.hpp"

namespace hailort
{

FileDescriptor::FileDescriptor(underlying_handle_t fd) : m_fd(fd)
{}

FileDescriptor::~FileDescriptor()
{
    // NOTE: This code never tested
    if (INVALID_HANDLE_VALUE != m_fd && m_fd) {
        auto close_success = CloseHandle(m_fd);
        if (0 == close_success) {
            LOGGER__ERROR("Failed to close handle. last_error={}", GetLastError());
        }
    }
}

FileDescriptor::FileDescriptor(FileDescriptor &&other) noexcept : m_fd(std::exchange(other.m_fd, INVALID_HANDLE_VALUE))
{}


Expected<FileDescriptor> FileDescriptor::duplicate()
{
    // NOTE: the DuplicateHadndle() can't be just used to share the file handle
    // between different processes. All the user-mode pointers got as a result
    // of mmap operations become invalid
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

} /* namespace hailort */
