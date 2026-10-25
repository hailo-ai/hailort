/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file llm_server.hpp
 * @brief Implementation for LLM server
 **/

#ifndef _HAILO_HAILO_GENAI_LLM_SERVER_HPP_
#define _HAILO_HAILO_GENAI_LLM_SERVER_HPP_


#include "hailo/hailort.h"
#include "hailo/buffer.hpp"
#include "hailo/hailo_session.hpp"
#include "common/utils.hpp"
#include "common/genai/session_wrapper/session_wrapper.hpp"
#include "common/genai/serializer/serializer.hpp"
#include "common/genai/connection_ports.hpp"

#include "common/genai/constants.hpp"
#include "hailo_tokenizer.hpp"
#include "pre_process.hpp"
#include "pre_process_config.hpp"
#include "reasoning.hpp"
#include "llm_inference_manager.hpp"
#include "post_process.hpp"
#include "genai_server.hpp"
#include "token_embedder.hpp"
#include "resource_arrivals.hpp"

#include "context_structure.hpp"

#include "hailort_server.hpp"

#include "nlohmann/json.hpp"
#include <optional>
#include <queue>
#include <future>

namespace hailort
{
namespace genai
{

class LLMServer
{
public:
    static constexpr float32_t DEFAULT_GENERATION_TEMPERATURE = 0.7f;
    static constexpr float32_t DEFAULT_GENERATION_TOP_P = 0.8f;
    static constexpr uint32_t DEFAULT_GENERATION_TOP_K = 20;
    static constexpr float32_t DEFAULT_GENERATION_FREQ_PENALTY = 1.1f;
    static constexpr bool DEFAULT_GENERATION_DO_SAMPLE = true;
    static constexpr uint32_t DEFAULT_GENERATION_MAX_GENERATED_TOKENS = 1024;

    static Expected<std::unique_ptr<LLMServer>> create_unique(std::shared_ptr<Session> session, std::shared_ptr<VDeviceManager> vdevice_manager);

    LLMServer(std::shared_ptr<Session> session, std::shared_ptr<VDeviceManager> vdevice_manager, LLMGeneratorParams &&post_process_params);
    virtual ~LLMServer();

    // Handlers
    Expected<Buffer> handle_create_llm_request(const MemoryView &request);
    Expected<Buffer> handle_get_generator_params_request(const MemoryView &request);
    Expected<Buffer> handle_create_generator_request(const MemoryView &request);
    Expected<Buffer> handle_write_request(const MemoryView &request);
    Expected<Buffer> handle_generate_request(const MemoryView &request);
    Expected<Buffer> handle_read_request(const MemoryView &request);
    Expected<Buffer> handle_generator_release_request(const MemoryView &request);
    Expected<Buffer> handle_tokenize_request(const MemoryView &request);
    Expected<Buffer> handle_clear_context_request(const MemoryView &request);
    Expected<Buffer> handle_get_context_request(const MemoryView &request);
    Expected<Buffer> handle_set_context_request(const MemoryView &request);
    Expected<Buffer> handle_abort_request(const MemoryView &request);
    Expected<Buffer> handle_set_generation_recovery_sequence_request(const MemoryView &request);
    Expected<Buffer> handle_get_generation_recovery_sequence_request(const MemoryView &request);
    Expected<Buffer> handle_set_stop_tokens_request(const MemoryView &request);
    Expected<Buffer> handle_get_stop_tokens_request(const MemoryView &request);
    Expected<Buffer> handle_get_context_usage_size(const MemoryView &request);
    Expected<Buffer> handle_get_max_context_capacity(const MemoryView &request);
    Expected<Buffer> handle_acquire_kv_cache_request(const MemoryView &request);
    Expected<Buffer> handle_check_hef_exists_request(const MemoryView &request);

protected:
    enum class ReasoningOpener { Prompt, Generation };
    using GenerationEndBeforeWrite = std::optional<std::pair<int, LLMGeneratorCompletion::Status>>;

    struct GenerationEnd {
        LLMGeneratorCompletion::Status status;
        std::optional<size_t> matched_stop_sequence_length;
    };

    Expected<std::pair<int, LLMGeneratorCompletion::Status>> generate_next_token_on_demand(const std::vector<int> &tokens,
        const std::vector<EmbeddingViewWrapper> &embeddings, const std::vector<EmbeddingViewWrapper> &per_layer_inputs);

