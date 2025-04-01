/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file mmap_buffer.hpp
 * @brief RAII wrapper around memory mapping (mmap)
 *
 *
 **/

#ifndef _OS_MMAP_BUFFER_H_
#define _OS_MMAP_BUFFER_H_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "common/logger_macros.hpp"
#include "common/utils.hpp"
#include "common/file_descriptor.hpp"

namespace hailort
{

class MmapBufferImpl final {
public:
    static Expected<MmapBufferImpl> create_shared_memory(size_t length);
    static Expected<MmapBufferImpl> create_file_map(size_t length, FileDescriptor &file, uintptr_t offset);

#if defined(__QNX__)
    static Expected<MmapBufferImpl> create_file_map_nocache(size_t length, FileDescriptor &file, uintptr_t offset);
#endif /* defined(__QNX__) */

    MmapBufferImpl() : m_address(INVALID_ADDR), m_length(0), m_unmappable(false) {}

    ~MmapBufferImpl()
    {
        // On failure `unmap` already logs the error
        (void) unmap();
    }

    MmapBufferImpl(const MmapBufferImpl &other) = delete;
    MmapBufferImpl &operator=(const MmapBufferImpl &other) = delete;
    MmapBufferImpl(MmapBufferImpl &&other) noexcept :
        m_address(std::exchange(other.m_address, INVALID_ADDR)),
        m_length(std::move(other.m_length)) {};
    MmapBufferImpl &operator=(MmapBufferImpl &&other) noexcept
    {
        std::swap(m_address, other.m_address);
        std::swap(m_length, other.m_length);
        std::swap(m_unmappable, other.m_unmappable);
        return *this;
    };

    void *address() {
        return m_address;
    }

    size_t size() const { return m_length; }

    bool is_mapped() const
    {
        return (INVALID_ADDR != m_address);
    }

    hailo_status unmap();


private:
    MmapBufferImpl(void *address, size_t length, bool unmappable = false) :
        m_address(address), m_length(length), m_unmappable(unmappable) {}

    static void * const INVALID_ADDR;

    void *m_address;
    size_t m_length;
    bool m_unmappable;
};

template<typename T>
class MmapBuffer final
{
public:

    static Expected<MmapBuffer<T>> create_shared_memory(size_t length)
    {
        auto mmap = MmapBufferImpl::create_shared_memory(length);
        CHECK_EXPECTED(mmap);
        return MmapBuffer<T>(mmap.release());
    }

    static Expected<MmapBuffer<T>> create_file_map(size_t length, FileDescriptor &file, uintptr_t offset)
    {
        auto mmap = MmapBufferImpl::create_file_map(length, file, offset);
        CHECK_EXPECTED(mmap);
        return MmapBuffer<T>(mmap.release());
    }

#if defined(__QNX__)
    static Expected<MmapBuffer<T>> create_file_map_nocache(size_t length, FileDescriptor &file, uintptr_t offset)
    {
        auto mmap = MmapBufferImpl::create_file_map_nocache(length, file, offset);
        CHECK_EXPECTED(mmap);
        return MmapBuffer<T>(mmap.release());
    }
#endif /* defined(__QNX__) */

    MmapBuffer() = default;
    ~MmapBuffer() = default;

    MmapBuffer(const MmapBuffer<T> &other) = delete;
    MmapBuffer<T> &operator=(const MmapBuffer<T> &other) = delete;
    MmapBuffer(MmapBuffer<T> &&other) noexcept = default;
    MmapBuffer<T> &operator=(MmapBuffer<T> &&other) noexcept = default;

    T* operator->()
    {
        return address();
    }

    T* address() {
        return reinterpret_cast<T*>(m_mmap.address());
    }

    size_t size() const { return m_mmap.size(); }

    template<typename U=T>
    std::enable_if_t<!std::is_void<U>::value, U&> operator*()
    {
        return address()[0];
    }

    template<typename U=T>
    std::enable_if_t<!std::is_void<U>::value, U&> operator[](size_t i)
    {
        return address()[i];
    }

    bool is_mapped() const
    {
        return m_mmap.is_mapped();
    }

    // 'munmap' the current mapped buffer (if currently mapped).
    // It it safe to call it multiple times
    hailo_status unmap()
    {
        return m_mmap.unmap();
    }

private:

    MmapBuffer(MmapBufferImpl mmap) :
        m_mmap(std::move(mmap)) {}

    MmapBufferImpl m_mmap;
};

} /* namespace hailort */

#endif //_OS_MMAP_BUFFER_H_