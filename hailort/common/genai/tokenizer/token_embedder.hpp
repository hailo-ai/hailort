/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
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
#include "common/utils.hpp"
#include "common/genai/config/embedding_tables.hpp"
#include "common/genai/eigen.hpp"
#include "common/genai/tokenizer/embedding_view_wrapper.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace hailort
{
namespace genai
{

constexpr int INVALID_TOKEN_VALUE = INT32_MAX;

// The per-layer inputs table may be shorter than the vocabulary (Gemma 3n), and the model reads row 0 for every
// token id outside the table
inline int per_layer_inputs_row(int token_id, size_t rows_count)
{
    const auto is_inside_table = (0 <= token_id) && (static_cast<size_t>(token_id) < rows_count);
    return is_inside_table ? token_id : 0;
}

// Row 0 must exist, and a trailing partial row would otherwise be dropped silently
inline Expected<size_t> embedding_table_rows_count(size_t table_size, size_t row_size, const std::string &resource)
{
    CHECK(0 < row_size, HAILO_INVALID_ARGUMENT, "The embedding table '{}' has no row width", resource);
    CHECK((0 < table_size) && (0 == (table_size % row_size)), HAILO_INVALID_ARGUMENT,
        "The embedding table '{}' has {} bytes, but must be a non-zero multiple of its {}-byte row",
        resource, table_size, row_size);

    return table_size / row_size;
}

class CodebookTable final : public EmbeddingRowDecoder
{
public:
    static Expected<CodebookTable> create(const MemoryView &codes, const MemoryView &codebook, size_t cols,
        const EmbeddingTableCodebook &codebook_params, const std::string &resource)
    {
        const auto code_bits = codebook_params.code_bits;
        const size_t codebooks_per_row = codebook_params.codebooks_per_row;
        CHECK((0 < code_bits) && (0 == (BITS_PER_BYTE % code_bits)), HAILO_INVALID_ARGUMENT,
            "The embedding table '{}' holds {}-bit codes, but a code width must divide a byte", resource, code_bits);
        CHECK((0 < codebooks_per_row) && (0 == (cols % codebooks_per_row)), HAILO_INVALID_ARGUMENT,
            "The embedding table '{}' splits each row into {} codebooks, which do not divide its {} columns",
            resource, codebooks_per_row, cols);
        const auto cols_per_run = cols / codebooks_per_row;
        const auto run_bits = cols_per_run * code_bits;
        CHECK(0 == (run_bits % BITS_PER_BYTE), HAILO_NOT_SUPPORTED,
            "The embedding table '{}' packs each codebook's {} columns into {} bits, which do not fill whole bytes, "
            "and the padding of such a run is unknown", resource, cols_per_run, run_bits);
        const size_t code_bytes_per_run = run_bits / BITS_PER_BYTE;
        TRY(const auto rows_count, embedding_table_rows_count(codes.size(), code_bytes_per_run * codebooks_per_row,
            resource));
        CHECK(codebook_params.rows == rows_count, HAILO_INVALID_ARGUMENT,
            "The codebook '{}' of the embedding table '{}' declares {} rows in its shape, but the table holds {}",
            codebook_params.resource, resource, codebook_params.rows, rows_count);
        const auto entries_per_codebook = size_t{1} << code_bits;
        const auto expected_codebook_size =
            uint64_t{rows_count} * codebooks_per_row * entries_per_codebook * sizeof(uint16_t);
        CHECK(expected_codebook_size == uint64_t{codebook.size()}, HAILO_INVALID_ARGUMENT,
            "The codebook '{}' of the embedding table '{}' has {} bytes, but its {} rows of {} codebooks of {} "
            "entries take {}", codebook_params.resource, resource, codebook.size(), rows_count, codebooks_per_row,
            entries_per_codebook, expected_codebook_size);

        return CodebookTable(codes.data(), codebook.data(), rows_count, cols, code_bits, codebooks_per_row,
            code_bytes_per_run);
    }

    size_t rows() const
    {
        return m_rows;
    }

    size_t cols() const
    {
        return m_cols;
    }

private:
    static constexpr uint32_t BITS_PER_BYTE = 8;
    static constexpr uint32_t WIDEST_CODE_BITS = BITS_PER_BYTE;
    static constexpr size_t MAX_ENTRIES_PER_CODEBOOK = size_t{1} << WIDEST_CODE_BITS;

    size_t row_size() const override
    {
        return m_cols * sizeof(uint16_t);
    }

    // The codebook resource carries no alignment guarantee, so each run's entries are copied out before indexing
    void decode_row(size_t row, uint16_t *row_values) const override
    {
        assert(row < m_rows);
        const auto *codes = m_codes + (row * m_codebooks_per_row * m_code_bytes_per_run);
        const auto run_codebook_bytes = m_entries_per_codebook * sizeof(uint16_t);
        const auto *run_codebook = m_codebook + (row * m_codebooks_per_row * run_codebook_bytes);
        std::array<uint16_t, MAX_ENTRIES_PER_CODEBOOK> entries;
        assert(run_codebook_bytes <= sizeof(entries));
        for (size_t run = 0; run < m_codebooks_per_row; run++) {
            std::memcpy(entries.data(), run_codebook, run_codebook_bytes);
            for (size_t byte_index = 0; byte_index < m_code_bytes_per_run; byte_index++) {
                auto codes_byte = static_cast<uint32_t>(*codes++);
                for (size_t code_index = 0; code_index < m_codes_per_byte; code_index++) {
                    *row_values++ = entries[codes_byte & m_code_mask];
                    codes_byte >>= m_code_bits;
                }
            }
            run_codebook += run_codebook_bytes;
        }
    }

    CodebookTable(const uint8_t *codes, const uint8_t *codebook, size_t rows, size_t cols, uint32_t code_bits,
        size_t codebooks_per_row, size_t code_bytes_per_run) :
        m_codes(codes), m_codebook(codebook), m_rows(rows), m_cols(cols), m_code_bits(code_bits),
        m_codebooks_per_row(codebooks_per_row), m_code_bytes_per_run(code_bytes_per_run),
        m_codes_per_byte(BITS_PER_BYTE / code_bits), m_entries_per_codebook(size_t{1} << code_bits),
        m_code_mask(static_cast<uint32_t>(m_entries_per_codebook - 1))
    {}

    const uint8_t *m_codes;
    const uint8_t *m_codebook;
    size_t m_rows;
    size_t m_cols;
    uint32_t m_code_bits;
    size_t m_codebooks_per_row;
    size_t m_code_bytes_per_run;
    size_t m_codes_per_byte;
    size_t m_entries_per_codebook;
    uint32_t m_code_mask;
};

template<typename T>
class TokenEmbedder
{
public:
    using eigen_matrix_2d_t = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
    using eigen_map_2d_t = Eigen::Map<eigen_matrix_2d_t>;

    static Expected<std::unique_ptr<TokenEmbedder>> create(const MemoryView &embeddings, size_t rows, size_t cols,
        int image_token_id = INVALID_TOKEN_VALUE, int video_token_id = INVALID_TOKEN_VALUE)
    {
        eigen_map_2d_t embeddings_matrix(embeddings.as_pointer<T>(), rows, cols);

        auto ptr = std::unique_ptr<TokenEmbedder>(new (std::nothrow) TokenEmbedder(std::move(embeddings_matrix),
            image_token_id, video_token_id));
        CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);

        return ptr;
    }

    static Expected<std::unique_ptr<TokenEmbedder>> create(CodebookTable &&codebook_table,
        int image_token_id = INVALID_TOKEN_VALUE, int video_token_id = INVALID_TOKEN_VALUE)
    {
        static_assert(std::is_same<T, uint16_t>::value, "A codebook decodes into uint16 rows");
        auto ptr = std::unique_ptr<TokenEmbedder>(new (std::nothrow) TokenEmbedder(std::move(codebook_table),
            image_token_id, video_token_id));
        CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);

        return ptr;
    }

    // Keeps a buffer the table views alive as long as the TokenEmbedder
    void add_resource_guard(BufferPtr resource)
    {
        m_resource_guards.push_back(std::move(resource));
    }

    hailo_status tokens_to_embeddings(MemoryView embeddings_buffer, std::vector<int> &input_tokens)
    {
        CHECK(!m_codebook_table, HAILO_NOT_SUPPORTED,
            "Gathering into a caller's buffer is not supported for a codebook embedding table");
        CHECK((INVALID_TOKEN_VALUE == m_image_token_id) && (INVALID_TOKEN_VALUE == m_video_token_id),
            HAILO_NOT_SUPPORTED, "Gathering into a caller's buffer is not supported with image or video pad tokens");
        const auto embeddings_size = input_tokens.size() * static_cast<size_t>(m_embeddings_matrix.cols()) * sizeof(T);
        CHECK(embeddings_buffer.size() >= embeddings_size, HAILO_INVALID_ARGUMENT,
            "The embeddings buffer has {} bytes, but {} tokens take {}", embeddings_buffer.size(), input_tokens.size(),
            embeddings_size);
        for (const auto token : input_tokens) {
            CHECK_SUCCESS(validate_text_token(token));
        }

        Eigen::Map<Eigen::VectorXi> input_tokens_eigen = Eigen::Map<Eigen::VectorXi>(input_tokens.data(), input_tokens.size());

        eigen_map_2d_t mapped_matrix(
            reinterpret_cast<T*>(embeddings_buffer.data()), input_tokens_eigen.size(), m_embeddings_matrix.cols());
        for (int i = 0; i < input_tokens_eigen.size(); ++i) {
            mapped_matrix.row(i) = m_embeddings_matrix.row(input_tokens_eigen[i]);
        }

        return HAILO_SUCCESS;
    }

    size_t rows() const
    {
        return m_codebook_table ? m_codebook_table->rows() : static_cast<size_t>(m_embeddings_matrix.rows());
    }

    size_t cols() const
    {
        return m_codebook_table ? m_codebook_table->cols() : static_cast<size_t>(m_embeddings_matrix.cols());
    }

    Expected<std::vector<EmbeddingViewWrapper>> tokens_to_embeddings(const std::vector<int> &tokens) const
    {
        std::vector<EmbeddingViewWrapper> views;
        views.reserve(tokens.size());
        size_t current_image_idx = 0;
        size_t current_video_idx = 0;
        for (int token : tokens) {
            if (is_image_token(token)) {
                // Special tokens are not a part of the embeddings-vocab. Setting empty view for placeholder
                CHECK(current_image_idx < m_embeddings_count_per_image.size(), HAILO_INVALID_ARGUMENT,
                    "More image tokens ({}) than actual images provided ({})",
                    current_image_idx + 1, m_embeddings_count_per_image.size());
                size_t embeddings_count = m_embeddings_count_per_image[current_image_idx];
                for (size_t i = 0; i < embeddings_count; i++) {
                    views.emplace_back(MemoryView(), EmbeddingViewWrapper::EmbeddingType::IMAGE);
                }
                current_image_idx++;
            } else if (is_video_token(token)) {
                // Special tokens are not a part of the embeddings-vocab. Setting empty view for placeholder
                CHECK(current_video_idx < m_embeddings_count_per_video.size(), HAILO_INVALID_ARGUMENT,
                    "More video tokens ({}) than actual videos provided ({})",
                    current_video_idx + 1, m_embeddings_count_per_video.size());
                size_t embeddings_count = m_embeddings_count_per_video[current_video_idx];
                for (size_t i = 0; i < embeddings_count; i++) {
                    views.emplace_back(MemoryView(), EmbeddingViewWrapper::EmbeddingType::VIDEO);
                }
                current_video_idx++;
            } else {
                CHECK_SUCCESS(validate_text_token(token));
                views.push_back(text_row(static_cast<size_t>(token)));
            }
        }
        return views;
    }

    void set_special_tokens_embeddings_count(
        const std::vector<size_t> &embeddings_count_per_image,
        const std::vector<size_t> &embeddings_count_per_video)
    {
        // Set per-image and per-video embedding counts before generation.
        // Each entry represents the total embedding count for that image/video.
        m_embeddings_count_per_image = embeddings_count_per_image;
        m_embeddings_count_per_video = embeddings_count_per_video;
    }

    TokenEmbedder(TokenEmbedder &&) = delete;
    TokenEmbedder(const TokenEmbedder &) = delete;
    TokenEmbedder &operator=(TokenEmbedder &&) = delete;
    TokenEmbedder &operator=(const TokenEmbedder &) = delete;
    virtual ~TokenEmbedder() = default;

private:
    TokenEmbedder(eigen_map_2d_t &&embeddings_matrix, int image_token_id, int video_token_id) :
        m_embeddings_matrix(std::move(embeddings_matrix)),
        m_image_token_id(image_token_id),
        m_video_token_id(video_token_id)
    {}

    TokenEmbedder(CodebookTable &&codebook_table, int image_token_id, int video_token_id) :
        m_embeddings_matrix(nullptr, 0, 0),
        m_codebook_table(std::move(codebook_table)),
        m_image_token_id(image_token_id),
        m_video_token_id(video_token_id)
    {}

    bool is_image_token(int token) const
    {
        return (INVALID_TOKEN_VALUE != m_image_token_id) && (token == m_image_token_id);
    }

    bool is_video_token(int token) const
    {
        return (INVALID_TOKEN_VALUE != m_video_token_id) && (token == m_video_token_id);
    }

    size_t row_size() const
    {
        return cols() * sizeof(T);
    }

    hailo_status validate_text_token(int token) const
    {
        CHECK((0 <= token) && (static_cast<size_t>(token) < rows()), HAILO_INVALID_ARGUMENT,
            "Token {} is out of embeddings matrix range [0, {})", token, rows());

        return HAILO_SUCCESS;
    }

    EmbeddingViewWrapper text_row(size_t row) const
    {
        if (m_codebook_table) {
            return EmbeddingViewWrapper(*m_codebook_table, row);
        }
        const T *row_ptr = m_embeddings_matrix.row(static_cast<Eigen::Index>(row)).data();
        return EmbeddingViewWrapper(MemoryView::create_const(row_ptr, row_size()));
    }

    eigen_map_2d_t m_embeddings_matrix;
    std::optional<CodebookTable> m_codebook_table;
    int m_image_token_id;
    int m_video_token_id;
    std::vector<size_t> m_embeddings_count_per_image;
    std::vector<size_t> m_embeddings_count_per_video;
    std::vector<BufferPtr> m_resource_guards;
};

