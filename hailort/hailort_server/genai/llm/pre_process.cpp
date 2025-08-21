/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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

void LLMPreProcess::tile_along_last_axis(const Eigen::Tensor<float32_t, 4, Eigen::RowMajor> &input, int groups, MemoryView &layer_buffer)
{
    auto height = input.dimension(1);
    auto width = input.dimension(2);
    auto features = input.dimension(3);

    assert((height * width * features * groups * sizeof(float32_t)) == layer_buffer.size());

    Eigen::TensorMap<Eigen::Tensor<float32_t, 4, Eigen::RowMajor>> output_tensor(
        layer_buffer.as_pointer<float32_t>(), height, width, groups, features);

    for (int g = 0; g < groups; ++g) {
        output_tensor.chip(g, 2) = input.chip(0, 0);
    }
}

// TODO: HRT-16646 - Remove this flow
Eigen::VectorXf LLMPreProcess::generate_default_theta()
{
    Eigen::VectorXf theta(TILE_SIZE * 2);
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

hailo_status LLMPreProcess::validate_inputs_names(const std::map<std::string, size_t> &inputs_map)
{
    TRY(auto layer_name, get_layer_name_from_suffix<size_t>(INPUT_LAYER_EMBEDDINGS_SUFF, inputs_map));
    TRY(layer_name, get_layer_name_from_suffix<size_t>(INPUT_LAYER_ATTENTION_MASK_SUFF, inputs_map));
    TRY(layer_name, get_layer_name_from_suffix<size_t>(INPUT_LAYER_PE_Q_COS_SUFF, inputs_map));
    TRY(layer_name, get_layer_name_from_suffix<size_t>(INPUT_LAYER_PE_Q_SIN_SUFF, inputs_map));
    TRY(layer_name, get_layer_name_from_suffix<size_t>(INPUT_LAYER_PE_K_COS_SUFF, inputs_map));
    TRY(layer_name, get_layer_name_from_suffix<size_t>(INPUT_LAYER_PE_K_SIN_SUFF, inputs_map));

    return HAILO_SUCCESS;
}

Expected<std::unique_ptr<LLMPreProcess>> LLMPreProcess::create(MemoryView &embeddings_memview,
    const std::map<std::string, size_t> &prefill_inputs_frame_size, const std::map<std::string, size_t> &tbt_inputs_frame_size,
    Eigen::VectorXf &&theta, uint32_t embeddings_layer_features, hailo_format_type_t embeddings_layer_type)
{
    CHECK(!embeddings_memview.empty(), HAILO_INVALID_ARGUMENT, "Creating LLM Pre-Process failed, `embeddings_memview` is empty");
    CHECK_SUCCESS(validate_inputs_names(prefill_inputs_frame_size));
    CHECK_SUCCESS(validate_inputs_names(tbt_inputs_frame_size));

    // TODO: HRT-16156 - Support dtypes for embeddings layer
    CHECK(embeddings_layer_type == HAILO_FORMAT_TYPE_UINT16, HAILO_INVALID_ARGUMENT, "Creating LLM Pre-Process failed, only uint16 data type is currently supported");
    auto cols = embeddings_layer_features;
    auto rows = embeddings_memview.size() / cols / sizeof(uint16_t);
    eigen_map_2d_u16_t embeddings_matrix(reinterpret_cast<uint16_t*>(embeddings_memview.data()), rows, cols);

    eigen_matrix_2d_u16_t local_cached_embeddings = eigen_matrix_2d_u16_t(PREFILL_INPUT_TOKENS_SIZE, cols);

    auto ptr = make_unique_nothrow<LLMPreProcess>(embeddings_matrix, std::move(theta), std::move(local_cached_embeddings),
        prefill_inputs_frame_size, tbt_inputs_frame_size);
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY); // Consider returning different status
    return ptr;
}