    virtual Expected<std::pair<int, LLMGeneratorCompletion::Status>> handle_prefill_phase(const std::vector<int> &tokens,
        const std::vector<EmbeddingViewWrapper> &embeddings, const std::vector<EmbeddingViewWrapper> &per_layer_inputs);
    virtual Expected<std::pair<int, LLMGeneratorCompletion::Status>> handle_tbt_phase(
        const std::vector<EmbeddingViewWrapper> &embeddings, const std::vector<EmbeddingViewWrapper> &per_layer_inputs);

    static hailo_status wait_for_table(ResourceArrivals &resource_arrivals, const EmbeddingTableParams &table,
        const std::string &table_description, EventPtr shutdown_event);
    static Expected<std::unique_ptr<TokenEmbedder<uint16_t>>> read_table_embedder(const Hef &hef,
        const EmbeddingTableParams &table, uint32_t features, const std::string &table_description,
        int image_pad_token_id = INVALID_TOKEN_VALUE, int video_pad_token_id = INVALID_TOKEN_VALUE);

    // Empty when the model declares no per-layer inputs table
    Expected<std::vector<EmbeddingViewWrapper>> gather_declared_per_layer_inputs(const std::vector<int> &tokens) const;

    std::string handle_next_token(int next_token);

    GenerationEnd get_current_generation_end(int next_token);

    // Returns GENERATING while recovery tokens of an ungraceful ending still need delivery
    Expected<LLMGeneratorCompletion::Status> resolve_generation_status(int next_token);
    void record_token_fed_back(int next_token);
    // Ends the generation without running this step's inference: aborted, or max tokens when none are expected back
    Expected<std::pair<int, LLMGeneratorCompletion::Status>> end_generation_without_new_token();
    std::pair<int, LLMGeneratorCompletion::Status> pop_recovery_token(
        LLMGeneratorCompletion::Status termination_status);

    // Handle generation completion and recovery sequence for ungraceful endings - returns GENERATING if recovery tokens need delivery
    Expected<LLMGeneratorCompletion::Status> handle_generation_completion(const GenerationEnd &generation_end,
        int next_token);

    void append_tokens_to_next_generation_prefix(const std::vector<int> &tokens_to_prepend);
    hailo_status apply_recovery_sequence(LLMGeneratorCompletion::Status termination_status);

    // Reasoning model helper functions
    void save_reasoning_rewind_point(ReasoningOpener opener);
    // Saves the device cache before a write that overwrites entries the rollback needs. If saving fails, drops the
    // thought and yields the generation's end, and the write must not run.
    Expected<GenerationEndBeforeWrite> prepare_device_cache_for_write(size_t written_tokens_count);
    std::optional<CacheRollbackLimit> find_reasoning_rollback_limit(size_t rollback_tokens_count) const;
    hailo_status rewind_device_cache_before_reasoning(const DeviceCacheRewindPlan &rewind_plan,
        size_t rollback_tokens_count);
    std::vector<int> last_tokens_kept_in_context(std::optional<size_t> matched_stop_sequence_length,
        int last_token) const;
    void handle_generated_reasoning_token(int next_token);
    void collect_token_if_outside_reasoning(int next_token);
    size_t reasoning_rollback_tokens_count() const;
    hailo_status handle_reasoning_generation_completion();

    virtual hailo_status parse_config_json(const MemoryView &config_json);
    hailo_status parse_config_json(const nlohmann::json &hailo_config_json, RopeLayout rope_layout);

    // One entry per attention group in group order, each phase quantized through that phase's own mask layer
    Expected<std::vector<AttentionGroupMaskValues>> resolve_group_mask_values(
        const std::map<std::string, size_t> &prefill_inputs_frame_size,
        const std::map<std::string, size_t> &tbt_inputs_frame_size) const;

