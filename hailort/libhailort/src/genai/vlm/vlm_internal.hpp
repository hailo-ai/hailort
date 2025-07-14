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

namespace hailort
{
namespace genai
{


class VLMGenerator::Impl final
{
public:
    Impl(std::shared_ptr<SessionWrapper> session);

    Expected<LLMGeneratorCompletion> generate(const std::string &prompt, const std::vector<MemoryView> &input_frames);

private:
    std::shared_ptr<SessionWrapper> m_session;
};


class VLM::Impl final
{
public:

    static Expected<std::unique_ptr<Impl>> create_unique(std::shared_ptr<hailort::VDevice> vdevice, const VLMParams &vlm_params);

    Expected<VLMGenerator> create_generator(const LLMGeneratorParams &params);
    Expected<LLMGeneratorParams> create_generator_params();
    Expected<std::vector<int>> tokenize(const std::string &prompt);
    hailo_status clear_context();

    uint32_t input_frame_size() const;
    const hailo_3d_image_shape_t& input_frame_shape() const;
    const hailo_format_type_t& input_frame_format_type() const;
    const hailo_format_order_t& input_frame_format_order() const;

    ~Impl();

    Impl(std::shared_ptr<SessionWrapper> session, const VLMParams &Vlm_params,
        const LLMGeneratorParams &default_generator_params, hailo_3d_image_shape_t input_frame_shape,
        hailo_format_t input_frame_format);

private:
    hailo_status validate_generator_params(const LLMGeneratorParams &params);

    std::shared_ptr<SessionWrapper> m_session;
    VLMParams m_vlm_params;
    LLMGeneratorParams m_default_generator_params;
    hailo_3d_image_shape_t m_input_frame_shape;
    hailo_format_t m_input_frame_format;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_VLM_INTERNAL_HPP_ */
