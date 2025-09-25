/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file text2image_internal.hpp
 * @brief HailoRT Text to Image internal implementation.
 **/

#ifndef _HAILO_GENAI_TEXT2IMAGE_INTERNAL_HPP_
#define _HAILO_GENAI_TEXT2IMAGE_INTERNAL_HPP_

#include "hailo/hailort.h"
#include "hailo/genai/text2image/text2image.hpp"
#include "common/genai/serializer/serializer.hpp"

namespace hailort
{
namespace genai
{

class Text2ImageGenerator::Impl final
{
public:
    Impl(std::shared_ptr<GenAISession> session, const Text2ImageGeneratorParams &params, uint32_t output_sample_frame_size,
        bool is_ip_adapter_supported, uint32_t ip_adapter_frame_size);

    Expected<std::vector<Buffer>> generate(const std::string &positive_prompt, const std::string &negative_prompt,
        std::chrono::milliseconds timeout);

    Expected<std::vector<Buffer>> generate(const std::string &positive_prompt, const std::string &negative_prompt,
        const MemoryView &ip_adapter, std::chrono::milliseconds timeout);

    hailo_status generate(std::vector<MemoryView> &output_images, const std::string &positive_prompt,
        const std::string &negative_prompt, std::chrono::milliseconds timeout);

    hailo_status generate(std::vector<MemoryView> &output_images, const std::string &positive_prompt,
        const std::string &negative_prompt, const MemoryView &ip_adapter,
        std::chrono::milliseconds timeout);

    hailo_status stop();

private:
    std::shared_ptr<GenAISession> m_session;
    Text2ImageGeneratorParams m_params;
    uint32_t m_output_sample_frame_size;
    bool m_is_ip_adapter_supported;
    uint32_t m_ip_adapter_frame_size;
};


typedef struct {
    hailo_format_t format;
    hailo_3d_image_shape_t shape;
} frame_info_t;

class Text2Image::Impl final
{
public:
    static Expected<std::unique_ptr<Text2Image::Impl>> create_unique(std::shared_ptr<VDevice> vdevice, const Text2ImageParams &params);

    Expected<Text2ImageGenerator> create_generator(const Text2ImageGeneratorParams &params = Text2ImageGeneratorParams());
    Expected<Text2ImageGeneratorParams> create_generator_params();

    uint32_t output_sample_frame_size() const;
    hailo_3d_image_shape_t output_sample_shape() const;
    hailo_format_type_t output_sample_format_type() const;
    hailo_format_order_t output_sample_format_order() const;
    Expected<uint32_t> ip_adapter_frame_size() const;
    Expected<hailo_3d_image_shape_t> ip_adapter_shape() const;
    Expected<hailo_format_type_t> ip_adapter_format_type() const;
    Expected<hailo_format_order_t> ip_adapter_format_order() const;

private:
    Impl(std::shared_ptr<GenAISession> session, const Text2ImageParams &params, const frame_info_t &output_sample_frame_info,
        const bool is_ip_adapter_supported, const frame_info_t &ip_adapter_frame_info = {});

    static hailo_status validate_params(const Text2ImageParams &params);
    static hailo_status load_params(std::shared_ptr<GenAISession> session, const Text2ImageParams &params);

    hailo_status validate_generator_params(const Text2ImageGeneratorParams &params);
    hailo_status load_generator_params(const Text2ImageGeneratorParams &params);

    std::shared_ptr<GenAISession> m_session;
    Text2ImageParams m_params;

    frame_info_t m_output_sample_frame_info;
    bool m_is_ip_adapter_supported;
    frame_info_t m_ip_adapter_frame_info;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_TEXT2IMAGE_INTERNAL_HPP_ */
