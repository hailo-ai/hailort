/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file embedding_tables.cpp
 * @brief Parsing of the embedding tables a model configuration declares
 **/

#include "config_json_reader.hpp"
#include "common/utils.hpp"

#include <map>

namespace hailort
{
namespace genai
{

static const std::string EMBEDDING_TABLES_KEY = "embeddings";
static const std::string EMBEDDINGS_SUFFIX_KEY = "embeddings";
static const std::string TABLE_ROLE_KEY = "role";
static const std::string TABLE_RESOURCE_KEY = "resource";
static const std::string TABLE_INPUT_LAYER_KEY = "input_layer";
static const std::string TABLE_DTYPE_KEY = "dtype";
static const std::string TABLE_SHAPE_KEY = "shape";
static const std::string TABLE_PACKING_KEY = "packing";
static const std::string TABLE_CODEBOOK_KEY = "codebook";
static const std::string TABLE_CODEBOOK_DTYPE_KEY = "codebook_dtype";
static const std::string TABLE_CODEBOOK_SHAPE_KEY = "codebook_shape";

static const std::string TOKEN_EMBEDDINGS_ROLE = "token_embeddings";
static const std::string PER_LAYER_INPUTS_ROLE = "per_layer_inputs";
static const std::vector<std::string> EMBEDDING_TABLE_ROLES = {TOKEN_EMBEDDINGS_ROLE, PER_LAYER_INPUTS_ROLE};

static const std::string UINT16_DTYPE_NAME = "uint16";
static const std::map<std::string, uint32_t> PACKED_DTYPE_CODE_BITS = {
    {"uint1", 1}, {"uint2", 2}, {"uint4", 4}, {"uint8", 8}};
static const std::string LSB_FIRST_PACKING_NAME = "lsb_first";

static std::vector<std::string> packed_dtype_names()
{
    std::vector<std::string> names;
    for (const auto &packed_dtype : PACKED_DTYPE_CODE_BITS) {
        names.push_back(packed_dtype.first);
    }
    return names;
}

static const std::vector<std::string> PACKED_DTYPE_NAMES = packed_dtype_names();

static const std::vector<std::string> REQUIRED_EMBEDDING_TABLE_KEYS = {TABLE_ROLE_KEY, TABLE_RESOURCE_KEY,
    TABLE_INPUT_LAYER_KEY};
static const std::vector<std::string> PACKED_TABLE_ONLY_KEYS = {TABLE_PACKING_KEY, TABLE_CODEBOOK_DTYPE_KEY,
    TABLE_CODEBOOK_SHAPE_KEY};
static const std::vector<std::string> REQUIRED_PACKED_TABLE_KEYS = {TABLE_DTYPE_KEY, TABLE_PACKING_KEY,
    TABLE_CODEBOOK_SHAPE_KEY};

static std::vector<std::string> optional_embedding_table_keys()
{
    std::vector<std::string> keys = {TABLE_DTYPE_KEY, TABLE_SHAPE_KEY, TABLE_CODEBOOK_KEY};
    keys.insert(keys.end(), PACKED_TABLE_ONLY_KEYS.begin(), PACKED_TABLE_ONLY_KEYS.end());
    return keys;
}

static const std::vector<std::string> OPTIONAL_EMBEDDING_TABLE_KEYS = optional_embedding_table_keys();

static constexpr size_t EMBEDDING_TABLE_SHAPE_RANK = 2;
static constexpr size_t MIN_CODEBOOK_SHAPE_RANK = 2;
static constexpr size_t MAX_CODEBOOK_SHAPE_RANK = 3;

static std::string embedding_table_description(size_t table_index)
{
    return ("embedding table " + std::to_string(table_index));
}

static hailo_status validate_table_keys(const nlohmann::json &table_json, const std::string &table_description)
{
    for (const auto &entry : table_json.items()) {
        const auto is_table_key = contains(REQUIRED_EMBEDDING_TABLE_KEYS, entry.key()) ||
            contains(OPTIONAL_EMBEDDING_TABLE_KEYS, entry.key());
        CHECK(is_table_key, HAILO_INVALID_ARGUMENT,
            "The model configuration's {} declares an unknown key '{}'. A table requires {} and may also give {}",
            table_description, entry.key(), quoted_key_list(REQUIRED_EMBEDDING_TABLE_KEYS),
            quoted_key_list(OPTIONAL_EMBEDDING_TABLE_KEYS));
    }

    for (const auto &key : REQUIRED_EMBEDDING_TABLE_KEYS) {
        CHECK(table_json.contains(key), HAILO_INVALID_ARGUMENT,
            "The model configuration's {} is missing the required key '{}'", table_description, key);
    }

    return HAILO_SUCCESS;
}

static hailo_status read_shape(const nlohmann::json &table_json, const std::string &shape_key,
    const std::string &table_description, std::vector<uint32_t> &shape)
{
    CHECK_SUCCESS(read_unsigned_array(table_json, shape_key, table_description, shape));
    for (const auto dimension : shape) {
        CHECK(0 != dimension, HAILO_INVALID_ARGUMENT,
            "'{}' in {} holds a zero - every axis holds at least one element", shape_key, table_description);
    }

    return HAILO_SUCCESS;
}

static Expected<std::optional<EmbeddingTableShape>> parse_declared_shape(const nlohmann::json &table_json,
    const std::string &table_description)
{
    if (!table_json.contains(TABLE_SHAPE_KEY)) {
        return std::optional<EmbeddingTableShape>();
    }

    std::vector<uint32_t> shape;
    CHECK_SUCCESS(read_shape(table_json, TABLE_SHAPE_KEY, table_description, shape));
    CHECK(EMBEDDING_TABLE_SHAPE_RANK == shape.size(), HAILO_INVALID_ARGUMENT,
        "'{}' in {} gives {} axes, but a table is rows by columns", TABLE_SHAPE_KEY, table_description, shape.size());

    return std::optional<EmbeddingTableShape>(EmbeddingTableShape{shape[0], shape[1]});
}

static hailo_status parse_codebook_shape(const nlohmann::json &table_json, const std::string &table_description,
    EmbeddingTableCodebook &codebook)
{
    std::vector<uint32_t> shape;
    CHECK_SUCCESS(read_shape(table_json, TABLE_CODEBOOK_SHAPE_KEY, table_description, shape));
    CHECK((MIN_CODEBOOK_SHAPE_RANK <= shape.size()) && (MAX_CODEBOOK_SHAPE_RANK >= shape.size()),
        HAILO_INVALID_ARGUMENT, "'{}' in {} gives {} axes, but a codebook shape is rows, then codebooks per row if a "
        "row has more than one, then entries", TABLE_CODEBOOK_SHAPE_KEY, table_description, shape.size());
    const auto entries_per_codebook = uint64_t{1} << codebook.code_bits;
    CHECK(entries_per_codebook == shape.back(), HAILO_INVALID_ARGUMENT,
        "'{}' in {} gives {} entries per codebook, but a {}-bit code indexes {} entries", TABLE_CODEBOOK_SHAPE_KEY,
        table_description, shape.back(), codebook.code_bits, entries_per_codebook);

    codebook.rows = shape.front();
    codebook.codebooks_per_row = (MAX_CODEBOOK_SHAPE_RANK == shape.size()) ? shape[1] : 1;

    return HAILO_SUCCESS;
}

static hailo_status validate_plain_table(const nlohmann::json &table_json, const std::string &table_description)
{
    for (const auto &key : PACKED_TABLE_ONLY_KEYS) {
        CHECK(!table_json.contains(key), HAILO_INVALID_ARGUMENT,
            "The model configuration's {} gives '{}', which only a table that names a '{}' takes", table_description,
            key, TABLE_CODEBOOK_KEY);
    }
    if (table_json.contains(TABLE_DTYPE_KEY)) {
        std::string dtype_name;
        CHECK_SUCCESS(read_string(table_json, TABLE_DTYPE_KEY, table_description, dtype_name));
        CHECK(UINT16_DTYPE_NAME == dtype_name, HAILO_INVALID_ARGUMENT,
            "'{}' in {} is '{}', but a table that names no '{}' holds {} rows", TABLE_DTYPE_KEY, table_description,
            dtype_name, TABLE_CODEBOOK_KEY, quoted(UINT16_DTYPE_NAME));
    }

    return HAILO_SUCCESS;
}

static Expected<uint32_t> read_packed_code_bits(const nlohmann::json &table_json,
    const std::string &table_description)
{
    std::string dtype_name;
    CHECK_SUCCESS(read_string(table_json, TABLE_DTYPE_KEY, table_description, dtype_name));
    const auto packed_dtype = PACKED_DTYPE_CODE_BITS.find(dtype_name);
    CHECK(PACKED_DTYPE_CODE_BITS.end() != packed_dtype, HAILO_INVALID_ARGUMENT,
        "'{}' in {} is '{}', but a table that names a '{}' gives the width of its codes, one of {}", TABLE_DTYPE_KEY,
        table_description, dtype_name, TABLE_CODEBOOK_KEY, quoted_key_list(PACKED_DTYPE_NAMES));

    return Expected<uint32_t>(packed_dtype->second);
}

// A packed table's code width, bit order and codebook layout cannot be inferred from its bytes
static Expected<EmbeddingTableCodebook> parse_codebook(const nlohmann::json &table_json,
    const std::string &table_description)
{
    for (const auto &key : REQUIRED_PACKED_TABLE_KEYS) {
        CHECK(table_json.contains(key), HAILO_INVALID_ARGUMENT,
            "The model configuration's {} names a '{}', which requires '{}'", table_description, TABLE_CODEBOOK_KEY,
            key);
    }

    EmbeddingTableCodebook codebook;
    CHECK_SUCCESS(read_string(table_json, TABLE_CODEBOOK_KEY, table_description, codebook.resource));
    CHECK(!codebook.resource.empty(), HAILO_INVALID_ARGUMENT,
        "'{}' in {} is empty - a packed table names the resource its codes index into", TABLE_CODEBOOK_KEY,
        table_description);
    TRY(codebook.code_bits, read_packed_code_bits(table_json, table_description));

    std::string packing;
    CHECK_SUCCESS(read_string(table_json, TABLE_PACKING_KEY, table_description, packing));
    CHECK(LSB_FIRST_PACKING_NAME == packing, HAILO_INVALID_ARGUMENT,
        "'{}' in {} is '{}', which names no bit order the runtime reads. It accepts {}", TABLE_PACKING_KEY,
        table_description, packing, quoted(LSB_FIRST_PACKING_NAME));

    if (table_json.contains(TABLE_CODEBOOK_DTYPE_KEY)) {
        std::string codebook_dtype;
        CHECK_SUCCESS(read_string(table_json, TABLE_CODEBOOK_DTYPE_KEY, table_description, codebook_dtype));
        CHECK(UINT16_DTYPE_NAME == codebook_dtype, HAILO_INVALID_ARGUMENT,
            "'{}' in {} is '{}', which names nothing the runtime gathers. It accepts {}", TABLE_CODEBOOK_DTYPE_KEY,
            table_description, codebook_dtype, quoted(UINT16_DTYPE_NAME));
    }
    CHECK_SUCCESS(parse_codebook_shape(table_json, table_description, codebook));

    return codebook;
}

static Expected<EmbeddingTableParams> parse_embedding_table(const nlohmann::json &table_json,
    const std::string &table_description)
{
    CHECK_SUCCESS(validate_table_keys(table_json, table_description));

    EmbeddingTableParams table;
    CHECK_SUCCESS(read_string(table_json, TABLE_RESOURCE_KEY, table_description, table.resource));
    CHECK(!table.resource.empty(), HAILO_INVALID_ARGUMENT,
        "'{}' in {} is empty - a table names the resource it gathers from", TABLE_RESOURCE_KEY, table_description);
    CHECK_SUCCESS(read_string(table_json, TABLE_INPUT_LAYER_KEY, table_description, table.input_layer_suffix));
    CHECK(!table.input_layer_suffix.empty(), HAILO_INVALID_ARGUMENT,
        "'{}' in {} is empty - a table names the layer its rows are written to", TABLE_INPUT_LAYER_KEY,
        table_description);
    TRY(table.declared_shape, parse_declared_shape(table_json, table_description));
    if (table_json.contains(TABLE_CODEBOOK_KEY)) {
        TRY(table.codebook, parse_codebook(table_json, table_description));
    } else {
        CHECK_SUCCESS(validate_plain_table(table_json, table_description));
    }

    return table;
}

static Expected<EmbeddingTables> parse_declared_embedding_tables(const nlohmann::json &hailo_config_json)
{
    const auto &tables_json = hailo_config_json[EMBEDDING_TABLES_KEY];
    CHECK(tables_json.is_array(), HAILO_INVALID_ARGUMENT,
        "'{}' in the model configuration must be an array, but it gives a value of type '{}'", EMBEDDING_TABLES_KEY,
        tables_json.type_name());
    CHECK(!tables_json.empty(), HAILO_INVALID_ARGUMENT,
        "'{}' in the model configuration is empty - the array names at least the '{}' table, and a model gathering "
        "only from '{}' omits the array instead", EMBEDDING_TABLES_KEY, TOKEN_EMBEDDINGS_ROLE, INPUT_EMB_BINARY);

    std::optional<EmbeddingTableParams> token_embeddings;
    std::optional<EmbeddingTableParams> per_layer_inputs;
    for (size_t table_index = 0; table_index < tables_json.size(); table_index++) {
        const auto &table_json = tables_json[table_index];
        CHECK(table_json.is_object(), HAILO_INVALID_ARGUMENT,
            "Entry {} of '{}' must be an object, but the model configuration gives a value of type '{}'",
            table_index, EMBEDDING_TABLES_KEY, table_json.type_name());
        const auto table_description = embedding_table_description(table_index);
        TRY(auto table, parse_embedding_table(table_json, table_description));

        std::string role;
        CHECK_SUCCESS(read_string(table_json, TABLE_ROLE_KEY, table_description, role));
        std::optional<EmbeddingTableParams> *role_table = nullptr;
        if (TOKEN_EMBEDDINGS_ROLE == role) {
            role_table = &token_embeddings;
        } else if (PER_LAYER_INPUTS_ROLE == role) {
            role_table = &per_layer_inputs;
        }
        CHECK(nullptr != role_table, HAILO_INVALID_ARGUMENT,
            "'{}' in {} is '{}', which names no role the runtime knows. It accepts {}", TABLE_ROLE_KEY,
            table_description, role, quoted_key_list(EMBEDDING_TABLE_ROLES));
        CHECK(!role_table->has_value(), HAILO_INVALID_ARGUMENT,
            "'{}' in {} repeats the role '{}' - a role takes one table, and an earlier entry already took it",
            TABLE_ROLE_KEY, table_description, role);
        *role_table = std::move(table);
    }

    CHECK(token_embeddings.has_value(), HAILO_INVALID_ARGUMENT,
        "'{}' in the model configuration declares no '{}' table - a declared array names the table every token "
        "is gathered from", EMBEDDING_TABLES_KEY, TOKEN_EMBEDDINGS_ROLE);

    return EmbeddingTables{std::move(*token_embeddings), std::move(per_layer_inputs)};
}

static Expected<std::string> parse_model_wide_embeddings_suffix(const nlohmann::json &hailo_config_json)
{
    std::string embeddings_suffix = INPUT_LAYER_EMBEDDINGS_SUFF;
    if (!hailo_config_json.contains(INPUT_LAYERS_NAMES_SUFFIXES_KEY)) {
        return embeddings_suffix;
    }

    const auto &suffixes_json = hailo_config_json[INPUT_LAYERS_NAMES_SUFFIXES_KEY];
    CHECK_SUCCESS(validate_section_is_object(suffixes_json, INPUT_LAYERS_NAMES_SUFFIXES_KEY));
    CHECK_SUCCESS(read_string(suffixes_json, EMBEDDINGS_SUFFIX_KEY, quoted(INPUT_LAYERS_NAMES_SUFFIXES_KEY),
        embeddings_suffix));

    return embeddings_suffix;
}

Expected<EmbeddingTables> parse_embedding_tables(const nlohmann::json &hailo_config_json)
{
    TRY(auto model_wide_embeddings_suffix, parse_model_wide_embeddings_suffix(hailo_config_json));
    if (!hailo_config_json.contains(EMBEDDING_TABLES_KEY)) {
        EmbeddingTables tables;
        tables.token_embeddings.input_layer_suffix = std::move(model_wide_embeddings_suffix);
        return tables;
    }

    return parse_declared_embedding_tables(hailo_config_json);
}

Expected<EmbeddingTables> read_embedding_tables(const MemoryView &hailo_config_json)
{
    const auto *config_begin = hailo_config_json.data();
    const auto parsed_config = nlohmann::json::parse(config_begin, config_begin + hailo_config_json.size(), nullptr,
        false);
    CHECK(!parsed_config.is_discarded(), HAILO_INVALID_ARGUMENT, "The model configuration is not valid JSON");

    return parse_embedding_tables(parsed_config);
}

} /* namespace genai */
} /* namespace hailort */
