/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file embedding_view_wrapper.hpp
 * @brief Embedding View Wrapper - an embedding row as a memory view or as a packed table's row decoded on copy
 **/

#ifndef _HAILO_GENAI_EMBEDDING_VIEW_WRAPPER_HPP_
#define _HAILO_GENAI_EMBEDDING_VIEW_WRAPPER_HPP_

#include "hailo/hailort.h"
#include "hailo/buffer.hpp"
#include "common/utils.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hailort
{
namespace genai
{

// Rebuilds a packed table's row as the uint16 values its layer takes
class EmbeddingRowDecoder {
public:
    virtual ~EmbeddingRowDecoder() = default;
    virtual size_t row_size() const = 0;
    virtual void decode_row(size_t row, uint16_t *row_values) const = 0;

protected:
    EmbeddingRowDecoder() = default;
    EmbeddingRowDecoder(const EmbeddingRowDecoder &) = default;
    EmbeddingRowDecoder(EmbeddingRowDecoder &&) = default;
    EmbeddingRowDecoder &operator=(const EmbeddingRowDecoder &) = default;
    EmbeddingRowDecoder &operator=(EmbeddingRowDecoder &&) = default;
};

class EmbeddingViewWrapper {
public:
    enum class EmbeddingType {
        DATA,
        IMAGE,
        VIDEO
    };

    EmbeddingViewWrapper(MemoryView memory_view, EmbeddingType embedding_type = EmbeddingType::DATA) :
        m_memory_view(memory_view), m_embedding_type(embedding_type) {}

    EmbeddingViewWrapper(BufferPtr buffer, EmbeddingType embedding_type = EmbeddingType::DATA) :
        m_buffer(buffer), m_embedding_type(embedding_type)
    {
        m_memory_view = MemoryView(buffer);
    }

    // A packed table's row, decoded only by copy_to; like a view into a table, it must not outlive the table
    EmbeddingViewWrapper(const EmbeddingRowDecoder &row_decoder, size_t row) :
        m_row_decoder(&row_decoder), m_packed_row(row), m_embedding_type(EmbeddingType::DATA) {}
    EmbeddingViewWrapper(const EmbeddingRowDecoder &&row_decoder, size_t row) = delete;

    // Only a plain row has bytes to point at; a packed row logs an error and returns null, read it with copy_to
    uint8_t* data() { return is_packed_row() ? packed_row_data_error() : m_memory_view.data(); }
    const uint8_t* data() const { return is_packed_row() ? packed_row_data_error() : m_memory_view.data(); }
    size_t size() const { return is_packed_row() ? m_row_decoder->row_size() : m_memory_view.size(); }

    EmbeddingType type() const { return m_embedding_type; }

    // A packed row decodes into a uint16-aligned destination; any row needs a destination of exactly size() bytes
    hailo_status copy_to(MemoryView destination) const
    {
        CHECK(destination.size() == size(), HAILO_INVALID_ARGUMENT,
            "Embedding row has {} bytes, but its destination has {} bytes", size(), destination.size());
        if (is_packed_row()) {
            CHECK(0 == (reinterpret_cast<uintptr_t>(destination.data()) % alignof(uint16_t)), HAILO_INVALID_ARGUMENT,
                "A packed embedding row needs a uint16-aligned destination");
            m_row_decoder->decode_row(m_packed_row, reinterpret_cast<uint16_t*>(destination.data()));
            return HAILO_SUCCESS;
        }
        if (0 < destination.size()) {
            std::memcpy(destination.data(), m_memory_view.data(), destination.size());
        }
        return HAILO_SUCCESS;
    }

private:
    bool is_packed_row() const { return nullptr != m_row_decoder; }
    static uint8_t* packed_row_data_error()
    {
        LOGGER__ERROR("A packed embedding row has no bytes to point at, read it with copy_to");
        assert(false);
        return nullptr;
    }

    MemoryView m_memory_view;
    BufferPtr m_buffer;
    const EmbeddingRowDecoder *m_row_decoder = nullptr;
    size_t m_packed_row = 0;
    EmbeddingType m_embedding_type;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_EMBEDDING_VIEW_WRAPPER_HPP_ */

