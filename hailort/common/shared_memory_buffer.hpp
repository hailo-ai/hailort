/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file shared_memory_buffer.hpp
 * @brief Shared memory buffer
 **/

#ifndef _HAILO_SHARED_MEMORY_BUFFER_HPP_
#define _HAILO_SHARED_MEMORY_BUFFER_HPP_

#include "common/file_descriptor.hpp"
#include "common/mmap_buffer.hpp"

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/buffer.hpp"

namespace hailort
{

#define SHARED_MEMORY_NAME_SEPERATOR '_'
#define INVALID_SHARED_MEMORY_CHAR '/'

#if defined(_MSC_VER)
#define SHARED_MEMORY_NAME_PREFIX "Local\\"
#else
#define SHARED_MEMORY_NAME_PREFIX '/'
#endif

class SharedMemoryBuffer;
using SharedMemoryBufferPtr = std::shared_ptr<SharedMemoryBuffer>;

class SharedMemoryBuffer
{
public:
    static Expected<SharedMemoryBufferPtr> create(size_t size, const std::string &shm_name);
    static Expected<SharedMemoryBufferPtr> open(size_t size, const std::string &shm_name);

    SharedMemoryBuffer(const SharedMemoryBuffer &) = delete;
    SharedMemoryBuffer &operator=(SharedMemoryBuffer &&) = delete;
    SharedMemoryBuffer &operator=(const SharedMemoryBuffer &) = delete;
    virtual ~SharedMemoryBuffer();

    SharedMemoryBuffer(const std::string &shm_name, MmapBuffer<void> &&shm_mmap_buffer, bool memory_owner) :
        m_shm_name(shm_name),
        m_shm_mmap_buffer(std::move(shm_mmap_buffer)),
        m_memory_owner(memory_owner)
    {}

    SharedMemoryBuffer(SharedMemoryBuffer&& other) noexcept :
        m_shm_name(std::exchange(other.m_shm_name, "")),
        m_shm_mmap_buffer(std::move(other.m_shm_mmap_buffer)),
        m_memory_owner(std::exchange(other.m_memory_owner, false))
    {}

    virtual size_t size() const;
    virtual void *user_address();
    std::string shm_name();

    static std::string get_valid_shm_name(const std::string &name)
    {
        std::string valid_shm_name = name;
        std::replace(valid_shm_name.begin(), valid_shm_name.end(), INVALID_SHARED_MEMORY_CHAR, SHARED_MEMORY_NAME_SEPERATOR);
        valid_shm_name = SHARED_MEMORY_NAME_PREFIX + valid_shm_name;
        return valid_shm_name;
    }

private:
    std::string m_shm_name;
    MmapBuffer<void> m_shm_mmap_buffer;
    bool m_memory_owner;
};

} /* namespace hailort */

#endif /* _HAILO_SHARED_MEMORY_BUFFER_HPP_ */
