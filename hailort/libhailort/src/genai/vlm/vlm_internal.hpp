/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vlm_internal.hpp
 * @brief HailoRT GenAI VLM (Vision Language Models) internal API.
 * This API is currently in preview and may undergo further changes.
 **/

#ifndef _HAILO_GENAI_VLM_INTERNAL_HPP_
#define _HAILO_GENAI_VLM_INTERNAL_HPP_

#include "hailo/genai/vlm/vlm.hpp"

#include "hailo/expected.hpp"
#include "hailo/vdevice.hpp"

#include "common/genai/session_wrapper/session_wrapper.hpp"

#include "llm/minja_wrapper/minja_wrapper.hpp"
#include "llm/llm_internal.hpp"

namespace hailort
{
namespace genai
{


class VLMGenerator::Impl final : public TextGeneratorBase
{
public:
    Impl(std::shared_ptr<SessionWrapper> session, std::shared_ptr<PromptTemplateHandler> prompt_template_handler,
        std::shared_ptr<HailoTokenizer> tokenizer, std::shared_ptr<TokenEmbedder<uint16_t>> token_embedder);

    Expected<LLMGeneratorCompletion> generate(const std::string &prompt, const std::vector<MemoryView> &input_frames);
    Expected<LLMGeneratorCompletion> generate(const std::vector<std::string> &messages_json_strings, const std::vector<MemoryView> &input_frames);

private:
    Expected<std::string> apply_vlm_template_from_json(const std::vector<std::string> &messages_json_strings);

    std::shared_ptr<SessionWrapper> m_session;
    std::shared_ptr<PromptTemplateHandler> m_prompt_template_handler;

    // Optionals, only used if optimize_memory_on_device is true
    std::shared_ptr<HailoTokenizer> m_tokenizer;
    std::shared_ptr<TokenEmbedder<uint16_t>> m_token_embedder;
};


class VLM::Impl final
{
public:

    static Expected<std::unique_ptr<Impl>> create_unique(std::shared_ptr<hailort::VDevice> vdevice, const VLMParams &vlm_params);

    Expected<VLMGenerator> create_generator(const LLMGeneratorParams &params);
    Expected<LLMGeneratorParams> create_generator_params();

    Expected<std::vector<int>> tokenize(const std::string &prompt);
    Expected<size_t> get_context_usage_size();
    Expected<size_t> max_context_capacity();
    hailo_status clear_context();
    Expected<BufferPtr> save_context();
    hailo_status load_context(const MemoryView &context);
    hailo_status set_generation_recovery_sequence(const std::string &abort_sequence);
    Expected<std::string> get_generation_recovery_sequence();
    Expected<std::string> prompt_template();
    hailo_status set_stop_tokens(const std::vector<std::string> &stop_tokens);
    Expected<std::vector<std::string>> get_stop_tokens();

    uint32_t input_frame_size() const;
    const hailo_3d_image_shape_t& input_frame_shape() const;
    const hailo_format_type_t& input_frame_format_type() const;
    const hailo_format_order_t& input_frame_format_order() const;

    // Direct generation method for JSON structured prompts
    Expected<LLMGeneratorCompletion> generate(const LLMGeneratorParams &params, const std::vector<std::string> &messages_json_strings,
        const std::vector<MemoryView> &input_frames);

    ~Impl();

    Impl(std::shared_ptr<SessionWrapper> session, const VLMParams &Vlm_params,
        const LLMGeneratorParams &default_generator_params, hailo_3d_image_shape_t input_frame_shape,
        hailo_format_t input_frame_format, std::shared_ptr<PromptTemplateHandler> prompt_template_handler,
        std::shared_ptr<HailoTokenizer> tokenizer, std::shared_ptr<TokenEmbedder<uint16_t>> token_embedder);

private:
    hailo_status validate_generator_params(const LLMGeneratorParams &params);

    std::shared_ptr<SessionWrapper> m_session;
    VLMParams m_vlm_params;
    LLMGeneratorParams m_default_generator_params;
    hailo_3d_image_shape_t m_input_frame_shape;
    hailo_format_t m_input_frame_format;
    std::shared_ptr<PromptTemplateHandler> m_prompt_template_handler;

    // Optionals, only used if optimize_memory_on_device is true
    std::shared_ptr<HailoTokenizer> m_tokenizer;
    std::shared_ptr<TokenEmbedder<uint16_t>> m_token_embedder;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_VLM_INTERNAL_HPP_ */
