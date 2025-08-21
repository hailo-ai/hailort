/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file text2image.hpp
 * @brief HailoRT GenAI Text to Image API.
 * This API is currently in preview and may undergo further changes.
 **/

#ifndef _HAILO_GENAI_TEXT2IMAGE_HPP_
#define _HAILO_GENAI_TEXT2IMAGE_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/buffer.hpp"
#include "hailo/vdevice.hpp"
#include "hailo/genai/common.hpp"

#include <vector>

namespace hailort
{
namespace genai
{

class Text2Image;

/*! Scheduler type for the diffusion process */
enum class HailoDiffuserSchedulerType
{
    EULER_DISCRETE = 0,
    DDIM,
};

/*! Parameters to configure the Text2Image model */
class HAILORTAPI Text2ImageParams
{
public:
    Text2ImageParams();

    /**
     * Sets the denoise model.
     *
     * @param[in] hef_path        The denoising hef model.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    hailo_status set_denoise_model(const std::string &hef_path);

    /**
     * Sets the text encoder model.
     *
     * @param[in] hef_path        The text encoder hef model.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    hailo_status set_text_encoder_model(const std::string &hef_path);

    /**
     * Sets the image decoder model.
     *
     * @param[in] hef_path        The image decoder hef model.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    hailo_status set_image_decoder_model(const std::string &hef_path);

    /**
     * Sets the IP Adapter model.
     *
     * @param[in] hef_path        The IP Adapter hef model.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    hailo_status set_ip_adapter_model(const std::string &hef_path);

    /**
     * Sets the scheduler for the diffusion process.
     *
     * @param[in] scheduler_type     The chosen scheduler type.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note: Default scheduler is HailoDiffuserSchedulerType::EULER_DISCRETE.
     */
    hailo_status set_scheduler(HailoDiffuserSchedulerType scheduler_type);

    /**
     * @return The Hef path of the denoising model.
     */
    const std::string& denoise_hef() const;

    /**
     * @return The Hef path of the text encoder model.
     */
    const std::string& text_encoder_hef() const;

    /**
     * @return The Hef path of the image decoder model.
     */
    const std::string& image_decoder_hef() const;

    /**
     * @return The Hef path of the ip adapter model.
     */
    const std::string& ip_adapter_hef() const;

    /**
     * @return The scheduler type for the diffusion process.
     */
    HailoDiffuserSchedulerType scheduler() const;

private:
    std::string m_denoise_hef;
    std::string m_text_encoder_hef;
    std::string m_image_decoder_hef;
    std::string m_ip_adapter_hef;
    HailoDiffuserSchedulerType m_scheduler_type;
};

/*! Parameters for image generation, which can be changed during runtime for each generator. */
class HAILORTAPI Text2ImageGeneratorParams
{
public:
    /**
     * Sets the numer of images to generate.
     *
     * @param[in] samples_count   The number of samples in the output of the model pipeline.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    hailo_status set_samples_count(uint32_t samples_count);

    /**
     * @return The number of images to generate.
     */
    uint32_t samples_count() const;

    /**
     * Sets the steps count
     *
     * @param[in] steps_count   The number of steps for the denoising stage of the model pipeline.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    hailo_status set_steps_count(uint32_t steps_count);

    /**
     * @return The number of steps for the denoising stage of the model pipeline.
     */
    uint32_t steps_count() const;

    /**
     * Sets the guidance scale
     *
     * @param[in] guidance_scale   The factor between the positive and negative prompts. Higher value will keep
     *                             the output image closer to the prompt.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    hailo_status set_guidance_scale(float32_t guidance_scale);

    /**
     * @return The guidance scale of the model pipeline.
     */
    float32_t guidance_scale() const;

    /**
     * Sets the seed for noise generation
     *
     * @param[in] seed   Random seed to control the noise generation.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note If seed is not set, or is set to its default value UINT32_MAX - a random seed will be used.
     */
    hailo_status set_seed(uint32_t seed);

    /**
     * @return The seed used in the model pipeline.
     */
    uint32_t seed() const;

