/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file buffer.cpp
 * @brief TODO: brief
 *
 * TODO: doc
 **/

#include "hailo/buffer.hpp"
#include "utils/buffer_storage.hpp"
#include "utils/exported_resource_manager.hpp"
#include "common/logger_macros.hpp"
#include "common/utils.hpp"
#include "common/utils.hpp"

#include <algorithm>
#include <string>
#include <cstring>
#include <iostream>
#include <iomanip>

namespace hailort
{

class Buffer::StorageImpl final {
public:
    StorageImpl(BufferStoragePtr storage) :
        m_storage(std::move(storage))
    {}

    BufferStoragePtr m_storage;
};

Buffer::Buffer() :
    m_storage_impl(),
    m_data(nullptr),
    m_size(0)
{}

// Declare on c++ file since StorageImpl definition is needed.
Buffer::~Buffer() = default;

Buffer::Buffer(std::unique_ptr<StorageImpl> storage) :
    m_storage_impl(std::move(storage)),
    m_data(static_cast<uint8_t *>(m_storage_impl->m_storage->user_address())),
    m_size(m_storage_impl->m_storage->size())
{}

Buffer::Buffer(Buffer&& other) :
    m_storage_impl(std::move(other.m_storage_impl)),
    m_data(std::exchange(other.m_data, nullptr)),
    m_size(std::exchange(other.m_size, 0))
{}

Expected<Buffer> Buffer::create(size_t size, const BufferStorageParams &params)
{
    if (0 == size) {
        return Buffer();
    }
    auto storage = BufferStorage::create(size, params);
    CHECK_EXPECTED(storage);

    return create(storage.release());
}

Expected<Buffer> Buffer::create(size_t size, uint8_t default_value, const BufferStorageParams &params)
{
    auto buffer = create(size, params);
    CHECK_EXPECTED(buffer);
    std::memset(static_cast<void*>(buffer->m_data), default_value, size);
    return buffer;
}

Expected<BufferPtr> Buffer::create_shared(size_t size, const BufferStorageParams &params)
{
    auto buffer = Buffer::create(size, params);
    CHECK_EXPECTED(buffer);
    auto buffer_ptr = make_shared_nothrow<Buffer>(buffer.release());
    CHECK_NOT_NULL_AS_EXPECTED(buffer_ptr, HAILO_OUT_OF_HOST_MEMORY);
    return buffer_ptr;
}

Expected<BufferPtr> Buffer::create_shared(size_t size, uint8_t default_value, const BufferStorageParams &params)
{
    auto buffer = Buffer::create(size, default_value, params);
    CHECK_EXPECTED(buffer);
    auto buffer_ptr = make_shared_nothrow<Buffer>(buffer.release());
    CHECK_NOT_NULL_AS_EXPECTED(buffer_ptr, HAILO_OUT_OF_HOST_MEMORY);
    return buffer_ptr;
}

Expected<BufferPtr> Buffer::create_shared(const uint8_t *src, size_t size, const BufferStorageParams &params)
{
    auto buffer = Buffer::create(src, size, params);
    CHECK_EXPECTED(buffer);
    auto buffer_ptr = make_shared_nothrow<Buffer>(buffer.release());
    CHECK_NOT_NULL_AS_EXPECTED(buffer_ptr, HAILO_OUT_OF_HOST_MEMORY);
    return buffer_ptr;
}

Expected<Buffer> Buffer::create(const uint8_t *src, size_t size, const BufferStorageParams &params)
{
    auto buffer = create(size, params);
    CHECK_EXPECTED(buffer);
    std::memcpy(static_cast<void*>(buffer->m_data), static_cast<const void*>(src), size);
    return buffer;
}

Expected<Buffer> Buffer::create(std::initializer_list<uint8_t> init, const BufferStorageParams &params)
{
    auto buffer = create(init.size(), params);
    CHECK_EXPECTED(buffer);
    size_t index = 0;
    for (const auto& n : init) {
        // Hackzzz
        buffer->m_data[index++] = n;
    }

    return buffer;
}

Expected<Buffer> Buffer::create(BufferStoragePtr storage)
{
    auto storage_impl = make_unique_nothrow<StorageImpl>(std::move(storage));
    CHECK_NOT_NULL(storage_impl, HAILO_OUT_OF_HOST_MEMORY);

    return Buffer(std::move(storage_impl));
}

Expected<BufferPtr> Buffer::create_shared(BufferStoragePtr storage)
{
    TRY(auto buffer, create(storage));
    auto buffer_ptr = make_shared_nothrow<Buffer>(std::move(buffer));
    CHECK_NOT_NULL_AS_EXPECTED(buffer_ptr, HAILO_OUT_OF_HOST_MEMORY);
    return buffer_ptr;
}

Expected<Buffer> Buffer::copy() const
{
    return Buffer::create(m_data, m_size);
}

Buffer& Buffer::operator=(Buffer&& other)
{
    m_storage_impl = std::move(other.m_storage_impl);
    m_data = std::exchange(other.m_data, nullptr);
    m_size = std::exchange(other.m_size, 0);
    return *this;
}

bool Buffer::operator==(const Buffer& rhs) const
{
    if (m_size != rhs.m_size) {
        return false;
    }
    return (0 == std::memcmp(m_data, rhs.m_data, m_size));
}

bool Buffer::operator!=(const Buffer& rhs) const
{
    if (m_size != rhs.m_size) {
        return true;
    }
    return (0 != std::memcmp(m_data, rhs.m_data, m_size));
}

uint8_t& Buffer::operator[](size_t pos)
{
    assert(pos < m_size);
    return m_data[pos];
}

const uint8_t& Buffer::operator[](size_t pos) const
{
    assert(pos < m_size);
    return m_data[pos];
}

Buffer::iterator Buffer::begin()
{
    return iterator(data());
}

Buffer::iterator Buffer::end()
{
    return iterator(data() + m_size);
}

BufferStorage &Buffer::storage()
{
    assert(m_storage_impl);
    return *m_storage_impl->m_storage;
}

uint8_t* Buffer::data() noexcept
{
    return m_data;
}

const uint8_t* Buffer::data() const noexcept
{
    return m_data;
}

size_t Buffer::size() const noexcept
{
    return m_size;
}

std::string Buffer::to_string() const
{
    for (size_t i = 0; i < m_size; i++) {
        if (m_data[i] == 0) {
            // We'll return a string that ends at the first null in the buffer
            return std::string(reinterpret_cast<const char*>(m_data));
        }
    }

    return std::string(reinterpret_cast<const char*>(m_data), m_size);
}

// Note: This is a friend function
std::ostream& operator<<(std::ostream& stream, const Buffer& buffer)
{
    BufferUtils::format_buffer(buffer, stream);
    return stream;
}

uint16_t Buffer::as_uint16() const
{
    return as_type<uint16_t>();
}

uint32_t Buffer::as_uint32() const
{
    return as_type<uint32_t>();
}

uint64_t Buffer::as_uint64() const
{
    return as_type<uint64_t>();
}

uint16_t& Buffer::as_uint16()
{
    return as_type<uint16_t>();
}

uint32_t& Buffer::as_uint32()
{
    return as_type<uint32_t>();
}

uint64_t& Buffer::as_uint64()
{
    return as_type<uint64_t>();
}

MemoryView Buffer::from(size_t offset)
{
    if (offset >= m_size) {
        return MemoryView();
    }

    return MemoryView(m_data + offset, m_size - offset);
}

const MemoryView Buffer::from(size_t offset) const
{
    return const_cast<Buffer*>(this)->from(offset);
}

MemoryView Buffer::to(size_t offset)
{
    if (offset >= m_size) {
        return MemoryView(m_data, m_size);
    }

    return MemoryView(m_data, offset);
}

const MemoryView Buffer::to(size_t offset) const
{
    return const_cast<Buffer*>(this)->to(offset);
}

MemoryView Buffer::slice(size_t from, size_t to)
{
    if (from >= m_size || from >= to) {
        return MemoryView();
    }

    if (to > m_size) {
        to = m_size;
    }

    return MemoryView(m_data + from, to - from);
}

const MemoryView Buffer::slice(size_t from, size_t to) const
{
    return const_cast<Buffer*>(this)->slice(from, to);
}

MemoryView Buffer::as_view()
{
    return MemoryView(m_data, m_size);
}

const MemoryView Buffer::as_view() const
{
    return MemoryView(m_data, m_size);
}

Expected<void *> Buffer::release() noexcept
{
    return m_storage_impl->m_storage->release();
}

MemoryView::MemoryView() noexcept :
    m_data(nullptr),
    m_size(0)
{}

MemoryView::MemoryView(Buffer &buffer) noexcept :
    m_data(buffer.data()),
    m_size(buffer.size())
{}

MemoryView::MemoryView(BufferPtr buffer) noexcept :
    m_data(buffer->data()),
    m_size(buffer->size())
{}

MemoryView::MemoryView(void *data, size_t size) noexcept :
    m_data(data),
    m_size(size)
{}

MemoryView::MemoryView(const std::string &data) noexcept :
    m_data(const_cast<char *>(data.data())),
    m_size(data.size())
{}

const MemoryView MemoryView::create_const(const void *data, size_t size) noexcept
{
    return MemoryView(const_cast<void *>(data), size);
}

uint8_t* MemoryView::data() noexcept
{
    return reinterpret_cast<uint8_t*>(m_data);
}

const uint8_t* MemoryView::data() const noexcept
{
    return reinterpret_cast<const uint8_t*>(m_data);
}

size_t MemoryView::size() const noexcept
{
    return m_size;
}

bool MemoryView::empty() const noexcept
{
    return (m_data == nullptr);
}

// Note: This is a friend function
std::ostream& operator<<(std::ostream& stream, const MemoryView& buffer)
{
    BufferUtils::format_buffer(buffer, stream);
    return stream;
}

MemoryView MemoryView::from(size_t offset)
{
    if (offset >= m_size) {
        return MemoryView();
    }

    return MemoryView(static_cast<uint8_t*>(m_data) + offset, m_size - offset);
}

const MemoryView MemoryView::from(size_t offset) const
{
    return const_cast<MemoryView*>(this)->from(offset);
}

} /* namespace hailort */
