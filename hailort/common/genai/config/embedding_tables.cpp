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

static const std::string TOKEN_EMBEDDINGS_ROLE = "token_embeddings";
static const std::string PER_LAYER_INPUTS_ROLE = "per_layer_inputs";
static const std::vector<std::string> EMBEDDING_TABLE_ROLES = {TOKEN_EMBEDDINGS_ROLE, PER_LAYER_INPUTS_ROLE};

static const std::string UINT16_DTYPE_NAME = "uint16";

static const std::vector<std::string> REQUIRED_EMBEDDING_TABLE_KEYS = {TABLE_ROLE_KEY, TABLE_RESOURCE_KEY,
    TABLE_INPUT_LAYER_KEY};
static const std::vector<std::string> OPTIONAL_EMBEDDING_TABLE_KEYS = {TABLE_DTYPE_KEY, TABLE_SHAPE_KEY};

static constexpr size_t EMBEDDING_TABLE_SHAPE_RANK = 2;

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

static hailo_status validate_table_dtype(const nlohmann::json &table_json, const std::string &table_description)
{
    if (!table_json.contains(TABLE_DTYPE_KEY)) {
        return HAILO_SUCCESS;
    }

    std::string dtype_name;
    CHECK_SUCCESS(read_string(table_json, TABLE_DTYPE_KEY, table_description, dtype_name));
    CHECK(UINT16_DTYPE_NAME == dtype_name, HAILO_INVALID_ARGUMENT,
        "'{}' in {} is '{}', which names nothing the runtime gathers. It accepts {}", TABLE_DTYPE_KEY,
        table_description, dtype_name, quoted(UINT16_DTYPE_NAME));

    return HAILO_SUCCESS;
}

static hailo_status validate_table_shape(const nlohmann::json &table_json, const std::string &table_description)
{
    if (!table_json.contains(TABLE_SHAPE_KEY)) {
        return HAILO_SUCCESS;
    }

    std::vector<uint32_t> shape;
    CHECK_SUCCESS(read_unsigned_array(table_json, TABLE_SHAPE_KEY, table_description, shape));
    CHECK(EMBEDDING_TABLE_SHAPE_RANK == shape.size(), HAILO_INVALID_ARGUMENT,
        "'{}' in {} gives {} axes, but a table is rows by columns", TABLE_SHAPE_KEY, table_description, shape.size());
    for (const auto dimension : shape) {
        CHECK(0 != dimension, HAILO_INVALID_ARGUMENT,
            "'{}' in {} holds a zero - a table holds at least one row and one column", TABLE_SHAPE_KEY,
            table_description);
    }

    return HAILO_SUCCESS;
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
    CHECK_SUCCESS(validate_table_dtype(table_json, table_description));
    CHECK_SUCCESS(validate_table_shape(table_json, table_description));

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
