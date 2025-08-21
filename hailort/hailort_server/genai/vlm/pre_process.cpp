/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pre_process.cpp
 * @brief Implementation for VLM pre processing
 **/

#include "pre_process.hpp"

namespace hailort
{
namespace genai
{

Expected<std::unique_ptr<LLMPreProcess>> VLMPreProcess::create(MemoryView &text_embeddings,
    const std::map<std::string, size_t> &prefill_inputs_frame_size, const std::map<std::string, size_t> &tbt_inputs_frame_size,
    Eigen::VectorXf &&theta, uint32_t text_embeddings_layer_features, hailo_format_type_t text_embeddings_layer_type, int vision_token_value,
    const hailo_3d_image_shape_t &input_encoder_shape)
{
    CHECK(!text_embeddings.empty(), HAILO_INVALID_ARGUMENT, "Creating LLM Pre-Process failed, `embeddings_memview` is empty");
    CHECK_SUCCESS(validate_inputs_names(prefill_inputs_frame_size));
    CHECK_SUCCESS(validate_inputs_names(tbt_inputs_frame_size));

    // TODO: HRT-16156 - Support dtypes for embeddings layer
    CHECK(text_embeddings_layer_type == HAILO_FORMAT_TYPE_UINT16, HAILO_INVALID_ARGUMENT, "Creating LLM Pre-Process failed, only uint16 data type is currently supported");
    auto cols = text_embeddings_layer_features;
    auto rows = text_embeddings.size() / cols / sizeof(uint16_t);
    eigen_map_2d_u16_t embeddings_matrix(reinterpret_cast<uint16_t*>(text_embeddings.data()), rows, cols);

    Eigen::Matrix<uint16_t, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> local_cached_embeddings =
        Eigen::Matrix<uint16_t, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>(PREFILL_INPUT_TOKENS_SIZE, cols);

    auto ptr = make_unique_nothrow<VLMPreProcess>(embeddings_matrix, std::move(theta),
        std::move(local_cached_embeddings),
        prefill_inputs_frame_size, tbt_inputs_frame_size, vision_token_value, input_encoder_shape);
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY); // Consider returning different status

