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
#include "common/genai/session_wrapper/session_wrapper.hpp"

namespace hailort
{
namespace genai
{

class Text2ImageGenerator::Impl final
{
public:
    Impl(std::shared_ptr<SessionWrapper> session, const Text2ImageGeneratorParams &params, uint32_t output_sample_frame_size,
        bool is_ip_adapter_supported, uint32_t ip_adapter_frame_size);

    hailo_status set_initial_noise(const MemoryView &noise);

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
    // RPC functions
    hailo_status text2image_generate(std::vector<MemoryView> &output_images, const std::string &positive_prompt,
        const std::string &negative_prompt, std::chrono::milliseconds timeout, const MemoryView &ip_adapter = MemoryView());

    std::shared_ptr<SessionWrapper> m_session;
    Text2ImageGeneratorParams m_params;
    uint32_t m_output_sample_frame_size;
    bool m_has_ip_adapter;
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

    ~Impl();

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
    uint32_t input_noise_frame_size() const;
    hailo_3d_image_shape_t input_noise_shape() const;
    hailo_format_type_t input_noise_format_type() const;
    hailo_format_order_t input_noise_format_order() const;
    Expected<std::vector<int>> tokenize(const std::string &prompt);

    Impl(std::shared_ptr<SessionWrapper> session, const Text2ImageParams &params, const Text2ImageGeneratorParams &generator_params, const frame_info_t &output_sample_frame_info,
        const bool is_ip_adapter_supported, const frame_info_t &input_noise_frame_info, const frame_info_t &ip_adapter_frame_info = {});

private:
    static hailo_status validate_params(const Text2ImageParams &params);
    static bool all_builtin_hefs(const Text2ImageParams &params);
    static bool has_builtin_hef(const Text2ImageParams &params);

    hailo_status validate_generator_params(const Text2ImageGeneratorParams &params);
    hailo_status load_generator_params(const Text2ImageGeneratorParams &params);

    // RPC functions
    static hailo_status send_hef(std::shared_ptr<SessionWrapper> session, const std::string &hef_path);
    static Expected<Text2ImageCreateSerializer::ReplyInfo> text2image_create(std::shared_ptr<SessionWrapper> session, const hailo_vdevice_params_t &vdevice_params, bool is_builtin,
        const Text2ImageParams &text2image_params);
    static Expected<std::pair<hailo_3d_image_shape_t, hailo_format_t>> text2image_get_ip_adapter_info(std::shared_ptr<SessionWrapper> session);
    static Expected<Text2ImageGeneratorParams> text2image_get_generator_params(std::shared_ptr<SessionWrapper> session);
    hailo_status text2image_generator_create(const Text2ImageGeneratorParams &params);
    hailo_status text2image_release();

    std::shared_ptr<SessionWrapper> m_session;
    Text2ImageParams m_params;
    Text2ImageGeneratorParams m_default_generator_params;

    frame_info_t m_output_sample_frame_info;
    bool m_has_ip_adapter;
    frame_info_t m_ip_adapter_frame_info;
    frame_info_t m_input_noise_frame_info;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_TEXT2IMAGE_INTERNAL_HPP_ */
