/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file minja_wrapper_internal.hpp
 * @brief a wrapper implementation for minja library, without exceptions.
 * See https://github.com/google/minja.
 **/

#ifndef _HAILO_MINJA_WRAPPER_INTERNAL_HPP_
#define _HAILO_MINJA_WRAPPER_INTERNAL_HPP_

#include "minja_wrapper.hpp"

// Disable warnings from the minja library:
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244 4101)
#else
/* GCC/Clang do not emit these warnings for minja headers */
#endif /* defined(_MSC_VER) */

#include <minja/chat-template.hpp>

#if defined(_MSC_VER)
#pragma warning(pop)
#else
/* No action needed — GCC/Clang did not push warnings */
#endif /* defined(_MSC_VER) */

#include <chrono>

namespace hailort
{
namespace genai
{


class PromptTemplateHandler::Impl final
{
public:
    Impl(const std::string &prompt_template, const std::string &bos_token, const std::string &eos_token);
    ~Impl() = default;

    Expected<std::string> render(const std::vector<std::string> &prompt_json_strings, const std::vector<std::string> &tools_json_strings);
    void reset_state();
    void mark_context_used();
    void start_loaded_context();
    Expected<std::string> prompt_template() const;
    hailo_status set_chat_template_kwargs(const std::string &chat_template_kwargs);
    std::string chat_template_kwargs() const;

private:
    std::string render_constant_prefix(const nlohmann::ordered_json &extra_context,
        std::chrono::system_clock::time_point start_time) const;
    void commit_context_start(std::chrono::system_clock::time_point start_time, std::string &&system_prompt) noexcept;

    minja::chat_template m_templ;

    bool m_is_first;
    nlohmann::ordered_json m_extra_context;
    std::string m_system_prompt;
    std::string m_prompt_template;
    std::chrono::system_clock::time_point m_context_start_time;
};


} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_MINJA_WRAPPER_INTERNAL_HPP_ */
