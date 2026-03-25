/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pre_process.hpp
 * @brief Implementation for VLM pre processing
 **/

#ifndef _HAILO_GENAI_VLM_PRE_PROCESS_HPP_
#define _HAILO_GENAI_VLM_PRE_PROCESS_HPP_

#include "hailo/hailort.h"
#include "hailo/buffer.hpp"
#include "hailo/transform.hpp"

#include <unordered_map>

#include "../llm/pre_process.hpp"


namespace hailort
{
namespace genai
{

// TODO: HRT-16260 - Get this info from Hef
static const uint32_t VISION_PATH_SIZE = 14;
static const uint32_t MERGE_SIZE_H = 2;
static const uint32_t MERGE_SIZE_W = 2;

class EmbeddingsVectorState {
public:
    EmbeddingsVectorState(const std::vector<BufferPtr> &embeddings_vector, const std::vector<size_t> &embeddings_count_per_item,
        const std::vector<std::unordered_map<std::string, BufferPtr>> &deepstack_buffers = {}) :
        m_embeddings_vector(embeddings_vector), m_embeddings_count_per_item(embeddings_count_per_item),
        m_deepstack_buffers(deepstack_buffers), m_current_frame_index(0), m_current_embedding_index_in_frame(0) {}

    Expected<std::pair<uint32_t, uint32_t>> get_next_embedding_index()
    {
        CHECK(m_current_frame_index < m_embeddings_count_per_item.size(), HAILO_INVALID_OPERATION,
            "Frame index {} is out of bounds, expected to be less than {}", m_current_frame_index, m_embeddings_count_per_item.size());
        if (m_current_embedding_index_in_frame >= m_embeddings_count_per_item[m_current_frame_index]) {
            m_current_frame_index++;
            m_current_embedding_index_in_frame = 0;
        }
        return std::make_pair(m_current_frame_index, m_current_embedding_index_in_frame++);
    }

    Expected<MemoryView> get_frame_embedding_view(uint32_t frame_index) const
    {
        CHECK(frame_index < m_embeddings_vector.size(), HAILO_INVALID_OPERATION, "Frame index {} is out of bounds, expected to be less than {}", frame_index, m_embeddings_vector.size());
        return MemoryView(m_embeddings_vector[frame_index]);
    }

    Expected<std::reference_wrapper<const std::unordered_map<std::string, BufferPtr>>> get_frame_deepstack_embeddings(uint32_t frame_index) const
    {
        CHECK(!m_deepstack_buffers.empty(), HAILO_INVALID_OPERATION, "No deepstack data was provided for the embeddings");
        CHECK(frame_index < m_deepstack_buffers.size(), HAILO_INVALID_OPERATION,
            "Deepstack frame index {} is out of bounds, expected to be less than {}", frame_index, m_deepstack_buffers.size());
        return std::cref(m_deepstack_buffers[frame_index]);
    }

private:
    std::vector<BufferPtr> m_embeddings_vector;
    std::vector<size_t> m_embeddings_count_per_item;
    std::vector<std::unordered_map<std::string, BufferPtr>> m_deepstack_buffers; // The encoder's outputs buffers for each frame for each deepstack layer
    uint32_t m_current_frame_index;
    uint32_t m_current_embedding_index_in_frame;
};


class VLMPreProcess : public LLMPreProcess
{
public:
    static Expected<std::unique_ptr<LLMPreProcess>> create(const std::map<std::string, size_t> &prefill_inputs_frame_size,
        const std::map<std::string, size_t> &tbt_inputs_frame_size, Eigen::VectorXf &&theta,
        uint32_t text_embeddings_layer_features, const hailo_3d_image_shape_t &input_encoder_shape, uint8_t scaled_mask_value,
        const InputLayersNamesSuffixes &input_layers_names_suffixes, const PreProcessParams &pre_process_params, bool has_deepstack_layers,
        const std::unordered_map<std::string, size_t> &deepstack_suffix_to_tokens_per_frame = {});

    hailo_status prepare_inputs_prefill(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer,
        std::vector<EmbeddingViewWrapper> input_embeddings, EmbeddingsVectorState &standalone_frame_embeddings_state,
        EmbeddingsVectorState &video_embeddings_state);

    VLMPreProcess(Eigen::VectorXf &&theta, Eigen::Matrix<uint16_t, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> &&local_cached_embeddings,
        const std::map<std::string, size_t> &prefill_inputs_frame_size, const std::map<std::string, size_t> &tbt_inputs_frame_size,
        const hailo_3d_image_shape_t &input_encoder_shape, uint8_t scaled_mask_value, const InputLayersNamesSuffixes &input_layers_names_suffixes,
        const PreProcessParams &pre_process_params, bool has_deepstack_layers,
        const std::unordered_map<std::string, size_t> &deepstack_suffix_to_tokens_per_frame);

    bool has_deepstack_layers() const { return m_has_deepstack_layers; }

    VLMPreProcess(VLMPreProcess &&) = default;
    VLMPreProcess(const VLMPreProcess &) = delete;
    VLMPreProcess &operator=(VLMPreProcess &&) = delete;
    VLMPreProcess &operator=(const VLMPreProcess &) = delete;
    virtual ~VLMPreProcess() = default;

private:
    Eigen::Matrix<uint16_t, 1, Eigen::Dynamic, Eigen::RowMajor> get_embedding(int token, const std::vector<MemoryView> &input_frames_embeddings,
        uint32_t &current_frame_index, uint32_t &current_emb_index_in_frame) const;

    hailo_status prepare_positional_embed_inputs(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer, 
        int layer_input_tokens_size, const std::vector<std::pair<uint32_t, uint32_t>> &frames_tokens_indices, int input_tokens_count);

    void fill_temporal_positional_embed(int pos_ids_start_index, int relative_st_image_index,
        int position_ids_features, int merged_patch_size_h, int merged_patch_size_w);

    using SuffixToLayerInfo = std::unordered_map<std::string, std::pair<std::string, size_t>>; // suffix → (layer name, bytes per token)

    hailo_status prepare_deepstack_inputs(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer,
        const std::vector<EmbeddingViewWrapper> &input_embeddings,
        EmbeddingsVectorState &standalone_frame_embeddings_state,
        EmbeddingsVectorState &video_embeddings_state);

    void zero_deepstack_token(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer,
        const std::vector<std::string> &deepstack_suffixes, const SuffixToLayerInfo &suffix_to_layer_info, size_t token_index);

    hailo_status copy_deepstack_vision_token(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer,
        const std::vector<std::string> &deepstack_suffixes, const SuffixToLayerInfo &suffix_to_layer_info,
        size_t token_index, EmbeddingsVectorState &ds_state);

    hailo_3d_image_shape_t m_input_encoder_shape;
    int m_temporal_pos_h;
    int m_temporal_pos_w;
    bool m_has_deepstack_layers;
    std::unordered_map<std::string, size_t> m_deepstack_suffix_to_tokens_per_frame; // suffix → actual token count for that encoder deepstack output
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_VLM_PRE_PROCESS_HPP_ */
