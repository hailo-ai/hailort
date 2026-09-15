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
#include "utils.hpp"
#include <fstream>

namespace hailort
{
namespace genai
{

static const int POSITIONAL_EMBEDDING_ROWS = 3;
static const int POSITIONAL_EMBEDDING_COLS = 1;
static constexpr int THETA_SIGNS_COUNT = 2;

void LLMPreProcess::tile_along_last_axis(const Eigen::Tensor<float32_t, 4, Eigen::RowMajor> &input, int heads_count, MemoryView &layer_buffer)
{
    auto height = input.dimension(1);
    auto width = input.dimension(2);
    auto features = input.dimension(3);

    assert((height * width * features * heads_count * sizeof(float32_t)) == layer_buffer.size());

    Eigen::TensorMap<Eigen::Tensor<float32_t, 4, Eigen::RowMajor>> output_tensor(
        layer_buffer.as_pointer<float32_t>(), height, width, heads_count, features);

    for (int head = 0; head < heads_count; ++head) {
        output_tensor.chip(head, 2) = input.chip(0, 0);
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

static hailo_status check_field_is_set(bool is_set, size_t group_index, const char *field_name)
{
    CHECK(is_set, HAILO_INVALID_ARGUMENT,
        "Attention group {} leaves '{}' unset - a configuration that declares groups has to describe each one in "
        "full", group_index, field_name);

    return HAILO_SUCCESS;
}

static hailo_status validate_group_is_complete(const AttentionGroupParams &group, size_t group_index)
{
    CHECK_SUCCESS(check_field_is_set(!group.suffixes.mask.empty(), group_index, "attention_mask"));
    CHECK_SUCCESS(check_field_is_set(!group.suffixes.pe_q_cos.empty(), group_index, "pe_q_cos"));
    CHECK_SUCCESS(check_field_is_set(!group.suffixes.pe_q_sin.empty(), group_index, "pe_q_sin"));
    CHECK_SUCCESS(check_field_is_set(!group.suffixes.pe_k_cos.empty(), group_index, "pe_k_cos"));
    CHECK_SUCCESS(check_field_is_set(!group.suffixes.pe_k_sin.empty(), group_index, "pe_k_sin"));
    CHECK_SUCCESS(check_field_is_set(0 != group.kv_cache_size, group_index, "kv_cache_size"));
    CHECK_SUCCESS(check_field_is_set(0 != group.num_attention_heads, group_index, "num_attention_heads"));
    CHECK_SUCCESS(check_field_is_set(0 != group.num_key_value_heads, group_index, "num_key_value_heads"));
    CHECK_SUCCESS(check_field_is_set(!group.mrope_section.empty(), group_index, "mrope_section"));

    return HAILO_SUCCESS;
}

static hailo_status validate_group_rope_partition(const AttentionGroupParams &group, size_t theta_size)
{
    size_t mrope_columns_count = 0;
    for (const auto section : group.mrope_section) {
        CHECK(section >= 0, HAILO_INVALID_ARGUMENT,
            "'mrope_section' in the model configuration holds a negative entry ({})", section);
        mrope_columns_count += (THETA_SIGNS_COUNT * static_cast<size_t>(section));
    }
    CHECK(mrope_columns_count == theta_size, HAILO_INVALID_ARGUMENT,
        "'mrope_section' covers {} rope columns but theta holds {} - the sections are laid out twice, "
        "so twice their sum must match",
        mrope_columns_count, theta_size);

    return HAILO_SUCCESS;
}

static hailo_status validate_group_frame_sizes(const std::map<std::string, size_t> &inputs_frame_sizes,
    const AttentionGroupParams &group, uint32_t input_tokens_count, size_t theta_size)
{
    const auto tokens_count = static_cast<size_t>(input_tokens_count);
    const auto mask_frame_size = tokens_count * group.kv_cache_size * group.num_attention_heads;
    CHECK_SUCCESS(validate_input_frame_size(inputs_frame_sizes, group.suffixes.mask, mask_frame_size,
        fmt::format("input tokens {} * kv_cache_size {} * num_attention_heads {}", input_tokens_count,
            group.kv_cache_size, group.num_attention_heads)));

    const auto rope_frame_size_per_head = sizeof(float32_t) * tokens_count * theta_size;
    const auto query_rope_frame_size = rope_frame_size_per_head * group.num_attention_heads;
    const auto query_rope_source = fmt::format(
        "sizeof(float32) * input tokens {} * theta size {} * num_attention_heads {}",
        input_tokens_count, theta_size, group.num_attention_heads);
    CHECK_SUCCESS(validate_input_frame_size(inputs_frame_sizes, group.suffixes.pe_q_cos, query_rope_frame_size,
        query_rope_source));
    CHECK_SUCCESS(validate_input_frame_size(inputs_frame_sizes, group.suffixes.pe_q_sin, query_rope_frame_size,
        query_rope_source));

    const auto key_rope_frame_size = rope_frame_size_per_head * group.num_key_value_heads;
    const auto key_rope_source = fmt::format(
        "sizeof(float32) * input tokens {} * theta size {} * num_key_value_heads {}",
        input_tokens_count, theta_size, group.num_key_value_heads);
    CHECK_SUCCESS(validate_input_frame_size(inputs_frame_sizes, group.suffixes.pe_k_cos, key_rope_frame_size,
        key_rope_source));
    CHECK_SUCCESS(validate_input_frame_size(inputs_frame_sizes, group.suffixes.pe_k_sin, key_rope_frame_size,
        key_rope_source));

    return HAILO_SUCCESS;
}

static hailo_status validate_frame_sizes(const std::map<std::string, size_t> &inputs_frame_sizes,
    const std::string &embeddings_suffix, const std::vector<AttentionGroupParams> &attention_groups,
    uint32_t input_tokens_count, uint32_t embeddings_layer_features, size_t theta_size)
{
    const auto embeddings_frame_size = static_cast<size_t>(input_tokens_count) * embeddings_layer_features *
        sizeof(uint16_t);
    CHECK_SUCCESS(validate_input_frame_size(inputs_frame_sizes, embeddings_suffix, embeddings_frame_size,
        fmt::format("input tokens {} * embeddings features {} * sizeof(uint16)", input_tokens_count,
            embeddings_layer_features)));

    for (const auto &group : attention_groups) {
        CHECK_SUCCESS(validate_group_frame_sizes(inputs_frame_sizes, group, input_tokens_count, theta_size));
    }

    return HAILO_SUCCESS;
}

// The frame-size maps are fixed at create(), so checking them once against the configuration the writes are laid
// out from keeps every later write inside its buffer
hailo_status LLMPreProcess::validate_inputs_layout(const std::map<std::string, size_t> &prefill_inputs_frame_size,
    const std::map<std::string, size_t> &tbt_inputs_frame_size, const InputLayersNamesSuffixes &input_names_suffixes,
    const PreProcessParams &pre_process_params, const std::vector<AttentionGroupParams> &attention_groups,
    uint32_t embeddings_layer_features, hailo_format_type_t embeddings_layer_type, size_t theta_size)
{
    // TODO: HRT-16156 - Support dtypes for embeddings layer
    CHECK(HAILO_FORMAT_TYPE_UINT16 == embeddings_layer_type, HAILO_INVALID_ARGUMENT,
        "Creating Pre-Process failed, only uint16 embeddings data type is currently supported");

    CHECK(0 == (theta_size % THETA_SIGNS_COUNT), HAILO_INVALID_ARGUMENT,
        "The rope theta resource holds an odd number of entries ({})", theta_size);

    const auto &declared_groups = pre_process_params.attention_groups;
    for (size_t group_index = 0; group_index < declared_groups.size(); group_index++) {
        CHECK_SUCCESS(validate_group_is_complete(declared_groups[group_index], group_index));
    }

    // TODO: HRT-20850 - measure each group against its own theta size rather than the model-wide one
    for (const auto &group : attention_groups) {
        CHECK_SUCCESS(validate_group_rope_partition(group, theta_size));
    }

    CHECK(!prefill_inputs_frame_size.empty(), HAILO_INVALID_ARGUMENT,
        "The model carries no prefill network, which pre-process requires");
    CHECK_SUCCESS(validate_frame_sizes(prefill_inputs_frame_size, input_names_suffixes.embeddings, attention_groups,
        pre_process_params.prefill_input_tokens_count, embeddings_layer_features, theta_size));

    CHECK(!tbt_inputs_frame_size.empty(), HAILO_INVALID_ARGUMENT,
        "The model carries no token-by-token network, which pre-process requires");
    CHECK_SUCCESS(validate_frame_sizes(tbt_inputs_frame_size, input_names_suffixes.embeddings, attention_groups,
        TBT_INPUT_TOKENS_SIZE, embeddings_layer_features, theta_size));

    return HAILO_SUCCESS;
}

std::vector<AttentionGroupParams> LLMPreProcess::build_attention_groups(
    const InputLayersNamesSuffixes &input_layers_names_suffixes, const PreProcessParams &pre_process_params)
{
    if (!pre_process_params.attention_groups.empty()) {
        return pre_process_params.attention_groups;
    }

    AttentionGroupParams single_group;
    single_group.suffixes = input_layers_names_suffixes.attention;
    single_group.kv_cache_size = pre_process_params.kv_cache_size;
    single_group.num_attention_heads = pre_process_params.num_attention_heads;
    single_group.num_key_value_heads = pre_process_params.num_key_value_heads;
    single_group.mrope_section = pre_process_params.mrope_section;

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

Expected<std::vector<AttentionGroupContext>> LLMPreProcess::discover_layer_groups(const Eigen::VectorXf &theta,
    const std::map<std::string, size_t> &prefill_inputs_frame_size,
    const std::map<std::string, size_t> &tbt_inputs_frame_size, uint8_t scaled_mask_value,
    const std::vector<AttentionGroupParams> &attention_groups)
{
    std::vector<AttentionGroupContext> groups;
    for (const auto &group_params : attention_groups) {
        AttentionGroupContext group;
        group.params = group_params;
        TRY(group.prefill_layers, resolve_layer_names(group_params.suffixes, prefill_inputs_frame_size));
        TRY(group.tbt_layers, resolve_layer_names(group_params.suffixes, tbt_inputs_frame_size));
        // TODO: HRT-20850 - give each group its own theta, read from its own rope resource
        group.theta = theta;
        group.repeated_mrope_section = repeat_mrope_section(group_params.mrope_section);
        group.cumulative_mrope_section = cumulative_sections(group.repeated_mrope_section);
        // TODO: HRT-20851 - take the scaled mask value per group
        group.scaled_mask_value = scaled_mask_value;
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
    const Eigen::VectorXf &theta, uint32_t embeddings_layer_features, hailo_format_type_t embeddings_layer_type,
    uint8_t scaled_mask_value, const InputLayersNamesSuffixes &input_layers_names_suffixes,
    const PreProcessParams &pre_process_params)
{
    const auto theta_size = static_cast<size_t>(theta.size());
    const auto attention_groups = build_attention_groups(input_layers_names_suffixes, pre_process_params);
    CHECK_SUCCESS(validate_inputs_layout(prefill_inputs_frame_size, tbt_inputs_frame_size,
        input_layers_names_suffixes, pre_process_params, attention_groups, embeddings_layer_features,
        embeddings_layer_type, theta_size));
    TRY(auto groups, discover_layer_groups(theta, prefill_inputs_frame_size, tbt_inputs_frame_size,
        scaled_mask_value, attention_groups));

    auto cols = embeddings_layer_features;
    eigen_matrix_2d_u16_t local_cached_embeddings = eigen_matrix_2d_u16_t(pre_process_params.prefill_input_tokens_count, cols);

    auto ptr = make_unique_nothrow<LLMPreProcess>(std::move(local_cached_embeddings), prefill_inputs_frame_size,
        tbt_inputs_frame_size, input_layers_names_suffixes, pre_process_params, std::move(groups));
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY); // Consider returning different status
    return ptr;
}

LLMPreProcess::LLMPreProcess(eigen_matrix_2d_u16_t &&local_cached_embeddings,
    const std::map<std::string, size_t> &prefill_inputs_frame_size, const std::map<std::string, size_t> &tbt_inputs_frame_size,
    const InputLayersNamesSuffixes &input_layers_names_suffixes, const PreProcessParams &pre_process_params,
    std::vector<AttentionGroupContext> &&groups) :
        m_cache_usage_size(0),
        m_local_cached_embeddings(std::move(local_cached_embeddings)),
        m_prefill_inputs_frame_size(prefill_inputs_frame_size),
        m_tbt_inputs_frame_size(tbt_inputs_frame_size),
        m_current_timestamp_value(0),
        m_groups(std::move(groups)),
        m_input_layers_names_suffixes(input_layers_names_suffixes),
        m_params(pre_process_params)
{
    // Allocate position_ids tensor
    int position_ids_height = POSITIONAL_EMBEDDING_ROWS;
    int position_ids_width = POSITIONAL_EMBEDDING_COLS;
    int position_ids_features = m_params.prefill_input_tokens_count;
    m_local_cached_pos_ids = Eigen::Tensor<uint32_t, 4, Eigen::RowMajor>(position_ids_height, position_ids_width, position_ids_features, 1);
}

void LLMPreProcess::prepare_embeddings_input(MemoryView &layer_buffer, uint32_t number_of_tokens)
{
    // TODO: HRT-16156 - Input layer 1 is always uint16. Support generic in the future.
    Eigen::Map<eigen_matrix_2d_u16_t> mapped_matrix(
        reinterpret_cast<uint16_t*>(layer_buffer.data()), number_of_tokens, m_local_cached_embeddings.cols());

    mapped_matrix.bottomRows(number_of_tokens) = m_local_cached_embeddings.bottomRows(number_of_tokens);
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
        write_attention_mask(group, layer_buffer, layer_input_tokens_count(phase));
    }

    return HAILO_SUCCESS;
}

// TODO: HRT-16225 - Optimization
void LLMPreProcess::write_attention_mask(const AttentionGroupContext &group, MemoryView &layer_buffer,
    int layer_input_tokens_size)
{
    const auto scaled_mask_value = group.scaled_mask_value;
    int mask_cache_usage = std::min(static_cast<uint32_t>(m_cache_usage_size), group.params.kv_cache_size);
    int block1_rows_count = std::max(layer_input_tokens_size - mask_cache_usage, 0);      // unused cache size
    int block2_rows_count = std::min(mask_cache_usage, layer_input_tokens_size);          // actual input tokens size
    int group_total_rows = block1_rows_count + block2_rows_count;
    int group_total_cols = group.params.kv_cache_size;
    assert(static_cast<size_t>(group_total_rows * group_total_cols * group.params.num_attention_heads) == layer_buffer.size());
    Eigen::Map<Eigen::Matrix<uint8_t, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>> attention_mask(
        layer_buffer.data(), group_total_rows, group_total_cols * group.params.num_attention_heads);

    // Fill Block 1: Padding tokens (all 1's scaled by scaled_mask_value)
    if (block1_rows_count > 0) {
        attention_mask.block(0, 0, block1_rows_count, group_total_cols).setConstant(scaled_mask_value);
    }

    // Fill Block 2: Valid tokens (cache + current tokens)
    if (block2_rows_count > 0) {
        int block2_row_index = block1_rows_count;

        // Left part: Unused cache (all 0's)
        int block2_left_col_index = 0;
        int block2_cols_count = group_total_cols - mask_cache_usage;
        attention_mask.block(block2_row_index, block2_left_col_index, block2_rows_count, block2_cols_count).setZero();

        // Middle part: Used cache (all 1's scaled by scaled_mask_value)
        int block2_mid_col_index = block2_cols_count;
        int block2_mid_cols_count = mask_cache_usage - block2_rows_count;
        attention_mask.block(block2_row_index, block2_mid_col_index, block2_rows_count, block2_mid_cols_count).setConstant(scaled_mask_value);

        // Right part: Self-attention (lower triangular scaled by scaled_mask_value)
        int block2_right_col_index = block2_mid_col_index + block2_mid_cols_count;
        int block2_right_cols_count = block2_rows_count;
        auto self_att_block = attention_mask.block(block2_row_index, block2_right_col_index, block2_rows_count, block2_right_cols_count);
        self_att_block.setZero();
        self_att_block.triangularView<Eigen::Lower>().setConstant(scaled_mask_value);
    }

    for (uint32_t head = 1; head < group.params.num_attention_heads; head++) {
        attention_mask.block(0, head * group_total_cols, group_total_rows, group_total_cols) = attention_mask.block(0, 0, group_total_rows, group_total_cols);
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

        // Tile the results for the Q and K heads
        tile_along_last_axis(rope_cos, group.params.num_attention_heads, q_cos_buffer);
        tile_along_last_axis(rope_sin, group.params.num_attention_heads, q_sin_buffer);
        tile_along_last_axis(rope_cos, group.params.num_key_value_heads, k_cos_buffer);
        tile_along_last_axis(rope_sin, group.params.num_key_value_heads, k_sin_buffer);
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

void LLMPreProcess::update_cache_from_embeddings(const std::vector<EmbeddingViewWrapper> &embedding_rows_views)
{
    auto single_row_bytes = static_cast<size_t>(m_local_cached_embeddings.cols() * sizeof(uint16_t));
    auto num_rows = static_cast<int>(embedding_rows_views.size());
    m_cache_usage_size += num_rows;

    auto head_size = m_local_cached_embeddings.rows() - num_rows;
    m_local_cached_embeddings.topRows(head_size) = m_local_cached_embeddings.bottomRows(head_size);

    // Copy directly from views into the bottom rows
    for (int i = 0; i < num_rows; ++i) {
        assert(embedding_rows_views[i].size() == single_row_bytes);
        std::memcpy(m_local_cached_embeddings.row(head_size + i).data(), embedding_rows_views[i].data(), single_row_bytes);
    }
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
    const std::vector<EmbeddingViewWrapper> &input_tokens_embeddings)
{
    int num_rows = static_cast<int>(input_tokens_embeddings.size());
    CHECK(static_cast<size_t>(num_rows) <= m_params.prefill_input_tokens_count, HAILO_INVALID_ARGUMENT,
        "Preparing prefill inputs failed. embeddings rows must be lower then {}", m_params.prefill_input_tokens_count);
    CHECK_SUCCESS(validate_inputs(layer_name_to_input_buffer, m_prefill_inputs_frame_size)); // TODO (HRT-16660): when tried to mix between prefill and tbt sizes this check didnt fail

    update_cache_from_embeddings(input_tokens_embeddings);
    TRY(auto input_layer_embeddings, get_layer_name_from_suffix<MemoryView>(m_input_layers_names_suffixes.embeddings, layer_name_to_input_buffer));
    prepare_embeddings_input(layer_name_to_input_buffer[input_layer_embeddings], m_params.prefill_input_tokens_count);

    CHECK_SUCCESS(prepare_attention_mask_inputs(layer_name_to_input_buffer, InferencePhase::Prefill));
    CHECK_SUCCESS(prepare_positional_embed_inputs(layer_name_to_input_buffer, InferencePhase::Prefill, num_rows));

    return HAILO_SUCCESS;
}

hailo_status LLMPreProcess::prepare_inputs_tbt(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer, const std::vector<EmbeddingViewWrapper> &input_token_embedding)
{
    CHECK_SUCCESS(validate_inputs(layer_name_to_input_buffer, m_tbt_inputs_frame_size));

    assert(input_token_embedding.size() == 1);
    update_cache_from_embeddings(input_token_embedding);

    TRY(auto input_layer_embeddings, get_layer_name_from_suffix<MemoryView>(m_input_layers_names_suffixes.embeddings, layer_name_to_input_buffer));
    prepare_embeddings_input(layer_name_to_input_buffer[input_layer_embeddings], TBT_INPUT_TOKENS_SIZE);

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
bool LLMPreProcess::is_positional_embed_layer(const std::string &name, const InputLayersNamesSuffixes &input_layers_names_suffixes)
{
    const auto &attention = input_layers_names_suffixes.attention;
    if ((!attention.pe_q_cos.empty() && has_suffix(name, attention.pe_q_cos)) ||
        (!attention.pe_q_sin.empty() && has_suffix(name, attention.pe_q_sin)) ||
        (!attention.pe_k_cos.empty() && has_suffix(name, attention.pe_k_cos)) ||
        (!attention.pe_k_sin.empty() && has_suffix(name, attention.pe_k_sin)))
    {
        return true;
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

std::tuple<size_t, eigen_matrix_2d_u16_t, eigen_tensor_4d_u32_t, int> LLMPreProcess::get_local_cache() const
{
    return {m_cache_usage_size, m_local_cached_embeddings, m_local_cached_pos_ids, m_current_timestamp_value};
}

void LLMPreProcess::set_local_cache(size_t cache_size, const eigen_matrix_2d_u16_t &embeddings, const eigen_tensor_4d_u32_t &pos_ids, int timestamp_value)
{
    m_cache_usage_size = cache_size;
    m_local_cached_embeddings = embeddings;
    m_local_cached_pos_ids = pos_ids;
    m_current_timestamp_value = timestamp_value;
}


} /* namespace genai */
} /* namespace hailort */
