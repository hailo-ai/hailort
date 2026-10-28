/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file embedding_tables.hpp
 * @brief The embedding tables a model configuration declares
 **/

#ifndef _HAILO_GENAI_EMBEDDING_TABLES_HPP_
#define _HAILO_GENAI_EMBEDDING_TABLES_HPP_

#include "hailo/expected.hpp"
#include "hailo/buffer.hpp"
#include "common/genai/constants.hpp"

#include <optional>
#include <string>

namespace hailort
{
namespace genai
{

// TODO: HRT-16260 - Get this info from Hef
static const std::string INPUT_LAYER_EMBEDDINGS_SUFF = "input_layer1";

// Each row splits into codebooks_per_row runs of contiguous columns; a run's code_bits-bit codes, lowest bits
// first, index its own (1 << code_bits)-entry uint16 codebook
struct EmbeddingTableCodebook {
    std::string resource;
    uint32_t code_bits = 0;
    uint32_t codebooks_per_row = 0;
    uint32_t rows = 0;
};

struct EmbeddingTableShape {
    uint32_t rows = 0;
    uint32_t cols = 0;
};

// A table of embedding rows the model gathers from, and the input layer the gathered rows are written to
struct EmbeddingTableParams {
    std::string resource;
    std::string input_layer_suffix;
    std::optional<EmbeddingTableCodebook> codebook = std::nullopt;
    std::optional<EmbeddingTableShape> declared_shape = std::nullopt;
};

// The defaults are the table every model that declares no embedding tables gathers from
struct EmbeddingTables {
    EmbeddingTableParams token_embeddings = {INPUT_EMB_BINARY, INPUT_LAYER_EMBEDDINGS_SUFF};
    std::optional<EmbeddingTableParams> per_layer_inputs;
};

// Never throws, so a build without exceptions can call it
Expected<EmbeddingTables> read_embedding_tables(const MemoryView &hailo_config_json);

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_EMBEDDING_TABLES_HPP_ */
