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
#include "common/thread_safe_queue.hpp"
#include "common/genai/session_wrapper/session_wrapper.hpp"
#include "common/genai/serializer/serializer.hpp"
#include "common/genai/connection_ports.hpp"

#include "hailo_tokenizer.hpp"
#include "pre_process.hpp"
#include "llm_inference_manager.hpp"
#include "post_process.hpp"

#include "nlohmann/json.hpp"

namespace hailort
{
namespace genai
{

// TODO: HRT-16824 - Get this values from hef
constexpr int EOS_TOKEN_ID_VAL = 151645;      //     # <|im_end|>               end of a sentence, QWEN
constexpr int EOT_TOKEN_ID_VAL = 151643;      //     # <|endoftext|>            end of generation, QWEN
constexpr int INVALID_TOKEN_VALUE = INT32_MAX;

static const std::string EOS_SPECIAL_TOKEN_RESOURCE_NAME = "special_token__end_of_sentence";
static const std::string EOT_SPECIAL_TOKEN_RESOURCE_NAME = "special_token__end_of_text";
static const std::string BOR_SPECIAL_TOKEN_RESOURCE_NAME = "special_token__begin_of_reasoning";
static const std::string EOR_SPECIAL_TOKEN_RESOURCE_NAME = "special_token__end_of_reasoning";

static const std::string INPUT_EMB_BINARY = "embeddings.bin";
static const std::string TOKENIZER = "tokenizer.json";
static const std::string THETA = "rope_theta_data.bin";
static const std::string HAILO_CONFIG_JSON = "hailo-config.json";

class LLMServer
{
public:
    static constexpr float32_t DEFAULT_GENERATION_TEMPERATURE = 0.7f;
    static constexpr float32_t DEFAULT_GENERATION_TOP_P = 0.8f;
    static constexpr uint32_t DEFAULT_GENERATION_TOP_K = 20;
    static constexpr float32_t DEFAULT_GENERATION_FREQ_PENALTY = 1.1f;
    static constexpr bool DEFAULT_GENERATION_DO_SAMPLE = true;
    static constexpr uint32_t DEFAULT_GENERATION_MAX_GENERATED_TOKENS = 1024;

    static Expected<std::unique_ptr<LLMServer>> create_unique(std::shared_ptr<Session> session);

    LLMServer(std::shared_ptr<Session> session, SpscQueue<std::pair<std::string, LLMGeneratorCompletion::Status>> &&generated_tokens_queue,
        std::unique_ptr<SpscQueue<std::string>> &&input_prompt_queue, EventPtr shutdown_event, LLMGeneratorParams &&post_process_params);
    virtual ~LLMServer();

    void terminate_generation_thread();

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
    Expected<Buffer> handle_abort_request(const MemoryView &request);
    Expected<Buffer> handle_set_generation_recovery_sequence_request(const MemoryView &request);
    Expected<Buffer> handle_get_generation_recovery_sequence_request(const MemoryView &request);
    Expected<Buffer> handle_set_stop_tokens_request(const MemoryView &request);
    Expected<Buffer> handle_get_stop_tokens_request(const MemoryView &request);

protected:
    virtual void async_generate_into_internal_db();
    void init_generation_thread();
    virtual void flush_internal_queues();
    hailo_status handle_generation_completion(LLMGeneratorCompletion::Status generation_completion_status);
    hailo_status process_reasoning_model(std::vector<int> &input_tokens, const LLMGeneratorParams &local_post_process_params);
    hailo_status process_non_reasoning_model(std::vector<int> &input_tokens, const LLMGeneratorParams &local_post_process_params);

    virtual hailo_status parse_config_json(const MemoryView &config_json);
    hailo_status parse_config_json(const nlohmann::json &hailo_config_json);

    // This function exports the generated token to the client and updates internal caches
    hailo_status handle_next_token(int next_token, LLMGeneratorCompletion::Status generation_status);

    // This function is used to process the prefill inputs and outputs, without handling (exporting) the generated token
    Expected<int> get_next_token_prefill(std::map<std::string, MemoryView> &prefill_inputs,
        std::map<std::string, MemoryView> &prefill_outputs, std::vector<int> &input_tokens,
        const LLMGeneratorParams &params);
    hailo_status process_prefill_inputs_chunk(std::map<std::string, MemoryView> &prefill_inputs,
        std::map<std::string, MemoryView> &prefill_outputs, std::vector<int> &input_tokens);

    // This function is used to process the TBT loop, until 'eot' is reached, including the handling of the generated token
    Expected<uint32_t> tbt_generation_loop(std::map<std::string, MemoryView> &tbt_inputs,
        std::map<std::string, MemoryView> &tbt_outputs, int next_token,
        const LLMGeneratorParams &params);

    // Check if current token history matches any custom stop sequence
    bool check_custom_stop_sequences(int latest_token);

    void reset_cnversation_context();

    // Check if current-generation generated-tokens matches any custom stop sequence
    bool check_stop_sequences(int latest_token);

    SessionWrapper m_session;
    std::unique_ptr<HailoTokenizer> m_tokenizer;

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
    int m_start_of_reasoning_token_id;
    int m_end_of_reasoning_token_id;

    // Custom stop tokens functionality
    std::vector<std::string> m_stop_tokens;
    std::vector<std::vector<int>> m_tokenized_stop_sequences;

    std::string m_chat_template;

    std::thread m_async_generation_thread;

    std::string m_aggregated_input_prompt;                          // The aggregated input prompt, which is used to chain the user's 'writes'
    std::string m_next_input_prompt_prefix;                         // The prefix of the next input prompt, which is used to chain the generated output without reasoning
    std::unique_ptr<SpscQueue<std::string>> m_input_prompt_queue;   // queue for passing the aggregated prompt for generation.
                                                                    // size is 1 as only 1 generation in parallel is possible

    using generated_token_t = std::pair<std::string, LLMGeneratorCompletion::Status>;
    SpscQueue<generated_token_t> m_generated_tokens_queue;

    EventPtr m_shutdown_event;
    enum class State {
        READY,
        GENERATING,
        ABORTING, // Indicate that the generation is being aborted by the client
        ERROR, // Indicate some internal error in the server, or something that makes it unusable
    };
    std::atomic<State> m_state;
    std::mutex m_generation_mutex;

    std::string m_generation_recovery_sequence;
    std::condition_variable m_cv;
};

// TODO (HRT-18240): Make this class generic for all genai servers
class LLMServerManager
{
public:
    static Expected<std::unique_ptr<LLMServerManager>> create(std::shared_ptr<Session> session);

    hailo_status flow();

    LLMServerManager(std::shared_ptr<Session> session, std::unique_ptr<LLMServer> &&server);

protected:
    SessionWrapper m_session;
    std::unique_ptr<LLMServer> m_server;
    std::array<std::function<Expected<Buffer>(const MemoryView &)>, static_cast<size_t>(HailoGenAIActionID::GENAI_ACTIONS_COUNT)> m_dispatcher;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_HAILO_GENAI_LLM_SERVER_HPP_ */
