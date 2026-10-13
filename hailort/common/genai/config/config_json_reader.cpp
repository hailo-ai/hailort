/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file config_json_reader.cpp
 * @brief Typed reading of values from a JSON model configuration
 **/

#include "config_json_reader.hpp"

#include "common/utils.hpp"

#include <limits>

namespace hailort
{
namespace genai
{

hailo_status read_unsigned(const nlohmann::json &object_json, const std::string &key, const std::string &object_name,
    uint32_t &value)
{
    if (!object_json.contains(key)) {
        return HAILO_SUCCESS;
    }

    CHECK(object_json[key].is_number_unsigned(), HAILO_INVALID_ARGUMENT,
        "'{}' in {} must be a non-negative whole number, but the model configuration gives a value of type '{}'",
        key, object_name, object_json[key].type_name());
    const auto declared_value = object_json[key].get<uint64_t>();
    CHECK(declared_value <= std::numeric_limits<uint32_t>::max(), HAILO_INVALID_ARGUMENT,
        "'{}' in {} is {}, which does not fit in 32 bits", key, object_name, declared_value);
    value = static_cast<uint32_t>(declared_value);

    return HAILO_SUCCESS;
}

hailo_status read_string(const nlohmann::json &object_json, const std::string &key, const std::string &object_name,
    std::string &value)
{
    if (!object_json.contains(key)) {
        return HAILO_SUCCESS;
    }

    CHECK(object_json[key].is_string(), HAILO_INVALID_ARGUMENT,
        "'{}' in {} must be a string, but the model configuration gives a value of type '{}'", key, object_name,
        object_json[key].type_name());
    value = object_json[key].get<std::string>();

    return HAILO_SUCCESS;
}

hailo_status read_unsigned_array(const nlohmann::json &object_json, const std::string &key,
    const std::string &object_name, std::vector<uint32_t> &values)
{
    if (!object_json.contains(key)) {
        return HAILO_SUCCESS;
    }

    const auto &array_json = object_json[key];
    CHECK(array_json.is_array(), HAILO_INVALID_ARGUMENT,
        "'{}' in {} must be an array, but the model configuration gives a value of type '{}'", key, object_name,
        array_json.type_name());

    std::vector<uint32_t> declared_values;
    declared_values.reserve(array_json.size());
    for (const auto &entry_json : array_json) {
        CHECK(entry_json.is_number_unsigned(), HAILO_INVALID_ARGUMENT,
            "'{}' in {} holds a value of type '{}', but every entry is a non-negative whole number", key,
            object_name, entry_json.type_name());
        const auto entry = entry_json.get<uint64_t>();
        CHECK(entry <= std::numeric_limits<uint32_t>::max(), HAILO_INVALID_ARGUMENT,
            "'{}' in {} holds {}, which does not fit in 32 bits", key, object_name, entry);
        declared_values.push_back(static_cast<uint32_t>(entry));
    }
    values = std::move(declared_values);

    return HAILO_SUCCESS;
}

static hailo_status check_entry_fits_int(const nlohmann::json &entry_json, const std::string &key,
    const std::string &object_name)
{
    const auto is_within_int_range = entry_json.is_number_unsigned() ?
        (entry_json.get<uint64_t>() <= static_cast<uint64_t>(std::numeric_limits<int>::max())) :
        ((entry_json.get<int64_t>() >= std::numeric_limits<int>::min()) &&
            (entry_json.get<int64_t>() <= std::numeric_limits<int>::max()));
    CHECK(is_within_int_range, HAILO_INVALID_ARGUMENT, "'{}' in {} holds {}, which does not fit in 32 bits", key,
        object_name, entry_json.dump());

    return HAILO_SUCCESS;
}

hailo_status read_int_array(const nlohmann::json &object_json, const std::string &key, const std::string &object_name,
    std::vector<int> &values)
{
    if (!object_json.contains(key)) {
        return HAILO_SUCCESS;
    }

    const auto &array_json = object_json[key];
    CHECK(array_json.is_array(), HAILO_INVALID_ARGUMENT,
        "'{}' in {} must be an array, but the model configuration gives a value of type '{}'", key, object_name,
        array_json.type_name());

    std::vector<int> declared_values;
    declared_values.reserve(array_json.size());
    for (const auto &entry_json : array_json) {
        CHECK(entry_json.is_number_integer(), HAILO_INVALID_ARGUMENT,
            "'{}' in {} holds a value of type '{}', but every entry is a whole number", key, object_name,
            entry_json.type_name());
        CHECK_SUCCESS(check_entry_fits_int(entry_json, key, object_name));
        declared_values.push_back(entry_json.get<int>());
    }
    values = std::move(declared_values);

    return HAILO_SUCCESS;
}

hailo_status validate_section_is_object(const nlohmann::json &section_json, const std::string &section_key)
{
    CHECK(section_json.is_object(), HAILO_INVALID_ARGUMENT,
        "'{}' in the model configuration must be an object, but it gives a value of type '{}'", section_key,
        section_json.type_name());

    return HAILO_SUCCESS;
}

} /* namespace genai */
} /* namespace hailort */
