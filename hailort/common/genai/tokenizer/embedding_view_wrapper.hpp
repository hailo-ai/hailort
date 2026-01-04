/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file embedding_view_wrapper.hpp
 * @brief Embedding View Wrapper - lightweight wrapper around memory views for embeddings
 **/

#ifndef _HAILO_GENAI_EMBEDDING_VIEW_WRAPPER_HPP_
#define _HAILO_GENAI_EMBEDDING_VIEW_WRAPPER_HPP_

#include "hailo/buffer.hpp"

namespace hailort
{
namespace genai
{

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

    uint8_t* data() { return m_memory_view.data(); }
    const uint8_t* data() const { return m_memory_view.data(); }
    size_t size() const { return m_memory_view.size(); }

    operator MemoryView() const { return m_memory_view; }
    EmbeddingType type() const { return m_embedding_type; }

private:
    MemoryView m_memory_view;
    BufferPtr m_buffer;
    EmbeddingType m_embedding_type;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_EMBEDDING_VIEW_WRAPPER_HPP_ */

