/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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
    static Expected<PromptTemplateHandler> create(const std::string &prompt_template);

    Expected<std::string> render(const std::vector<std::string> &prompt_json_strings);
    void reset_state();
    Expected<std::string> prompt_template() const;

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