    Text2ImageGeneratorParams(uint32_t samples_count, uint32_t steps_count, float32_t guidance_scale, uint32_t seed) :
        m_samples_count(samples_count), m_steps_count(steps_count), m_guidance_scale(guidance_scale), m_seed(seed) {}

private:
    Text2ImageGeneratorParams() = default;
    friend class Text2Image;
    friend class Text2ImageServer;

    uint32_t m_samples_count;
    uint32_t m_steps_count;
    float32_t m_guidance_scale;
    uint32_t m_seed;
};

/*! The GeneratorCompletion object is used to generate the output images samples */
class HAILORTAPI Text2ImageGenerator
{
public:
    Text2ImageGenerator(Text2ImageGenerator &&);
    Text2ImageGenerator &operator=(Text2ImageGenerator &&) = delete;
    Text2ImageGenerator(const Text2ImageGenerator &) = delete;
    Text2ImageGenerator &operator=(const Text2ImageGenerator &) = delete;
    virtual ~Text2ImageGenerator();

    /**
     * Sets an external noise tensor to be used as the initial latent input for the first denoising iteration.
     * 
     * @param noise       A MemoryView containing the external noise tensor data.
     *                    The size of the data should be `Text2Image::input_noise_frame_size()`.
     * 
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     *
     * @note The provided noise will be used as the initial noise for all subsequent generations, using this generator instance.
     *       Creating a new generator restores the default random noise behavior.
     */
    hailo_status set_initial_noise(const MemoryView &noise);

   /**
     * Generates the output samples images.
     *
     * @param[in] positive_prompt       A non-empty positive prompt to be sent to the model.
     * @param[in] negative_prompt       The negative prompt to be sent to the model. Can be an empty string.
     * @param[in] timeout               The timeout for the generate operation.
     *
     * @return Upon success, returns Expected of std::vector<Buffers> containing the output samples images.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     * @note: If the pipeline is configured with IP Adapter this function will fail and return error.
     * @note: If abort() is called during a generation, the generation call will return ::HAILO_OPERATION_ABORTED.
     */
    Expected<std::vector<Buffer>> generate(const std::string &positive_prompt, const std::string &negative_prompt,
        std::chrono::milliseconds timeout = DEFAULT_OPERATION_TIMEOUT);

   /**
     * Generates the output samples images.
     *
     * @param[in] positive_prompt       A non-empty positive prompt to be sent to the model.
     * @param[in] negative_prompt       The negative prompt to be sent to the model. Can be an empty string.
     * @param[in] ip_adapter            The image to be sent to the ip-adapter step of the model.
     *                                  The size of the image should be `Text2Image::ip_adapter_frame_size()`.
     * @param[in] timeout               The timeout for the generate operation.
     *
     * @return Upon success, returns Expected of std::vector<Buffers> containing the output samples images.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     * @note: If the pipeline is configured without IP Adapter the function will fail and return error.
     * @note: If abort() is called during a generation, the generation call will return ::HAILO_OPERATION_ABORTED.
     */
    Expected<std::vector<Buffer>> generate(const std::string &positive_prompt, const std::string &negative_prompt,
        const MemoryView &ip_adapter, std::chrono::milliseconds timeout = DEFAULT_OPERATION_TIMEOUT);

    /**
     * Generates the output samples images.
     *
     * @param[out] output_images        Vector of pre-allocated memory to be filled with the output images.
     *                                  The size of the vector should be the size of `samples_count`.
     *                                  The size of each MemoryView in the vector should be `Text2Image::output_sample_frame_size()`.
     * @param[in] positive_prompt       A non-empty positive prompt to be sent to the model.
     * @param[in] negative_prompt       The negative prompt to be sent to the model. Can be an empty string.
     * @param[in] timeout               The timeout for the generate operation.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note: If the pipeline is configured with IP Adapter this function will fail and return error.
     * @note: If abort() is called during a generation, the generation call will return ::HAILO_OPERATION_ABORTED.
     */
    hailo_status generate(std::vector<MemoryView> &output_images, const std::string &positive_prompt,
        const std::string &negative_prompt, std::chrono::milliseconds timeout = DEFAULT_OPERATION_TIMEOUT);

