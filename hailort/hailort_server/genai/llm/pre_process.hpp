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
#include "common/genai/constants.hpp"
#include "common/genai/config/embedding_tables.hpp"
#include "common/genai/tokenizer/token_embedder.hpp"

#include <optional>
#include <string>

namespace hailort
{
namespace genai
{

using layer_name_t = std::string;

enum class InferencePhase { Prefill, TokenByToken };

// TODO: HRT-16260 - Get this info from Hef
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
static constexpr const char *MISSING_TBT_NETWORK_ERROR =
    "The model carries no token-by-token network, which pre-process requires";

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

struct AttentionGroupParams {
    std::string name;
    AttentionLayerSuffixes suffixes;
    uint32_t kv_cache_size = 0;
    // Zero means the group attends over the whole of its cache
    uint32_t sliding_window = 0;
    // How many copies of one mask row or one theta vector fill the layer's feature axis. The compiler decides this,
    // so it can differ from the model's attention head count.
    uint32_t mask_frame_repeat = 0;
    uint32_t rope_q_frame_repeat = 0;
    uint32_t rope_k_frame_repeat = 0;
    std::string rope_theta;
    hailo_format_type_t rope_theta_dtype = HAILO_FORMAT_TYPE_AUTO;
    std::vector<uint32_t> rope_theta_shape;
    std::vector<int> mrope_section;
};

// An attended and a masked element as one mask layer holds them, quantized at that layer's element type
struct AttentionMaskValues {
    hailo_format_type_t element_type = HAILO_FORMAT_TYPE_AUTO;
    uint16_t attended = 0;
    uint16_t masked = 0;
};

// A group's mask values per phase, since each phase's network carries its own mask layer
struct AttentionGroupMaskValues {
    AttentionMaskValues prefill;
    AttentionMaskValues tbt;
};

// A group a model configuration leaves unnamed is identified by its position in the list
inline std::string attention_group_name(const AttentionGroupParams &group, size_t group_index)
{
    return group.name.empty() ? std::to_string(group_index) : group.name;
}

struct PreProcessParams {
    uint32_t kv_cache_size = KV_CACHE_SIZE;
    uint32_t num_attention_heads = NUM_ATTENTION_MASK;
    uint32_t num_key_value_heads = NUM_KEY_VALUE_HEADS;
    uint32_t prefill_input_tokens_count = PREFILL_INPUT_TOKENS_SIZE;
    // Defaults to the Qwen2-VL partition; empty means a single section spanning each theta half
    std::vector<int> mrope_section = std::vector<int>(MROPE_SECTION_ORIGINAL.begin(), MROPE_SECTION_ORIGINAL.end());

    // Left empty by a model configuration that describes a single group through the flat keys above
    std::vector<AttentionGroupParams> attention_groups;
};

// The groups a model configuration describes: the ones it declares, or the single group its flat keys describe
std::vector<AttentionGroupParams> build_attention_groups(const InputLayersNamesSuffixes &input_layers_names_suffixes,
    const PreProcessParams &pre_process_params);

// The number of tokens the model can hold in context: the shallowest cache among groups declaring no sliding
// window. A sliding group's ring keeps wrapping, so it never bounds the context, even one as wide as its cache.
Expected<uint32_t> context_capacity(const std::vector<AttentionGroupParams> &attention_groups);

// Quantizes the attended and masked values through a mask layer, saturating to its element's range
Expected<AttentionMaskValues> quantize_attention_mask_values(hailo_format_type_t element_type,
    const hailo_quant_info_t &quant_info);

struct CacheRollbackLimit {
    std::string group_name;
    uint64_t retained_tokens_count;
    uint64_t absorbable_rollback_tokens_count;
};

// Rewinding the cache restores positions, not entries: a ring may have wrapped over tokens the next prefill reads,
// all of them for a global group. Yields the first group whose ring cannot absorb the rollback.
std::optional<CacheRollbackLimit> find_exceeded_cache_rollback_limit(
    const std::vector<AttentionGroupParams> &attention_groups, uint32_t prefill_input_tokens_count,
    size_t cache_usage_after_rollback, size_t rollback_tokens_count);

struct AttentionGroupContext {
    AttentionGroupParams params;
    AttentionLayerNames prefill_layers;
    AttentionLayerNames tbt_layers;
    Eigen::VectorXf theta;
    std::vector<int> repeated_mrope_section;
    std::vector<int> cumulative_mrope_section;
    AttentionGroupMaskValues mask_values;
};

// Everything the pre-process rolls forward between inferences, so a restore rewinds all of it together
struct PreProcessCache {
    size_t usage_size = 0;
    eigen_matrix_2d_u16_t embeddings;
    eigen_matrix_2d_u16_t per_layer_inputs;
    eigen_tensor_4d_u32_t pos_ids;
    int timestamp_value = 0;
};

class LLMPreProcess
{
public:
    static Expected<std::unique_ptr<LLMPreProcess>> create(
        const std::map<std::string, size_t> &prefill_inputs_frame_size, const std::map<std::string, size_t> &tbt_inputs_frame_size,
        const std::vector<Eigen::VectorXf> &group_thetas, uint32_t embeddings_layer_features,
        hailo_format_type_t embeddings_layer_type, const EmbeddingTables &embedding_tables,
        uint32_t per_layer_inputs_features, const std::vector<AttentionGroupMaskValues> &group_mask_values,
        const InputLayersNamesSuffixes &input_layers_names_suffixes, const PreProcessParams &pre_process_params,
        const std::vector<AttentionGroupParams> &attention_groups);

