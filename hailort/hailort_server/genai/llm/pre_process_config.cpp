/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pre_process_config.cpp
 * @brief Parsing of the pre-process section of a model configuration
 **/

#include "pre_process_config.hpp"

#include "common/genai/config/config_json_reader.hpp"
#include "common/utils.hpp"

namespace hailort
{
namespace genai
{

static const std::string PRE_PROCESS_PARAMS_KEY = "pre_process_params";
static const std::string ATTENTION_GROUPS_KEY = "attention_groups";

static const std::string KV_CACHE_SIZE_KEY = "kv_cache_size";
static const std::string SLIDING_WINDOW_KEY = "sliding_window";
static const std::string NUM_ATTENTION_HEADS_KEY = "num_attention_heads";
static const std::string NUM_KEY_VALUE_HEADS_KEY = "num_key_value_heads";
static const std::string MROPE_SECTION_KEY = "mrope_section";
static const std::string PREFILL_INPUT_TOKENS_COUNT_KEY = "prefill_input_tokens_count";

static const std::string GROUP_NAME_KEY = "name";
static const std::string MASK_FRAME_REPEAT_KEY = "mask_frame_repeat";
static const std::string ROPE_Q_FRAME_REPEAT_KEY = "rope_q_frame_repeat";
static const std::string ROPE_K_FRAME_REPEAT_KEY = "rope_k_frame_repeat";
static const std::string ROPE_THETA_KEY = "rope_theta";
static const std::string ROPE_THETA_DTYPE_KEY = "rope_theta_dtype";
static const std::string ROPE_THETA_SHAPE_KEY = "rope_theta_shape";
static const std::string GROUP_INPUT_LAYERS_KEY = "input_layers";

static const std::string ATTENTION_MASK_SUFFIX_KEY = "attention_mask";
static const std::string PE_Q_COS_SUFFIX_KEY = "pe_q_cos";
static const std::string PE_Q_SIN_SUFFIX_KEY = "pe_q_sin";
static const std::string PE_K_COS_SUFFIX_KEY = "pe_k_cos";
static const std::string PE_K_SIN_SUFFIX_KEY = "pe_k_sin";

static const std::string FLOAT32_DTYPE_NAME = "float32";

static const std::vector<std::string> ATTENTION_GROUP_LAYER_KEYS = {ATTENTION_MASK_SUFFIX_KEY, PE_Q_COS_SUFFIX_KEY,
    PE_Q_SIN_SUFFIX_KEY, PE_K_COS_SUFFIX_KEY, PE_K_SIN_SUFFIX_KEY};

// Keys a group may omit, each default standing for 'not declared'
static const std::vector<std::string> OPTIONAL_ATTENTION_GROUP_KEYS = {GROUP_NAME_KEY, SLIDING_WINDOW_KEY,
    MROPE_SECTION_KEY, ROPE_THETA_DTYPE_KEY, ROPE_THETA_SHAPE_KEY};

static const std::vector<std::string> REQUIRED_ATTENTION_GROUP_KEYS = {KV_CACHE_SIZE_KEY, MASK_FRAME_REPEAT_KEY,
    ROPE_Q_FRAME_REPEAT_KEY, ROPE_K_FRAME_REPEAT_KEY, ROPE_THETA_KEY, GROUP_INPUT_LAYERS_KEY};

static std::string attention_group_description(const std::string &group_name)
{
    return ("attention group " + quoted(group_name));
}

static hailo_status validate_group_keys(const nlohmann::json &group_json, const std::string &group_name)
{
    for (const auto &entry : group_json.items()) {
        const auto is_group_key = contains(REQUIRED_ATTENTION_GROUP_KEYS, entry.key()) ||
            contains(OPTIONAL_ATTENTION_GROUP_KEYS, entry.key());
        CHECK(is_group_key, HAILO_INVALID_ARGUMENT,
            "Attention group '{}' declares an unknown key '{}'. A group requires {} and may also give {}", group_name,
            entry.key(), quoted_key_list(REQUIRED_ATTENTION_GROUP_KEYS),
            quoted_key_list(OPTIONAL_ATTENTION_GROUP_KEYS));
    }

    return HAILO_SUCCESS;
}

static hailo_status validate_group_input_layer_keys(const nlohmann::json &input_layers_json,
    const std::string &group_name)
{
    for (const auto &entry : input_layers_json.items()) {
        CHECK(contains(ATTENTION_GROUP_LAYER_KEYS, entry.key()), HAILO_INVALID_ARGUMENT,
            "Attention group '{}' declares an unknown input layer '{}'. A group accepts {}", group_name,
            entry.key(), quoted_key_list(ATTENTION_GROUP_LAYER_KEYS));
    }

    return HAILO_SUCCESS;
}

static hailo_status read_group_rope_theta_dtype(const nlohmann::json &group_json,
    const std::string &group_description, hailo_format_type_t &dtype)
{
    if (!group_json.contains(ROPE_THETA_DTYPE_KEY)) {
        return HAILO_SUCCESS;
    }

    std::string dtype_name;
    CHECK_SUCCESS(read_string(group_json, ROPE_THETA_DTYPE_KEY, group_description, dtype_name));

    CHECK(FLOAT32_DTYPE_NAME == dtype_name, HAILO_INVALID_ARGUMENT,
        "'{}' in {} is '{}', which names nothing the runtime knows. It accepts {}", ROPE_THETA_DTYPE_KEY,
        group_description, dtype_name, quoted(FLOAT32_DTYPE_NAME));
    dtype = HAILO_FORMAT_TYPE_FLOAT32;

    return HAILO_SUCCESS;
}

static hailo_status read_group_rope_theta_shape(const nlohmann::json &group_json,
    const std::string &group_description, std::vector<uint32_t> &rope_theta_shape)
{
    if (!group_json.contains(ROPE_THETA_SHAPE_KEY)) {
        return HAILO_SUCCESS;
    }

    CHECK_SUCCESS(read_unsigned_array(group_json, ROPE_THETA_SHAPE_KEY, group_description, rope_theta_shape));

    CHECK(!rope_theta_shape.empty(), HAILO_INVALID_ARGUMENT,
        "'{}' in {} covers no axis - a shape states what the theta resource holds", ROPE_THETA_SHAPE_KEY,
        group_description);
    for (const auto dimension : rope_theta_shape) {
        CHECK(0 != dimension, HAILO_INVALID_ARGUMENT,
            "'{}' in {} holds a zero - a theta holds at least one entry along every axis", ROPE_THETA_SHAPE_KEY,
            group_description);
    }

    return HAILO_SUCCESS;
}

static hailo_status validate_group_has_required_keys(const nlohmann::json &group_json, const std::string &group_name)
{
    for (const auto &key : REQUIRED_ATTENTION_GROUP_KEYS) {
        CHECK(group_json.contains(key), HAILO_INVALID_ARGUMENT,
            "Attention group '{}' is missing the required key '{}'", group_name, key);
    }

    return HAILO_SUCCESS;
}

static Expected<AttentionLayerSuffixes> parse_group_input_layers(const nlohmann::json &group_json,
    const std::string &group_name)
{
    const auto group_description = attention_group_description(group_name);
    const auto &input_layers_json = group_json[GROUP_INPUT_LAYERS_KEY];
    CHECK(input_layers_json.is_object(), HAILO_INVALID_ARGUMENT,
        "'{}' in {} must be an object, but the model configuration gives a value of type '{}'",
        GROUP_INPUT_LAYERS_KEY, group_description, input_layers_json.type_name());
    CHECK_SUCCESS(validate_group_input_layer_keys(input_layers_json, group_name));

    for (const auto &key : ATTENTION_GROUP_LAYER_KEYS) {
        CHECK(input_layers_json.contains(key), HAILO_INVALID_ARGUMENT,
            "Attention group '{}' names no '{}' input layer - a declared group names all of {}", group_name, key,
            quoted_key_list(ATTENTION_GROUP_LAYER_KEYS));
    }

    AttentionLayerSuffixes suffixes;
    CHECK_SUCCESS(read_string(input_layers_json, ATTENTION_MASK_SUFFIX_KEY, group_description, suffixes.mask));
    CHECK_SUCCESS(read_string(input_layers_json, PE_Q_COS_SUFFIX_KEY, group_description, suffixes.pe_q_cos));
    CHECK_SUCCESS(read_string(input_layers_json, PE_Q_SIN_SUFFIX_KEY, group_description, suffixes.pe_q_sin));
    CHECK_SUCCESS(read_string(input_layers_json, PE_K_COS_SUFFIX_KEY, group_description, suffixes.pe_k_cos));
    CHECK_SUCCESS(read_string(input_layers_json, PE_K_SIN_SUFFIX_KEY, group_description, suffixes.pe_k_sin));

    return suffixes;
}

static hailo_status check_field_is_set(bool is_set, const std::string &group_name, const std::string &field_name)
{
    CHECK(is_set, HAILO_INVALID_ARGUMENT,
        "Attention group '{}' gives an empty or zero '{}' - a declared group describes each of its layers and sizes",
        group_name, field_name);

    return HAILO_SUCCESS;
}

static hailo_status validate_group_is_complete(const AttentionGroupParams &group, const std::string &group_name)
{
    CHECK_SUCCESS(check_field_is_set(!group.suffixes.mask.empty(), group_name, ATTENTION_MASK_SUFFIX_KEY));
    CHECK_SUCCESS(check_field_is_set(!group.suffixes.pe_q_cos.empty(), group_name, PE_Q_COS_SUFFIX_KEY));
    CHECK_SUCCESS(check_field_is_set(!group.suffixes.pe_q_sin.empty(), group_name, PE_Q_SIN_SUFFIX_KEY));
    CHECK_SUCCESS(check_field_is_set(!group.suffixes.pe_k_cos.empty(), group_name, PE_K_COS_SUFFIX_KEY));
    CHECK_SUCCESS(check_field_is_set(!group.suffixes.pe_k_sin.empty(), group_name, PE_K_SIN_SUFFIX_KEY));
    CHECK_SUCCESS(check_field_is_set(0 != group.kv_cache_size, group_name, KV_CACHE_SIZE_KEY));
    CHECK_SUCCESS(check_field_is_set(0 != group.mask_frame_repeat, group_name, MASK_FRAME_REPEAT_KEY));
    CHECK_SUCCESS(check_field_is_set(0 != group.rope_q_frame_repeat, group_name, ROPE_Q_FRAME_REPEAT_KEY));
    CHECK_SUCCESS(check_field_is_set(0 != group.rope_k_frame_repeat, group_name, ROPE_K_FRAME_REPEAT_KEY));

    return HAILO_SUCCESS;
}

static Expected<AttentionGroupParams> parse_attention_group(const nlohmann::json &group_json, size_t group_index)
{
    CHECK(group_json.is_object(), HAILO_INVALID_ARGUMENT,
        "Entry {} of '{}' must be an object, but the model configuration gives a value of type '{}'", group_index,
        ATTENTION_GROUPS_KEY, group_json.type_name());

    AttentionGroupParams group;
    CHECK_SUCCESS(read_string(group_json, GROUP_NAME_KEY, attention_group_description(std::to_string(group_index)),
        group.name));
    const auto group_name = attention_group_name(group, group_index);
    const auto group_description = attention_group_description(group_name);

    CHECK_SUCCESS(validate_group_keys(group_json, group_name));
    CHECK_SUCCESS(validate_group_has_required_keys(group_json, group_name));
    TRY(group.suffixes, parse_group_input_layers(group_json, group_name));

    CHECK_SUCCESS(read_unsigned(group_json, KV_CACHE_SIZE_KEY, group_description, group.kv_cache_size));
    CHECK_SUCCESS(read_unsigned(group_json, SLIDING_WINDOW_KEY, group_description, group.sliding_window));
    CHECK_SUCCESS(read_unsigned(group_json, MASK_FRAME_REPEAT_KEY, group_description, group.mask_frame_repeat));
    CHECK_SUCCESS(read_unsigned(group_json, ROPE_Q_FRAME_REPEAT_KEY, group_description, group.rope_q_frame_repeat));
    CHECK_SUCCESS(read_unsigned(group_json, ROPE_K_FRAME_REPEAT_KEY, group_description, group.rope_k_frame_repeat));
    CHECK_SUCCESS(read_string(group_json, ROPE_THETA_KEY, group_description, group.rope_theta));
    CHECK(!group.rope_theta.empty(), HAILO_INVALID_ARGUMENT,
        "Attention group '{}' gives an empty '{}' - a declared group names the resource holding its RoPE theta",
        group_name, ROPE_THETA_KEY);
    CHECK_SUCCESS(read_group_rope_theta_dtype(group_json, group_description, group.rope_theta_dtype));
    CHECK_SUCCESS(read_group_rope_theta_shape(group_json, group_description, group.rope_theta_shape));
    CHECK_SUCCESS(read_int_array(group_json, MROPE_SECTION_KEY, group_description, group.mrope_section));

    CHECK_SUCCESS(validate_group_is_complete(group, group_name));
    CHECK((0 == group.sliding_window) || (group.sliding_window <= group.kv_cache_size), HAILO_INVALID_ARGUMENT,
        "Attention group '{}' declares a '{}' of {} over a '{}' of {} - a window cannot reach past the cache it "
        "slides over", group_name, SLIDING_WINDOW_KEY, group.sliding_window, KV_CACHE_SIZE_KEY, group.kv_cache_size);

    return group;
}

static hailo_status parse_attention_groups(const nlohmann::json &hailo_config_json,
    std::vector<AttentionGroupParams> &attention_groups)
{
    const auto &groups_json = hailo_config_json[ATTENTION_GROUPS_KEY];
    CHECK(groups_json.is_array(), HAILO_INVALID_ARGUMENT,
        "'{}' in the model configuration must be an array, but it gives a value of type '{}'", ATTENTION_GROUPS_KEY,
        groups_json.type_name());
    CHECK(!groups_json.empty(), HAILO_INVALID_ARGUMENT,
        "'{}' in the model configuration is empty - the array names at least one attention group, and a model that "
        "describes its single group through the flat '{}' keys omits the array instead", ATTENTION_GROUPS_KEY,
        PRE_PROCESS_PARAMS_KEY);

    attention_groups.reserve(groups_json.size());
    for (size_t group_index = 0; group_index < groups_json.size(); group_index++) {
        TRY(auto group, parse_attention_group(groups_json[group_index], group_index));
        attention_groups.emplace_back(std::move(group));
    }

    return HAILO_SUCCESS;
}

static hailo_status validate_attention_groups_placement(const nlohmann::json &hailo_config_json)
{
    if (hailo_config_json.contains(PRE_PROCESS_PARAMS_KEY)) {
        CHECK(!hailo_config_json[PRE_PROCESS_PARAMS_KEY].contains(ATTENTION_GROUPS_KEY), HAILO_INVALID_ARGUMENT,
            "The model configuration declares '{}' under '{}', where it is not read - an attention group list is "
            "model-wide and belongs at the top level", ATTENTION_GROUPS_KEY, PRE_PROCESS_PARAMS_KEY);
    }

    return HAILO_SUCCESS;
}

static hailo_status parse_flat_attention_params(const nlohmann::json &hailo_config_json, PreProcessParams &params)
{
    if (!hailo_config_json.contains(PRE_PROCESS_PARAMS_KEY)) {
        return HAILO_SUCCESS;
    }

    const auto &params_json = hailo_config_json[PRE_PROCESS_PARAMS_KEY];
    CHECK_SUCCESS(validate_section_is_object(params_json, PRE_PROCESS_PARAMS_KEY));
    const auto description = quoted(PRE_PROCESS_PARAMS_KEY);
    CHECK_SUCCESS(read_unsigned(params_json, KV_CACHE_SIZE_KEY, description, params.kv_cache_size));
    CHECK_SUCCESS(read_unsigned(params_json, NUM_ATTENTION_HEADS_KEY, description, params.num_attention_heads));
    CHECK_SUCCESS(read_unsigned(params_json, NUM_KEY_VALUE_HEADS_KEY, description, params.num_key_value_heads));
    CHECK_SUCCESS(read_unsigned(params_json, PREFILL_INPUT_TOKENS_COUNT_KEY, description,
        params.prefill_input_tokens_count));
    CHECK_SUCCESS(read_int_array(params_json, MROPE_SECTION_KEY, description, params.mrope_section));

    return HAILO_SUCCESS;
}

static hailo_status parse_model_wide_layer_suffixes(const nlohmann::json &hailo_config_json,
    InputLayersNamesSuffixes &suffixes)
{
    if (!hailo_config_json.contains(INPUT_LAYERS_NAMES_SUFFIXES_KEY)) {
        return HAILO_SUCCESS;
    }

    const auto &suffixes_json = hailo_config_json[INPUT_LAYERS_NAMES_SUFFIXES_KEY];
    CHECK_SUCCESS(validate_section_is_object(suffixes_json, INPUT_LAYERS_NAMES_SUFFIXES_KEY));
    const auto description = quoted(INPUT_LAYERS_NAMES_SUFFIXES_KEY);
    CHECK_SUCCESS(read_string(suffixes_json, ATTENTION_MASK_SUFFIX_KEY, description, suffixes.attention.mask));
    CHECK_SUCCESS(read_string(suffixes_json, PE_Q_COS_SUFFIX_KEY, description, suffixes.attention.pe_q_cos));
    CHECK_SUCCESS(read_string(suffixes_json, PE_Q_SIN_SUFFIX_KEY, description, suffixes.attention.pe_q_sin));
    CHECK_SUCCESS(read_string(suffixes_json, PE_K_COS_SUFFIX_KEY, description, suffixes.attention.pe_k_cos));
    CHECK_SUCCESS(read_string(suffixes_json, PE_K_SIN_SUFFIX_KEY, description, suffixes.attention.pe_k_sin));

    return HAILO_SUCCESS;
}

static hailo_status apply_rope_layout(RopeLayout rope_layout, PreProcessParams &params)
{
    if (RopeLayout::TextOnly == rope_layout) {
        params.mrope_section.clear();
        return HAILO_SUCCESS;
    }

    for (size_t group_index = 0; group_index < params.attention_groups.size(); group_index++) {
        const auto &group = params.attention_groups[group_index];
        CHECK(!group.mrope_section.empty(), HAILO_INVALID_ARGUMENT,
            "Attention group '{}' gives no '{}' - a multimodal model partitions every group's rope into sections",
            attention_group_name(group, group_index), MROPE_SECTION_KEY);
    }

    CHECK(!params.attention_groups.empty() || !params.mrope_section.empty(), HAILO_INVALID_ARGUMENT,
        "'{}' under '{}' is empty - a multimodal model's flat keys have to partition its rope into sections",
        MROPE_SECTION_KEY, PRE_PROCESS_PARAMS_KEY);

    return HAILO_SUCCESS;
}

Expected<PreProcessConfig> parse_pre_process_config(const nlohmann::json &hailo_config_json, RopeLayout rope_layout)
{
    PreProcessConfig config;

    CHECK_SUCCESS(validate_attention_groups_placement(hailo_config_json));

    CHECK_SUCCESS(parse_flat_attention_params(hailo_config_json, config.params));
    CHECK_SUCCESS(parse_model_wide_layer_suffixes(hailo_config_json, config.suffixes));
    TRY(config.embedding_tables, parse_embedding_tables(hailo_config_json));

    if (hailo_config_json.contains(ATTENTION_GROUPS_KEY)) {
        CHECK_SUCCESS(parse_attention_groups(hailo_config_json, config.params.attention_groups));
    }
    CHECK_SUCCESS(apply_rope_layout(rope_layout, config.params));

    return config;
}

} /* namespace genai */
} /* namespace hailort */
