/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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
#include "llm_inference_manager.hpp"
#include "post_process.hpp"
#include "genai_server.hpp"
#include "token_embedder.hpp"

#include "context_structure.hpp"

#include "hailort_server.hpp"

#include "nlohmann/json.hpp"
#include <queue>

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

protected:
    Expected<std::pair<int, LLMGeneratorCompletion::Status>> generate_next_token_on_demand(const std::vector<int> &tokens, const std::vector<MemoryView> &embeddings);

    virtual Expected<std::pair<int, LLMGeneratorCompletion::Status>> handle_prefill_phase(const std::vector<int> &tokens,
        const std::vector<MemoryView> &embeddings);
    virtual Expected<std::pair<int, LLMGeneratorCompletion::Status>> handle_tbt_phase(const std::vector<MemoryView> &embeddings);

    std::string handle_next_token(int next_token);

    LLMGeneratorCompletion::Status get_current_generation_status(int next_token);

    // Handle generation completion and recovery sequence for ungraceful endings - returns GENERATING if recovery tokens need delivery
    Expected<LLMGeneratorCompletion::Status> handle_generation_completion(LLMGeneratorCompletion::Status completion_status, int next_token);

    void append_tokens_to_next_generation_prefix(const std::vector<int> &tokens_to_prepend);
    hailo_status apply_recovery_sequence(LLMGeneratorCompletion::Status termination_status);

    // Reasoning model helper functions
    void handle_reasoning_token_tracking(int next_token);
    hailo_status handle_reasoning_generation_completion();

    virtual hailo_status parse_config_json(const MemoryView &config_json);
    hailo_status parse_config_json(const nlohmann::json &hailo_config_json);

    // This function is used to process the prefill inputs and outputs, without handling (exporting) the generated token
    Expected<int> get_next_token_prefill(std::map<std::string, MemoryView> &prefill_inputs,
        std::map<std::string, MemoryView> &prefill_outputs, const std::vector<MemoryView> &input_embeddings,
        const LLMGeneratorParams &params);
    hailo_status process_prefill_inputs_chunk(std::map<std::string, MemoryView> &prefill_inputs,
        std::map<std::string, MemoryView> &prefill_outputs, const std::vector<MemoryView> &input_embeddings);

    // Check if current token history matches any custom stop sequence
    bool check_custom_stop_sequences(int latest_token);

    void prepare_for_new_generation();

    void reset_cnversation_context();

    // Check if current-generation generated-tokens matches any custom stop sequence
    bool check_stop_sequences(int latest_token);

    Expected<Buffer> get_context() const;
    hailo_status set_context(const MemoryView &context_buffer);

    SessionWrapper m_session;
    std::shared_ptr<VDeviceManager> m_vdevice_manager;
    std::unique_ptr<HailoTokenizer> m_tokenizer;
    std::unique_ptr<TokenEmbedder<uint16_t>> m_token_embedder;

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
    std::deque<int> m_recent_tokens_sequence;
    static constexpr size_t MAX_STOP_SEQUENCE_LENGTH = 50; // Reasonable limit for stop sequences

    int m_end_of_sentence_token_id;

    // Relevant for reasoning models only
    // Reasoning model configuration and state
    struct ReasoningState {
        bool generation_with_reasoning;

        int start_token_id;
        int end_token_id;
        bool past_end_of_reasoning;  // True once we've seen the end-of-reasoning token
        std::unordered_set<int> tokens_history_before_reasoning;
        std::tuple<size_t, eigen_matrix_2d_u16_t, eigen_tensor_4d_u32_t, int> pre_process_cache_before_reasoning;
        std::vector<int> after_reasoning_tokens;

        // Track tokens processed during reasoning section for accurate cache rollback
        size_t reasoning_section_token_count;

        ReasoningState() : generation_with_reasoning(false), start_token_id(INVALID_TOKEN_VALUE),
            end_token_id(INVALID_TOKEN_VALUE), past_end_of_reasoning(false), reasoning_section_token_count(0) {}

        void clear() {
            generation_with_reasoning = false;
            past_end_of_reasoning = false;
            tokens_history_before_reasoning.clear();
            after_reasoning_tokens.clear();
            reasoning_section_token_count = 0;
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

    std::vector<int> m_next_input_prompt_prefix_tokens;             // The prefix tokens of the next input prompt, which is used to chain the generated output without reasoning

    std::mutex m_generation_mutex;

    uint32_t m_generated_token_count;
    LLMGeneratorParams m_current_generation_params;
    bool m_abort_requested;

    InputLayersNamesSuffixes m_input_layers_names_suffixes;
    PreProcessParams m_pre_process_params;
};

class LLMServerManager : public GenAIServerManager
{
public:
    static Expected<std::unique_ptr<LLMServerManager>> create(std::shared_ptr<Session> session, std::shared_ptr<VDeviceManager> vdevice_manager);

    LLMServerManager(std::shared_ptr<Session> session, std::unique_ptr<LLMServer> &&server);

protected:
    std::unique_ptr<LLMServer> m_server;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_HAILO_GENAI_LLM_SERVER_HPP_ */
