/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
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
#include "common/genai/eigen.hpp"
#include "common/genai/tokenizer/token_embedder.hpp"

namespace hailort
{
namespace genai
{

using layer_name_t = std::string;

enum class InferencePhase { Prefill, TokenByToken };

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
    // Every name ends with the empty string, so an unset suffix would resolve to whatever layer comes first
    CHECK(!suffix.empty(), HAILO_INVALID_ARGUMENT, "Cannot look up an input layer by an empty suffix");

    auto layer_pair = std::find_if(layer_name_to_input_buffer.begin(), layer_name_to_input_buffer.end(),
        [&suffix](const auto &input_pair)
            { return has_suffix(input_pair.first, suffix); });
    CHECK(layer_pair != layer_name_to_input_buffer.end(), HAILO_INVALID_ARGUMENT,
        "Could not find layer with suffix {}", suffix);
    auto name = layer_pair->first;
    return name;
}

using eigen_matrix_2d_u16_t = Eigen::Matrix<uint16_t, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
using eigen_map_2d_u16_t = Eigen::Map<eigen_matrix_2d_u16_t>;
using eigen_tensor_4d_u32_t = Eigen::Tensor<uint32_t, 4, Eigen::RowMajor>;

constexpr std::array<int, 3> MROPE_SECTION_ORIGINAL = {16, 24, 24};

// What a model configuration calls one attention group's layers
struct AttentionLayerSuffixes {
    std::string mask;
    std::string pe_q_cos;
    std::string pe_q_sin;
    std::string pe_k_cos;
    std::string pe_k_sin;
};

// What a model actually calls them, once the suffixes have been matched against its inputs. Distinct from
// AttentionLayerSuffixes so that handing a suffix to a write, or a resolved name to a lookup, does not compile.
struct AttentionLayerNames {
    layer_name_t mask;
    layer_name_t pe_q_cos;
    layer_name_t pe_q_sin;
    layer_name_t pe_k_cos;
    layer_name_t pe_k_sin;
};

struct InputLayersNamesSuffixes {
    std::string embeddings = INPUT_LAYER_EMBEDDINGS_SUFF;
    AttentionLayerSuffixes attention = {INPUT_LAYER_ATTENTION_MASK_SUFF, INPUT_LAYER_PE_Q_COS_SUFF,
        INPUT_LAYER_PE_Q_SIN_SUFF, INPUT_LAYER_PE_K_COS_SUFF, INPUT_LAYER_PE_K_SIN_SUFF};

    std::string deepstack_layer1;
    std::string deepstack_layer2;
    std::string deepstack_layer3;

    std::vector<std::string> deepstack_as_vector() const {
        std::vector<std::string> result;
        if (!deepstack_layer1.empty()) result.push_back(deepstack_layer1);
        if (!deepstack_layer2.empty()) result.push_back(deepstack_layer2);
        if (!deepstack_layer3.empty()) result.push_back(deepstack_layer3);
        return result;
    }
};

// TODO: HRT-20851 - add the sliding-window field, and a kv_cache_size that differs between groups
struct AttentionGroupParams {
    AttentionLayerSuffixes suffixes;
    uint32_t kv_cache_size = 0;
    uint32_t num_attention_heads = 0;
    uint32_t num_key_value_heads = 0;
    std::vector<int> mrope_section;
};

struct PreProcessParams {
    uint32_t kv_cache_size = KV_CACHE_SIZE;
    uint32_t num_attention_heads = NUM_ATTENTION_MASK;
    uint32_t num_key_value_heads = NUM_KEY_VALUE_HEADS;
    uint32_t prefill_input_tokens_count = PREFILL_INPUT_TOKENS_SIZE;
    // Default value that is relevant for VLM only. for LLM we override it in LLMServer::create_pre_process
    std::vector<int> mrope_section = std::vector<int>(MROPE_SECTION_ORIGINAL.begin(), MROPE_SECTION_ORIGINAL.end());

    // Left empty by a model configuration that describes a single group through the flat keys above
    std::vector<AttentionGroupParams> attention_groups;
};

struct AttentionGroupContext {
    AttentionGroupParams params;
    AttentionLayerNames prefill_layers;
    AttentionLayerNames tbt_layers;
    Eigen::VectorXf theta;
    std::vector<int> repeated_mrope_section;
    std::vector<int> cumulative_mrope_section;
    uint8_t scaled_mask_value = 0;
};

class LLMPreProcess
{
public:
    static Expected<std::unique_ptr<LLMPreProcess>> create(
        const std::map<std::string, size_t> &prefill_inputs_frame_size, const std::map<std::string, size_t> &tbt_inputs_frame_size,
        const Eigen::VectorXf &theta, uint32_t embeddings_layer_features, hailo_format_type_t embeddings_layer_type,
        uint8_t scaled_mask_value, const InputLayersNamesSuffixes &input_layers_names_suffixes,
        const PreProcessParams &pre_process_params);

    static Eigen::VectorXf generate_default_theta(); // TODO: HRT-16646 - Remove this flow
    static Eigen::VectorXf generate_theta_from_memview(const MemoryView theta); // TODO: HRT-16646 - Move to c'tor (get theta memview)

