/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file buffer.hpp
 * @brief Buffer helper class
 **/

#ifndef _HAILO_BUFFER_HPP_
#define _HAILO_BUFFER_HPP_

#include "hailo/expected.hpp"

#include <memory>
#include <cstdint>
#include <type_traits>
#include <string>
#include <cstring>
#include <cassert>


/** hailort namespace */
namespace hailort
{

class BufferStorage;
class Buffer;
class MemoryView;

using BufferPtr = std::shared_ptr<Buffer>;
using BufferStoragePtr = std::shared_ptr<BufferStorage>;


/*! Buffer storage parameters. Analogical to hailo_buffer_parameters_t */
struct HAILORTAPI BufferStorageParams
{
public:

    static BufferStorageParams create_dma();
    static BufferStorageParams create_shared_memory(const std::string &shm_name, bool memory_owner = true);
    static BufferStorageParams open_shared_memory(const std::string &shm_name);
    // Defaults to heap params
    BufferStorageParams();

    hailo_buffer_flags_t flags;

    // params for shared_memory_buffer
    std::string shared_memory_name;
    bool memory_owner;
};

class HAILORTAPI Buffer final
{
public:
    // Based on https://en.cppreference.com/w/cpp/iterator/iterator
    class iterator: public std::iterator<std::input_iterator_tag,   // iterator_category
                                         uint8_t,                   // value_type
                                         uint8_t,                   // difference_type
                                         uint8_t*,                  // pointer
                                         uint8_t&>                  // reference
    {
    public:
        explicit iterator(pointer index = 0) : m_index(index) {}
        iterator& operator++() { m_index++; return *this; }
        iterator operator++(int) { iterator retval = *this; ++(*this); return retval; }
        bool operator==(iterator other) const { return m_index == other.m_index; }
        bool operator!=(iterator other) const { return !(*this == other); }
        reference operator*() const { return *m_index; }
    private:
        pointer m_index;
    };

    // Empty buffer (points to null, size is zero)
    Buffer();
    ~Buffer();

    Buffer(const Buffer& other) = delete;
    Buffer& operator=(const Buffer& other) = delete;

    // Moves the data pointed to by other into the newly created buffer; other is invalidated
    Buffer(Buffer&& other);

    /**
     * Create functions, may fail be due to out of memory
     */
    // Creates a buffer size bytes long, without setting the memory
    static Expected<Buffer> create(size_t size, const BufferStorageParams &params = {});
    // Creates a buffer size bytes long, setting the memory to default_value
    static Expected<Buffer> create(size_t size, uint8_t default_value, const BufferStorageParams &params = {});
    // Creates a copy of the data pointed to by src, size bytes long
    static Expected<Buffer> create(const uint8_t *src, size_t size, const BufferStorageParams &params = {});
    // Creates a new buffer with the contents of the initializer_list
    static Expected<Buffer> create(std::initializer_list<uint8_t> init, const BufferStorageParams &params = {});

    // Creates a buffer size bytes long, without setting the memory
    static Expected<BufferPtr> create_shared(size_t size, const BufferStorageParams &params = {});
    // Creates a buffer size bytes long, setting the memory to default_value
    static Expected<BufferPtr> create_shared(size_t size, uint8_t default_value, const BufferStorageParams &params = {});
    // Creates a copy of the data pointed to by src, size bytes long
    static Expected<BufferPtr> create_shared(const uint8_t *src, size_t size, const BufferStorageParams &params = {});

    // Moves the data pointed to by other into the lvalue:
    // * other is invalidated.
    // * The previous data pointed to by the lvalue is freed
    Buffer& operator=(Buffer&& other);

    Expected<Buffer> copy() const;

    // Byte-wise comparison
    bool operator==(const Buffer& rhs) const;
    bool operator!=(const Buffer& rhs) const;

    // Note: No bounds checking
    uint8_t& operator[](size_t pos);
    const uint8_t& operator[](size_t pos) const;

    // Iterator funcs
    iterator begin();
    iterator end();

    BufferStorage &storage();

    // Returns a pointer to the start of the buffer
    uint8_t* data() noexcept;
    const uint8_t* data() const noexcept;

    // Returns the size of the buffer
    size_t size() const noexcept;

    // Casts the buffer to a string of length size().
    // If there's a null char in the buffer, the string will terminate at the null char
    std::string to_string() const;

    // Stream operator overload
    friend std::ostream& operator<<(std::ostream&, const Buffer&);

    // Returns a pointer to the start of the buffer, cast to T*
    // Note: If this->size() is less than sizeof(T), then part of the data pointed to by the returned pointer
    //       will be outside of the buffer's bounds.
    template<typename T, std::enable_if_t<std::is_pod<T>::value, int> = 0>
    T* as_pointer() const
    {
        assert(m_size >= sizeof(T));
        return reinterpret_cast<T*>(m_data);
    }

