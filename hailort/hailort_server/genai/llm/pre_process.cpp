/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pre_process.cpp
 * @brief Implementation for LLM pre processing
 **/

#include "pre_process.hpp"
#include "hailo/hailort.h"
#include "hailo/hailort_common.hpp"
#include "hailo/quantization.hpp"
#include "common/genai/constants.hpp"
#include "utils.hpp"

#include <algorithm>
#include <fstream>
#include <limits>

namespace hailort
{
namespace genai
{

static const int POSITIONAL_EMBEDDING_ROWS = 3;
static const int POSITIONAL_EMBEDDING_COLS = 1;
static constexpr int THETA_SIGNS_COUNT = 2;
// What an attended and a masked mask element dequantize to, making the mask multiplicative
static constexpr float32_t MASK_ATTENDED_VALUE = 1.0f;
static constexpr float32_t MASK_MASKED_VALUE = 0.0f;

void LLMPreProcess::tile_along_last_axis(const Eigen::Tensor<float32_t, 4, Eigen::RowMajor> &input, int repeat_count, MemoryView &layer_buffer)
{
    auto height = input.dimension(1);
    auto width = input.dimension(2);
    auto features = input.dimension(3);

    assert((height * width * features * repeat_count * sizeof(float32_t)) == layer_buffer.size());

    Eigen::TensorMap<Eigen::Tensor<float32_t, 4, Eigen::RowMajor>> output_tensor(
        layer_buffer.as_pointer<float32_t>(), height, width, repeat_count, features);

    for (int repeat = 0; repeat < repeat_count; ++repeat) {
        output_tensor.chip(repeat, 2) = input.chip(0, 0);
    }
}

// TODO: HRT-16646 - Remove this flow
Eigen::VectorXf LLMPreProcess::generate_default_theta()
{
    Eigen::VectorXf theta(TILE_SIZE * THETA_SIGNS_COUNT);
    for (int i = 0; i < TILE_SIZE; i++) {
        float32_t exp = static_cast<float32_t>(std::pow(1e6, -i / 64.0));
        theta(i) = exp * -1.0f;
        theta(i + TILE_SIZE) = exp;
    }

    return theta;
}

Eigen::VectorXf LLMPreProcess::generate_theta_from_memview(const MemoryView theta)
{
    size_t num_floats = theta.size() / sizeof(float);
    Eigen::Map<const Eigen::VectorXf> vec(reinterpret_cast<const float*>(theta.data()), num_floats);
    return vec;
}

static hailo_status validate_input_frame_size(const std::map<std::string, size_t> &inputs_frame_sizes,
    const std::string &suffix, size_t expected_frame_size, const std::string &expectation_source)
{
    TRY(const auto layer_name, get_layer_name_from_suffix<size_t>(suffix, inputs_frame_sizes));
    const auto frame_size = inputs_frame_sizes.at(layer_name);
    CHECK(frame_size == expected_frame_size, HAILO_INVALID_ARGUMENT,
        "Input layer '{}' has a frame size of {} bytes, but the model configuration implies {} bytes ({}). "
        "Pre-process writes the configured layout, so it would write past the layer buffer",
        layer_name, frame_size, expected_frame_size, expectation_source);

    return HAILO_SUCCESS;
}

static bool is_global_group(const AttentionGroupParams &group)
{
    return 0 == group.sliding_window;
}

// A group that names no sections gets a single section spanning each theta half
static std::vector<int> resolve_mrope_section(const std::vector<int> &declared_mrope_section, size_t theta_size)
{
    if (!declared_mrope_section.empty()) {
        return declared_mrope_section;
    }

    return {static_cast<int>(theta_size / THETA_SIGNS_COUNT)};
}

static hailo_status validate_group_rope_partition(const AttentionGroupParams &group, const std::string &group_name,
    size_t theta_size)
{
    size_t mrope_columns_count = 0;
    for (const auto section : resolve_mrope_section(group.mrope_section, theta_size)) {
        CHECK(section >= 0, HAILO_INVALID_ARGUMENT,
            "Attention group '{}' declares an 'mrope_section' holding a negative entry ({})", group_name, section);
        mrope_columns_count += (THETA_SIGNS_COUNT * static_cast<size_t>(section));
    }
    CHECK(mrope_columns_count == theta_size, HAILO_INVALID_ARGUMENT,
        "Attention group '{}' declares an 'mrope_section' covering {} rope columns but theta holds {} - the "
        "sections are laid out twice, so twice their sum must match",
        group_name, mrope_columns_count, theta_size);

    return HAILO_SUCCESS;
}

static const AttentionMaskValues &phase_mask_values(const AttentionGroupMaskValues &group_mask_values,
    InferencePhase phase)
{
    return (InferencePhase::TokenByToken == phase) ? group_mask_values.tbt : group_mask_values.prefill;
}

static hailo_status validate_group_frame_sizes(const std::map<std::string, size_t> &inputs_frame_sizes,
    const AttentionGroupParams &group, const AttentionMaskValues &mask_values, uint32_t input_tokens_count,
    size_t theta_size)
{
    const auto tokens_count = static_cast<size_t>(input_tokens_count);
    const auto mask_element_bytes = HailoRTCommon::get_data_bytes(mask_values.element_type);
    const auto mask_frame_size = tokens_count * group.kv_cache_size * group.mask_frame_repeat * mask_element_bytes;
    CHECK_SUCCESS(validate_input_frame_size(inputs_frame_sizes, group.suffixes.mask, mask_frame_size,
        fmt::format("input tokens {} * kv_cache_size {} * mask_frame_repeat {} * mask element bytes {}",
            input_tokens_count, group.kv_cache_size, group.mask_frame_repeat, mask_element_bytes)));

    const auto rope_frame_size_per_repeat = sizeof(float32_t) * tokens_count * theta_size;
    const auto query_rope_frame_size = rope_frame_size_per_repeat * group.rope_q_frame_repeat;
    const auto query_rope_source = fmt::format(
        "sizeof(float32) * input tokens {} * theta size {} * rope_q_frame_repeat {}",
        input_tokens_count, theta_size, group.rope_q_frame_repeat);
    CHECK_SUCCESS(validate_input_frame_size(inputs_frame_sizes, group.suffixes.pe_q_cos, query_rope_frame_size,
        query_rope_source));
    CHECK_SUCCESS(validate_input_frame_size(inputs_frame_sizes, group.suffixes.pe_q_sin, query_rope_frame_size,
        query_rope_source));

    const auto key_rope_frame_size = rope_frame_size_per_repeat * group.rope_k_frame_repeat;
    const auto key_rope_source = fmt::format(
        "sizeof(float32) * input tokens {} * theta size {} * rope_k_frame_repeat {}",
        input_tokens_count, theta_size, group.rope_k_frame_repeat);
    CHECK_SUCCESS(validate_input_frame_size(inputs_frame_sizes, group.suffixes.pe_k_cos, key_rope_frame_size,
        key_rope_source));
    CHECK_SUCCESS(validate_input_frame_size(inputs_frame_sizes, group.suffixes.pe_k_sin, key_rope_frame_size,
        key_rope_source));

    return HAILO_SUCCESS;
}

static hailo_status validate_gathered_layer_frame_size(const std::map<std::string, size_t> &inputs_frame_sizes,
    const std::string &layer_suffix, uint32_t input_tokens_count, uint32_t layer_features,
    const std::string &features_description)
{
    const auto frame_size = static_cast<size_t>(input_tokens_count) * layer_features * sizeof(uint16_t);
    return validate_input_frame_size(inputs_frame_sizes, layer_suffix, frame_size,
        fmt::format("input tokens {} * {} {} * sizeof(uint16)", input_tokens_count, features_description,
            layer_features));
}

static hailo_status validate_frame_sizes(const std::map<std::string, size_t> &inputs_frame_sizes,
    const std::string &embeddings_suffix, const std::vector<AttentionGroupParams> &attention_groups,
    uint32_t input_tokens_count, uint32_t embeddings_layer_features, const std::vector<Eigen::VectorXf> &group_thetas,
    const std::vector<AttentionGroupMaskValues> &group_mask_values, InferencePhase phase)
{
    CHECK_SUCCESS(validate_gathered_layer_frame_size(inputs_frame_sizes, embeddings_suffix, input_tokens_count,
        embeddings_layer_features, "embeddings features"));

    for (size_t group_index = 0; group_index < attention_groups.size(); group_index++) {
        CHECK_SUCCESS(validate_group_frame_sizes(inputs_frame_sizes, attention_groups[group_index],
            phase_mask_values(group_mask_values[group_index], phase), input_tokens_count,
            static_cast<size_t>(group_thetas[group_index].size())));
    }

    return HAILO_SUCCESS;
}

static bool is_writable_mask_element_type(hailo_format_type_t element_type)
{
    return (HAILO_FORMAT_TYPE_UINT8 == element_type) || (HAILO_FORMAT_TYPE_UINT16 == element_type);
}

static std::string inference_phase_name(InferencePhase phase)
{
    return (InferencePhase::TokenByToken == phase) ? "token-by-token" : "prefill";
}

static hailo_status validate_mask_values_count(const std::vector<AttentionGroupMaskValues> &group_mask_values,
    const std::vector<AttentionGroupParams> &attention_groups)
{
    CHECK(group_mask_values.size() == attention_groups.size(), HAILO_INVALID_ARGUMENT,
        "Got {} mask value pairs for {} attention groups - every group's mask is quantized through its own layer",
        group_mask_values.size(), attention_groups.size());
    return HAILO_SUCCESS;
}

static hailo_status validate_group_mask_values(const std::vector<AttentionGroupMaskValues> &group_mask_values,
    const std::vector<AttentionGroupParams> &attention_groups)
{
    CHECK_SUCCESS(validate_mask_values_count(group_mask_values, attention_groups));
    for (size_t group_index = 0; group_index < attention_groups.size(); group_index++) {
        for (const auto phase : {InferencePhase::Prefill, InferencePhase::TokenByToken}) {
            const auto element_type = phase_mask_values(group_mask_values[group_index], phase).element_type;
            CHECK(is_writable_mask_element_type(element_type), HAILO_INVALID_ARGUMENT,
                "Attention group '{}' {} mask layer holds {} elements, which pre-process cannot write",
                attention_group_name(attention_groups[group_index], group_index), inference_phase_name(phase),
                HailoRTCommon::get_format_type_str(element_type));
        }
    }

    return HAILO_SUCCESS;
}

static hailo_status validate_theta_count(const std::vector<Eigen::VectorXf> &group_thetas,
    const std::vector<AttentionGroupParams> &attention_groups)
{
    CHECK(group_thetas.size() == attention_groups.size(), HAILO_INVALID_ARGUMENT,
        "Got {} rope thetas for {} attention groups - every group rotates through its own theta",
        group_thetas.size(), attention_groups.size());
    return HAILO_SUCCESS;
}

// The frame-size maps are fixed at create(), so checking them once against the configuration the writes are laid
// out from keeps every later write inside its buffer
hailo_status LLMPreProcess::validate_inputs_layout(const std::map<std::string, size_t> &prefill_inputs_frame_size,
    const std::map<std::string, size_t> &tbt_inputs_frame_size, const EmbeddingTables &embedding_tables,
    uint32_t per_layer_inputs_features, const PreProcessParams &pre_process_params,
    const std::vector<AttentionGroupParams> &attention_groups, uint32_t embeddings_layer_features,
    hailo_format_type_t embeddings_layer_type, const std::vector<Eigen::VectorXf> &group_thetas,
    const std::vector<AttentionGroupMaskValues> &group_mask_values)
{
    // TODO: HRT-16156 - Support dtypes for embeddings layer
    CHECK(HAILO_FORMAT_TYPE_UINT16 == embeddings_layer_type, HAILO_INVALID_ARGUMENT,
        "Creating Pre-Process failed, only uint16 embeddings data type is currently supported");

    CHECK_SUCCESS(validate_theta_count(group_thetas, attention_groups));
    for (size_t group_index = 0; group_index < attention_groups.size(); group_index++) {
        const auto &group = attention_groups[group_index];
        const auto group_name = attention_group_name(group, group_index);
        const auto theta_size = static_cast<size_t>(group_thetas[group_index].size());
        CHECK(0 == (theta_size % THETA_SIGNS_COUNT), HAILO_INVALID_ARGUMENT,
            "Attention group '{}' rope theta resource '{}' holds an odd number of entries ({})", group_name,
            group.rope_theta, theta_size);
        CHECK_SUCCESS(validate_group_rope_partition(group, group_name, theta_size));
    }

    CHECK_SUCCESS(validate_mask_values_count(group_mask_values, attention_groups));

    CHECK(!prefill_inputs_frame_size.empty(), HAILO_INVALID_ARGUMENT,
        "The model carries no prefill network, which pre-process requires");
    const auto &token_layer_suffix = embedding_tables.token_embeddings.input_layer_suffix;
    CHECK_SUCCESS(validate_frame_sizes(prefill_inputs_frame_size, token_layer_suffix, attention_groups,
        pre_process_params.prefill_input_tokens_count, embeddings_layer_features, group_thetas, group_mask_values,
        InferencePhase::Prefill));

    CHECK(!tbt_inputs_frame_size.empty(), HAILO_INVALID_ARGUMENT, "{}", MISSING_TBT_NETWORK_ERROR);
    CHECK_SUCCESS(validate_frame_sizes(tbt_inputs_frame_size, token_layer_suffix, attention_groups,
        TBT_INPUT_TOKENS_SIZE, embeddings_layer_features, group_thetas, group_mask_values,
        InferencePhase::TokenByToken));

    if (embedding_tables.per_layer_inputs) {
        const auto &per_layer_inputs_suffix = embedding_tables.per_layer_inputs->input_layer_suffix;
        CHECK_SUCCESS(validate_gathered_layer_frame_size(prefill_inputs_frame_size, per_layer_inputs_suffix,
            pre_process_params.prefill_input_tokens_count, per_layer_inputs_features, "per-layer input features"));
        CHECK_SUCCESS(validate_gathered_layer_frame_size(tbt_inputs_frame_size, per_layer_inputs_suffix,
            TBT_INPUT_TOKENS_SIZE, per_layer_inputs_features, "per-layer input features"));
    }

    return HAILO_SUCCESS;
}

Expected<uint32_t> context_capacity(const std::vector<AttentionGroupParams> &attention_groups)
{
    static constexpr auto NO_GLOBAL_GROUP = std::numeric_limits<uint32_t>::max();
    auto global_capacity = NO_GLOBAL_GROUP;
    for (const auto &group : attention_groups) {
        if (is_global_group(group)) {
            global_capacity = std::min(global_capacity, group.kv_cache_size);
        }
    }
    CHECK(NO_GLOBAL_GROUP != global_capacity, HAILO_INVALID_ARGUMENT,
        "Every attention group of the model slides a window - a model needs at least one group with no "
        "'sliding_window', whose cache is the context the model can hold");

    return global_capacity;
}

template<typename MaskElement>
static MaskElement quantize_saturated(float32_t value, const hailo_quant_info_t &quant_info)
{
    static constexpr uint32_t SINGLE_VALUE_COUNT = 1;
    float32_t quantized = 0;
    Quantization::quantize_input_buffer<float32_t, float32_t>(&value, &quantized, SINGLE_VALUE_COUNT, quant_info);
    // A value inside limvals can still quantize past the element's range, and casting a float past it is undefined
    const auto element_max = static_cast<float32_t>(std::numeric_limits<MaskElement>::max());
    return static_cast<MaskElement>(std::clamp(quantized, 0.0f, element_max));
}

Expected<AttentionMaskValues> quantize_attention_mask_values(hailo_format_type_t element_type,
    const hailo_quant_info_t &quant_info)
{
    CHECK(is_writable_mask_element_type(element_type), HAILO_INVALID_ARGUMENT,
        "Pre-process cannot write an attention mask of {} elements", HailoRTCommon::get_format_type_str(element_type));

    AttentionMaskValues mask_values;
    mask_values.element_type = element_type;
    if (HAILO_FORMAT_TYPE_UINT16 == element_type) {
        mask_values.attended = quantize_saturated<uint16_t>(MASK_ATTENDED_VALUE, quant_info);
        mask_values.masked = quantize_saturated<uint16_t>(MASK_MASKED_VALUE, quant_info);
    } else {
        mask_values.attended = quantize_saturated<uint8_t>(MASK_ATTENDED_VALUE, quant_info);
        mask_values.masked = quantize_saturated<uint8_t>(MASK_MASKED_VALUE, quant_info);
    }
    CHECK(mask_values.attended != mask_values.masked, HAILO_INVALID_ARGUMENT,
        "Mask layer quantizes attended {} and masked {} to the same code {}", MASK_ATTENDED_VALUE, MASK_MASKED_VALUE,
        mask_values.attended);
    return mask_values;
}

std::optional<CacheRollbackLimit> find_exceeded_cache_rollback_limit(
    const std::vector<AttentionGroupParams> &attention_groups, uint32_t prefill_input_tokens_count,
    size_t cache_usage_after_rollback, size_t rollback_tokens_count)
{
    for (size_t group_index = 0; group_index < attention_groups.size(); group_index++) {
        const auto &group = attention_groups[group_index];
        uint64_t read_tokens_limit = group.kv_cache_size;
        if (!is_global_group(group)) {
            // The next prefill re-feeds up to prefill_input_tokens_count - 1 retained tokens ahead of the new one, and
            // the first re-fed row attends sliding_window - 1 tokens behind itself
            const auto next_prefill_reach =
                static_cast<uint64_t>(group.sliding_window) + prefill_input_tokens_count - 2;
            read_tokens_limit = next_prefill_reach;
        }
        const auto retained_tokens_count = std::min<uint64_t>(cache_usage_after_rollback, read_tokens_limit);
        if ((static_cast<uint64_t>(rollback_tokens_count) + retained_tokens_count) > group.kv_cache_size) {
            const auto absorbable_rollback_tokens_count =
                (retained_tokens_count < group.kv_cache_size) ? (group.kv_cache_size - retained_tokens_count) : 0;
            return CacheRollbackLimit{attention_group_name(group, group_index), retained_tokens_count,
                absorbable_rollback_tokens_count};
        }
    }

    return std::nullopt;
}

std::vector<AttentionGroupParams> build_attention_groups(const InputLayersNamesSuffixes &input_layers_names_suffixes,
    const PreProcessParams &pre_process_params)
{
    if (!pre_process_params.attention_groups.empty()) {
        return pre_process_params.attention_groups;
    }

    AttentionGroupParams single_group;
    single_group.suffixes = input_layers_names_suffixes.attention;
    single_group.kv_cache_size = pre_process_params.kv_cache_size;
    single_group.mask_frame_repeat = pre_process_params.num_attention_heads;
    single_group.rope_q_frame_repeat = pre_process_params.num_attention_heads;
    single_group.rope_k_frame_repeat = pre_process_params.num_key_value_heads;
    single_group.mrope_section = pre_process_params.mrope_section;
    single_group.rope_theta = THETA;

    return {single_group};
}

static Expected<AttentionLayerNames> resolve_layer_names(const AttentionLayerSuffixes &suffixes,
    const std::map<std::string, size_t> &inputs_frame_sizes)
{
    TRY(auto mask, get_layer_name_from_suffix<size_t>(suffixes.mask, inputs_frame_sizes));
    TRY(auto pe_q_cos, get_layer_name_from_suffix<size_t>(suffixes.pe_q_cos, inputs_frame_sizes));
    TRY(auto pe_q_sin, get_layer_name_from_suffix<size_t>(suffixes.pe_q_sin, inputs_frame_sizes));
    TRY(auto pe_k_cos, get_layer_name_from_suffix<size_t>(suffixes.pe_k_cos, inputs_frame_sizes));
    TRY(auto pe_k_sin, get_layer_name_from_suffix<size_t>(suffixes.pe_k_sin, inputs_frame_sizes));

    return AttentionLayerNames{std::move(mask), std::move(pe_q_cos), std::move(pe_q_sin), std::move(pe_k_cos),
        std::move(pe_k_sin)};
}

static std::vector<int> repeat_mrope_section(const std::vector<int> &mrope_section)
{
    std::vector<int> repeated;
    repeated.reserve(mrope_section.size() * THETA_SIGNS_COUNT);
    for (int repeat = 0; repeat < THETA_SIGNS_COUNT; repeat++) {
        repeated.insert(repeated.end(), mrope_section.begin(), mrope_section.end());
    }

    return repeated;
}

static std::vector<int> cumulative_sections(const std::vector<int> &sections)
{
    std::vector<int> cumulative(sections.size());
    std::partial_sum(sections.begin(), sections.end(), cumulative.begin());
    return cumulative;
}

Expected<std::vector<AttentionGroupContext>> LLMPreProcess::discover_layer_groups(
    const std::vector<Eigen::VectorXf> &group_thetas, const std::map<std::string, size_t> &prefill_inputs_frame_size,
    const std::map<std::string, size_t> &tbt_inputs_frame_size,
    const std::vector<AttentionGroupMaskValues> &group_mask_values,
    const std::vector<AttentionGroupParams> &attention_groups)
{
    CHECK_SUCCESS(validate_group_mask_values(group_mask_values, attention_groups));
    CHECK_SUCCESS(validate_theta_count(group_thetas, attention_groups));

    std::vector<AttentionGroupContext> groups;
    for (size_t group_index = 0; group_index < attention_groups.size(); group_index++) {
        const auto &group_params = attention_groups[group_index];
        AttentionGroupContext group;
        group.params = group_params;
        TRY(group.prefill_layers, resolve_layer_names(group_params.suffixes, prefill_inputs_frame_size));
        TRY(group.tbt_layers, resolve_layer_names(group_params.suffixes, tbt_inputs_frame_size));
        group.theta = group_thetas[group_index];
        group.repeated_mrope_section = repeat_mrope_section(
            resolve_mrope_section(group_params.mrope_section, static_cast<size_t>(group.theta.size())));
        group.cumulative_mrope_section = cumulative_sections(group.repeated_mrope_section);
        group.mask_values = group_mask_values[group_index];
        groups.push_back(std::move(group));
    }

    return groups;
}

static const AttentionLayerNames &group_layer_names(const AttentionGroupContext &group, InferencePhase phase)
{
    return (InferencePhase::TokenByToken == phase) ? group.tbt_layers : group.prefill_layers;
}

static Expected<MemoryView> input_layer_buffer(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer,
    const layer_name_t &layer_name, const std::string &layer_suffix)
{
    const auto layer = layer_name_to_input_buffer.find(layer_name);
    CHECK(layer != layer_name_to_input_buffer.end(), HAILO_INVALID_ARGUMENT,
        "Could not find layer {} (attention group suffix '{}') among the pre-process input buffers", layer_name,
        layer_suffix);

    return MemoryView(layer->second);
}

Expected<std::unique_ptr<LLMPreProcess>> LLMPreProcess::create(
    const std::map<std::string, size_t> &prefill_inputs_frame_size, const std::map<std::string, size_t> &tbt_inputs_frame_size,
    const std::vector<Eigen::VectorXf> &group_thetas, uint32_t embeddings_layer_features,
    hailo_format_type_t embeddings_layer_type, const EmbeddingTables &embedding_tables,
    uint32_t per_layer_inputs_features, const std::vector<AttentionGroupMaskValues> &group_mask_values,
    const InputLayersNamesSuffixes &input_layers_names_suffixes, const PreProcessParams &pre_process_params,
    const std::vector<AttentionGroupParams> &attention_groups)
{
    CHECK_SUCCESS(validate_inputs_layout(prefill_inputs_frame_size, tbt_inputs_frame_size,
        embedding_tables, per_layer_inputs_features, pre_process_params, attention_groups, embeddings_layer_features,
        embeddings_layer_type, group_thetas, group_mask_values));
    TRY(auto groups, discover_layer_groups(group_thetas, prefill_inputs_frame_size, tbt_inputs_frame_size,
        group_mask_values, attention_groups));

    auto cols = embeddings_layer_features;
    eigen_matrix_2d_u16_t local_cached_embeddings = eigen_matrix_2d_u16_t(pre_process_params.prefill_input_tokens_count, cols);

    auto ptr = make_unique_nothrow<LLMPreProcess>(std::move(local_cached_embeddings), prefill_inputs_frame_size,
        tbt_inputs_frame_size, embedding_tables, per_layer_inputs_features, input_layers_names_suffixes,
        pre_process_params, std::move(groups));
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY); // Consider returning different status
    return ptr;
}

LLMPreProcess::LLMPreProcess(eigen_matrix_2d_u16_t &&local_cached_embeddings,
    const std::map<std::string, size_t> &prefill_inputs_frame_size, const std::map<std::string, size_t> &tbt_inputs_frame_size,
    const EmbeddingTables &embedding_tables, uint32_t per_layer_inputs_features,
    const InputLayersNamesSuffixes &input_layers_names_suffixes, const PreProcessParams &pre_process_params,
    std::vector<AttentionGroupContext> &&groups) :
        m_cache_usage_size(0),
        m_local_cached_embeddings(std::move(local_cached_embeddings)),
        m_local_cached_per_layer_inputs(pre_process_params.prefill_input_tokens_count, per_layer_inputs_features),
        m_prefill_inputs_frame_size(prefill_inputs_frame_size),
        m_tbt_inputs_frame_size(tbt_inputs_frame_size),
        m_current_timestamp_value(0),
        m_groups(std::move(groups)),
        m_embedding_tables(embedding_tables),
        m_input_layers_names_suffixes(input_layers_names_suffixes),
        m_params(pre_process_params)
{
    // Allocate position_ids tensor
    int position_ids_height = POSITIONAL_EMBEDDING_ROWS;
    int position_ids_width = POSITIONAL_EMBEDDING_COLS;
    int position_ids_features = m_params.prefill_input_tokens_count;
    m_local_cached_pos_ids = Eigen::Tensor<uint32_t, 4, Eigen::RowMajor>(position_ids_height, position_ids_width, position_ids_features, 1);
}

// TODO: HRT-16156 - The embeddings and per-layer inputs layers are always uint16. Support generic in the future.
static void write_cache_bottom_rows(const eigen_matrix_2d_u16_t &local_cache, MemoryView &layer_buffer,
    uint32_t number_of_tokens)
{
    Eigen::Map<eigen_matrix_2d_u16_t> mapped_matrix(
        reinterpret_cast<uint16_t*>(layer_buffer.data()), number_of_tokens, local_cache.cols());

    mapped_matrix.bottomRows(number_of_tokens) = local_cache.bottomRows(number_of_tokens);
}

static hailo_status append_rows_to_cache(eigen_matrix_2d_u16_t &local_cache,
    const std::vector<EmbeddingViewWrapper> &row_views)
{
    const auto rows_count = static_cast<int>(row_views.size());

    const auto head_size = local_cache.rows() - rows_count;
    local_cache.topRows(head_size) = local_cache.bottomRows(head_size);

    for (int row_index = 0; row_index < rows_count; ++row_index) {
        auto cache_row = local_cache.row(head_size + row_index);
        const auto cache_row_bytes = static_cast<size_t>(cache_row.size()) * sizeof(uint16_t);
        CHECK_SUCCESS(row_views[row_index].copy_to(MemoryView(cache_row.data(), cache_row_bytes)));
    }

    return HAILO_SUCCESS;
}

static hailo_status validate_rows_width(const std::vector<EmbeddingViewWrapper> &row_views,
    const eigen_matrix_2d_u16_t &local_cache, const std::string &rows_kind)
{
    const auto single_row_bytes = static_cast<size_t>(local_cache.cols() * sizeof(uint16_t));
    for (size_t row_index = 0; row_index < row_views.size(); ++row_index) {
        CHECK(row_views[row_index].size() == single_row_bytes, HAILO_INVALID_ARGUMENT,
            "{} row {} has {} bytes, but the cache expects {} bytes per row", rows_kind, row_index,
            row_views[row_index].size(), single_row_bytes);
    }

    return HAILO_SUCCESS;
}

static Expected<MemoryView> embedding_table_layer_buffer(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer,
    const EmbeddingTableParams &embedding_table)
{
    TRY(const auto layer_name, get_layer_name_from_suffix<MemoryView>(embedding_table.input_layer_suffix,
        layer_name_to_input_buffer));
    return MemoryView(layer_name_to_input_buffer.at(layer_name));
}

void LLMPreProcess::prepare_embeddings_input(MemoryView &layer_buffer, uint32_t number_of_tokens)
{
    write_cache_bottom_rows(m_local_cached_embeddings, layer_buffer, number_of_tokens);
}

int LLMPreProcess::layer_input_tokens_count(InferencePhase phase) const
{
    return (InferencePhase::TokenByToken == phase) ? TBT_INPUT_TOKENS_SIZE :
        static_cast<int>(m_params.prefill_input_tokens_count);
}

hailo_status LLMPreProcess::prepare_attention_mask_inputs(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer,
    InferencePhase phase)
{
    for (const auto &group : m_groups) {
        TRY(auto layer_buffer, input_layer_buffer(layer_name_to_input_buffer,
            group_layer_names(group, phase).mask, group.params.suffixes.mask));
        write_attention_mask(group, phase, layer_buffer);
    }

    return HAILO_SUCCESS;
}

// TODO: HRT-16225 - Optimization
template<typename MaskElement>
static void fill_attention_mask(const AttentionGroupParams &group, const AttentionMaskValues &mask_values,
    int mask_cache_usage, int layer_input_tokens_size, MemoryView &layer_buffer)
{
    using mask_matrix_t = Eigen::Matrix<MaskElement, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
    const auto attended = static_cast<MaskElement>(mask_values.attended);
    const auto masked = static_cast<MaskElement>(mask_values.masked);
    const int padding_rows_count = std::max(layer_input_tokens_size - mask_cache_usage, 0);
    const int attending_rows_count = std::min(mask_cache_usage, layer_input_tokens_size);
    const int group_total_rows = padding_rows_count + attending_rows_count;
    const int group_total_cols = group.kv_cache_size;
    assert(static_cast<size_t>(group_total_rows * group_total_cols * group.mask_frame_repeat) * sizeof(MaskElement) ==
        layer_buffer.size());
    Eigen::Map<mask_matrix_t> attention_mask(layer_buffer.as_pointer<MaskElement>(), group_total_rows,
        group_total_cols * group.mask_frame_repeat);

    if (padding_rows_count > 0) {
        attention_mask.block(0, 0, padding_rows_count, group_total_cols).setConstant(attended);
    }

    // A cache ring is presented linearized oldest-to-newest, so its unused columns are always at the front of the
    // row - the mask needs a width, never a rotation.
    const int first_occupied_col = group_total_cols - mask_cache_usage;
    const int window_size = is_global_group(group) ? group_total_cols : static_cast<int>(group.sliding_window);

    for (int attending_row = 0; attending_row < attending_rows_count; attending_row++) {
        const int self_attention_col = (group_total_cols - attending_rows_count) + attending_row;
        const int band_lower_col = std::max(first_occupied_col, (self_attention_col - window_size) + 1);
        const int band_cols_count = (self_attention_col - band_lower_col) + 1;

        auto mask_row = attention_mask.row(padding_rows_count + attending_row);
        mask_row.segment(0, band_lower_col).setConstant(masked);
        mask_row.segment(band_lower_col, band_cols_count).setConstant(attended);
        mask_row.segment(self_attention_col + 1, (group_total_cols - self_attention_col) - 1).setConstant(masked);
    }

    for (uint32_t repeat = 1; repeat < group.mask_frame_repeat; repeat++) {
        attention_mask.block(0, repeat * group_total_cols, group_total_rows, group_total_cols) =
            attention_mask.block(0, 0, group_total_rows, group_total_cols);
    }
}

void LLMPreProcess::write_attention_mask(const AttentionGroupContext &group, InferencePhase phase,
    MemoryView &layer_buffer)
{
    const auto &mask_values = phase_mask_values(group.mask_values, phase);
    const int layer_input_tokens_size = layer_input_tokens_count(phase);
    const int mask_cache_usage = std::min(static_cast<uint32_t>(m_cache_usage_size), group.params.kv_cache_size);
    if (HAILO_FORMAT_TYPE_UINT16 == mask_values.element_type) {
        fill_attention_mask<uint16_t>(group.params, mask_values, mask_cache_usage, layer_input_tokens_size,
            layer_buffer);
    } else {
        assert(HAILO_FORMAT_TYPE_UINT8 == mask_values.element_type);
        fill_attention_mask<uint8_t>(group.params, mask_values, mask_cache_usage, layer_input_tokens_size,
            layer_buffer);
    }
}

void LLMPreProcess::incremental_positional_embed(int start_index, int tokens_count)
{
    int position_ids_height = static_cast<int>(m_local_cached_pos_ids.dimension(0));
    assert(1 == static_cast<int>(m_local_cached_pos_ids.dimension(1)));

    for (int t = 0; t < tokens_count; ++t) {
        for (int h = 0; h < position_ids_height; ++h) {
            m_local_cached_pos_ids(h, 0, start_index + t, 0) = static_cast<uint32_t>(m_current_timestamp_value);
        }
        ++m_current_timestamp_value;
    }
}

hailo_status LLMPreProcess::fill_positional_embed_inputs(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer,
    InferencePhase phase)
{
    for (const auto &group : m_groups) {
        const auto &layer_names = group_layer_names(group, phase);
        const auto &suffixes = group.params.suffixes;
        const auto rope_pair = angular_positional_embed(group, phase);
        const auto &rope_cos = rope_pair.first;
        const auto &rope_sin = rope_pair.second;

        TRY(auto q_cos_buffer, input_layer_buffer(layer_name_to_input_buffer, layer_names.pe_q_cos, suffixes.pe_q_cos));
        TRY(auto q_sin_buffer, input_layer_buffer(layer_name_to_input_buffer, layer_names.pe_q_sin, suffixes.pe_q_sin));
        TRY(auto k_cos_buffer, input_layer_buffer(layer_name_to_input_buffer, layer_names.pe_k_cos, suffixes.pe_k_cos));
        TRY(auto k_sin_buffer, input_layer_buffer(layer_name_to_input_buffer, layer_names.pe_k_sin, suffixes.pe_k_sin));

        tile_along_last_axis(rope_cos, group.params.rope_q_frame_repeat, q_cos_buffer);
        tile_along_last_axis(rope_sin, group.params.rope_q_frame_repeat, q_sin_buffer);
        tile_along_last_axis(rope_cos, group.params.rope_k_frame_repeat, k_cos_buffer);
        tile_along_last_axis(rope_sin, group.params.rope_k_frame_repeat, k_sin_buffer);
    }

    return HAILO_SUCCESS;
}

std::pair<Eigen::Tensor<float32_t, 4, Eigen::RowMajor>, Eigen::Tensor<float32_t, 4, Eigen::RowMajor>>
    LLMPreProcess::angular_positional_embed(const AttentionGroupContext &group, InferencePhase phase)
{
    const auto layer_input_tokens_size = layer_input_tokens_count(phase);
    int position_ids_height = static_cast<int>(m_local_cached_pos_ids.dimension(0));
    int position_ids_width = static_cast<int>(m_local_cached_pos_ids.dimension(1));
    int position_ids_features = static_cast<int>(m_local_cached_pos_ids.dimension(2));

    Eigen::Tensor<float32_t, 4, Eigen::RowMajor> rope_cos(position_ids_height, position_ids_width, layer_input_tokens_size, group.theta.size());
    Eigen::Tensor<float32_t, 4, Eigen::RowMajor> rope_sin(position_ids_height, position_ids_width, layer_input_tokens_size, group.theta.size());

    int current_col = 0;

    for (size_t section = 0; section < group.repeated_mrope_section.size(); ++section) {
        int start_idx = (section == 0) ? 0 : group.cumulative_mrope_section[section - 1];
        int slice_size = group.repeated_mrope_section[section];
        int section_mod_3 = static_cast<int>(section % 3);

        for (int j = 0; j < position_ids_width; ++j) {
            for (int k = 0; k < layer_input_tokens_size; ++k) {
                int src_idx = (InferencePhase::TokenByToken == phase) ? (position_ids_features - 1) : k;

                float32_t pos_value = static_cast<float32_t>(m_local_cached_pos_ids(section_mod_3, j, src_idx, 0));
                for (int l = 0; l < slice_size; ++l) {
                    float32_t angle = group.theta[start_idx + l] * pos_value;
                    rope_cos(0, j, k, current_col + l) = std::cos(angle);
                    rope_sin(0, j, k, current_col + l) = std::sin(angle);
                }
            }
        }

        current_col += slice_size;
    }

    return std::make_pair(rope_cos, rope_sin);
}

hailo_status LLMPreProcess::prepare_positional_embed_inputs(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer,
    InferencePhase phase, int input_tokens_count)
{
    shift_local_cached_positional_embeds(input_tokens_count);

    int position_ids_features = static_cast<int>(m_local_cached_pos_ids.dimension(2));

    auto pos_ids_start_index = static_cast<int>(position_ids_features - input_tokens_count);

    incremental_positional_embed(pos_ids_start_index, input_tokens_count);

    return fill_positional_embed_inputs(layer_name_to_input_buffer, phase);
}

hailo_status LLMPreProcess::update_cache_from_embeddings(const std::vector<EmbeddingViewWrapper> &embedding_rows_views)
{
    CHECK_SUCCESS(append_rows_to_cache(m_local_cached_embeddings, embedding_rows_views));
    m_cache_usage_size += embedding_rows_views.size();
    return HAILO_SUCCESS;
}

hailo_status LLMPreProcess::validate_input_rows(const std::vector<EmbeddingViewWrapper> &token_rows,
    const std::vector<EmbeddingViewWrapper> &per_layer_inputs_rows) const
{
    CHECK_SUCCESS(validate_per_layer_inputs_count(per_layer_inputs_rows, token_rows.size()));
    CHECK_SUCCESS(validate_rows_width(token_rows, m_local_cached_embeddings, "Token embedding"));
    CHECK_SUCCESS(validate_rows_width(per_layer_inputs_rows, m_local_cached_per_layer_inputs, "Per-layer input"));

    return HAILO_SUCCESS;
}

// One per-layer row per token keeps each per-layer row at its token's cache position
hailo_status LLMPreProcess::validate_per_layer_inputs_count(
    const std::vector<EmbeddingViewWrapper> &per_layer_inputs_rows, size_t token_rows_count) const
{
    const auto has_per_layer_inputs_table = m_embedding_tables.per_layer_inputs.has_value();
    const auto expected_rows_count = has_per_layer_inputs_table ? token_rows_count : 0;
    CHECK(per_layer_inputs_rows.size() == expected_rows_count, HAILO_INVALID_ARGUMENT,
        "Got {} per-layer input rows for {} tokens, but the model declares {} per-layer inputs table, so it expects {}",
        per_layer_inputs_rows.size(), token_rows_count, has_per_layer_inputs_table ? "a" : "no",
        expected_rows_count);

    return HAILO_SUCCESS;
}

Expected<MemoryView> LLMPreProcess::per_layer_inputs_buffer(
    std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer) const
{
    if (!m_embedding_tables.per_layer_inputs) {
        return MemoryView();
    }

    return embedding_table_layer_buffer(layer_name_to_input_buffer, *m_embedding_tables.per_layer_inputs);
}

hailo_status LLMPreProcess::prepare_per_layer_inputs(MemoryView &layer_buffer,
    const std::vector<EmbeddingViewWrapper> &per_layer_inputs_rows, InferencePhase phase)
{
    if (!m_embedding_tables.per_layer_inputs) {
        return HAILO_SUCCESS;
    }

    CHECK_SUCCESS(append_rows_to_cache(m_local_cached_per_layer_inputs, per_layer_inputs_rows));
    write_cache_bottom_rows(m_local_cached_per_layer_inputs, layer_buffer,
        static_cast<uint32_t>(layer_input_tokens_count(phase)));

    return HAILO_SUCCESS;
}

hailo_status LLMPreProcess::validate_inputs(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer, const std::map<std::string, size_t> &expected_sizes)
{
    for (const auto &input : layer_name_to_input_buffer) {
        CHECK(input.second.size() == expected_sizes.at(input.first), HAILO_INVALID_ARGUMENT,
            "Size of input {} is not as expected Got = {}, expected = {}", input.first, input.second.size(), expected_sizes.at(input.first));
    }
    return HAILO_SUCCESS;
}

void LLMPreProcess::shift_local_cached_positional_embeds(int tokens_count)
{
    int position_ids_height = static_cast<int>(m_local_cached_pos_ids.dimension(0));
    int position_ids_width = static_cast<int>(m_local_cached_pos_ids.dimension(1));
    int position_ids_features = static_cast<int>(m_local_cached_pos_ids.dimension(2));

    // Define the range for the source and destination views
    Eigen::array<int, 4> source_start = {0, 0, tokens_count, 0};
    Eigen::array<int, 4> source_size = {position_ids_height, position_ids_width, position_ids_features - tokens_count, 1};
    Eigen::array<int, 4> dest_start = {0, 0, 0, 0};
    Eigen::array<int, 4> dest_size = {position_ids_height, position_ids_width, position_ids_features - tokens_count, 1};
    // Shift the tensor
    m_local_cached_pos_ids.slice(dest_start, dest_size) = m_local_cached_pos_ids.slice(source_start, source_size);
}

hailo_status LLMPreProcess::prepare_inputs_prefill(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer,
    const std::vector<EmbeddingViewWrapper> &input_tokens_embeddings,
    const std::vector<EmbeddingViewWrapper> &input_tokens_per_layer_inputs)
{
    int num_rows = static_cast<int>(input_tokens_embeddings.size());
    CHECK(static_cast<size_t>(num_rows) <= m_params.prefill_input_tokens_count, HAILO_INVALID_ARGUMENT,
        "Preparing prefill inputs failed. embeddings rows must be lower then {}", m_params.prefill_input_tokens_count);
    CHECK_SUCCESS(validate_inputs(layer_name_to_input_buffer, m_prefill_inputs_frame_size)); // TODO (HRT-16660): when tried to mix between prefill and tbt sizes this check didnt fail
    CHECK_SUCCESS(validate_input_rows(input_tokens_embeddings, input_tokens_per_layer_inputs));
    TRY(auto embeddings_buffer, embedding_table_layer_buffer(layer_name_to_input_buffer,
        m_embedding_tables.token_embeddings));
    TRY(auto per_layer_buffer, per_layer_inputs_buffer(layer_name_to_input_buffer));

    CHECK_SUCCESS(update_cache_from_embeddings(input_tokens_embeddings));
    prepare_embeddings_input(embeddings_buffer, m_params.prefill_input_tokens_count);
    CHECK_SUCCESS(prepare_per_layer_inputs(per_layer_buffer, input_tokens_per_layer_inputs, InferencePhase::Prefill));

    CHECK_SUCCESS(prepare_attention_mask_inputs(layer_name_to_input_buffer, InferencePhase::Prefill));
    CHECK_SUCCESS(prepare_positional_embed_inputs(layer_name_to_input_buffer, InferencePhase::Prefill, num_rows));

    return HAILO_SUCCESS;
}

hailo_status LLMPreProcess::prepare_inputs_tbt(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer,
    const std::vector<EmbeddingViewWrapper> &input_token_embedding,
    const std::vector<EmbeddingViewWrapper> &input_token_per_layer_inputs)
{
    CHECK_SUCCESS(validate_inputs(layer_name_to_input_buffer, m_tbt_inputs_frame_size));
    CHECK_SUCCESS(validate_input_rows(input_token_embedding, input_token_per_layer_inputs));
    TRY(auto embeddings_buffer, embedding_table_layer_buffer(layer_name_to_input_buffer,
        m_embedding_tables.token_embeddings));
    TRY(auto per_layer_buffer, per_layer_inputs_buffer(layer_name_to_input_buffer));

    assert(input_token_embedding.size() == 1);
    CHECK_SUCCESS(update_cache_from_embeddings(input_token_embedding));
    prepare_embeddings_input(embeddings_buffer, TBT_INPUT_TOKENS_SIZE);
    CHECK_SUCCESS(prepare_per_layer_inputs(per_layer_buffer, input_token_per_layer_inputs,
        InferencePhase::TokenByToken));

    CHECK_SUCCESS(prepare_attention_mask_inputs(layer_name_to_input_buffer, InferencePhase::TokenByToken));
    CHECK_SUCCESS(prepare_positional_embed_inputs(layer_name_to_input_buffer, InferencePhase::TokenByToken, 1));

    return HAILO_SUCCESS;
}

void LLMPreProcess::reset_local_cache()
{
    m_cache_usage_size = 0;
    m_current_timestamp_value = 0;
}

// TODO: HRT-16261 - Use layers info
bool LLMPreProcess::is_positional_embed_layer(const std::string &name,
    const std::vector<AttentionGroupParams> &attention_groups)
{
    for (const auto &group : attention_groups) {
        const auto &suffixes = group.suffixes;
        if ((!suffixes.pe_q_cos.empty() && has_suffix(name, suffixes.pe_q_cos)) ||
            (!suffixes.pe_q_sin.empty() && has_suffix(name, suffixes.pe_q_sin)) ||
            (!suffixes.pe_k_cos.empty() && has_suffix(name, suffixes.pe_k_cos)) ||
            (!suffixes.pe_k_sin.empty() && has_suffix(name, suffixes.pe_k_sin)))
        {
            return true;
        }
    }

    return false;
}

bool LLMPreProcess::is_deepstack_layer(const std::string &name, const InputLayersNamesSuffixes &input_layers_names_suffixes)
{
    if ((!input_layers_names_suffixes.deepstack_layer1.empty() && has_suffix(name, input_layers_names_suffixes.deepstack_layer1)) ||
        (!input_layers_names_suffixes.deepstack_layer2.empty() && has_suffix(name, input_layers_names_suffixes.deepstack_layer2)) ||
        (!input_layers_names_suffixes.deepstack_layer3.empty() && has_suffix(name, input_layers_names_suffixes.deepstack_layer3)))
    {
        return true;
    }

    return false;
}

PreProcessCache LLMPreProcess::get_local_cache() const
{
    return {m_cache_usage_size, m_local_cached_embeddings, m_local_cached_per_layer_inputs, m_local_cached_pos_ids,
        m_current_timestamp_value};
}

void LLMPreProcess::set_local_cache(const PreProcessCache &cache)
{
    m_cache_usage_size = cache.usage_size;
    m_local_cached_embeddings = cache.embeddings;
    m_local_cached_per_layer_inputs = cache.per_layer_inputs;
    m_local_cached_pos_ids = cache.pos_ids;
    m_current_timestamp_value = cache.timestamp_value;
}


} /* namespace genai */
} /* namespace hailort */
