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

namespace hailort
{
namespace genai
{


class PromptTemplateHandler::Impl final
{
public:
    Impl(const std::string &prompt_template);
    ~Impl() = default;

    Expected<std::string> render(const std::vector<std::string> &prompt_json_strings, const std::vector<std::string> &tools_json_strings);
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