    // This function is used to process the prefill inputs and outputs, without handling (exporting) the generated token
    Expected<int> get_next_token_prefill(std::map<std::string, MemoryView> &prefill_inputs,
        std::map<std::string, MemoryView> &prefill_outputs, const std::vector<EmbeddingViewWrapper> &input_embeddings,
        const std::vector<EmbeddingViewWrapper> &per_layer_inputs, const LLMGeneratorParams &params);
    hailo_status process_prefill_inputs_chunk(std::map<std::string, MemoryView> &prefill_inputs,
        std::map<std::string, MemoryView> &prefill_outputs, const std::vector<EmbeddingViewWrapper> &input_embeddings,
        const std::vector<EmbeddingViewWrapper> &per_layer_inputs);

    // Check if current token history matches any custom stop sequence
    bool check_custom_stop_sequences(int latest_token);

    void prepare_for_new_generation();

    hailo_status reset_conversation_context();
    void clear_host_conversation();
    // A partly updated device cache no longer matches the host state, so the conversation starts over
    void reset_conversation_after_device_cache_failure(hailo_status failure_status);

    // Records the latest token and yields the length of the stop sequence it completes, if any
    std::optional<size_t> check_stop_sequences(int latest_token);
    std::optional<size_t> find_matched_stop_sequence_length() const;

    Expected<Buffer> get_context() const;
    hailo_status set_context(const MemoryView &context_buffer);

    Expected<std::future<hailo_status>> create_resources_async(std::shared_ptr<VDevice>, std::shared_ptr<Buffer> hef_buffer, const std::string lora_name,
        bool tokenizer_on_host, HefSource hef_source, ResourceArrivalsPtr resource_arrivals,
        StatusEventPtr ccws_ready_event, std::shared_ptr<Event> shutdown_event);

    virtual std::future<hailo_status> create_inference_managers_future(std::shared_ptr<VDevice> vdevice, const Hef &hef,
        const std::string &lora_name, std::shared_ptr<Event> external_resources_created_event,
        std::shared_ptr<Event> inference_models_created_event, StatusEventPtr ccws_ready_event,
        std::shared_ptr<Event> shutdown_event);

    virtual std::future<Expected<std::vector<Eigen::VectorXf>>> parse_external_resources_future(const Hef &hef,
        ResourceArrivalsPtr resource_arrivals, std::shared_ptr<Event> external_resources_created_event,
        std::shared_ptr<Event> shutdown_event);

    virtual std::future<hailo_status> create_tokenizer_future(const Hef &hef, ResourceArrivalsPtr resource_arrivals,
        std::shared_ptr<Event> shutdown_event);

    virtual std::future<hailo_status> create_token_embedder_future(const Hef &hef,
        ResourceArrivalsPtr resource_arrivals, std::shared_ptr<Event> pre_process_created_event,
        std::shared_ptr<Event> shutdown_event);

    virtual std::future<hailo_status> create_pre_process_future(const Hef &hef,
        std::shared_ptr<Event> inference_models_created_event,
        std::future<Expected<std::vector<Eigen::VectorXf>>> &external_resources_future,
        std::shared_ptr<Event> pre_process_created_event, std::shared_ptr<Event> shutdown_event);

    SessionWrapper m_session;
    std::shared_ptr<VDeviceManager> m_vdevice_manager;
    std::unique_ptr<HailoTokenizer> m_tokenizer;
    std::unique_ptr<TokenEmbedder<uint16_t>> m_token_embedder;
    std::unique_ptr<TokenEmbedder<uint16_t>> m_per_layer_inputs_embedder;

    std::unique_ptr<InferenceManager> m_inference_manager_tbt;
    std::pair<std::map<std::string, BufferPtr>, std::map<std::string, BufferPtr>> m_tbt_buffers;
    std::map<std::string, MemoryView> m_tbt_inputs;
    std::map<std::string, MemoryView> m_tbt_outputs;

    std::unique_ptr<InferenceManager> m_inference_manager_prefill;
    std::pair<std::map<std::string, BufferPtr>, std::map<std::string, BufferPtr>> m_prefill_buffers;
    std::map<std::string, MemoryView> m_prefill_inputs;
    std::map<std::string, MemoryView> m_prefill_outputs;

    std::unique_ptr<LLMPreProcess> m_pre_process;

    LLMPostProcess m_post_process;
    LLMGeneratorParams m_post_process_params;

    std::unordered_set<int> m_tokens_history;