LLMPreProcess::LLMPreProcess(eigen_map_2d_u16_t embeddings_matrix, Eigen::VectorXf &&theta,
    eigen_matrix_2d_u16_t &&local_cached_embeddings,
        const std::map<std::string, size_t> &prefill_inputs_frame_size, const std::map<std::string, size_t> &tbt_inputs_frame_size) :
        m_embeddings_matrix(embeddings_matrix),
        m_cache_usage_size(0),
        m_local_cached_embeddings(std::move(local_cached_embeddings)),
        m_theta(std::move(theta)),
        m_prefill_inputs_frame_size(prefill_inputs_frame_size),
        m_tbt_inputs_frame_size(tbt_inputs_frame_size),
        m_current_timestamp_value(0)
{
    // Allocate position_ids tensor
    int position_ids_height = POSITIONAL_EMBEDDING_ROWS;
    int position_ids_width = POSITIONAL_EMBEDDING_COLS;
    int position_ids_features = PREFILL_INPUT_TOKENS_SIZE;
    m_local_cached_pos_ids = Eigen::Tensor<uint32_t, 4, Eigen::RowMajor>(position_ids_height, position_ids_width, position_ids_features, 1);

    const int MROPE_SECTIONS_COUNT = MROPE_SECTION_ORIGINAL.size();

    // Generate cumulative mrope_section indices
    m_mrope_section = std::vector<int>(MROPE_SECTIONS_COUNT);
    std::partial_sum(MROPE_SECTION_ORIGINAL.begin(), MROPE_SECTION_ORIGINAL.end(), m_mrope_section.begin());
}

void LLMPreProcess::prepare_embeddings_input(MemoryView &layer_buffer, uint32_t number_of_tokens)
{
    // TODO: HRT-16156 - Input layer 1 is always uint16. Support generic in the future.
    Eigen::Map<eigen_matrix_2d_u16_t> mapped_matrix(
        reinterpret_cast<uint16_t*>(layer_buffer.data()), number_of_tokens, m_embeddings_matrix.cols());

    mapped_matrix.bottomRows(number_of_tokens) = m_local_cached_embeddings.bottomRows(number_of_tokens);
}