inline hailo_status validate_declared_table_shape(const EmbeddingTableParams &table, size_t rows_count,
    size_t features)
{
    if (!table.declared_shape) {
        return HAILO_SUCCESS;
    }
    const auto &declared_shape = *table.declared_shape;
    CHECK((declared_shape.rows == rows_count) && (declared_shape.cols == features), HAILO_INVALID_ARGUMENT,
        "The embedding table '{}' declares a shape of {} rows by {} columns, but it holds {} rows and its layer takes "
        "{} columns", table.resource, declared_shape.rows, declared_shape.cols, rows_count, features);

    return HAILO_SUCCESS;
}

inline Expected<std::unique_ptr<TokenEmbedder<uint16_t>>> create_table_embedder(const EmbeddingTableParams &table,
    const MemoryView &table_view, const MemoryView &codebook_view, size_t features,
    int image_token_id = INVALID_TOKEN_VALUE, int video_token_id = INVALID_TOKEN_VALUE)
{
    if (!table.codebook) {
        TRY(const auto rows_count, embedding_table_rows_count(table_view.size(), sizeof(uint16_t) * features,
            table.resource));
        CHECK_SUCCESS(validate_declared_table_shape(table, rows_count, features));
        return TokenEmbedder<uint16_t>::create(table_view, rows_count, features, image_token_id, video_token_id);
    }

    TRY(auto codebook_table, CodebookTable::create(table_view, codebook_view, features, *table.codebook,
        table.resource));
    CHECK_SUCCESS(validate_declared_table_shape(table, codebook_table.rows(), features));
    return TokenEmbedder<uint16_t>::create(std::move(codebook_table), image_token_id, video_token_id);
}

inline Expected<std::vector<EmbeddingViewWrapper>> gather_per_layer_inputs(const TokenEmbedder<uint16_t> &table,
    const std::vector<int> &tokens)
{
    std::vector<int> rows;
    rows.reserve(tokens.size());
    for (const auto token : tokens) {
        rows.push_back(per_layer_inputs_row(token, table.rows()));
    }

    return table.tokens_to_embeddings(rows);
}

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_TOKEN_EMBEDDER_HPP_ */
