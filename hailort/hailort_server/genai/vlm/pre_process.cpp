/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
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

Expected<std::unique_ptr<LLMPreProcess>> VLMPreProcess::create(const std::map<std::string, size_t> &prefill_inputs_frame_size,
    const std::map<std::string, size_t> &tbt_inputs_frame_size, Eigen::VectorXf &&theta, uint32_t text_embeddings_layer_features,
    const hailo_3d_image_shape_t &input_encoder_shape, uint8_t scaled_mask_value, const InputLayersNamesSuffixes &input_layers_names_suffixes,
    const PreProcessParams &pre_process_params, bool has_deepstack_layers,
    const std::unordered_map<std::string, size_t> &deepstack_suffix_to_tokens_per_frame)
{
    CHECK_SUCCESS(validate_inputs_names(prefill_inputs_frame_size, input_layers_names_suffixes));
    CHECK_SUCCESS(validate_inputs_names(tbt_inputs_frame_size, input_layers_names_suffixes));

    auto cols = text_embeddings_layer_features;

    Eigen::Matrix<uint16_t, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> local_cached_embeddings =
        Eigen::Matrix<uint16_t, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>(pre_process_params.prefill_input_tokens_count, cols);

    auto ptr = make_unique_nothrow<VLMPreProcess>(std::move(theta),
        std::move(local_cached_embeddings),
        prefill_inputs_frame_size, tbt_inputs_frame_size, input_encoder_shape, scaled_mask_value, input_layers_names_suffixes,
        pre_process_params, has_deepstack_layers, deepstack_suffix_to_tokens_per_frame);
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY); // Consider returning different status

    return std::unique_ptr<LLMPreProcess>(std::move(ptr));
}

VLMPreProcess::VLMPreProcess(Eigen::VectorXf &&theta,
    Eigen::Matrix<uint16_t, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> &&local_cached_embeddings,
    const std::map<std::string, size_t> &prefill_inputs_frame_size, const std::map<std::string, size_t> &tbt_inputs_frame_size,
    const hailo_3d_image_shape_t &input_encoder_shape, uint8_t scaled_mask_value, const InputLayersNamesSuffixes &input_layers_names_suffixes,
    const PreProcessParams &pre_process_params, bool has_deepstack_layers,
    const std::unordered_map<std::string, size_t> &deepstack_suffix_to_tokens_per_frame) :
        LLMPreProcess(std::move(theta), std::move(local_cached_embeddings), prefill_inputs_frame_size,
            tbt_inputs_frame_size, scaled_mask_value, input_layers_names_suffixes, pre_process_params),
        m_input_encoder_shape(input_encoder_shape),
        m_temporal_pos_h(0),
        m_temporal_pos_w(0),
        m_has_deepstack_layers(has_deepstack_layers),
        m_deepstack_suffix_to_tokens_per_frame(deepstack_suffix_to_tokens_per_frame)
{}

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

hailo_status VLMPreProcess::prepare_deepstack_inputs(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer,
    const std::vector<EmbeddingViewWrapper> &input_embeddings,
    EmbeddingsVectorState &standalone_frame_embeddings_state,
    EmbeddingsVectorState &video_embeddings_state)
{
    const auto deepstack_suffixes = m_input_layers_names_suffixes.deepstack_as_vector();

    // Resolve suffix → (full layer name, bytes per token)
    std::unordered_map<std::string, std::pair<std::string, size_t>> suffix_to_layer_info;
    for (const auto &suffix : deepstack_suffixes) {
        TRY(auto name, get_layer_name_from_suffix<MemoryView>(suffix, layer_name_to_input_buffer));
        auto bytes_per_token = layer_name_to_input_buffer[name].size() / m_params.prefill_input_tokens_count;
        suffix_to_layer_info[suffix] = {std::move(name), bytes_per_token};
    }

    EmbeddingsVectorState standalone_ds_state(standalone_frame_embeddings_state);
    EmbeddingsVectorState video_ds_state(video_embeddings_state);

    for (size_t i = 0; i < m_params.prefill_input_tokens_count; i++) {
        bool is_vision_token = (i < input_embeddings.size()) &&
            (input_embeddings[i].type() == EmbeddingViewWrapper::EmbeddingType::IMAGE ||
             input_embeddings[i].type() == EmbeddingViewWrapper::EmbeddingType::VIDEO);

        if (!is_vision_token) {
            zero_deepstack_token(layer_name_to_input_buffer, deepstack_suffixes, suffix_to_layer_info, i);
            continue;
        }

        EmbeddingsVectorState *ds_state = nullptr;
        if (input_embeddings[i].type() == EmbeddingViewWrapper::EmbeddingType::IMAGE) {
            ds_state = &standalone_ds_state;
        } else if (input_embeddings[i].type() == EmbeddingViewWrapper::EmbeddingType::VIDEO) {
            ds_state = &video_ds_state;
        }
        CHECK(ds_state != nullptr, HAILO_INVALID_OPERATION, "Invalid embedding type for deepstack");

        CHECK_SUCCESS(copy_deepstack_vision_token(layer_name_to_input_buffer, deepstack_suffixes, suffix_to_layer_info, i, *ds_state));
    }

    return HAILO_SUCCESS;
}

void VLMPreProcess::zero_deepstack_token(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer,
    const std::vector<std::string> &deepstack_suffixes, const SuffixToLayerInfo &suffix_to_layer_info, size_t token_index)
{
    for (const auto &suffix : deepstack_suffixes) {
        const auto &[layer_name, bytes_per_token] = suffix_to_layer_info.at(suffix);
        auto *dst_ptr = layer_name_to_input_buffer[layer_name].data() + (token_index * bytes_per_token);
        std::memset(dst_ptr, 0, bytes_per_token);
    }
}

