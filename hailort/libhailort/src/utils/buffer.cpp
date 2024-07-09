/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
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
#include "common/string_utils.hpp"

#include <algorithm>
#include <string>
#include <cstring>
#include <iostream>
#include <iomanip>

namespace hailort
{

static void format_buffer(std::ostream& stream, const uint8_t *buffer, size_t size)
{
    assert(nullptr != buffer);

    stream << "[addr = " << static_cast<const void *>(buffer) << ", size = " << size << "]" << std::endl;

    static const bool UPPERCASE = true;
    static const size_t BYTES_PER_LINE = 32;
    static const char *BYTE_DELIM = "  ";
    for (size_t offset = 0; offset < size; offset += BYTES_PER_LINE) {
        const size_t line_size = std::min(BYTES_PER_LINE, size - offset);
        stream << fmt::format("0x{:08X}", offset) << BYTE_DELIM; // 32 bit offset into a buffer should be enough
        stream << StringUtils::to_hex_string(buffer + offset, line_size, UPPERCASE, BYTE_DELIM) << std::endl;
    }
}

class Buffer::StorageImpl final {
public:
    StorageImpl(BufferStoragePtr storage, std::unique_ptr<BufferStorageRegisteredResource> storage_resource) :
        m_storage(std::move(storage)),
        m_storage_resource(std::move(storage_resource))
    {}

    BufferStoragePtr m_storage;

    // Optionally we register the resource. By default we register the resource to the manager, but on some cases (for
    // example - the unit tests, we want to skip the registration).
    std::unique_ptr<BufferStorageRegisteredResource> m_storage_resource;
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

Expected<Buffer> Buffer::create(BufferStoragePtr storage, bool register_storage /* = true */)
{
    // If needed, register the storage
    std::unique_ptr<BufferStorageRegisteredResource> optional_registered_resource;
    if (register_storage) {
        const auto storage_key = std::make_pair(storage->user_address(), storage->size());
        auto registered_resource = BufferStorageRegisteredResource::create(storage, storage_key);
        CHECK_EXPECTED(registered_resource);
        optional_registered_resource = make_unique_nothrow<BufferStorageRegisteredResource>(registered_resource.release());
        CHECK_NOT_NULL(optional_registered_resource, HAILO_OUT_OF_HOST_MEMORY);
    }

    auto storage_impl = make_unique_nothrow<StorageImpl>(std::move(storage), std::move(optional_registered_resource));
    CHECK_NOT_NULL(storage_impl, HAILO_OUT_OF_HOST_MEMORY);

    return Buffer(std::move(storage_impl));
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
    format_buffer(stream, buffer.data(), buffer.size());
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

Expected<void *> Buffer::release() noexcept
{
    return m_storage_impl->m_storage->release();
}

MemoryView::MemoryView() :
    m_data(nullptr),
    m_size(0)
{}

MemoryView::MemoryView(Buffer &buffer) :
    m_data(buffer.data()),
    m_size(buffer.size())
{}

MemoryView::MemoryView(void *data, size_t size) :
    m_data(data),
    m_size(size)
{}

const MemoryView MemoryView::create_const(const void *data, size_t size)
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
    format_buffer(stream, buffer.data(), buffer.size());
    return stream;
}

} /* namespace hailort */
