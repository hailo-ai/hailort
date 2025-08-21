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

#include "common/genai/session_wrapper/session_wrapper.hpp"

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

class LLMGeneratorCompletion::Impl final
{
public:
    Impl(std::shared_ptr<SessionWrapper> session, std::shared_ptr<TextGeneratorBase> generator);
    ~Impl();

    Expected<size_t> read(char *output, size_t output_size, std::chrono::milliseconds timeout);
    Expected<std::string> read(std::chrono::milliseconds timeout);
    Status generation_status() const;

    hailo_status abort();

private:
    std::shared_ptr<TextGeneratorBase> m_generator_scope_guard;
    std::shared_ptr<SessionWrapper> m_session;
    Status m_generation_status;
};


class LLMGenerator::Impl final : public TextGeneratorBase
{
public:
    Impl(std::shared_ptr<SessionWrapper> session, std::shared_ptr<PromptTemplateHandler> prompt_template_handler);
    ~Impl();

    hailo_status write(const std::vector<std::string> &prompt_json_strings);
    hailo_status write(const std::string &prompt);
    Expected<std::string> apply_prompt_tempalate_from_json(const std::vector<std::string> &prompt_json_strings);

    Expected<LLMGeneratorCompletion> generate();

private:
    std::shared_ptr<SessionWrapper> m_session;
    std::shared_ptr<PromptTemplateHandler> m_prompt_template_handler;
};


class LLM::Impl final
{
public:

    static Expected<std::unique_ptr<Impl>> create_unique(std::shared_ptr<VDevice> vdevice, const LLMParams &llm_params);

    Expected<LLMGenerator> create_generator(const LLMGeneratorParams &params);
    Expected<LLMGeneratorParams> create_generator_params();
    Expected<std::vector<int>> tokenize(const std::string &prompt);
    hailo_status clear_context();
    Expected<std::string> prompt_template();
    hailo_status set_generation_recovery_sequence(const std::string &abort_sequence);
    Expected<std::string> get_generation_recovery_sequence();
    hailo_status set_stop_tokens(const std::vector<std::string> &stop_tokens);
    Expected<std::vector<std::string>> get_stop_tokens();

    // Direct generation method for JSON structured prompts
    Expected<LLMGeneratorCompletion> generate(const LLMGeneratorParams &params, const std::vector<std::string> &prompt_json_strings);

    ~Impl();

    Impl(std::shared_ptr<SessionWrapper> session, const LLMParams &llm_params,
        const LLMGeneratorParams &default_generator_params, std::shared_ptr<PromptTemplateHandler> prompt_template_handler);

private:
    hailo_status validate_generator_params(const LLMGeneratorParams &params);

    std::shared_ptr<SessionWrapper> m_session;
    LLMParams m_llm_params;
    LLMGeneratorParams m_default_generator_params;
    std::shared_ptr<PromptTemplateHandler> m_prompt_template_handler;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_LLM_INTERNAL_HPP_ */
