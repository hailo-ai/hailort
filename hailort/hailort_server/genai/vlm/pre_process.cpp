/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pre_process.cpp
 * @brief Implementation for VLM pre processing
 **/

#include "pre_process.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace hailort
{
namespace genai
{

static constexpr uint32_t NO_PER_LAYER_INPUTS_FEATURES = 0;

static constexpr size_t UINT16_CODES_COUNT = static_cast<size_t>(UINT16_MAX) + 1;

static bool does_patch_tile_frame(const hailo_3d_image_shape_t &encoder_input_shape, uint32_t patch_size,
    size_t embeddings_per_frame)
{
    if (0 == patch_size) {
        return false;
    }
    const auto merged_patch_h = patch_size * MERGE_SIZE_H;
    const auto merged_patch_w = patch_size * MERGE_SIZE_W;
    const bool is_tiled = (0 == encoder_input_shape.height % merged_patch_h) &&
        (0 == encoder_input_shape.width % merged_patch_w);
    const auto merged_patches_count = static_cast<size_t>(encoder_input_shape.height / merged_patch_h) *
        (encoder_input_shape.width / merged_patch_w);
    return is_tiled && (embeddings_per_frame == merged_patches_count);
}

// Patches are square, so the frame area per embedding fixes the patch side
Expected<uint32_t> resolve_vision_patch_size(const hailo_3d_image_shape_t &encoder_input_shape,
    size_t encoder_embeddings_per_frame)
{
    CHECK(0 != encoder_embeddings_per_frame, HAILO_INVALID_HEF, "The vision encoder outputs no embeddings per frame");
    const auto pixels_per_patch = static_cast<double>(encoder_input_shape.height) * encoder_input_shape.width /
        static_cast<double>(encoder_embeddings_per_frame * MERGE_SIZE_H * MERGE_SIZE_W);
    const auto patch_size = static_cast<uint32_t>(std::lround(std::sqrt(pixels_per_patch)));
    CHECK(does_patch_tile_frame(encoder_input_shape, patch_size, encoder_embeddings_per_frame), HAILO_INVALID_HEF,
        "No vision patch size tiles the encoder frame {}x{} into {} embeddings", encoder_input_shape.height,
        encoder_input_shape.width, encoder_embeddings_per_frame);
    return Expected<uint32_t>(patch_size);
}

EmbeddingsRequantization::EmbeddingsRequantization(BufferPtr decoder_code_by_encoder_code) :
    m_decoder_code_by_encoder_code(std::move(decoder_code_by_encoder_code))
{}

Expected<EmbeddingsRequantization> EmbeddingsRequantization::create(const hailo_quant_info_t &encoder_quant,
    const hailo_quant_info_t &decoder_quant)
{
    const float32_t scale = encoder_quant.qp_scale / decoder_quant.qp_scale;
    const float32_t offset = decoder_quant.qp_zp - (encoder_quant.qp_zp * scale);
    if ((1.0f == scale) && (0.0f == offset)) {
        return EmbeddingsRequantization();
    }

    TRY(auto decoder_code_by_encoder_code, Buffer::create_shared(UINT16_CODES_COUNT * sizeof(uint16_t)));
    auto *decoder_codes = decoder_code_by_encoder_code->as_pointer<uint16_t>();
    for (size_t code = 0; code < UINT16_CODES_COUNT; code++) {
        const auto decoder_code = std::round((scale * static_cast<float32_t>(code)) + offset);
        decoder_codes[code] = static_cast<uint16_t>(std::clamp(decoder_code, 0.0f, static_cast<float32_t>(UINT16_MAX)));
    }
    return EmbeddingsRequantization(std::move(decoder_code_by_encoder_code));
}

bool EmbeddingsRequantization::is_needed() const
{
    return nullptr != m_decoder_code_by_encoder_code;
}

void EmbeddingsRequantization::apply(MemoryView codes) const
{
    if (!is_needed()) {
        return;
    }
    const auto *decoder_codes = m_decoder_code_by_encoder_code->as_pointer<uint16_t>();
    auto *code_values = reinterpret_cast<uint16_t*>(codes.data());
    const auto codes_count = codes.size() / sizeof(uint16_t);
    for (size_t i = 0; i < codes_count; i++) {
        code_values[i] = decoder_codes[code_values[i]];
    }
}

static size_t vision_rows_per_frame(size_t frame_size, size_t features)
{
    return frame_size / (features * sizeof(uint16_t));
}

static hailo_status validate_deepstack_layer(const std::string &suffix,
    const std::map<std::string, size_t> &prefill_inputs_frame_size, uint32_t prefill_input_tokens_count,
    const VisionEncoderLayout &vision_encoder_layout)
{
    TRY(const auto layer_name, get_layer_name_from_suffix<size_t>(suffix, prefill_inputs_frame_size),
        "No prefill input carries deepstack suffix '{}'", suffix);
    const auto encoder_output = vision_encoder_layout.deepstack.find(suffix);
    CHECK(encoder_output != vision_encoder_layout.deepstack.end(), HAILO_INVALID_ARGUMENT,
        "Deepstack input layer '{}' (suffix '{}') has no vision encoder output feeding it", layer_name, suffix);

    const auto &[encoder_frame_size, tokens_per_frame] = encoder_output->second;
    CHECK(0 != tokens_per_frame, HAILO_INVALID_ARGUMENT,
        "The vision encoder deepstack output feeding decoder input suffix '{}' holds 0 tokens per frame", suffix);
    CHECK(0 == (encoder_frame_size % tokens_per_frame), HAILO_INVALID_ARGUMENT,
        "The vision encoder deepstack output feeding decoder input suffix '{}' has a frame size of {} bytes, which "
        "does not divide evenly into its {} tokens", suffix, encoder_frame_size, tokens_per_frame);

    const auto decoder_frame_size = prefill_inputs_frame_size.at(layer_name);
    CHECK(0 == (decoder_frame_size % prefill_input_tokens_count), HAILO_INVALID_ARGUMENT,
        "Deepstack input layer '{}' has a frame size of {} bytes, which does not divide evenly into its {} prefill "
        "tokens", layer_name, decoder_frame_size, prefill_input_tokens_count);

    const auto encoder_bytes_per_token = encoder_frame_size / tokens_per_frame;
    const auto decoder_bytes_per_token = decoder_frame_size / prefill_input_tokens_count;
    CHECK(encoder_bytes_per_token == decoder_bytes_per_token, HAILO_INVALID_ARGUMENT,
        "Deepstack input layer '{}' (suffix '{}') takes {} bytes per token, but the vision encoder deepstack output "
        "feeding it holds {} bytes per token", layer_name, suffix, decoder_bytes_per_token, encoder_bytes_per_token);
    CHECK(tokens_per_frame == vision_encoder_layout.embeddings_per_frame, HAILO_INVALID_ARGUMENT,
        "The vision encoder deepstack output feeding decoder input suffix '{}' holds {} tokens per frame, but the "
        "vision embeddings output holds {} tokens per frame", suffix, tokens_per_frame,
        vision_encoder_layout.embeddings_per_frame);

    return HAILO_SUCCESS;
}

// Each vision token copies one encoder deepstack row into its own decoder slot, so a row must be exactly one slot
// wide and the encoder must hold one deepstack row per vision embedding of the frame
static hailo_status validate_deepstack_layout(const std::map<std::string, size_t> &prefill_inputs_frame_size,
    const InputLayersNamesSuffixes &input_layers_names_suffixes, uint32_t prefill_input_tokens_count,
    uint32_t text_embeddings_layer_features, const VisionEncoderLayout &vision_encoder_layout)
{
    CHECK(0 != prefill_input_tokens_count, HAILO_INVALID_ARGUMENT, "The prefill input holds 0 tokens");
    CHECK(0 != text_embeddings_layer_features, HAILO_INVALID_ARGUMENT, "The embeddings input holds 0 features");
    const auto deepstack_suffixes = input_layers_names_suffixes.deepstack_as_vector();
    CHECK(!deepstack_suffixes.empty(), HAILO_INVALID_ARGUMENT,
        "The model has deepstack layers, but no deepstack input suffixes were provided");
    const auto vision_row_size = text_embeddings_layer_features * sizeof(uint16_t);
    CHECK(0 == (vision_encoder_layout.embeddings_frame_size % vision_row_size), HAILO_INVALID_ARGUMENT,
        "The vision encoder embeddings output has a frame size of {} bytes, which does not divide evenly into rows "
        "of {} bytes", vision_encoder_layout.embeddings_frame_size, vision_row_size);

    for (const auto &suffix : deepstack_suffixes) {
        CHECK_SUCCESS(validate_deepstack_layer(suffix, prefill_inputs_frame_size, prefill_input_tokens_count,
            vision_encoder_layout));
    }

    return HAILO_SUCCESS;
}

Expected<size_t> find_start_of_reasoning_row(const std::vector<int> &tokens,
    const std::vector<EmbeddingViewWrapper> &embeddings, size_t start_of_reasoning_token_index)
{
    CHECK(start_of_reasoning_token_index < tokens.size(), HAILO_INVALID_ARGUMENT,
        "The reasoning start index {} is past the prompt's {} tokens", start_of_reasoning_token_index, tokens.size());
    const auto rows_from_reasoning_start = tokens.size() - start_of_reasoning_token_index;
    CHECK(rows_from_reasoning_start <= embeddings.size(), HAILO_INVALID_ARGUMENT,
        "Got {} embedding rows, expected at least {}: one for the reasoning start and one per token after it",
        embeddings.size(), rows_from_reasoning_start);

    size_t start_of_reasoning_row = embeddings.size() - rows_from_reasoning_start;
    const auto is_vision_row = [](const EmbeddingViewWrapper &embedding) {
        return (EmbeddingViewWrapper::EmbeddingType::IMAGE == embedding.type()) ||
            (EmbeddingViewWrapper::EmbeddingType::VIDEO == embedding.type());
    };
    CHECK(std::none_of(embeddings.begin() + static_cast<std::ptrdiff_t>(start_of_reasoning_row), embeddings.end(),
        is_vision_row), HAILO_INVALID_ARGUMENT, "An image or video follows the prompt's reasoning start");

    return start_of_reasoning_row;
}

Expected<std::unique_ptr<LLMPreProcess>> VLMPreProcess::create(const std::map<std::string, size_t> &prefill_inputs_frame_size,
    const std::map<std::string, size_t> &tbt_inputs_frame_size, const std::vector<Eigen::VectorXf> &group_thetas,
    uint32_t text_embeddings_layer_features, hailo_format_type_t text_embeddings_layer_type,
    const EmbeddingTables &embedding_tables, const hailo_3d_image_shape_t &input_encoder_shape,
    const std::vector<AttentionGroupMaskValues> &group_mask_values,
    const InputLayersNamesSuffixes &input_layers_names_suffixes, const PreProcessParams &pre_process_params,
    const std::vector<AttentionGroupParams> &attention_groups, bool has_deepstack_layers,
    const VisionEncoderLayout &vision_encoder_layout)
{
    // TODO: HRT-21092 - write the per-layer inputs layer in VLM pre-process
    CHECK(!embedding_tables.per_layer_inputs, HAILO_NOT_IMPLEMENTED,
        "A per-layer inputs table is not supported in VLM yet");
    CHECK_SUCCESS(validate_inputs_layout(prefill_inputs_frame_size, tbt_inputs_frame_size,
        embedding_tables, NO_PER_LAYER_INPUTS_FEATURES, pre_process_params, attention_groups,
        text_embeddings_layer_features, text_embeddings_layer_type, group_thetas, group_mask_values));
    if (has_deepstack_layers) {
        CHECK_SUCCESS(validate_deepstack_layout(prefill_inputs_frame_size, input_layers_names_suffixes,
            pre_process_params.prefill_input_tokens_count, text_embeddings_layer_features, vision_encoder_layout));
    }
    TRY(auto groups, discover_layer_groups(group_thetas, prefill_inputs_frame_size, tbt_inputs_frame_size,
        group_mask_values, attention_groups));

    auto cols = text_embeddings_layer_features;

    Eigen::Matrix<uint16_t, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> local_cached_embeddings =
        Eigen::Matrix<uint16_t, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>(pre_process_params.prefill_input_tokens_count, cols);

    TRY(const auto vision_patch_size, resolve_vision_patch_size(input_encoder_shape,
        vision_encoder_layout.embeddings_per_frame));
    auto ptr = make_unique_nothrow<VLMPreProcess>(std::move(local_cached_embeddings),
        prefill_inputs_frame_size, tbt_inputs_frame_size, input_encoder_shape, embedding_tables,
        input_layers_names_suffixes, pre_process_params, std::move(groups), has_deepstack_layers, vision_patch_size);
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY); // Consider returning different status

    return std::unique_ptr<LLMPreProcess>(std::move(ptr));
}

void VLMPreProcess::reset_local_cache()
{
    LLMPreProcess::reset_local_cache();
    m_temporal_pos_h = 0;
    m_temporal_pos_w = 0;
}

VLMPreProcess::VLMPreProcess(Eigen::Matrix<uint16_t, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> &&local_cached_embeddings,
    const std::map<std::string, size_t> &prefill_inputs_frame_size, const std::map<std::string, size_t> &tbt_inputs_frame_size,
    const hailo_3d_image_shape_t &input_encoder_shape, const EmbeddingTables &embedding_tables,
    const InputLayersNamesSuffixes &input_layers_names_suffixes, const PreProcessParams &pre_process_params,
    std::vector<AttentionGroupContext> &&groups, bool has_deepstack_layers, uint32_t vision_patch_size) :
        LLMPreProcess(std::move(local_cached_embeddings), prefill_inputs_frame_size, tbt_inputs_frame_size,
            embedding_tables, NO_PER_LAYER_INPUTS_FEATURES, input_layers_names_suffixes, pre_process_params,
            std::move(groups)),
        m_input_encoder_shape(input_encoder_shape),
        m_vision_patch_size(vision_patch_size),
        m_temporal_pos_h(0),
        m_temporal_pos_w(0),
        m_has_deepstack_layers(has_deepstack_layers)
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

hailo_status VLMPreProcess::prepare_positional_embed_inputs_mrope(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer,
    InferencePhase phase, const std::vector<std::pair<uint32_t, uint32_t>> &frames_tokens_indices, int input_tokens_count)
{
    shift_local_cached_positional_embeds(input_tokens_count);

    int position_ids_height = static_cast<int>(m_local_cached_pos_ids.dimension(0));
    int position_ids_features = static_cast<int>(m_local_cached_pos_ids.dimension(2));

    auto pos_ids_start_index = static_cast<int>(position_ids_features - input_tokens_count);

    int merged_patch_size_h = m_input_encoder_shape.height / (m_vision_patch_size * MERGE_SIZE_H);
    int merged_patch_size_w = m_input_encoder_shape.width / (m_vision_patch_size * MERGE_SIZE_W);

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

    return fill_positional_embed_inputs(layer_name_to_input_buffer, phase);
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

    // A chunk shorter than the prefill input fills its last slots: the embeddings, position ids and mask are all
    // bottom-aligned, so the deepstack rows have to be as well or each token adds another token's vision features
    const auto first_token_slot = m_params.prefill_input_tokens_count - input_embeddings.size();
    for (size_t slot = 0; slot < m_params.prefill_input_tokens_count; slot++) {
        const auto *embedding = (slot >= first_token_slot) ? &input_embeddings[slot - first_token_slot] : nullptr;
        bool is_vision_token = (embedding != nullptr) &&
            (embedding->type() == EmbeddingViewWrapper::EmbeddingType::IMAGE ||
             embedding->type() == EmbeddingViewWrapper::EmbeddingType::VIDEO);

        if (!is_vision_token) {
            zero_deepstack_token(layer_name_to_input_buffer, deepstack_suffixes, suffix_to_layer_info, slot);
            continue;
        }

        EmbeddingsVectorState *ds_state = nullptr;
        if (embedding->type() == EmbeddingViewWrapper::EmbeddingType::IMAGE) {
            ds_state = &standalone_ds_state;
        } else if (embedding->type() == EmbeddingViewWrapper::EmbeddingType::VIDEO) {
            ds_state = &video_ds_state;
        }
        CHECK(ds_state != nullptr, HAILO_INVALID_OPERATION, "Invalid embedding type for deepstack");

        CHECK_SUCCESS(copy_deepstack_vision_token(layer_name_to_input_buffer, deepstack_suffixes, suffix_to_layer_info, slot, *ds_state));
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
        const auto &[layer_name, bytes_per_token] = suffix_to_layer_info.at(suffix);
        auto &dst_buffer = layer_name_to_input_buffer[layer_name];

        const auto src_offset = emb_index_in_frame * bytes_per_token;
        CHECK((src_offset + bytes_per_token) <= src_buffer->size(), HAILO_INVALID_ARGUMENT,
            "Deepstack row {} of suffix '{}' ends at byte {}, past the {} bytes of its vision encoder buffer",
            emb_index_in_frame, suffix, src_offset + bytes_per_token, src_buffer->size());

        auto *dst_ptr = dst_buffer.data() + (token_index * bytes_per_token);
        std::memcpy(dst_ptr, src_buffer->data() + src_offset, bytes_per_token);
    }

    return HAILO_SUCCESS;
}

// 'input_embeddings' already have placeholders for the vision tokens inside
hailo_status VLMPreProcess::prepare_inputs_prefill_multimodal(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer,
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
            const auto embeddings_per_frame = vision_rows_per_frame(current_frame.size(),
                static_cast<size_t>(m_local_cached_embeddings.cols()));
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
    CHECK_SUCCESS(update_cache_from_embeddings(input_embeddings));

    TRY(auto embeddings_layer_name, get_layer_name_from_suffix<MemoryView>(
        m_embedding_tables.token_embeddings.input_layer_suffix, layer_name_to_input_buffer));
    prepare_embeddings_input(layer_name_to_input_buffer[embeddings_layer_name], m_params.prefill_input_tokens_count);

    CHECK_SUCCESS(prepare_attention_mask_inputs(layer_name_to_input_buffer, InferencePhase::Prefill));
    CHECK_SUCCESS(prepare_positional_embed_inputs_mrope(layer_name_to_input_buffer, InferencePhase::Prefill,
        relative_frames_tokens_indices, static_cast<int>(input_embeddings.size())));

    return HAILO_SUCCESS;
}

} /* namespace genai */
} /* namespace hailort */