    /**
     * Generates the output samples images.
     *
     * @param[out] output_images        Vector of pre-allocated memory to be filled with the output images.
     *                                  The size of the vector should be the size of `samples_count`.
     *                                  The size of each MemoryView in the vector should be `Text2Image::output_sample_frame_size()`.
     * @param[in] positive_prompt       A non-empty positive prompt to be sent to the model.
     * @param[in] negative_prompt       The negative prompt to be sent to the model. Can be an empty string.
     * @param[in] ip_adapter            The image to be sent to the ip-adapter step of the model.
     *                                  The size of the image should be `Text2Image::ip_adapter_frame_size()`.
     * @param[in] timeout               The timeout for the generate operation.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note: If the pipeline is configured without IP Adapter the function will fail and return error.
     * @note: If abort() is called during a generation, the generation call will return ::HAILO_OPERATION_ABORTED.
     */
    hailo_status generate(std::vector<MemoryView> &output_images, const std::string &positive_prompt,
        const std::string &negative_prompt, const MemoryView &ip_adapter,
        std::chrono::milliseconds timeout = DEFAULT_OPERATION_TIMEOUT);

    /**
     * Aborts the current generation.
     *
     * If a generation is in progress, it will be aborted, and the corresponding generate call will return ::HAILO_OPERATION_ABORTED.
     * If no generation is in progress, this function has no effect and returns ::HAILO_SUCCESS.
     * Once this function returns, a new generation can be initiated.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note This function blocks until the ongoing generation is fully aborted.
     */
    hailo_status abort();

    static constexpr std::chrono::milliseconds DEFAULT_OPERATION_TIMEOUT = std::chrono::seconds(30);

    class Impl;
    Text2ImageGenerator(std::unique_ptr<Impl> pimpl);
private:
    std::unique_ptr<Impl> m_pimpl;
};

/*! Represents the Text2Image Model pipeline.
 *  Manages the lifecycle and configuration of a Text2Image model instance.
 *  The entire Text2Image pipeline is offloaded to the Hailo device (tokenization, pre and post-process, etc), allowing for efficient processing of Text2Image models.
 */
class HAILORTAPI Text2Image
{
public:
    Text2Image(Text2Image &&);
    Text2Image(const Text2Image &) = delete;
    Text2Image &operator=(Text2Image &&) = delete;
    Text2Image &operator=(const Text2Image &) = delete;
    virtual ~Text2Image();

    /**
     * Creates Text2Image model pipeline instance configured with the specified parameters.
     *
     * @param[in] vdevice           The VDevice object used to communicate with the Hailo device.
     * @param[in] params            The Text2ImageParams object used to configure the model.
     * @return Upon success, returns Expected of Text2Image. Otherwise, returns Unexpected of ::hailo_status error.
     */
    static Expected<Text2Image> create(std::shared_ptr<VDevice> vdevice, const Text2ImageParams &params);

    /**
     * Creates an Text2ImageGeneratorParams object with the model's default values.
     *
     * @return Upon success, returns Expected of Text2ImageGeneratorParams. Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<Text2ImageGeneratorParams> create_generator_params();

    /**
     * Creates a Generator object from the provided generator parameters.
     *
     * @param[in] params            The Text2ImageGeneratorParams used to set the generator parameters.
     *
     * @return Upon success, returns Expected of Text2ImageGenerator. Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<Text2ImageGenerator> create_generator(const Text2ImageGeneratorParams &params);

    /**
     * Creates a Generator object using the model's default generator parameters.
     *
     * @return Upon success, returns Expected of Text2ImageGenerator. Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<Text2ImageGenerator> create_generator();

    /**
     * Generates text directly using default generation parameters without explicitly creating a generator.
     * This is a convenience method that creates a generator with default parameters, and initiates generation.
     *
     * @param[in] positive_prompt       A non-empty positive prompt to be sent to the model.
     * @param[in] negative_prompt       The negative prompt to be sent to the model. Can be an empty string.
     * @param[in] timeout               The timeout for the generate operation.
     *
     * @return Upon success, returns Expected of std::vector<Buffers> containing the output samples images.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     * @note This method is equivalent to: create_generator() -> generate(positive_prompt, negative_prompt, timeout).
     * @note: If the pipeline is configured with IP Adapter this function will fail and return error.
     */
     Expected<std::vector<Buffer>> generate(const std::string &positive_prompt, const std::string &negative_prompt,
        std::chrono::milliseconds timeout = Text2ImageGenerator::DEFAULT_OPERATION_TIMEOUT);

