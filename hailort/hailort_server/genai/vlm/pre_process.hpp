/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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

#include "../llm/pre_process.hpp"


namespace hailort
{
namespace genai
{

// TODO: HRT-16260 - Get this info from Hef
static const uint32_t VISION_PATH_SIZE = 14;
static const uint32_t MERGE_SIZE_H = 2;
static const uint32_t MERGE_SIZE_W = 2;

class VLMPreProcess : public LLMPreProcess
{
public:
    static Expected<std::unique_ptr<LLMPreProcess>> create(const std::map<std::string, size_t> &prefill_inputs_frame_size,
        const std::map<std::string, size_t> &tbt_inputs_frame_size, Eigen::VectorXf &&theta,
        uint32_t text_embeddings_layer_features, const hailo_3d_image_shape_t &input_encoder_shape, uint8_t scaled_mask_value,
        const InputLayersNamesSuffixes &input_layers_names_suffixes, const PreProcessParams &pre_process_params);

    hailo_status prepare_inputs_prefill(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer,
        std::vector<MemoryView> input_embeddings, const std::vector<MemoryView> &input_frames_embeddings,
        uint32_t &current_frame_index, uint32_t &current_emb_index_in_frame);

    VLMPreProcess(Eigen::VectorXf &&theta, Eigen::Matrix<uint16_t, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> &&local_cached_embeddings,
        const std::map<std::string, size_t> &prefill_inputs_frame_size, const std::map<std::string, size_t> &tbt_inputs_frame_size,
        const hailo_3d_image_shape_t &input_encoder_shape, uint8_t scaled_mask_value, const InputLayersNamesSuffixes &input_layers_names_suffixes,
        const PreProcessParams &pre_process_params);

    inline uint32_t embeddings_per_frame() const
    {
        return (m_input_encoder_shape.height * m_input_encoder_shape.width) /
            (VISION_PATH_SIZE * VISION_PATH_SIZE * MERGE_SIZE_H * MERGE_SIZE_W);
    }

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

    hailo_3d_image_shape_t m_input_encoder_shape;
    int m_temporal_pos_h;
    int m_temporal_pos_w;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_VLM_PRE_PROCESS_HPP_ */
