/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file speech2text_internal.hpp
 * @brief HailoRT GenAI Speech2Text internal implementation of client side.
 **/

#ifndef _HAILO_GENAI_SPEECH2TEXT_INTERNAL_HPP_
#define _HAILO_GENAI_SPEECH2TEXT_INTERNAL_HPP_

#include "hailo/hailort.h"
#include "hailo/genai/speech2text/speech2text.hpp"
#include "common/genai/session_wrapper/session_wrapper.hpp"
#include "hailo/expected.hpp"
#include "hailo/vdevice.hpp"

namespace hailort
{
namespace genai
{

static const std::string SPEECH2TEXT_DEFAULT_LANGUAGE = "en";

class Speech2Text::Impl final
{
public:
    static Expected<std::unique_ptr<Impl>> create_unique(std::shared_ptr<hailort::VDevice> vdevice, const Speech2TextParams &speech2text_params);

    Expected<Speech2TextGeneratorParams> create_generator_params();
    Expected<std::string> generate_all_text(MemoryView audio_buffer, const Speech2TextGeneratorParams &generator_params, std::chrono::milliseconds timeout);
    Expected<std::string> generate_all_text(MemoryView audio_buffer, std::chrono::milliseconds timeout);
    Expected<std::vector<Speech2Text::SegmentInfo>> generate_all_segments(MemoryView audio_buffer, const Speech2TextGeneratorParams &generator_params, std::chrono::milliseconds timeout);
    Expected<std::vector<Speech2Text::SegmentInfo>> generate_all_segments(MemoryView audio_buffer, std::chrono::milliseconds timeout);
    Expected<std::vector<int>> tokenize(const std::string &text);

    Impl(std::shared_ptr<SessionWrapper> session, const Speech2TextParams &speech2text_params);
    ~Impl();
private:
    Expected<std::vector<Speech2Text::SegmentInfo>> generate_impl(MemoryView audio_buffer, const Speech2TextGeneratorParams &generator_params, std::chrono::milliseconds timeout);

    std::shared_ptr<SessionWrapper> m_session;
    Speech2TextParams m_speech2text_params;
};


} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_SPEECH2TEXT_INTERNAL_HPP_ */
