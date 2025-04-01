/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file file_descriptor.cpp
 * @brief Wrapper around system file descriptors for Unix
 **/


#include "common/logger_macros.hpp"
#include "common/file_descriptor.hpp"
#include <errno.h>

namespace hailort
{

#define INVALID_FD (-1)


FileDescriptor::FileDescriptor(underlying_handle_t fd) : m_fd(fd)
{}

FileDescriptor::~FileDescriptor()
{
    if (m_fd != INVALID_FD) {
        if (0 != close(m_fd)) {
            LOGGER__ERROR("Failed to close fd. errno={}", errno);
        }
    }
}

FileDescriptor::FileDescriptor(FileDescriptor &&other) noexcept : m_fd(std::exchange(other.m_fd, INVALID_FD))
{}

Expected<FileDescriptor> FileDescriptor::duplicate()
{
    auto new_fd = FileDescriptor(dup(m_fd));
    if (INVALID_FD == new_fd) {
        LOGGER__ERROR("Failed duplicating fd. errno={}", errno);
        return make_unexpected(HAILO_OPEN_FILE_FAILURE);
    }

    return new_fd;
}

} /* namespace hailort */