// TODO: HRT-16225 - Optimization
void LLMPreProcess::prepare_attention_mask_input(MemoryView &layer_buffer, int layer_input_tokens_size)
{
    int mask_cache_usage = std::min(static_cast<int>(m_cache_usage_size), KV_CACHE_SIZE);
    int block1_rows_count = std::max(layer_input_tokens_size - mask_cache_usage, 0);      // unused cache size
    int block2_rows_count = std::min(mask_cache_usage, layer_input_tokens_size);          // actual input tokens size
    int group_total_rows = block1_rows_count + block2_rows_count;
    int group_total_cols = KV_CACHE_SIZE;
    assert(static_cast<size_t>(group_total_rows * group_total_cols * MASK_GROUPS_SIZE) == layer_buffer.size());
    Eigen::Map<Eigen::Matrix<uint8_t, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>> attention_mask(
        layer_buffer.data(), group_total_rows, group_total_cols * MASK_GROUPS_SIZE);

    // Fill Block 1: Padding tokens (all 1's scaled by SCALED_MASK_VALUE)
    if (block1_rows_count > 0) {
        attention_mask.block(0, 0, block1_rows_count, group_total_cols).setConstant(SCALED_MASK_VALUE);
    }

    // Fill Block 2: Valid tokens (cache + current tokens)
    if (block2_rows_count > 0) {
        int block2_row_index = block1_rows_count;

        // Left part: Unused cache (all 0's)
        int block2_left_col_index = 0;
        int block2_cols_count = group_total_cols - mask_cache_usage;
        attention_mask.block(block2_row_index, block2_left_col_index, block2_rows_count, block2_cols_count).setZero();

        // Middle part: Used cache (all 1's scaled by SCALED_MASK_VALUE)
        int block2_mid_col_index = block2_cols_count;
        int block2_mid_cols_count = mask_cache_usage - block2_rows_count;
        attention_mask.block(block2_row_index, block2_mid_col_index, block2_rows_count, block2_mid_cols_count).setConstant(SCALED_MASK_VALUE);

        // Right part: Self-attention (lower triangular scaled by SCALED_MASK_VALUE)
        int block2_right_col_index = block2_mid_col_index + block2_mid_cols_count;
        int block2_right_cols_count = block2_rows_count;
        auto self_att_block = attention_mask.block(block2_row_index, block2_right_col_index, block2_rows_count, block2_right_cols_count);
        self_att_block.setZero();
        self_att_block.triangularView<Eigen::Lower>().setConstant(SCALED_MASK_VALUE);
    }

    for (int group = 1; group < MASK_GROUPS_SIZE; group++) {
        attention_mask.block(0, group * group_total_cols, group_total_rows, group_total_cols) = attention_mask.block(0, 0, group_total_rows, group_total_cols);
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

hailo_status LLMPreProcess::fill_positional_embed(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer,
    const Eigen::Tensor<float32_t, 4, Eigen::RowMajor> &rope_cos, const Eigen::Tensor<float32_t, 4, Eigen::RowMajor> &rope_sin)
{
    // Get the target layer names
    TRY(auto q_cos_layer_name, get_layer_name_from_suffix<MemoryView>(INPUT_LAYER_PE_Q_COS_SUFF, layer_name_to_input_buffer));
    TRY(auto k_cos_layer_name, get_layer_name_from_suffix<MemoryView>(INPUT_LAYER_PE_K_COS_SUFF, layer_name_to_input_buffer));
    TRY(auto q_sin_layer_name, get_layer_name_from_suffix<MemoryView>(INPUT_LAYER_PE_Q_SIN_SUFF, layer_name_to_input_buffer));
    TRY(auto k_sin_layer_name, get_layer_name_from_suffix<MemoryView>(INPUT_LAYER_PE_K_SIN_SUFF, layer_name_to_input_buffer));

    // Tile the results for the Q and K groups
    tile_along_last_axis(rope_cos, Q_GROUPS_SIZE, layer_name_to_input_buffer[q_cos_layer_name]);
    tile_along_last_axis(rope_sin, Q_GROUPS_SIZE, layer_name_to_input_buffer[q_sin_layer_name]);
    tile_along_last_axis(rope_cos, K_GROUPS_SIZE, layer_name_to_input_buffer[k_cos_layer_name]);
    tile_along_last_axis(rope_sin, K_GROUPS_SIZE, layer_name_to_input_buffer[k_sin_layer_name]);

    return HAILO_SUCCESS;
}

std::pair<Eigen::Tensor<float32_t, 4, Eigen::RowMajor>, Eigen::Tensor<float32_t, 4, Eigen::RowMajor>>
    LLMPreProcess::angular_positional_embed(int layer_input_tokens_size)
{
    int position_ids_height = static_cast<int>(m_local_cached_pos_ids.dimension(0));
    int position_ids_width = static_cast<int>(m_local_cached_pos_ids.dimension(1));
    int position_ids_features = static_cast<int>(m_local_cached_pos_ids.dimension(2));

    Eigen::Tensor<float32_t, 4, Eigen::RowMajor> rope_cos(position_ids_height, position_ids_width, layer_input_tokens_size, m_theta.size());
    Eigen::Tensor<float32_t, 4, Eigen::RowMajor> rope_sin(position_ids_height, position_ids_width, layer_input_tokens_size, m_theta.size());

    int current_col = 0;
    
    for (size_t section = 0; section < MROPE_SECTION_ORIGINAL.size(); ++section) {
        int start_idx = (section == 0) ? 0 : m_mrope_section[section - 1];
        int slice_size = MROPE_SECTION_ORIGINAL[section];
        int section_mod_3 = static_cast<int>(section % 3);

        for (int j = 0; j < position_ids_width; ++j) {
            for (int k = 0; k < layer_input_tokens_size; ++k) {
                int src_idx = (layer_input_tokens_size == TBT_INPUT_TOKENS_SIZE) ?
                    (position_ids_features - 1) : k;

                float32_t pos_value = static_cast<float32_t>(m_local_cached_pos_ids(section_mod_3, j, src_idx, 0));
                for (int l = 0; l < slice_size; ++l) {
                    float32_t angle = m_theta[start_idx + l] * pos_value;
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
    int layer_input_tokens_size, int input_tokens_count)
{
    shift_local_cached_positional_embeds(input_tokens_count);

    int position_ids_features = static_cast<int>(m_local_cached_pos_ids.dimension(2));

    auto pos_ids_start_index = static_cast<int>(position_ids_features - input_tokens_count);

    incremental_positional_embed(pos_ids_start_index, input_tokens_count);
    auto rope_pair = angular_positional_embed(layer_input_tokens_size);

    return fill_positional_embed(layer_name_to_input_buffer, rope_pair.first, rope_pair.second);
}

void LLMPreProcess::update_cache_from_tokens(const std::vector<int> &tokens, std::function<Eigen::Matrix<uint16_t, 1, Eigen::Dynamic>(int)> tokens_lookup)
{
    m_cache_usage_size += tokens.size();

    // Calculate the sizes for shifting the matrix
    auto head_size = m_local_cached_embeddings.rows() - tokens.size();

    // Shift the existing embeddings
    m_local_cached_embeddings.topRows(head_size) = m_local_cached_embeddings.bottomRows(head_size); // Move older embeddings up
    // Directly populate the tail portion with embeddings from tokens
    for (size_t i = 0; i < tokens.size(); ++i) {
        m_local_cached_embeddings.row(head_size + i) = tokens_lookup(tokens[i]);
    }
}

hailo_status LLMPreProcess::validate_inputs(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer, const std::map<std::string, size_t> &expected_sizes)
{
    assert(HAILO_SUCCESS == get_layer_name_from_suffix<MemoryView>(INPUT_LAYER_EMBEDDINGS_SUFF, layer_name_to_input_buffer).status());
    assert(HAILO_SUCCESS == get_layer_name_from_suffix<MemoryView>(INPUT_LAYER_ATTENTION_MASK_SUFF, layer_name_to_input_buffer).status());
    assert(HAILO_SUCCESS == get_layer_name_from_suffix<MemoryView>(INPUT_LAYER_PE_Q_COS_SUFF, layer_name_to_input_buffer).status());
    assert(HAILO_SUCCESS == get_layer_name_from_suffix<MemoryView>(INPUT_LAYER_PE_Q_SIN_SUFF, layer_name_to_input_buffer).status());
    assert(HAILO_SUCCESS == get_layer_name_from_suffix<MemoryView>(INPUT_LAYER_PE_K_COS_SUFF, layer_name_to_input_buffer).status());
    assert(HAILO_SUCCESS == get_layer_name_from_suffix<MemoryView>(INPUT_LAYER_PE_K_SIN_SUFF, layer_name_to_input_buffer).status());

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
    std::vector<int> &input_tokens)
{
    CHECK(input_tokens.size() <= PREFILL_INPUT_TOKENS_SIZE, HAILO_INVALID_ARGUMENT,
        "Preparing prefill inputs failed. `input_tokens` size must be lower then {}, but got {}", PREFILL_INPUT_TOKENS_SIZE, input_tokens.size());
    CHECK_SUCCESS(validate_inputs(layer_name_to_input_buffer, m_prefill_inputs_frame_size)); // TODO (HRT-16660): when tried to mix between prefill and tbt sizes this check didnt fail

    update_cache_from_tokens(input_tokens, [this](int token) {
        return m_embeddings_matrix.row(token);
    });
    TRY(auto input_layer_embeddings, get_layer_name_from_suffix<MemoryView>(INPUT_LAYER_EMBEDDINGS_SUFF, layer_name_to_input_buffer));
    prepare_embeddings_input(layer_name_to_input_buffer[input_layer_embeddings], PREFILL_INPUT_TOKENS_SIZE);

    TRY(auto input_layer_attention_mask, get_layer_name_from_suffix<MemoryView>(INPUT_LAYER_ATTENTION_MASK_SUFF, layer_name_to_input_buffer));
    prepare_attention_mask_input(layer_name_to_input_buffer[input_layer_attention_mask], PREFILL_INPUT_TOKENS_SIZE);
    CHECK_SUCCESS(prepare_positional_embed_inputs(layer_name_to_input_buffer, PREFILL_INPUT_TOKENS_SIZE, static_cast<int>(input_tokens.size())));

    return HAILO_SUCCESS;
}

hailo_status LLMPreProcess::prepare_inputs_tbt(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer, int input_token)
{
    CHECK_SUCCESS(validate_inputs(layer_name_to_input_buffer, m_tbt_inputs_frame_size));

    std::vector<int> input_tokens = {input_token};
    update_cache_from_tokens(input_tokens, [this](int token) {
        return m_embeddings_matrix.row(token);
    });

    TRY(auto input_layer_embeddings, get_layer_name_from_suffix<MemoryView>(INPUT_LAYER_EMBEDDINGS_SUFF, layer_name_to_input_buffer));
    prepare_embeddings_input(layer_name_to_input_buffer[input_layer_embeddings], TBT_INPUT_TOKENS_SIZE);

    TRY(auto input_layer_attention_mask, get_layer_name_from_suffix<MemoryView>(INPUT_LAYER_ATTENTION_MASK_SUFF, layer_name_to_input_buffer));
    prepare_attention_mask_input(layer_name_to_input_buffer[input_layer_attention_mask], TBT_INPUT_TOKENS_SIZE);
    CHECK_SUCCESS(prepare_positional_embed_inputs(layer_name_to_input_buffer, TBT_INPUT_TOKENS_SIZE, static_cast<int>(input_tokens.size())));

    return HAILO_SUCCESS;
}

void LLMPreProcess::reset_local_cache()
{
    m_cache_usage_size = 0;
    m_current_timestamp_value = 0;
}

// TODO: HRT-16261 - Use layers info
bool LLMPreProcess::is_positional_embed_layer(const std::string &name)
{
    if (has_suffix(name, INPUT_LAYER_PE_Q_COS_SUFF) ||
        has_suffix(name, INPUT_LAYER_PE_Q_SIN_SUFF) ||
        has_suffix(name, INPUT_LAYER_PE_K_COS_SUFF) ||
        has_suffix(name, INPUT_LAYER_PE_K_SIN_SUFF))
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
