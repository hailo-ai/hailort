/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vlm.hpp
 * @brief HailoRT GenAI VLM (Vision Language Models) API.
 * This API is currently in preview and may undergo further changes.
 **/

#ifndef _HAILO_GENAI_VLM_HPP_
#define _HAILO_GENAI_VLM_HPP_

#include "hailo/expected.hpp"
#include "hailo/vdevice.hpp"
#include "hailo/genai/llm/llm.hpp"
#include "hailo/genai/common.hpp"

namespace hailort
{
namespace genai
{

/*! Parameters to configure the VLM model */
class HAILORTAPI VLMParams
{
public:
    VLMParams() = default;

    /**
     * Sets VLM model.
     *
     * @param[in] hef_path        The path of the Hef file.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    hailo_status set_model(const std::string &hef_path);

    /**
     * @return The Hef path of the VLM model.
     */
    const std::string& hef() const;

private:
    std::string m_hef_path;
};

/*! The VLMGenerator object is used to send input prompt and input frame to the VLM model for generating responses */
class HAILORTAPI VLMGenerator
{
public:
    VLMGenerator(VLMGenerator &&other);
    VLMGenerator &operator=(VLMGenerator &&) = delete;
    VLMGenerator(const VLMGenerator &) = delete;
    VLMGenerator &operator=(const VLMGenerator &) = delete;
    virtual ~VLMGenerator();

    /**
     * Initiates generation from the input prompt and input frame to the VLM model.
     * Returns an LLMGeneratorCompletion, which allows fetching generated tokens incrementally.
     *
     * @param[in] prompt        The prompt to be processed by the VLM model
     * @param[in] input_frames  The input frames to be sent to the VLM model.
     *
     * @return Upon success, returns Expected of LLMGeneratorCompletion. Otherwise, returns Unexpected of ::hailo_status error.
     * @note Calling this function while the model is already generating may lead to undefined behavior.
     */
    Expected<LLMGeneratorCompletion> generate(const std::string &prompt, const std::vector<MemoryView> &input_frames);

    /**
     * Initiates generation from the input prompt and input frame to the VLM model.
     * Returns an LLMGeneratorCompletion, which allows fetching generated tokens incrementally.
     *
     * @param[in] prompt        The prompt to be processed by the VLM model
     * @param[in] prompt_size   The prompt size.
     * @param[in] input_frames  The input frames to be sent to the VLM model.
     *
     * @return Upon success, returns Expected of LLMGeneratorCompletion. Otherwise, returns Unexpected of ::hailo_status error.
     * @note Calling this function while the model is already generating may lead to undefined behavior.
     */
    Expected<LLMGeneratorCompletion> generate(const char *prompt, size_t prompt_size, const std::vector<MemoryView> &input_frames);

    class Impl;
    VLMGenerator(std::unique_ptr<Impl> pimpl);
private:
    std::unique_ptr<Impl> m_pimpl;
};

/*! Represents the VLM Model.
 *  Manages the lifecycle and configuration of a vision language model instance.
 *  Provides methods to generate text, and manage the conversation context that is stored on the Hailo device.
 *  The entire VLM pipeline is offloaded to the Hailo device (tokenization, pre and post-process, etc), allowing for efficient processing of vision language models.
 */
class HAILORTAPI VLM
{
public:

    /**
     * Creates an VLM model instance configured with the specified parameters.
     *
     * @param[in] vdevice           The VDevice object used to communicate with the Hailo device.
     * @param[in] vlm_params        The VLMParams object used to configure the VLM model.
     *
     * @return Upon success, returns Expected of VLM. Otherwise, returns Unexpected of ::hailo_status error.
     */
    static Expected<VLM> create(std::shared_ptr<VDevice> vdevice, const VLMParams &vlm_params);

    /**
     * Creates an LLMGeneratorParams object with the model's default values.
     *
     * @return Upon success, returns Expected of LLMGeneratorParams. Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<LLMGeneratorParams> create_generator_params();

    /**
     * Creates an VLMGenerator object from the provided generation parameters.
     *
     * @param[in] params            The LLMGeneratorParams used to set the generator parameters.
     * @return Upon success, returns Expected of VLMGenerator. Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<VLMGenerator> create_generator(const LLMGeneratorParams &params);

    /**
     * Creates an VLMGenerator object using the model's default generator parameters.
     *
     * @return Upon success, returns Expected of VLMGenerator. Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<VLMGenerator> create_generator();

    /**
     * Tokenizes a given string into a vector of integers representing the tokens.
     *
     * @param[in] prompt     The input string to tokenize.
     * @return Upon success, returns Expected of vector of integers. Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<std::vector<int>> tokenize(const std::string &prompt);

    /**
     * Clears the conversation context of the LLM.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note This function must not be called while a generation is in progress
     */
    hailo_status clear_context();

    /**
     * @return The size of the input frame.
     */
    uint32_t input_frame_size() const;

    /**
     * @return The shape of the input frame.
     */
    const hailo_3d_image_shape_t& input_frame_shape() const;

    /**
     * @return The format type of the input frame.
     */
    const hailo_format_type_t& input_frame_format_type() const;

    /**
     * @return The format order of the input frame.
     */
    const hailo_format_order_t& input_frame_format_order() const;


    VLM(VLM &&);
    VLM &operator=(VLM &&) = delete;
    VLM(const VLM &) = delete;
    VLM &operator=(const VLM &) = delete;
    virtual ~VLM();

    class Impl;
    VLM(std::unique_ptr<Impl> pimpl);
private:
    std::unique_ptr<Impl> m_pimpl;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_VLM_HPP_ */