hailo_status VLMPreProcess::copy_deepstack_vision_token(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer,
    const std::vector<std::string> &deepstack_suffixes, const SuffixToLayerInfo &suffix_to_layer_info,
    size_t token_index, EmbeddingsVectorState &ds_state)
{
    TRY(auto index_pair, ds_state.get_next_embedding_index());
    auto [frame_index, emb_index_in_frame] = index_pair;

    TRY(auto frame_deepstack_embeddings, ds_state.get_frame_deepstack_embeddings(frame_index));
    for (const auto &suffix : deepstack_suffixes) {
        const auto &src_buffer = frame_deepstack_embeddings.get().at(suffix);
        const auto &[layer_name, dst_bytes_per_token] = suffix_to_layer_info.at(suffix);
        auto &dst_buffer = layer_name_to_input_buffer[layer_name];

        auto src_tokens_per_frame = m_deepstack_suffix_to_tokens_per_frame.find(suffix);
        CHECK(src_tokens_per_frame != m_deepstack_suffix_to_tokens_per_frame.end(), HAILO_INVALID_OPERATION,
            "Missing tokens_per_frame for deepstack suffix '{}'", suffix);

        size_t src_bytes_per_token = src_buffer->size() / src_tokens_per_frame->second;
        auto *dst_ptr = dst_buffer.data() + (token_index * dst_bytes_per_token);
        auto *src_ptr = src_buffer->data() + (emb_index_in_frame * src_bytes_per_token);
        std::memcpy(dst_ptr, src_ptr, src_bytes_per_token);
    }

    return HAILO_SUCCESS;
}

// 'input_embeddings' already have placeholders for the vision tokens inside
hailo_status VLMPreProcess::prepare_inputs_prefill(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer,
    std::vector<EmbeddingViewWrapper> input_embeddings, EmbeddingsVectorState &standalone_frame_embeddings_state,
    EmbeddingsVectorState &video_embeddings_state)
{
    CHECK(input_embeddings.size() <= m_params.prefill_input_tokens_count, HAILO_INVALID_ARGUMENT,
        "Preparing prefill inputs failed. `input_embeddings` size must be lower then {}, but got {} tokens",
        m_params.prefill_input_tokens_count, input_embeddings.size());
    CHECK_SUCCESS(validate_inputs(layer_name_to_input_buffer, m_prefill_inputs_frame_size));

    if (m_has_deepstack_layers) {
        CHECK_SUCCESS(prepare_deepstack_inputs(layer_name_to_input_buffer, input_embeddings,
            standalone_frame_embeddings_state, video_embeddings_state));
    }

    std::vector<std::pair<uint32_t, uint32_t>> relative_frames_tokens_indices;
    bool during_frame = false;
    // Edit input_embeddings in place. Go over all indices of input_embeddings. if these indices are of vision tokens, add the corresponding frame embedding.
    for (size_t i = 0; i < input_embeddings.size(); i++) {
        if (0 == input_embeddings[i].size()) { // placeholder for vision token
            MemoryView current_frame;
            uint32_t current_frame_index;
            uint32_t current_emb_index_in_frame;
            if (input_embeddings[i].type() == EmbeddingViewWrapper::EmbeddingType::IMAGE) {
                TRY(auto index_pair, standalone_frame_embeddings_state.get_next_embedding_index());
                std::tie(current_frame_index, current_emb_index_in_frame) = index_pair;
                TRY(current_frame, standalone_frame_embeddings_state.get_frame_embedding_view(current_frame_index));
            } else if (input_embeddings[i].type() == EmbeddingViewWrapper::EmbeddingType::VIDEO) {
                TRY(auto index_pair, video_embeddings_state.get_next_embedding_index());
                std::tie(current_frame_index, current_emb_index_in_frame) = index_pair;
                TRY(current_frame, video_embeddings_state.get_frame_embedding_view(current_frame_index));
            } else {
                CHECK(false, HAILO_INVALID_OPERATION, "Invalid embedding type");
            }
            // View the frame data as a matrix to fetch the appropriate row
            const auto embeddings_per_frame = current_frame.size() / (m_local_cached_embeddings.cols() * sizeof(uint16_t));
            Eigen::Map<const Eigen::Matrix<uint16_t, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>> frame_matrix(
                reinterpret_cast<const uint16_t*>(current_frame.data()), embeddings_per_frame, m_local_cached_embeddings.cols());

            input_embeddings[i] = MemoryView::create_const(frame_matrix.row(current_emb_index_in_frame).data(), frame_matrix.cols() * sizeof(uint16_t));

            if (!during_frame) {
                relative_frames_tokens_indices.emplace_back(i, i);
                during_frame = true;
            } else {
                relative_frames_tokens_indices.back().second++;
            }
        } else {
            during_frame = false;
        }
    }
    update_cache_from_embeddings(input_embeddings);

    TRY(auto embeddings_layer_name, get_layer_name_from_suffix<MemoryView>(m_input_layers_names_suffixes.embeddings, layer_name_to_input_buffer));
    prepare_embeddings_input(layer_name_to_input_buffer[embeddings_layer_name], m_params.prefill_input_tokens_count);

    TRY(auto attention_mask_layer_name, get_layer_name_from_suffix<MemoryView>(m_input_layers_names_suffixes.attention_mask, layer_name_to_input_buffer));
    prepare_attention_mask_input(layer_name_to_input_buffer[attention_mask_layer_name], m_params.prefill_input_tokens_count);
    CHECK_SUCCESS(prepare_positional_embed_inputs(layer_name_to_input_buffer, m_params.prefill_input_tokens_count,
        relative_frames_tokens_indices, static_cast<int>(input_embeddings.size())));

    return HAILO_SUCCESS;
}

} /* namespace genai */
} /* namespace hailort */
