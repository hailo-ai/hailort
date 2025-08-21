/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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

#include <minja/chat-template.hpp>

namespace hailort
{
namespace genai
{


class PromptTemplateHandler::Impl final
{
public:
    Impl(const std::string &prompt_template);
    ~Impl() = default;

    Expected<std::string> render(const std::vector<std::string> &prompt_json_strings);
    void reset_state();
    Expected<std::string> prompt_template() const;

private:
    minja::chat_template m_templ;

    bool m_is_first;
    std::string m_system_prompt;
    std::string m_prompt_template;
};


} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_MINJA_WRAPPER_INTERNAL_HPP_ */
