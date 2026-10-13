/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file minja_wrapper.cpp
 * @brief minja wrapper implementation.
 **/

#include "minja_wrapper_internal.hpp"
#include "common/utils.hpp"

#include <stdexcept>

namespace hailort
{
namespace genai
{

static const json empty_role = {{"role", "TMP"}, {"content", "TMP"}};
static constexpr const char *EMPTY_CHAT_TEMPLATE_KWARGS = "{}";

// Returns the kwargs as a JSON object with sorted keys
static Expected<nlohmann::json> parse_chat_template_kwargs(const std::string &chat_template_kwargs)
{
    if (chat_template_kwargs.empty()) {
        return nlohmann::json::object();
    }

    auto parsed = nlohmann::json::parse(chat_template_kwargs, nullptr, false);
    CHECK(!parsed.is_discarded(), HAILO_INVALID_ARGUMENT, "chat_template_kwargs is not valid JSON");
    CHECK(parsed.is_object(), HAILO_INVALID_ARGUMENT, "chat_template_kwargs must be a JSON object, got '{}'",
        parsed.type_name());
    return parsed;
}

static minja::chat_template_inputs make_inputs(bool add_generation_prompt, const nlohmann::ordered_json &extra_context,
    std::chrono::system_clock::time_point start_time)
{
    minja::chat_template_inputs inputs;
    inputs.add_generation_prompt = add_generation_prompt;
    inputs.extra_context = extra_context;
    // TODO: HRT-21075 - keep the rendered date stable when the time zone changes inside a context
    inputs.now = start_time;
    return inputs;
}

Expected<PromptTemplateHandler> PromptTemplateHandler::create(const std::string &prompt_template,
    const std::string &bos_token, const std::string &eos_token)
{
    if (prompt_template.empty()) {
        // Create an empty PromptTemplateHandler
        return PromptTemplateHandler(nullptr);
    }

    try {
        auto pimpl = std::make_unique<PromptTemplateHandler::Impl>(prompt_template, bos_token, eos_token);
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

PromptTemplateHandler::Impl::Impl(const std::string &prompt_template, const std::string &bos_token,
    const std::string &eos_token)
    : m_templ(prompt_template, bos_token, eos_token), m_is_first(true),
      m_extra_context(nlohmann::ordered_json::object()), m_prompt_template(prompt_template),
      m_context_start_time(std::chrono::system_clock::now())
{
    m_system_prompt = render_constant_prefix(m_extra_context, m_context_start_time);
}

std::string PromptTemplateHandler::Impl::render_constant_prefix(const nlohmann::ordered_json &extra_context,
    std::chrono::system_clock::time_point start_time) const
{
    auto inputs = make_inputs(false, extra_context, start_time);
    inputs.messages = json::array({ empty_role });
    return m_templ.apply(inputs);
}

Expected<std::string> PromptTemplateHandler::render(const std::vector<std::string> &prompt_json_strings, const std::vector<std::string> &tools_json_strings)
{
    CHECK(m_pimpl, HAILO_INTERNAL_FAILURE, "Prompt handler is not initialized. This may happen if the prompt_template is not defined.");
    try {
        return m_pimpl->render(prompt_json_strings, tools_json_strings);
    } catch (const std::exception &e) {
        LOGGER__ERROR("Failed to render prompt from JSON strings: {}", e.what());
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }
}

Expected<std::string> PromptTemplateHandler::Impl::render(const std::vector<std::string> &prompt_json_strings, const std::vector<std::string> &tools_json_strings)
{
    const auto start_time = m_is_first ? std::chrono::system_clock::now() : m_context_start_time;
    auto inputs = make_inputs(true, m_extra_context, start_time);
    for (const auto &tools_json_string : tools_json_strings) {
        auto parsed_json = json::parse(tools_json_string);
        inputs.tools.push_back(parsed_json);
    }
    if (m_is_first) {
        // Messages array for the first prompt
        for (const auto &json_string : prompt_json_strings) {
            auto parsed_json = json::parse(json_string);
            inputs.messages.push_back(parsed_json);
        }
        auto system_prompt = render_constant_prefix(m_extra_context, start_time);
        auto templated_prompt = m_templ.apply(inputs);
        commit_context_start(start_time, std::move(system_prompt));
        mark_context_used();
        return templated_prompt;
    } else {
        CHECK_AS_EXPECTED(tools_json_strings.empty(), HAILO_INVALID_OPERATION, "Tools can only be provided on the first prompt of the context");
        inputs.messages.push_back(empty_role);
        for (const auto &json_string : prompt_json_strings) {
            auto parsed_json = json::parse(json_string);
            // Check for system role messages in consecutive calls
            CHECK_AS_EXPECTED(!(parsed_json.contains("role") && (parsed_json["role"] == std::string("system"))),
                HAILO_INVALID_OPERATION, "System role messages can only be provided on the first prompt");
            inputs.messages.push_back(parsed_json);
        }
        std::string templated_prompt = m_templ.apply(inputs);
        // Remove the bos and system prompt from the beginning
        CHECK(0 == templated_prompt.rfind(m_system_prompt, 0), HAILO_INTERNAL_FAILURE,
            "Rendered prompt does not start with the template's constant prefix, refusing to strip it");
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

void PromptTemplateHandler::mark_context_used()
{
    if (m_pimpl) {
        m_pimpl->mark_context_used();
    }
}

void PromptTemplateHandler::Impl::mark_context_used()
{
    m_is_first = false;
}

void PromptTemplateHandler::Impl::start_loaded_context()
{
    mark_context_used();
    const auto start_time = std::chrono::system_clock::now();
    auto system_prompt = render_constant_prefix(m_extra_context, start_time);
    commit_context_start(start_time, std::move(system_prompt));
}

void PromptTemplateHandler::Impl::commit_context_start(std::chrono::system_clock::time_point start_time,
    std::string &&system_prompt) noexcept
{
    m_context_start_time = start_time;
    m_system_prompt = std::move(system_prompt);
}

void PromptTemplateHandler::on_context_loaded(size_t context_usage)
{
    if (0 == context_usage) {
        reset_state();
        return;
    }

    try {
        if (m_pimpl) {
            m_pimpl->start_loaded_context();
        }
    } catch (const std::exception &e) {
        LOGGER__ERROR("Failed to render the constant prefix of the loaded context: {}", e.what());
    }
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

hailo_status PromptTemplateHandler::set_chat_template_kwargs(const std::string &chat_template_kwargs)
{
    try {
        if (!m_pimpl) {
            const auto parsed = parse_chat_template_kwargs(chat_template_kwargs);
            if (!parsed) {
                return parsed.status();
            }
            CHECK(parsed->empty(), HAILO_NOT_AVAILABLE,
                "chat_template_kwargs cannot be set, because the model has no chat template");
            return HAILO_SUCCESS;
        }
        return m_pimpl->set_chat_template_kwargs(chat_template_kwargs);
    } catch (const std::exception &e) {
        LOGGER__ERROR("Failed to set chat_template_kwargs: {}", e.what());
        return HAILO_INTERNAL_FAILURE;
    }
}

hailo_status PromptTemplateHandler::Impl::set_chat_template_kwargs(const std::string &chat_template_kwargs)
{
    const auto parsed = parse_chat_template_kwargs(chat_template_kwargs);
    if (!parsed) {
        return parsed.status();
    }
    auto canonical = parsed->dump();
    if (canonical == m_extra_context.dump()) {
        return HAILO_SUCCESS;
    }
    CHECK(m_is_first, HAILO_INVALID_OPERATION,
        "chat_template_kwargs can only be changed on a fresh context. Call clear_context() first");

    nlohmann::ordered_json extra_context = parsed.value();
    std::string system_prompt;
    try {
        system_prompt = render_constant_prefix(extra_context, m_context_start_time);
    } catch (const std::runtime_error &e) {
        LOGGER__ERROR("The chat template failed to render with chat_template_kwargs '{}': {}", canonical, e.what());
        return HAILO_INVALID_ARGUMENT;
    }

    m_extra_context = std::move(extra_context);
    m_system_prompt = std::move(system_prompt);
    return HAILO_SUCCESS;
}

Expected<std::string> PromptTemplateHandler::chat_template_kwargs() const
{
    try {
        if (!m_pimpl) {
            return std::string(EMPTY_CHAT_TEMPLATE_KWARGS);
        }
        return m_pimpl->chat_template_kwargs();
    } catch (const std::exception &e) {
        LOGGER__ERROR("Failed to get chat_template_kwargs: {}", e.what());
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }
}

std::string PromptTemplateHandler::Impl::chat_template_kwargs() const
{
    return m_extra_context.dump();
}

// https://stackoverflow.com/questions/71104545/constructor-and-destructor-in-c-when-using-the-pimpl-idiom
// All member functions shoud be implemented in the cpp module
PromptTemplateHandler::~PromptTemplateHandler() = default;
PromptTemplateHandler::PromptTemplateHandler(PromptTemplateHandler &&) = default;


} /* namespace genai */
} /* namespace hailort */