    // For ordered sequence matching (needed for multi-token stop sequences)
    struct RecentToken {
        int token_id;
        bool is_inside_reasoning;
    };
    std::deque<RecentToken> m_recent_tokens_sequence;
    static constexpr size_t MAX_STOP_SEQUENCE_LENGTH = 50; // Reasonable limit for stop sequences

    int m_end_of_sentence_token_id = INVALID_TOKEN_VALUE;
    std::string m_bos_token;
    std::string m_eos_token;

    // Relevant for reasoning models only
    // Reasoning model configuration and state
    struct ReasoningState {
        bool has_rewind_point;
        // A tool call outside every thought keeps the generation's thoughts, so the turn has no rewind point
        bool is_tool_call_turn;

        int start_token_id;
        int end_token_id;
        int tool_call_start_token_id;
        bool is_inside_reasoning;
        bool is_opened_by_prompt;
        std::unordered_set<int> tokens_history_before_reasoning;
        PreProcessCache pre_process_cache_before_reasoning;
        // Taken right before the first write that overwrites device cache entries the next prompt still reads
        std::optional<std::unordered_map<uint32_t, BufferPtr>> device_cache_snapshot;
        std::vector<int> tokens_outside_reasoning;

        ReasoningState() : has_rewind_point(false), is_tool_call_turn(false),
            start_token_id(INVALID_TOKEN_VALUE), end_token_id(INVALID_TOKEN_VALUE),
            tool_call_start_token_id(INVALID_TOKEN_VALUE), is_inside_reasoning(false), is_opened_by_prompt(false),
            pre_process_cache_before_reasoning() {}

        void release_rewind_point() {
            has_rewind_point = false;
            is_inside_reasoning = false;
            is_opened_by_prompt = false;
            tokens_history_before_reasoning.clear();
            pre_process_cache_before_reasoning = PreProcessCache();
            device_cache_snapshot.reset();
            tokens_outside_reasoning.clear();
        }

        void clear() {
            release_rewind_point();
            is_tool_call_turn = false;
        }
    } m_reasoning;

    // Recovery sequence configuration and state
    struct RecoverySequenceState {
        std::vector<int> tokens;                        // Configured recovery tokens
        std::queue<int> delivery_queue;                 // Queue for token-by-token delivery
        LLMGeneratorCompletion::Status termination_status;  // Status to deliver with last recovery token

        RecoverySequenceState() : termination_status(LLMGeneratorCompletion::Status::GENERATING) {}

        void clear() {
            while (!delivery_queue.empty()) {
                delivery_queue.pop();
            }
            termination_status = LLMGeneratorCompletion::Status::GENERATING;
        }

        bool has_pending_tokens() const
        {
            return !delivery_queue.empty();
        }
    } m_recovery;

    // Custom stop tokens functionality
    std::vector<std::vector<int>> m_tokenized_stop_sequences;

    std::string m_chat_template;
    uint32_t m_embeddings_features;
    uint32_t m_per_layer_inputs_features = 0;

    std::vector<int> m_next_input_prompt_prefix_tokens;             // The prefix tokens of the next input prompt, which is used to chain the generated output without reasoning

    std::mutex m_generation_mutex;

    uint32_t m_generated_token_count;
    LLMGeneratorParams m_current_generation_params;
    bool m_abort_requested;

    InputLayersNamesSuffixes m_input_layers_names_suffixes;
    EmbeddingTables m_embedding_tables;
    PreProcessParams m_pre_process_params;

    std::vector<AttentionGroupParams> m_attention_groups;
    uint32_t m_context_capacity = 0;

    std::optional<KvCacheGuard> m_kv_cache_guard;
};

class LLMServerManager : public GenAIServerManager
{
public:
    static Expected<std::unique_ptr<LLMServerManager>> create(std::shared_ptr<Session> session, std::shared_ptr<VDeviceManager> vdevice_manager);

    LLMServerManager(std::shared_ptr<Session> session, std::unique_ptr<LLMServer> &&server);

protected:
    std::unique_ptr<LLMServer> m_server;
};

class EventGuard {
public:
    EventGuard(std::shared_ptr<Event> event) : m_event(event) {}
    ~EventGuard() {
        auto status = m_event->signal();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to signal event: {}", status);
        }
    }
private:
    std::shared_ptr<Event> m_event;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_HAILO_GENAI_LLM_SERVER_HPP_ */