    static Eigen::VectorXf generate_default_theta(); // TODO: HRT-16646 - Remove this flow
    static Eigen::VectorXf generate_theta_from_memview(const MemoryView theta); // TODO: HRT-16646 - Move to c'tor (get theta memview)

    hailo_status prepare_inputs_prefill(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer,
        const std::vector<EmbeddingViewWrapper> &input_tokens_embeddings,
        const std::vector<EmbeddingViewWrapper> &input_tokens_per_layer_inputs);

    hailo_status prepare_inputs_tbt(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer,
        const std::vector<EmbeddingViewWrapper> &input_token_embedding,
        const std::vector<EmbeddingViewWrapper> &input_token_per_layer_inputs);
    hailo_status validate_input_rows(const std::vector<EmbeddingViewWrapper> &token_rows,
        const std::vector<EmbeddingViewWrapper> &per_layer_inputs_rows) const;
    virtual void reset_local_cache();
    static bool is_positional_embed_layer(const std::string &name,
        const std::vector<AttentionGroupParams> &attention_groups);
    static bool is_deepstack_layer(const std::string &name, const InputLayersNamesSuffixes &input_layers_names_suffixes);

    PreProcessCache get_local_cache() const;
    void set_local_cache(const PreProcessCache &cache);

    size_t cache_usage_size() const {
        return m_cache_usage_size;
    }

    LLMPreProcess(eigen_matrix_2d_u16_t &&local_cached_embeddings,
        const std::map<std::string, size_t> &prefill_inputs_frame_size, const std::map<std::string, size_t> &tbt_inputs_frame_size,
        const EmbeddingTables &embedding_tables, uint32_t per_layer_inputs_features,
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
        const EmbeddingTables &embedding_tables, uint32_t per_layer_inputs_features,
        const PreProcessParams &pre_process_params, const std::vector<AttentionGroupParams> &attention_groups,
        uint32_t embeddings_layer_features, hailo_format_type_t embeddings_layer_type,
        const std::vector<Eigen::VectorXf> &group_thetas,
        const std::vector<AttentionGroupMaskValues> &group_mask_values);

    static Expected<std::vector<AttentionGroupContext>> discover_layer_groups(
        const std::vector<Eigen::VectorXf> &group_thetas,
        const std::map<std::string, size_t> &prefill_inputs_frame_size,
        const std::map<std::string, size_t> &tbt_inputs_frame_size,
        const std::vector<AttentionGroupMaskValues> &group_mask_values,
        const std::vector<AttentionGroupParams> &attention_groups);

    static void tile_along_last_axis(const Eigen::Tensor<float32_t, 4, Eigen::RowMajor> &input, int repeat_count, MemoryView &layer_buffer);

    hailo_status validate_inputs(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer, const std::map<std::string, size_t> &expected_sizes);
    hailo_status update_cache_from_embeddings(const std::vector<EmbeddingViewWrapper> &embedding_rows_views);
    void prepare_embeddings_input(MemoryView &layer_buffer, uint32_t number_of_tokens);
    hailo_status validate_per_layer_inputs_count(const std::vector<EmbeddingViewWrapper> &per_layer_inputs_rows,
        size_t token_rows_count) const;
    Expected<MemoryView> per_layer_inputs_buffer(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer) const;
    hailo_status prepare_per_layer_inputs(MemoryView &layer_buffer,
        const std::vector<EmbeddingViewWrapper> &per_layer_inputs_rows, InferencePhase phase);
    int layer_input_tokens_count(InferencePhase phase) const;

    hailo_status prepare_attention_mask_inputs(std::map<layer_name_t, MemoryView> &layer_name_to_input_buffer,
        InferencePhase phase);
    void write_attention_mask(const AttentionGroupContext &group, InferencePhase phase, MemoryView &layer_buffer);
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
    eigen_matrix_2d_u16_t m_local_cached_per_layer_inputs;
    eigen_tensor_4d_u32_t m_local_cached_pos_ids;

    const std::map<std::string, size_t> m_prefill_inputs_frame_size;
    const std::map<std::string, size_t> m_tbt_inputs_frame_size;

    int m_current_timestamp_value;

    std::vector<AttentionGroupContext> m_groups;

    EmbeddingTables m_embedding_tables;
    InputLayersNamesSuffixes m_input_layers_names_suffixes;
    PreProcessParams m_params;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_LLM_PRE_PROCESS_HPP_ */
