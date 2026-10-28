/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file minja_wrapper.hpp
 * @brief a wrapper for minja library, without exceptions.
 * See https://github.com/google/minja.
 **/

#ifndef _HAILO_MINJA_WRAPPER_HPP_
#define _HAILO_MINJA_WRAPPER_HPP_

#include "hailo/expected.hpp"

#include <string>
#include <map>
#include <vector>
#include <memory>

namespace hailort
{
namespace genai
{


class PromptTemplateHandler final
{
public:
    // bos_token / eos_token are the strings the template substitutes for '{{bos_token}}' / '{{eos_token}}'.
    // A model whose template does not reference them may pass empty strings.
    static Expected<PromptTemplateHandler> create(const std::string &prompt_template, const std::string &bos_token,
        const std::string &eos_token);

    Expected<std::string> render(const std::vector<std::string> &prompt_json_strings, const std::vector<std::string> &tools_json_strings = {});
    void reset_state();
    void mark_context_used();
    void on_context_loaded(size_t context_usage);
    Expected<std::string> prompt_template() const;

    // "" or "{}" clears. A new value is accepted only while the context is fresh.
    hailo_status set_chat_template_kwargs(const std::string &chat_template_kwargs);
    Expected<std::string> chat_template_kwargs() const;

    class Impl;
    PromptTemplateHandler(std::unique_ptr<Impl> pimpl);
    ~PromptTemplateHandler();

    PromptTemplateHandler(const PromptTemplateHandler &) = delete;
    PromptTemplateHandler &operator=(const PromptTemplateHandler &) = delete;
    PromptTemplateHandler &operator=(PromptTemplateHandler &&) = delete;
    PromptTemplateHandler(PromptTemplateHandler &&);
private:
    std::unique_ptr<Impl> m_pimpl;
};


} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_MINJA_WRAPPER_HPP_ */
