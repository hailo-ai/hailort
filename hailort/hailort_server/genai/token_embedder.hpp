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
#include "hailo/expected.hpp"
#include "hailo/hailort_common.hpp"
#include "eigen.hpp"

namespace hailort
{
namespace genai
{


template<typename T>
class TokenEmbedder
{
public:
    using eigen_matrix_2d_t = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
    using eigen_map_2d_t = Eigen::Map<eigen_matrix_2d_t>;

    static Expected<std::unique_ptr<TokenEmbedder>> create(const MemoryView &embeddings, size_t rows, size_t cols)
    {
        eigen_map_2d_t embeddings_matrix(embeddings.as_pointer<T>(), rows, cols);

        auto ptr = std::make_unique<TokenEmbedder>(std::move(embeddings_matrix));
        CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY); // Consider returning different status

        return ptr;
    }

    void tokens_to_embeddings(MemoryView embeddings_buffer, std::vector<int> &input_tokens)
    {
        assert(embeddings_buffer.size() >= (input_tokens.size() * m_embeddings_matrix.cols() * sizeof(T)));

        Eigen::Map<Eigen::VectorXi> input_tokens_eigen = Eigen::Map<Eigen::VectorXi>(input_tokens.data(), input_tokens.size());

        eigen_map_2d_t mapped_matrix(
            reinterpret_cast<T*>(embeddings_buffer.data()), input_tokens_eigen.size(), m_embeddings_matrix.cols());
        for (int i = 0; i < input_tokens_eigen.size(); ++i) {
            mapped_matrix.row(i) = m_embeddings_matrix.row(input_tokens_eigen[i]);
        }
    }

    TokenEmbedder(eigen_map_2d_t &&embeddings_matrix) :
        m_embeddings_matrix(std::move(embeddings_matrix))
    {}

    TokenEmbedder(TokenEmbedder &&) = delete;
    TokenEmbedder(const TokenEmbedder &) = delete;
    TokenEmbedder &operator=(TokenEmbedder &&) = delete;
    TokenEmbedder &operator=(const TokenEmbedder &) = delete;
    virtual ~TokenEmbedder() = default;

private:
    eigen_map_2d_t m_embeddings_matrix;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_TOKEN_EMBEDDER_HPP_ */