    hailo_status prepare_inputs_prefill(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer,
        const std::vector<EmbeddingViewWrapper> &input_tokens_embeddings);

    hailo_status prepare_inputs_tbt(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer, const std::vector<EmbeddingViewWrapper> &input_token_embedding);
    virtual void reset_local_cache();
    static bool is_positional_embed_layer(const std::string &name, const InputLayersNamesSuffixes &input_layers_names_suffixes);
    static bool is_deepstack_layer(const std::string &name, const InputLayersNamesSuffixes &input_layers_names_suffixes);

    std::tuple<size_t, eigen_matrix_2d_u16_t, eigen_tensor_4d_u32_t, int> get_local_cache() const;
    void set_local_cache(size_t cache_size, const eigen_matrix_2d_u16_t &embeddings, const eigen_tensor_4d_u32_t &pos_ids, int timestamp_value);

    size_t cache_usage_size() const {
        return m_cache_usage_size;
    }

    LLMPreProcess(eigen_matrix_2d_u16_t &&local_cached_embeddings,
        const std::map<std::string, size_t> &prefill_inputs_frame_size, const std::map<std::string, size_t> &tbt_inputs_frame_size,
        const InputLayersNamesSuffixes &input_layers_names_suffixes, const PreProcessParams &pre_process_params,
        std::vector<AttentionGroupContext> &&groups);

    LLMPreProcess(LLMPreProcess &&) = default;
    LLMPreProcess(const LLMPreProcess &) = delete;
    LLMPreProcess &operator=(LLMPreProcess &&) = delete;
    LLMPreProcess &operator=(const LLMPreProcess &) = delete;
    virtual ~LLMPreProcess() = default;

protected:
    static hailo_status validate_inputs_layout(const std::map<std::string, size_t> &prefill_inputs_frame_size,
        const std::map<std::string, size_t> &tbt_inputs_frame_size,
        const InputLayersNamesSuffixes &input_layers_names_suffixes, const PreProcessParams &pre_process_params,
        const std::vector<AttentionGroupParams> &attention_groups, uint32_t embeddings_layer_features,
        hailo_format_type_t embeddings_layer_type, size_t theta_size);

    // The flat attention keys describe a single group, which a configuration declaring its own groups replaces
    // outright. Layer classification and the server's mask quantization lookup still read those keys directly, so
    // they see only the first group.
    // TODO: HRT-20850 - make layer classification group-aware
    // TODO: HRT-20849 - make the mask quantization lookup group-aware
    static std::vector<AttentionGroupParams> build_attention_groups(
        const InputLayersNamesSuffixes &input_layers_names_suffixes, const PreProcessParams &pre_process_params);

    static Expected<std::vector<AttentionGroupContext>> discover_layer_groups(const Eigen::VectorXf &theta,
        const std::map<std::string, size_t> &prefill_inputs_frame_size,
        const std::map<std::string, size_t> &tbt_inputs_frame_size, uint8_t scaled_mask_value,
        const std::vector<AttentionGroupParams> &attention_groups);

    static void tile_along_last_axis(const Eigen::Tensor<float32_t, 4, Eigen::RowMajor> &input, int heads_count, MemoryView &layer_buffer);

    hailo_status validate_inputs(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer, const std::map<std::string, size_t> &expected_sizes);
    void update_cache_from_embeddings(const std::vector<EmbeddingViewWrapper> &embedding_rows_views);
    void prepare_embeddings_input(MemoryView &layer_buffer, uint32_t number_of_tokens);
    int layer_input_tokens_count(InferencePhase phase) const;

    hailo_status prepare_attention_mask_inputs(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer,
        InferencePhase phase);
    void write_attention_mask(const AttentionGroupContext &group, MemoryView &layer_buffer,
        int layer_input_tokens_size);
    hailo_status prepare_positional_embed_inputs(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer,
        InferencePhase phase, int input_tokens_count);

    void shift_local_cached_positional_embeds(int tokens_count);
    void incremental_positional_embed(int start_index, int tokens_count);
    std::pair<Eigen::Tensor<float32_t, 4, Eigen::RowMajor>, Eigen::Tensor<float32_t, 4, Eigen::RowMajor>>
        angular_positional_embed(const AttentionGroupContext &group, InferencePhase phase);

    // The text-only and multimodal paths compute the position ids their own way and re-converge here, so this is
    // the only place the rope layers are written
    hailo_status fill_positional_embed_inputs(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer,
        InferencePhase phase);

    size_t m_cache_usage_size;

    // TODO (HRT-16835) - consider using circ buffer
    eigen_matrix_2d_u16_t m_local_cached_embeddings;
    eigen_tensor_4d_u32_t m_local_cached_pos_ids;

    const std::map<std::string, size_t> m_prefill_inputs_frame_size;
    const std::map<std::string, size_t> m_tbt_inputs_frame_size;

    int m_current_timestamp_value;

    std::vector<AttentionGroupContext> m_groups;

    InputLayersNamesSuffixes m_input_layers_names_suffixes;
    PreProcessParams m_params;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_LLM_PRE_PROCESS_HPP_ */
