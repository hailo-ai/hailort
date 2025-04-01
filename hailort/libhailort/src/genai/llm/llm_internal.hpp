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
#include "hailo/genai/vdevice_genai.hpp"

namespace hailort
{
namespace genai
{


class LLMGeneratorCompletion::Impl final
{
public:
    Impl(std::shared_ptr<GenAISession> session);

    Expected<size_t> read(char *output, size_t output_size, std::chrono::milliseconds timeout);
    Expected<std::string> read(std::chrono::milliseconds timeout);
    Status generation_status() const;

private:
    std::shared_ptr<GenAISession> m_session;
    std::mutex m_mutex;
    Status m_generation_status;
};


class LLMGenerator::Impl final
{
public:
    Impl(std::shared_ptr<GenAISession> session);

    hailo_status write(const std::string &prompt);
    Expected<LLMGeneratorCompletion> generate();

private:
    std::shared_ptr<GenAISession> m_session;
    std::vector<std::string> m_prompts;
    std::mutex m_mutex;
    bool m_should_stop_write;
};


class LLM::Impl final
{
public:

    static Expected<std::unique_ptr<Impl>> create_unique(std::shared_ptr<VDevice> vdevice, const LLMParams &llm_params);

    Expected<LLMGenerator> create_generator(const LLMGeneratorParams &params);
    Expected<LLMGeneratorParams> create_generator_params();

private:
    Impl(std::shared_ptr<GenAISession> session, const LLMParams &llm_params,
        const LLMGeneratorParams &default_generator_params);
    hailo_status validate_generator_params(const LLMGeneratorParams &params);

    std::shared_ptr<GenAISession> m_session;
    LLMParams m_llm_params;
    LLMGeneratorParams m_default_generator_params;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_LLM_INTERNAL_HPP_ */