    // Returns a copy of the data at the start of the buffer, cast to T
    // Note: If this->size() is less than sizeof(T), then the copy will hold data that isn't from the buffer!
    template<typename T, std::enable_if_t<std::is_pod<T>::value, int> = 0>
    T as_type() const
    {
        assert(m_size >= sizeof(T));
        return *(reinterpret_cast<const T*>(m_data));
    }

    // The following functions return a copy of the data at the start of the buffer, cast to uint16/32/64_t
    // Note: If this->size() is less than the size of the integer type, then the copy will hold data
    //       that isn't from the buffer!
    uint16_t as_uint16() const;
    uint32_t as_uint32() const;
    uint64_t as_uint64() const;

    // Returns a reference to the data at the start of the buffer, cast to T
    // Note: If this->size() is less than sizeof(T), then the reference will hold data that isn't from the buffer!
    template<typename T, std::enable_if_t<std::is_pod<T>::value, int> = 0>
    T& as_type()
    {
        assert(m_size >= sizeof(T));
        return reinterpret_cast<T&>(m_data[0]);
    }

    // The following functions return references of the data at the start of the buffer, cast to uint16/32/64_t
    // Note: If this->size() is less than the size of the integer type, then the copy will hold data
    //       that isn't from the buffer!
    uint16_t& as_uint16();
    uint32_t& as_uint32();
    uint64_t& as_uint64();

    // Returns a view of the current buffer starting at offset, till the end of the buffer
    // If offset is greater than the buffer size, an empty view is returned
    MemoryView from(size_t offset);
    const MemoryView from(size_t offset) const;

    // Returns a view of the current buffer starting at the beginning, till offset exclusive
    // If offset is greater than the buffer size, a view of the entire buffer is returned
    MemoryView to(size_t offset);
    const MemoryView to(size_t offset) const;

    // Returns a view of the current buffer starting at from, till to exclusive
    // If from is greater than the buffer size, an empty view is returned
    // If to is greater than the buffer size, a view from from to the end of the buffer is returned
    // If from is greater than to, an empty view is returned
    MemoryView slice(size_t from, size_t to);
    const MemoryView slice(size_t from, size_t to) const;

    // Returns the pointer managed by this object and releases ownership
    Expected<void *> release() noexcept;

    // Internal functions
    static Expected<Buffer> create(BufferStoragePtr storage, bool register_storage = true);
    static Expected<BufferPtr> create_shared(BufferStoragePtr storage, bool register_storage = true);

private:
    class StorageImpl;

    // Buffer backed by the storage param
    Buffer(std::unique_ptr<StorageImpl> storage);

    // Initialization dependency
    std::unique_ptr<StorageImpl> m_storage_impl;
    uint8_t *m_data;
    size_t m_size;
};

/**
 * Object that can refer to a contiguous sequence of bytes.
 * This object does not own the memory and therefore it assumes that the memory exists and valid.
 */
class HAILORTAPI MemoryView final
{
public:
    MemoryView() noexcept;
    explicit MemoryView(Buffer &buffer) noexcept;
    MemoryView(void *data, size_t size) noexcept;
    MemoryView(const std::string &data) noexcept;
    ~MemoryView() = default;

    MemoryView& operator=(MemoryView&& other) noexcept = default;
    MemoryView(const MemoryView &) noexcept = default;
    MemoryView& operator=(const MemoryView &) noexcept = default;
    MemoryView(MemoryView &&) noexcept = default;

    static const MemoryView create_const(const void *data, size_t size) noexcept;

    uint8_t* data() noexcept;
    const uint8_t* data() const noexcept;
    size_t size() const noexcept;
    bool empty() const noexcept;

    // Byte-wise comparison
    inline bool operator==(const MemoryView& rhs) const noexcept
    {
        if (m_size != rhs.m_size) {
            return false;
        }
        return (0 == std::memcmp(m_data, rhs.m_data, m_size));
    }

    inline bool operator!=(const MemoryView& rhs) const noexcept
    {
        if (m_size != rhs.m_size) {
            return true;
        }
        return (0 != std::memcmp(m_data, rhs.m_data, m_size));
    }

    // Stream operator overload
    friend std::ostream& operator<<(std::ostream&, const MemoryView&);

    // Returns a pointer to the start of the buffer, cast to T*
    // Note: If this->size() is less than sizeof(T), then part of the data pointed to by the returned pointer
    //       will be outside of the buffer's bounds.
    template<typename T, std::enable_if_t<std::is_pod<T>::value, int> = 0>
    T* as_pointer() const
    {
        assert(m_size >= sizeof(T));
        return reinterpret_cast<T*>(m_data);
    }

private:
    void *m_data;
    size_t m_size;
};

} /* namespace hailort */

#endif /* _HAILO_BUFFER_HPP_ */