    /**
     * Generates text directly without explicitly creating a generator.
     * This is a convenience method that creates a generator, and initiates generation.
     *
     * @param[in] params                The Text2ImageGeneratorParams used to set the generator parameters.
     * @param[in] positive_prompt       A non-empty positive prompt to be sent to the model.
     * @param[in] negative_prompt       The negative prompt to be sent to the model. Can be an empty string.
     * @param[in] timeout               The timeout for the generate operation.
     *
     * @return Upon success, returns Expected of std::vector<Buffers> containing the output samples images.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     * @note This method is equivalent to: create_generator(params) -> generate(positive_prompt, negative_prompt, timeout).
     * @note: If the pipeline is configured with IP Adapter this function will fail and return error.
     */
     Expected<std::vector<Buffer>> generate(const Text2ImageGeneratorParams &params, const std::string &positive_prompt,
        const std::string &negative_prompt, std::chrono::milliseconds timeout = Text2ImageGenerator::DEFAULT_OPERATION_TIMEOUT);

    /**
     * @return The frame size of a single output sample.
     */
    uint32_t output_sample_frame_size() const;

    /**
     * @return The shape of a single output sample.
     */
    hailo_3d_image_shape_t output_sample_shape() const;

    /**
     * @return The format type of a single output sample.
     */
    hailo_format_type_t output_sample_format_type() const;

    /**
     * @return The format order of a single output sample.
     */
    hailo_format_order_t output_sample_format_order() const;

    /**
     * @return Upon success, The frame size of the IP Adapter image. Otherwise, returns Unexpected of ::hailo_status error.
     * @note This function is only valid when the pipeline is configured with an IP Adapter.
     */
    Expected<uint32_t> ip_adapter_frame_size() const;

    /**
     * @return Upon success, The shape of the IP Adapter image. Otherwise, returns Unexpected of ::hailo_status error.
     * @note This function is only valid when the pipeline is configured with an IP Adapter.
     */
    Expected<hailo_3d_image_shape_t> ip_adapter_shape() const;

    /**
     * @return Upon success, The format type of the IP Adapter image. Otherwise, returns Unexpected of ::hailo_status error.
     * @note This function is only valid when the pipeline is configured with an IP Adapter.
     */
    Expected<hailo_format_type_t> ip_adapter_format_type() const;

    /**
     * @return Upon success, The format order of the IP Adapter image. Otherwise, returns Unexpected of ::hailo_status error.
     * @note This function is only valid when the pipeline is configured with an IP Adapter.
     */
    Expected<hailo_format_order_t> ip_adapter_format_order() const;

    /**
     * @return The frame size of the initial noise.
     */
    uint32_t input_noise_frame_size() const;

    /**
     * @return The shape of the initial noise.
     */
    hailo_3d_image_shape_t input_noise_shape() const;

    /**
     * @return The format type of the initial noise.
     */
    hailo_format_type_t input_noise_format_type() const;

    /**
     * @return The format order of the initial noise.
     */
    hailo_format_order_t input_noise_format_order() const;

    /**
     * Tokenizes a given string into a vector of integers representing the tokens.
     *
     * @param[in] prompt     The input string to tokenize.
     * @return Upon success, returns Expected of vector of integers. Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<std::vector<int>> tokenize(const std::string &prompt);

    class Impl;
    Text2Image(std::unique_ptr<Impl> pimpl);
private:
    std::unique_ptr<Impl> m_pimpl;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_TEXT2IMAGE_HPP_ */
