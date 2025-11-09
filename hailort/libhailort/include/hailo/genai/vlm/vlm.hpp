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
     * Constructs a VLMParams object with the specified Hef path.
     *
     * @param[in] hef_path                   The path of the Hef file.
     * @param[in] optimize_memory_on_device  Whether to optimize memory usage on device by enabling client-side tokenization.
     *                                       When true, tokenization is performed on the host, reducing device memory usage.
     *                                       Requires libhailort to be compiled with HAILO_BUILD_CLIENT_TOKENIZER=ON.
     *                                       Default is false.
     */
    VLMParams(const std::string &hef_path, bool optimize_memory_on_device = false) : m_hef_path(hef_path), m_optimize_memory_on_device(optimize_memory_on_device) {}

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

    /**
     * @return Whether memory optimization on device is enabled.
     */
    bool optimize_memory_on_device() const;

    /**
     * Sets whether to optimize memory usage on device.
     *
     * @param[in] optimize_memory_on_device  Whether to enable client-side tokenization for memory optimization.
     *                                       When true, tokenization is performed on the host, reducing device memory usage.
     *                                       Requires libhailort to be compiled with HAILO_BUILD_CLIENT_TOKENIZER=ON.
     */
    void set_optimize_memory_on_device(bool optimize_memory_on_device);

private:
    std::string m_hef_path;
    bool m_optimize_memory_on_device;
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
     * @param[in] prompt        The prompt to be processed by the VLM model. The prompt is written as-is without any template formatting.
     * @param[in] input_frames  The input frames to be sent to the VLM model. This can be empty, in which case the model will generate text only.
     *
     * @return Upon success, returns Expected of LLMGeneratorCompletion. Otherwise, returns Unexpected of ::hailo_status error.
     * @note Calling this function while the model is already generating may lead to undefined behavior.
     * @note Only one `LLMGeneratorCompletion` instance should exist at a time. Creating multiple instances concurrently may lead to undefined behavior.
     *       A subsequent LLMGeneratorCompletion must only be created once the previous one has been fully destructed.
     */
    Expected<LLMGeneratorCompletion> generate(const std::string &prompt, const std::vector<MemoryView> &input_frames);

    /**
     * Initiates generation from structured messages in JSON format and input frames to the VLM model.
     * Returns an LLMGeneratorCompletion, which allows fetching generated tokens incrementally.
     *
     * @param[in] messages_json_strings A vector of JSON strings representing structured messages
     *                                  (e.g., {R"({"role": "user", "content": [{"type": "text", "text": "Describe this image"}, {"type": "image"}]})"}).
     *                                  This structured input is combined with the model's prompt template (retrievable via `VLM::prompt_template()`).
     *                                  Each element in the vector represents a message in JSON format following the VLM message structure.
     * @param[in] input_frames          The input frames to be sent to the VLM model, corresponding to image placeholders in the messages.
     *                                  The number of frames must match the number of image references in the messages.
     *
     * @return Upon success, returns Expected of LLMGeneratorCompletion. Otherwise, returns Unexpected of ::hailo_status error.
     * @note Currently only supported content type is "image" and "text".
     * @note In case a given key is not supported by the model's template, or if an expected key is missing (e.g., "content" without "role"), the word `None` will replace it.
     * @note Image references in the JSON messages are replaced with appropriate vision tokens during template processing.
     * @note The number of input_frames must match the total number of image entries across all messages.
     * @note Calling this function while the model is already generating may lead to undefined behavior.
     * @note Only one `LLMGeneratorCompletion` instance should exist at a time. Creating multiple instances concurrently may lead to undefined behavior.
     *       A subsequent LLMGeneratorCompletion must only be created once the previous one has been fully destructed.
     */
    Expected<LLMGeneratorCompletion> generate(const std::vector<std::string> &messages_json_strings, const std::vector<MemoryView> &input_frames);

    class Impl;
    VLMGenerator(std::shared_ptr<Impl> pimpl);
