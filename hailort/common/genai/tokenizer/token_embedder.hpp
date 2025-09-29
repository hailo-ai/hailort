/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file token_embedder.hpp
 * @brief Token Embedder implementation
 **/

#ifndef _HAILO_GENAI_TOKEN_EMBEDDER_HPP_
#define _HAILO_GENAI_TOKEN_EMBEDDER_HPP_

#include "hailo/hailort.h"
#include "hailo/buffer.hpp"
#include "hailo/expected.hpp"
#include "hailo/hailort_common.hpp"
#include "eigen.hpp"

namespace hailort
{
namespace genai
{

constexpr int INVALID_TOKEN_VALUE = INT32_MAX;

template<typename T>
class TokenEmbedder
{
public:
    using eigen_matrix_2d_t = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
    using eigen_map_2d_t = Eigen::Map<eigen_matrix_2d_t>;

    static Expected<std::unique_ptr<TokenEmbedder>> create(BufferPtr embeddings, size_t rows, size_t cols,
        int special_token_id = INVALID_TOKEN_VALUE, size_t special_token_embeddings_count = 1)
    {
        TRY(auto ptr, create(embeddings->as_view(), rows, cols, special_token_id, special_token_embeddings_count));
        ptr->set_resource_guard(embeddings);
        return ptr;
    }

    static Expected<std::unique_ptr<TokenEmbedder>> create(const MemoryView &embeddings, size_t rows, size_t cols,
        int special_token_id = INVALID_TOKEN_VALUE, size_t special_token_embeddings_count = 1)
    {
        eigen_map_2d_t embeddings_matrix(embeddings.as_pointer<T>(), rows, cols);

        auto ptr = std::make_unique<TokenEmbedder>(std::move(embeddings_matrix), special_token_id, special_token_embeddings_count);
        CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY); // Consider returning different status

        return ptr;
    }

    void set_resource_guard(BufferPtr embeddings)
    {
        // Makes the embeddings buffer to be destroyed when the TokenEmbedder is destroyed
        m_embeddings = embeddings;
    }

    void tokens_to_embeddings(MemoryView embeddings_buffer, std::vector<int> &input_tokens)
    {
        assert(m_special_token_id == INVALID_TOKEN_VALUE); // This method doesnt work with special-tokens-id see the other tokens_to_embeddings()
        assert(embeddings_buffer.size() >= (input_tokens.size() * m_embeddings_matrix.cols() * sizeof(T)));

        Eigen::Map<Eigen::VectorXi> input_tokens_eigen = Eigen::Map<Eigen::VectorXi>(input_tokens.data(), input_tokens.size());

        eigen_map_2d_t mapped_matrix(
            reinterpret_cast<T*>(embeddings_buffer.data()), input_tokens_eigen.size(), m_embeddings_matrix.cols());
        for (int i = 0; i < input_tokens_eigen.size(); ++i) {
            mapped_matrix.row(i) = m_embeddings_matrix.row(input_tokens_eigen[i]);
        }
    }

    TokenEmbedder(eigen_map_2d_t &&embeddings_matrix, int special_token_id, size_t special_token_embeddings_count) :
        m_embeddings_matrix(std::move(embeddings_matrix)),
        m_special_token_id(special_token_id),
        m_special_token_embeddings_count(special_token_embeddings_count)
    {}

    size_t cols() const
    {
        return static_cast<size_t>(m_embeddings_matrix.cols());
    }

    MemoryView get_text_embedding(int token) const
    {
        assert((0 <= token) && (token < m_embeddings_matrix.rows()));
        const T *row_ptr = m_embeddings_matrix.row(token).data();
        size_t row_bytes = static_cast<size_t>(m_embeddings_matrix.cols()) * sizeof(T);
        return MemoryView::create_const(row_ptr, row_bytes);
    }

    std::vector<MemoryView> tokens_to_embeddings(const std::vector<int> &tokens) const
    {
        std::vector<MemoryView> views;
        views.reserve(tokens.size());
        for (int token : tokens) {
            if ((INVALID_TOKEN_VALUE != m_special_token_id) && (token == m_special_token_id)) {
                // Special tokens are not a part of the embeddings-vocab. Setting empty view for placeholder 'special_token_embeddings_size' times
                for (size_t i = 0; i < m_special_token_embeddings_count; i++) {
                    views.emplace_back(MemoryView());
                }
            } else {
                assert((0 <= token) && (token < m_embeddings_matrix.rows()));
                views.emplace_back(get_text_embedding(token));
            }
        }
        return views;
    }

    TokenEmbedder(TokenEmbedder &&) = delete;
    TokenEmbedder(const TokenEmbedder &) = delete;
    TokenEmbedder &operator=(TokenEmbedder &&) = delete;
    TokenEmbedder &operator=(const TokenEmbedder &) = delete;
    virtual ~TokenEmbedder() = default;

private:
    eigen_map_2d_t m_embeddings_matrix;
    int m_special_token_id;
    size_t m_special_token_embeddings_count;
    BufferPtr m_embeddings;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_TOKEN_EMBEDDER_HPP_ */
