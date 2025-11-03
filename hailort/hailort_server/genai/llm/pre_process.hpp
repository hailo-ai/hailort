/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pre_process.hpp
 * @brief Implementation for LLM pre processing
 **/

#ifndef _HAILO_GENAI_LLM_PRE_PROCESS_HPP_
#define _HAILO_GENAI_LLM_PRE_PROCESS_HPP_

#include "hailo/hailort.h"
#include "hailo/buffer.hpp"
#include "common/utils.hpp"
#include "eigen.hpp"

namespace hailort
{
namespace genai
{

using layer_name_t = std::string;

// TODO: HRT-16260 - Get this info from Hef
static const std::string INPUT_LAYER_EMBEDDINGS_SUFF = "input_layer1";
static const std::string INPUT_LAYER_ATTENTION_MASK_SUFF = "input_layer2";
static const std::string INPUT_LAYER_PE_Q_COS_SUFF = "input_layer3";
static const std::string INPUT_LAYER_PE_Q_SIN_SUFF = "input_layer4";
static const std::string INPUT_LAYER_PE_K_COS_SUFF = "input_layer5";
static const std::string INPUT_LAYER_PE_K_SIN_SUFF = "input_layer6";

static constexpr int KV_CACHE_SIZE = 2048; // TODO: HRT-16287 - Use getter
static constexpr int NUM_ATTENTION_MASK = 12;
static constexpr int NUM_KEY_VALUE_HEADS = 2;
static constexpr int PREFILL_INPUT_TOKENS_SIZE = 96;

static constexpr int TILE_SIZE = 64;
static constexpr int TBT_INPUT_TOKENS_SIZE = 1;

template<typename T>
Expected<std::string> get_layer_name_from_suffix(const std::string &suffix, const std::map<std::string, T> &layer_name_to_input_buffer)
{
    auto layer_pair = std::find_if(layer_name_to_input_buffer.begin(), layer_name_to_input_buffer.end(),
        [&suffix](const auto &input_pair)
            { return has_suffix(input_pair.first, suffix); });
    CHECK_AS_EXPECTED(layer_pair != layer_name_to_input_buffer.end(), HAILO_INVALID_ARGUMENT, "Could not find layer with suffix {}", suffix);
    auto name = layer_pair->first;
    return name;
}

using eigen_matrix_2d_u16_t = Eigen::Matrix<uint16_t, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
using eigen_map_2d_u16_t = Eigen::Map<eigen_matrix_2d_u16_t>;
using eigen_tensor_4d_u32_t = Eigen::Tensor<uint32_t, 4, Eigen::RowMajor>;

constexpr std::array<int, 6> MROPE_SECTION_ORIGINAL = {16, 24, 24, 16, 24, 24}; // TODO (HRT-17263): Get from HEF

struct InputLayersNamesSuffixes {
    std::string embeddings = INPUT_LAYER_EMBEDDINGS_SUFF;
    std::string attention_mask = INPUT_LAYER_ATTENTION_MASK_SUFF;
    std::string pe_q_cos = INPUT_LAYER_PE_Q_COS_SUFF;
    std::string pe_q_sin = INPUT_LAYER_PE_Q_SIN_SUFF;
    std::string pe_k_cos = INPUT_LAYER_PE_K_COS_SUFF;
    std::string pe_k_sin = INPUT_LAYER_PE_K_SIN_SUFF;
};

struct PreProcessParams {
    uint32_t kv_cache_size = KV_CACHE_SIZE;
    uint32_t num_attention_heads = NUM_ATTENTION_MASK;
    uint32_t num_key_value_heads = NUM_KEY_VALUE_HEADS;
    uint32_t prefill_input_tokens_count = PREFILL_INPUT_TOKENS_SIZE;
};

class LLMPreProcess
{
public:
    static Expected<std::unique_ptr<LLMPreProcess>> create(
        const std::map<std::string, size_t> &prefill_inputs_frame_size, const std::map<std::string, size_t> &tbt_inputs_frame_size,
        Eigen::VectorXf &&theta, uint32_t embeddings_layer_features, hailo_format_type_t embeddings_layer_type, uint8_t scaled_mask_value,
        const InputLayersNamesSuffixes &input_layers_names_suffixes, const PreProcessParams &pre_process_params);