private:
    std::shared_ptr<Impl> m_pimpl;
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
     * Generates text directly using structured JSON messages and input frames with default generation parameters without explicitly creating a generator.
     * This is a convenience method that creates a generator with default parameters, writes the structured prompt, and initiates generation.
     *
     * @param[in] messages_json_strings A vector of JSON strings representing structured messages.
     * @param[in] input_frames          The input frames to be sent to the VLM model.
     * @return Upon success, returns Expected of LLMGeneratorCompletion. Otherwise, returns Unexpected of ::hailo_status error.
     * @note This method uses the model's default generation parameters.
     * @note This method is equivalent to: create_generator() -> generate(messages_json_strings, input_frames).
     * @note Since this function creates a generator, and only one generator can be active at a time,
     *       no other generator can be created while this function is running.
     * @note Only one `LLMGeneratorCompletion` instance should exist at a time. Creating multiple instances concurrently may lead to undefined behavior.
     *       A subsequent LLMGeneratorCompletion must only be created once the previous one has been fully destructed.
     */
    Expected<LLMGeneratorCompletion> generate(const std::vector<std::string> &messages_json_strings, const std::vector<MemoryView> &input_frames);

    /**
     * Generates text directly using structured JSON messages and input frames without explicitly creating a generator.
     * This is a convenience method that creates a generator, writes the structured prompt, and initiates generation.
     *
     * @param[in] params                The LLMGeneratorParams used to configure generation.
     * @param[in] messages_json_strings A vector of JSON strings representing structured messages.
     * @param[in] input_frames          The input frames to be sent to the VLM model.
     * @return Upon success, returns Expected of LLMGeneratorCompletion. Otherwise, returns Unexpected of ::hailo_status error.
     * @note This method is equivalent to: create_generator(params) -> generate(messages_json_strings, input_frames).
     * @note Since this function creates a generator, and only one generator can be active at a time,
     *       no other generator can be created while this function is running.
     * @note Only one `LLMGeneratorCompletion` instance should exist at a time. Creating multiple instances concurrently may lead to undefined behavior.
     *       A subsequent LLMGeneratorCompletion must only be created once the previous one has been fully destructed.
     */
    Expected<LLMGeneratorCompletion> generate(const LLMGeneratorParams &params, const std::vector<std::string> &messages_json_strings,
        const std::vector<MemoryView> &input_frames);

    /**
     * Tokenizes a given string into a vector of integers representing the tokens.
     *
     * @param[in] prompt     The input string to tokenize.
     * @return Upon success, returns Expected of vector of integers. Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<std::vector<int>> tokenize(const std::string &prompt);

    /**
     * Gets the current context usage of the VLM model.
     *
     * @return Upon success, returns Expected of size_t representing the current number of tokens in the context.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     * @note This function returns the total number of tokens currently stored in the model's context,
     *       including both input tokens and generated tokens from previous interactions.
     * @note The context usage includes the conversation history and any tokens that have been
     *       processed but not yet cleared by clear_context().
     */
    Expected<size_t> get_context_usage_size();

    /**
     * Obtains the maximum context capacity of the VLM model.
     *
     * @return Upon success, returns Expected of size_t representing the maximum number of tokens
     *         that can be stored in the model's context. Otherwise, returns Unexpected of ::hailo_status error.
     * @note This function returns the maximum number of tokens that can be stored in the model's context,
     *       which is determined in the model compilation, and is not configurable.
     */
    Expected<size_t> max_context_capacity();

    /**
     * Clears the conversation context of the VLM.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note This function must not be called while a generation is in progress
     */
    hailo_status clear_context();

    /**
     * Saves the current context of the VLM, as a binary blob that can be used to load the context later on.
     *
     * @return Upon success, returns Expected of BufferPtr. Otherwise, returns Unexpected of ::hailo_status error.
     * @note The context is unique for a specific VLM model, and can be used to set the context using load_context().
     *       Trying to set this context on a different VLM (e.g. QWEN context on a LLAMA model) will result in an error.
     */
    Expected<BufferPtr> save_context();

    /**
     * Loads a context to the VLM.
     *
     * @param[in] context     The context to set.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note The context is unique for a specific VLM model. It is retrieved using save_context().
     *       Trying to set a context from a different VLM (e.g. QWEN2.5-VL context on a QWEN2-VL model) will result in an error.
     */
    hailo_status load_context(const MemoryView &context);

    /**
     * Sets a custom generation recovery sequence to be injected into the model when generation ends abruptly
     * (i.e., due to max-token termination or abort, but not when reaching a stop-token).
     * This sequence helps maintain model usability after abrupt terminations and is not counted towards the max-generated-tokens limit.
     *
     * @param sequence    The recovery sequence to inject after abrupt generation endings. Can be empty string.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note If not set, the model's default sequence will be used. See also get_generation_recovery_sequence().
     * @note This sequence is NOT applied for LOGICAL_END_OF_GENERATION completions.
     */
    hailo_status set_generation_recovery_sequence(const std::string &sequence);

    /**
     * Gets the generation recovery sequence of the VLM model.
     *
     * @return Upon success, returns Expected of std::string, representing the generation recovery sequence of the VLM model. Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<std::string> get_generation_recovery_sequence();

    /**
     * Returns the prompt template used by the VLM.
     *
     * @return Upon success, returns Expected of std::string containing the prompt template. Otherwise, returns Unexpected of ::hailo_status error.
     */
     Expected<std::string> prompt_template();

    /**
     * Sets custom stop token sequences for generation.
     * The generation process will stop when any of the provided sequences is encountered.
     *
     * @param[in] stop_tokens   A vector of strings, where each string represents a stop sequence.
     *                          If a string is encoded to multiple tokens, the full sequence must be matched.
     *                          An empty vector clears all custom stop tokens.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note This replaces any previously set stop tokens.
     * @note The built-in end-of-sentence token from the model is always active regardless of this setting.
     * @note Stop tokens are checked after each generated token, so very long sequences may impact performance.
     */
    hailo_status set_stop_tokens(const std::vector<std::string> &stop_tokens);

    /**
     * Gets the currently configured custom stop token sequences.
     *
     * @return Upon success, returns Expected of vector of strings representing the current stop token sequences.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     * @note This returns only the custom stop tokens set by the user, not the built-in model stop tokens.
     */
    Expected<std::vector<std::string>> get_stop_tokens();

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
