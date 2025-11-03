/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file llm_internal.hpp
 * @brief HailoRT GenAI LLM (Large Language Models) internal API.
 * This API is currently in preview and may undergo further changes.
 **/

#ifndef _HAILO_GENAI_LLM_INTERNAL_HPP_
#define _HAILO_GENAI_LLM_INTERNAL_HPP_

#include "hailo/genai/llm/llm.hpp"

#include "hailo/expected.hpp"
#include "hailo/vdevice.hpp"

#include "common/genai/serializer/serializer.hpp"
#include "common/genai/session_wrapper/session_wrapper.hpp"
#include "common/thread_safe_queue.hpp"

#include <thread>
#include <atomic>
#include <mutex>

#ifdef HAILO_CLIENT_TOKENIZER_ENABLED
#include "hailo_tokenizer.hpp"
#include "token_embedder.hpp"
#else
// Forward declarations for tokenizer and token embedder
class HailoTokenizer {};

template<typename T>
class TokenEmbedder {};
#endif // HAILO_CLIENT_TOKENIZER_ENABLED

#include "minja_wrapper/minja_wrapper.hpp"

namespace hailort
{
namespace genai
{

// Pure virtual base class for all text generators
class TextGeneratorBase : public std::enable_shared_from_this<TextGeneratorBase>
{
public:
    virtual ~TextGeneratorBase() = default;
};

// Type alias for token-status pairs used in client-side queuing
using LLMTokenPair = std::pair<std::string, LLMGeneratorCompletion::Status>;

class LLMGeneratorCompletion::Impl final
{
public:
    Impl(std::shared_ptr<SessionWrapper> session, std::shared_ptr<TextGeneratorBase> generator,
         SpscQueue<LLMTokenPair> &&client_token_queue, EventPtr shutdown_event,
         std::shared_ptr<HailoTokenizer> tokenizer, std::shared_ptr<TokenEmbedder<uint16_t>> token_embedder,
         const std::string &aggregated_prompt, const std::vector<int> &initial_prefix_tokens = {});
    ~Impl();

    Expected<size_t> read(char *output, size_t output_size, std::chrono::milliseconds timeout);
    Expected<std::string> read(std::chrono::milliseconds timeout);
    Status generation_status() const;

    hailo_status abort();

private:
    void token_reader_thread(const std::string &aggregated_prompt, const std::vector<int> &initial_prefix_tokens);
    void stop_token_reader_thread();

    // Helper functions for token_reader_thread
    hailo_status prepare_client_side_embeddings(LLMGeneratorReadSerializer::TextGenerationInput &input);
    Expected<std::pair<LLMGeneratorReadSerializer::TextGenerationOutput, Status>> send_read_request(
        const LLMGeneratorReadSerializer::TextGenerationInput &input, std::chrono::milliseconds timeout);
    void prepare_next_iteration(LLMGeneratorReadSerializer::TextGenerationInput &input, int next_token_id);

    std::shared_ptr<TextGeneratorBase> m_generator_scope_guard;
    std::shared_ptr<SessionWrapper> m_session;

    mutable std::mutex m_status_mutex;
    Status m_generation_status;

    SpscQueue<LLMTokenPair> m_client_token_queue;
    std::thread m_token_reader_thread;
    EventPtr m_shutdown_event;
    bool m_context_full_warning_issued;

    // Optionals, only used if optimize_memory_on_device is true
    std::shared_ptr<HailoTokenizer> m_tokenizer;
    std::shared_ptr<TokenEmbedder<uint16_t>> m_token_embedder;
};


class LLMGenerator::Impl final : public TextGeneratorBase
{
public:
    Impl(std::shared_ptr<SessionWrapper> session, std::shared_ptr<PromptTemplateHandler> prompt_template_handler,
         std::shared_ptr<HailoTokenizer> tokenizer, std::shared_ptr<TokenEmbedder<uint16_t>> token_embedder);
    ~Impl();

    hailo_status write(const std::vector<std::string> &prompt_json_strings);
    hailo_status write(const std::string &prompt);
    Expected<std::string> apply_prompt_tempalate_from_json(const std::vector<std::string> &prompt_json_strings);

    Expected<LLMGeneratorCompletion> generate();

private:
    std::shared_ptr<SessionWrapper> m_session;
    std::shared_ptr<PromptTemplateHandler> m_prompt_template_handler;

    std::string m_aggregated_prompt;

    // Optionals, only used if optimize_memory_on_device is true
    std::shared_ptr<HailoTokenizer> m_tokenizer;
    std::shared_ptr<TokenEmbedder<uint16_t>> m_token_embedder;
};


class LLM::Impl final
{
public:

    static Expected<std::unique_ptr<Impl>> create_unique(std::shared_ptr<VDevice> vdevice, const LLMParams &llm_params);

    Expected<LLMGenerator> create_generator(const LLMGeneratorParams &params);
    Expected<LLMGeneratorParams> create_generator_params();
    Expected<std::vector<int>> tokenize(const std::string &prompt);
    Expected<size_t> get_context_usage_size();
    Expected<size_t> max_context_capacity();
    hailo_status clear_context();
    Expected<BufferPtr> save_context();
    hailo_status load_context(const MemoryView &context);
    Expected<std::string> prompt_template();
    hailo_status set_generation_recovery_sequence(const std::string &abort_sequence);
    Expected<std::string> get_generation_recovery_sequence();
    hailo_status set_stop_tokens(const std::vector<std::string> &stop_tokens);
    Expected<std::vector<std::string>> get_stop_tokens();

    // Direct generation method for JSON structured prompts
    Expected<LLMGeneratorCompletion> generate(const LLMGeneratorParams &params, const std::vector<std::string> &prompt_json_strings);

    ~Impl();

    Impl(std::shared_ptr<SessionWrapper> session, const LLMParams &llm_params,
        const LLMGeneratorParams &default_generator_params, std::shared_ptr<PromptTemplateHandler> prompt_template_handler,
        std::shared_ptr<HailoTokenizer> tokenizer, std::shared_ptr<TokenEmbedder<uint16_t>> token_embedder);

private:
    hailo_status validate_generator_params(const LLMGeneratorParams &params);

    std::shared_ptr<SessionWrapper> m_session;
    LLMParams m_llm_params;
    LLMGeneratorParams m_default_generator_params;
    std::shared_ptr<PromptTemplateHandler> m_prompt_template_handler;

    // Optionals, only used if optimize_memory_on_device is true
    std::shared_ptr<HailoTokenizer> m_tokenizer;
    std::shared_ptr<TokenEmbedder<uint16_t>> m_token_embedder;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_LLM_INTERNAL_HPP_ */
