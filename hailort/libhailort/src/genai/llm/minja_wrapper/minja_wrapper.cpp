/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file minja_wrapper.cpp
 * @brief minja wrapper implementation.
 **/

#include "minja_wrapper_internal.hpp"
#include "common/utils.hpp"

namespace hailort
{
namespace genai
{

static const json empty_role = {{"role", "TMP"}, {"content", "TMP"}};

Expected<PromptTemplateHandler> PromptTemplateHandler::create(const std::string &prompt_template)
{
    if (prompt_template.empty()) {
        // Create an empty PromptTemplateHandler
        return PromptTemplateHandler(nullptr);
    }

    try {
        auto pimpl = std::make_unique<PromptTemplateHandler::Impl>(prompt_template);
        return PromptTemplateHandler(std::move(pimpl));
    } catch (const std::exception &e) {
        LOGGER__ERROR("Failed to create PromptTemplateHandler: {}", e.what());
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }
}

PromptTemplateHandler::PromptTemplateHandler(std::unique_ptr<Impl> pimpl)
    : m_pimpl(std::move(pimpl))
{
}

PromptTemplateHandler::Impl::Impl(const std::string &prompt_template)
    : m_templ(prompt_template, "", ""), m_is_first(true), m_prompt_template(prompt_template)
{
    minja::chat_template_inputs inputs;
    inputs.add_generation_prompt = false;
    inputs.messages = json::array({ empty_role });
    m_system_prompt = m_templ.apply(inputs);
}

Expected<std::string> PromptTemplateHandler::render(const std::vector<std::string> &prompt_json_strings)
{
    CHECK(m_pimpl, HAILO_INTERNAL_FAILURE, "Prompt handler is not initialized. This may happen if the prompt_template is not defined.");
    try {
        return m_pimpl->render(prompt_json_strings);
    } catch (const std::exception &e) {
        LOGGER__ERROR("Failed to render prompt from JSON strings: {}", e.what());
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }
}

Expected<std::string> PromptTemplateHandler::Impl::render(const std::vector<std::string> &prompt_json_strings)
{
    minja::chat_template_inputs inputs;
    inputs.add_generation_prompt = true;

    if (m_is_first) {
        m_is_first = false;
        // Messages array for the first prompt
        for (const auto &json_string : prompt_json_strings) {
            auto parsed_json = json::parse(json_string);
            inputs.messages.push_back(parsed_json);
        }
        return m_templ.apply(inputs);
    } else {
        inputs.messages.push_back(empty_role);
        for (const auto &json_string : prompt_json_strings) {
            auto parsed_json = json::parse(json_string);
            inputs.messages.push_back(parsed_json);
        }
        std::string templated_prompt = m_templ.apply(inputs);
        // Remove system prompt from the beginning
        templated_prompt.erase(0, m_system_prompt.size());

        return templated_prompt;
    }
}

void PromptTemplateHandler::reset_state()
{
    try {
        if (m_pimpl) {
            m_pimpl->reset_state();
        }
    } catch (const std::exception &e) {
        LOGGER__ERROR("Failed to render prompt: {}", e.what());
    }
}

void PromptTemplateHandler::Impl::reset_state()
{
    m_is_first = true;
}

Expected<std::string> PromptTemplateHandler::prompt_template() const
{
    CHECK(m_pimpl, HAILO_NOT_AVAILABLE, "Prompt handler is not initialized. This may happen if the prompt_template is not defined.");
    try {
        return m_pimpl->prompt_template();
    } catch (const std::exception &e) {
        LOGGER__ERROR("Failed to get prompt_template: {}", e.what());
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }
}

Expected<std::string> PromptTemplateHandler::Impl::prompt_template() const
{
    auto res = m_prompt_template;
    return res;
}

// https://stackoverflow.com/questions/71104545/constructor-and-destructor-in-c-when-using-the-pimpl-idiom
// All member functions shoud be implemented in the cpp module
PromptTemplateHandler::~PromptTemplateHandler() = default;
PromptTemplateHandler::PromptTemplateHandler(PromptTemplateHandler &&) = default;


} /* namespace genai */
} /* namespace hailort */