    return std::unique_ptr<LLMPreProcess>(std::move(ptr));
}

VLMPreProcess::VLMPreProcess(eigen_map_2d_u16_t embeddings_matrix, Eigen::VectorXf &&theta,
    Eigen::Matrix<uint16_t, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> &&local_cached_embeddings,
    const std::map<std::string, size_t> &prefill_inputs_frame_size, const std::map<std::string, size_t> &tbt_inputs_frame_size, int vision_token_value,
    const hailo_3d_image_shape_t &input_encoder_shape) :
        LLMPreProcess(embeddings_matrix, std::move(theta), std::move(local_cached_embeddings), prefill_inputs_frame_size, tbt_inputs_frame_size),
        m_vision_token_value(vision_token_value), m_input_encoder_shape(input_encoder_shape), m_temporal_pos_h(0), m_temporal_pos_w(0)
{
}

std::vector<std::pair<uint32_t, uint32_t>> VLMPreProcess::get_vision_embeds_indices(const std::vector<int> &ids) const
{
    std::vector<std::pair<uint32_t, uint32_t>> res;
    uint32_t index = 0;
    bool during_frame = false;

    for (; index < ids.size(); index++) {
        if (ids[index] == m_vision_token_value) {
            if (!during_frame) {
                res.emplace_back(index, index);
                during_frame = true;
            } else {
                res.back().second++;
            }
        } else {
            during_frame = false;
        }
    }

    return res;
}

void VLMPreProcess::fill_temporal_positional_embed(int pos_ids_start_index, int relative_st_image_index,
    int position_ids_features, int merged_patch_size_h, int merged_patch_size_w)
{
    auto src_idx = pos_ids_start_index + relative_st_image_index;
    for (; m_temporal_pos_h < merged_patch_size_h; m_temporal_pos_h++) {
        for (; m_temporal_pos_w < merged_patch_size_w; m_temporal_pos_w++) {
            if (src_idx < position_ids_features) {
                m_local_cached_pos_ids(2, 0, src_idx, 0) =
                    static_cast<uint32_t>(m_current_timestamp_value + m_temporal_pos_w);
                    m_local_cached_pos_ids(1, 0, src_idx, 0) =
                        static_cast<uint32_t>(m_current_timestamp_value + m_temporal_pos_h);
                    src_idx++;
            } else {
                return;
            }
        }
        m_temporal_pos_w = 0;
    }
    m_temporal_pos_h = 0;
}

hailo_status VLMPreProcess::prepare_positional_embed_inputs(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer, 
    int layer_input_tokens_size, const std::vector<std::pair<uint32_t, uint32_t>> &frames_tokens_indices, int input_tokens_count)
{
    shift_local_cached_positional_embeds(input_tokens_count);


    int position_ids_height = static_cast<int>(m_local_cached_pos_ids.dimension(0));
    int position_ids_features = static_cast<int>(m_local_cached_pos_ids.dimension(2));

    auto pos_ids_start_index = static_cast<int>(position_ids_features - input_tokens_count);

    int merged_patch_size_h = m_input_encoder_shape.height / (VISION_PATH_SIZE * MERGE_SIZE_H);
    int merged_patch_size_w = m_input_encoder_shape.width / (VISION_PATH_SIZE * MERGE_SIZE_W);

    // TODO (HRT-17264): Optimize this code, utilize Eigen functionality and reduce memcpy

    if (!frames_tokens_indices.empty()) {
        for (auto &frame_tokens_indices : frames_tokens_indices) {
            int relative_st_image_index = frame_tokens_indices.first;
            int relative_end_image_index = frame_tokens_indices.second;

            // Fill temporal positioning
            for (int i = 0; i < relative_st_image_index; ++i) {
                m_local_cached_pos_ids(0, 0, i + pos_ids_start_index, 0) = static_cast<uint32_t>(m_current_timestamp_value); // Set range [0, st_image_index)
                m_local_cached_pos_ids(1, 0, i + pos_ids_start_index, 0) = static_cast<uint32_t>(m_current_timestamp_value); // Set range [0, st_image_index)
                m_local_cached_pos_ids(2, 0, i + pos_ids_start_index, 0) = static_cast<uint32_t>(m_current_timestamp_value); // Set range [0, st_image_index)
                m_current_timestamp_value++;
            }

            // Set temporal positioning (constant value) from st_image_index to end_image_index
            for (int i = relative_st_image_index; i <= relative_end_image_index; ++i) {
                m_local_cached_pos_ids(0, 0, i + pos_ids_start_index, 0) = static_cast<uint32_t>(m_current_timestamp_value);
            }

            fill_temporal_positional_embed(pos_ids_start_index, relative_st_image_index,
                position_ids_features, merged_patch_size_h, merged_patch_size_w);

            if ((relative_end_image_index + 1) < (position_ids_features - pos_ids_start_index)) {
                m_current_timestamp_value = static_cast<uint32_t>(m_current_timestamp_value +
                    std::max(merged_patch_size_h, merged_patch_size_w));
            }

            // Rest of the text tokens
            for (int i = (relative_end_image_index + 1); i < (position_ids_features - pos_ids_start_index); ++i) {
                for (int h = 0; h < position_ids_height; ++h) {
                    m_local_cached_pos_ids(h, 0, pos_ids_start_index + i, 0) = m_current_timestamp_value;
                }
                m_current_timestamp_value++;
            }
        }
    } else {
        incremental_positional_embed(pos_ids_start_index, input_tokens_count);
    }

    auto rope_pair = angular_positional_embed(layer_input_tokens_size);

    return fill_positional_embed(layer_name_to_input_buffer, rope_pair.first, rope_pair.second);
}

Eigen::Matrix<uint16_t, 1, Eigen::Dynamic, Eigen::RowMajor> VLMPreProcess::get_embedding(int token, const std::vector<MemoryView> &input_frames_embeddings,
    uint32_t &current_frame_index, uint32_t &current_emb_index_in_frame) const
{
    // Check if this is a vision token
    if (token == m_vision_token_value) {
        if (current_emb_index_in_frame >= embeddings_per_frame()) {
            // Reset the embedding index for the next frame
            current_emb_index_in_frame = 0;
            current_frame_index++;
        }
        assert(current_frame_index < input_frames_embeddings.size());
        const auto &current_frame = input_frames_embeddings[current_frame_index];

        // View the frame data as a matrix to fetch the appropriate row
        Eigen::Map<const Eigen::Matrix<uint16_t, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>> frame_matrix(
            reinterpret_cast<const uint16_t*>(current_frame.data()), embeddings_per_frame(), m_embeddings_matrix.cols());

        return frame_matrix.row(current_emb_index_in_frame++);
    } else {
        return m_embeddings_matrix.row(token);
    }
}

hailo_status VLMPreProcess::prepare_inputs_prefill(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer,
    std::vector<int> &input_tokens, const std::vector<MemoryView> &input_frames_embeddings,
    uint32_t &current_frame_index, uint32_t &current_emb_index_in_frame)
{
    CHECK(input_tokens.size() <= PREFILL_INPUT_TOKENS_SIZE, HAILO_INVALID_ARGUMENT,
        "Preparing prefill inputs failed. `input_tokens` size must be lower then {}, but got {} tokens",
        PREFILL_INPUT_TOKENS_SIZE, input_tokens.size());
    CHECK_SUCCESS(validate_inputs(layer_name_to_input_buffer, m_prefill_inputs_frame_size));

    update_cache_from_tokens(input_tokens, [&](int token) {
        return get_embedding(token, input_frames_embeddings, current_frame_index, current_emb_index_in_frame);
    });

    TRY(auto embeddings_layer_name, get_layer_name_from_suffix<MemoryView>(INPUT_LAYER_EMBEDDINGS_SUFF, layer_name_to_input_buffer));

    prepare_embeddings_input(layer_name_to_input_buffer[embeddings_layer_name], PREFILL_INPUT_TOKENS_SIZE);

    auto frames_tokens_indices = get_vision_embeds_indices(input_tokens);

    TRY(auto attention_mask_layer_name, get_layer_name_from_suffix<MemoryView>(INPUT_LAYER_ATTENTION_MASK_SUFF, layer_name_to_input_buffer));
    prepare_attention_mask_input(layer_name_to_input_buffer[attention_mask_layer_name], PREFILL_INPUT_TOKENS_SIZE);
    CHECK_SUCCESS(prepare_positional_embed_inputs(layer_name_to_input_buffer, PREFILL_INPUT_TOKENS_SIZE,
        frames_tokens_indices, static_cast<int>(input_tokens.size())));

    return HAILO_SUCCESS;
}

} /* namespace genai */
} /* namespace hailort */
