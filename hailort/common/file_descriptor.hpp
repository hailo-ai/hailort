/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file file_descriptor.hpp
 * @brief Wrapper around system file descriptors
 *
 * 
 **/

#ifndef _OS_FILE_DESCRIPTOR_H_
#define _OS_FILE_DESCRIPTOR_H_

#include "common/logger_macros.hpp"
#include "hailo/expected.hpp"

namespace hailort
{

class FileDescriptor
{
  public:
    FileDescriptor(underlying_handle_t fd);
    ~FileDescriptor();

    FileDescriptor(const FileDescriptor &other) = delete;
    FileDescriptor &operator=(const FileDescriptor &other) = delete;
    FileDescriptor(FileDescriptor &&other) noexcept;
    FileDescriptor &operator=(FileDescriptor &&other) noexcept 
    {
        std::swap(m_fd, other.m_fd);
        return *this;
    };

    operator underlying_handle_t() const
    {
        return m_fd;
    }

    Expected<FileDescriptor> duplicate();

  private:
    underlying_handle_t m_fd;
};

} /* namespace hailort */

#endif /* _OS_FILE_DESCRIPTOR_H_ */