    static Eigen::VectorXf generate_default_theta(); // TODO: HRT-16646 - Remove this flow
    static Eigen::VectorXf generate_theta_from_memview(const MemoryView theta); // TODO: HRT-16646 - Move to c'tor (get theta memview)

    hailo_status prepare_inputs_prefill(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer,
        const std::vector<MemoryView> &input_tokens_embeddings);

    hailo_status prepare_inputs_tbt(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer, const std::vector<MemoryView> &input_token_embedding);
    void reset_local_cache();
    static bool is_positional_embed_layer(const std::string &name, const InputLayersNamesSuffixes &input_layers_names_suffixes);

    std::tuple<size_t, eigen_matrix_2d_u16_t, eigen_tensor_4d_u32_t, int> get_local_cache() const;
    void set_local_cache(size_t cache_size, const eigen_matrix_2d_u16_t &embeddings, const eigen_tensor_4d_u32_t &pos_ids, int timestamp_value);

    size_t cache_usage_size() const {
        return m_cache_usage_size;
    }

    LLMPreProcess(Eigen::VectorXf &&theta, eigen_matrix_2d_u16_t &&local_cached_embeddings,
        const std::map<std::string, size_t> &prefill_inputs_frame_size, const std::map<std::string, size_t> &tbt_inputs_frame_size,
        uint8_t scaled_mask_value, const InputLayersNamesSuffixes &input_layers_names_suffixes, const PreProcessParams &pre_process_params);

    LLMPreProcess(LLMPreProcess &&) = default;
    LLMPreProcess(const LLMPreProcess &) = delete;
    LLMPreProcess &operator=(LLMPreProcess &&) = delete;
    LLMPreProcess &operator=(const LLMPreProcess &) = delete;
    virtual ~LLMPreProcess() = default;

protected:
    static hailo_status validate_inputs_names(const std::map<std::string, size_t> &inputs_map, const InputLayersNamesSuffixes &input_layers_names_suffixes);

    static void tile_along_last_axis(const Eigen::Tensor<float32_t, 4, Eigen::RowMajor> &input, int groups, MemoryView &layer_buffer);

    hailo_status validate_inputs(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer, const std::map<std::string, size_t> &expected_sizes);
    void update_cache_from_embeddings(const std::vector<MemoryView> &embedding_rows_views);
    void prepare_embeddings_input(MemoryView &layer_buffer, uint32_t number_of_tokens);
    void prepare_attention_mask_input(MemoryView &layer_buffer, int layer_input_tokens_size);
    hailo_status prepare_positional_embed_inputs(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer, int layer_input_tokens_size,
        int input_tokens_count);

    void shift_local_cached_positional_embeds(int tokens_count);
    void incremental_positional_embed(int start_index, int tokens_count);
    std::pair<Eigen::Tensor<float32_t, 4, Eigen::RowMajor>, Eigen::Tensor<float32_t, 4, Eigen::RowMajor>>
        angular_positional_embed(int layer_input_tokens_size);

    hailo_status fill_positional_embed(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer,
        const Eigen::Tensor<float32_t, 4, Eigen::RowMajor> &rope_cos, const Eigen::Tensor<float32_t, 4, Eigen::RowMajor> &rope_sin);

    size_t m_cache_usage_size;

    // TODO (HRT-16835) - consider using circ buffer
    eigen_matrix_2d_u16_t m_local_cached_embeddings;
    eigen_tensor_4d_u32_t m_local_cached_pos_ids;

    Eigen::VectorXf m_theta;
    const std::map<std::string, size_t> m_prefill_inputs_frame_size;
    const std::map<std::string, size_t> m_tbt_inputs_frame_size;

    std::vector<int> m_mrope_section;
    int m_current_timestamp_value;
    uint8_t m_scaled_mask_value;

    InputLayersNamesSuffixes m_input_layers_names_suffixes;
    PreProcessParams m_params;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_LLM_PRE_PROCESS_HPP_ */
