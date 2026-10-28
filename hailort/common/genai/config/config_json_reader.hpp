/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file config_json_reader.hpp
 * @brief Typed reading of values from a JSON model configuration
 **/

#ifndef _HAILO_GENAI_CONFIG_JSON_READER_HPP_
#define _HAILO_GENAI_CONFIG_JSON_READER_HPP_

#include "hailo/hailort.h"
#include "embedding_tables.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace hailort
{
namespace genai
{

static const std::string INPUT_LAYERS_NAMES_SUFFIXES_KEY = "input_layers_names_suffixes";

inline std::string quoted(const std::string &key)
{
    return ("'" + key + "'");
}

// {"a", "b"} -> "'a', 'b'"
inline std::string quoted_key_list(const std::vector<std::string> &keys)
{
    std::string list;
    for (const auto &key : keys) {
        if (!list.empty()) {
            list += ", ";
        }
        list += quoted(key);
    }

    return list;
}

// Read a value without throwing: a mistyped or out-of-range value returns HAILO_INVALID_ARGUMENT, and a missing
// key is not an error, leaving the value as the caller set it
hailo_status read_unsigned(const nlohmann::json &object_json, const std::string &key, const std::string &object_name,
    uint32_t &value);
hailo_status read_string(const nlohmann::json &object_json, const std::string &key, const std::string &object_name,
    std::string &value);
hailo_status read_unsigned_array(const nlohmann::json &object_json, const std::string &key,
    const std::string &object_name, std::vector<uint32_t> &values);
hailo_status read_int_array(const nlohmann::json &object_json, const std::string &key, const std::string &object_name,
    std::vector<int> &values);

hailo_status validate_section_is_object(const nlohmann::json &section_json, const std::string &section_key);

// Without an embeddings array, the model-wide embeddings layer suffix overrides the default token table's layer
Expected<EmbeddingTables> parse_embedding_tables(const nlohmann::json &hailo_config_json);

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_CONFIG_JSON_READER_HPP_ */
