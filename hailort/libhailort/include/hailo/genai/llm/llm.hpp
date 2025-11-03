/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file llm.hpp
 * @brief HailoRT GenAI LLM (Large Language Models) API.
 * This API is currently in preview and may undergo further changes.
 **/

#ifndef _HAILO_GENAI_LLM_HPP_
#define _HAILO_GENAI_LLM_HPP_

#include "hailo/expected.hpp"
#include "hailo/vdevice.hpp"
#include "hailo/genai/common.hpp"
#include "hailo/hailort_common.hpp"

namespace hailort
{
namespace genai
{

class LLM;

/*! Parameters to configure the LLM model */
class HAILORTAPI LLMParams
{
public:
    LLMParams() = default;

    /**
     * Creates LLMParams with optional model configuration.
     *
     * @param[in] hef_path                   The path of the Hef file. If empty, set_model() must be called later.
     * @param[in] lora_name                  The name of the chosen LoRA. Default is empty string.
     * @param[in] optimize_memory_on_device  Whether to optimize memory usage on device by enabling client-side tokenization.
     *                                       When true, tokenization is performed on the host, reducing device memory usage.
     *                                       Requires libhailort to be compiled with HAILO_BUILD_CLIENT_TOKENIZER=ON.
     *                                       Default is false.
     */
     LLMParams(const std::string &hef_path, const std::string &lora_name = "", bool optimize_memory_on_device = false);

    /**
     * Sets LLM model.
     *
     * @param[in] hef_path        The path of the Hef file.
     * @param[in] lora_name       The name of the chosen LoRA.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    hailo_status set_model(const std::string &hef_path, const std::string &lora_name="");

    /**
     * @return The Hef path of the LLM model.
     */
    const std::string& hef() const;

    /**
     * @return The LoRA name of the LLM model.
     */
    const std::string& lora() const;

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
    std::string m_lora;
    bool m_optimize_memory_on_device;
};

/*! The LLMGeneratorParams represents the parameters for text generation, which can be changed during runtime for each generator. */
class HAILORTAPI LLMGeneratorParams
{
public:
    /**
     * Sets the sampling temperature of the LLM model.
     *
     * @param[in] temperature           The sampling temperature.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    hailo_status set_temperature(float32_t temperature);

    /**
     * @return The sampling temperature.
     */
    float32_t temperature() const;

    /**
     * Sets the top_p parameter of the LLM model.
     *
     * @param[in] top_p                 The top_p sampling value.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    hailo_status set_top_p(float32_t top_p);

    /**
     * @return The top_p sampling value.
     */
    float32_t top_p() const;

    /**
     * Sets the top_k parameter of the LLM model.
     *
     * @param[in] top_k                 The top_k sampling value.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    hailo_status set_top_k(uint32_t top_k);

    /**
     * @return The top_k sampling value.
     */
    uint32_t top_k() const;

    /**
     * Sets the frequency_penalty parameter of the LLM model.
     *
     * @param[in] frequency_penalty     The frequency_penalty for generated tokens.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    hailo_status set_frequency_penalty(float32_t frequency_penalty);

    /**
     * @return The frequency_penalty for generated tokens.
     */
    float32_t frequency_penalty() const;

    /**
     * Sets the max_generated_tokens parameter of the LLM model.
     *
     * @param[in] max_generated_tokens  The maximum number of tokens that can be generated, not including the input tokens.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note This number includes all special tokens such as start and end tokens.
     * @note This number does not include the tokens of the generation recovery sequence. See also LLM::set_generation_recovery_sequence().
     */
    hailo_status set_max_generated_tokens(uint32_t max_generated_tokens);

    /**
     * @return The maximum number of tokens that can be generated.
     */
    uint32_t max_generated_tokens() const;

    /**
     * Whether the LLM sampling should be statistical or greedy.
     *
     * @param[in] do_sample     true meaning the model will perform statistical sampling, false meaning the model will perform greedy sampling.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    hailo_status set_do_sample(bool do_sample);

    /**
     * @return Whether the LLM sampling should be statistical or greedy.
     */
    bool do_sample() const;

    /**
     * Sets the seed for the LLM model.
     *
     * @param[in] seed          The seed for the random number generator for statistical sampling.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note If seed is not set, or is set to its default value HAILO_RANDOM_SEED - a random seed will be used.
     */
    hailo_status set_seed(uint32_t seed);

    /**
     * @return The seed for the random number generator.
     */
    uint32_t seed() const;

    LLMGeneratorParams(float32_t temperature, float32_t top_p, uint32_t top_k, float32_t frequency_penalty,
        uint32_t max_generated_tokens, bool do_sample, uint32_t seed) :
            m_temperature(temperature), m_top_p(top_p), m_top_k(top_k), m_frequency_penalty(frequency_penalty),
            m_max_generated_tokens(max_generated_tokens), m_do_sample(do_sample), m_seed(seed) {}
 
private:
    LLMGeneratorParams() = default;
    friend class LLM;

    float32_t m_temperature;
    float32_t m_top_p;
    uint32_t m_top_k;
    float32_t m_frequency_penalty;
    uint32_t m_max_generated_tokens;
    bool m_do_sample;
    uint32_t m_seed;
};

/*! The LLMGeneratorCompletion object is used to read token completions */
class HAILORTAPI LLMGeneratorCompletion final
{
public:
    LLMGeneratorCompletion(LLMGeneratorCompletion &&);
    LLMGeneratorCompletion(const LLMGeneratorCompletion &) = delete;
    LLMGeneratorCompletion &operator=(LLMGeneratorCompletion &&) = delete;
    LLMGeneratorCompletion &operator=(const LLMGeneratorCompletion &) = delete;
    ~LLMGeneratorCompletion();

    enum class Status {
        GENERATING = 0,
        MAX_TOKENS_REACHED,
        LOGICAL_END_OF_GENERATION,
        ABORTED,

        COUNT,

        MAX_VALUE = HAILO_MAX_ENUM,
    };

    /**
     * Reads the next token completion.
     *
     * @param[out] output          The buffer to store the next generated token completion.
     * @param[in]  output_size     The output buffer size.
     * @param[in]  timeout         The timeout for the read operation.
     * @return Upon success, returns Expected to size_t, representing the number of bytes read. Otherwise, returns Unexpected of ::hailo_status error.
     * @note The returned output is a UTF-8 encoded string.
     * @note In case the next token completion is the replacement character (U+FFFD), this token will be buffered until the next printable character is received.
     */
    Expected<size_t> read(char *output, size_t output_size, std::chrono::milliseconds timeout = DEFAULT_READ_TIMEOUT);

    /**
     * Reads the next token completion.
     *
     * @param[in] timeout          The timeout for the read operation.
     * @return Upon success, returns Expected of std::string, representing the next token completion. Otherwise, returns Unexpected of ::hailo_status error.
     * @note The returned output is a UTF-8 encoded string.
     * @note In case the next token completion is the replacement character (U+FFFD), this token will be buffered until the next printable character is received.
     */
    Expected<std::string> read(std::chrono::milliseconds timeout = DEFAULT_READ_TIMEOUT);

    /**
     * Reads all remaining token completions and returns them as a complete string.
     *
     * @param[in] timeout         The timeout for the entire read operation.
     * @return Upon success, returns Expected of std::string containing all remaining generated text.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     * @note This function will read tokens until the generation is complete (status is not GENERATING).
     * @note The returned output is a UTF-8 encoded string.
     * @note After calling this function, no further reads should be attempted on this LLMGeneratorCompletion object.
     */
     Expected<std::string> read_all(std::chrono::milliseconds timeout = HAILO_INFINITE_TIMEOUT);

    /**
     * Returns the current generation status.
     *
     * @return Status - The current generation status, which can indicate:
     *         - GENERATING: The generation process is ongoing.
     *         - MAX_TOKENS_REACHED: The maximum number of tokens has been generated.
     *         - LOGICAL_END_OF_GENERATION: The generation reached its logical end.
     *         - ABORTED: The generation was aborted by the user.
     * @note Once this function returns a status indicating the end of generation (e.g., MAX_TOKENS_REACHED, LOGICAL_END_OF_GENERATION or ABORTED),
     *  no further reads should be attempted on this LLMGeneratorCompletion object,
     *  as it indicates that all token completions have been provided.
     */
    Status generation_status() const;

    /**
     * Aborts the current generation by injecting the model's generation recovery sequence
     * (from LLM::get_generation_recovery_sequence() or VLM::get_generation_recovery_sequence()).
     *
     * The function will:
     * - Stop the generation process.
     * - Flush any tokens that were generated but not yet read.
     * - Set the generation status to ABORTED.
     * - Allow a new generation to start once this function returns.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note This function blocks until the generation is fully aborted.
     */
    hailo_status abort();

    static constexpr std::chrono::milliseconds DEFAULT_READ_TIMEOUT = std::chrono::seconds(10);

    class Impl;
    LLMGeneratorCompletion(std::unique_ptr<Impl> pimpl);
private:
    std::unique_ptr<Impl> m_pimpl;
};

/*! The LLMGenerator object is used to send input prompt to the LLM model for generating responses */
class HAILORTAPI LLMGenerator
{
public:
    LLMGenerator(LLMGenerator &&other);
    LLMGenerator &operator=(LLMGenerator &&) = delete;
    LLMGenerator(const LLMGenerator &) = delete;
    LLMGenerator &operator=(const LLMGenerator &) = delete;
    virtual ~LLMGenerator();

    /**
     * Writes an input prompt to the LLM model.
     *
     * @param[in] prompt        The text prompt to be processed by the LLM model. The prompt is written as-is without any template formatting.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note Multiple consecutive writes will concatenate prompts without adding separators.
     * @note Writing while the model is actively generating content can result in undefined behavior.
     * @note If the LLMGenerator is destructed before generate() is called, all inputs written to it will be discarded.
     */
    hailo_status write(const std::string &prompt);

    /**
     * Writes a structured input prompt to the LLM model using JSON strings.
     *
     * @param[in] prompt_json_strings    A vector of JSON strings representing the structured prompt
     *                                   (e.g., {R"({"role": "system", "content": "You are a helpful assistant"})", R"({"role": "user", "content": "Hello"})"}).
     *                                   This structured input is combined with the model's prompt template (retrievable via `LLM::prompt_template()`).
     *                                   Each element in the vector represents a message in JSON format.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note In case a given key is not supported by the model's template, or if an expected key is missing (e.g., "content" without "role"), the word `None` will replace it.
     * @note Multiple consecutive writes will concatenate prompts without adding separators.
     * @note Writing while the model is actively generating content can result in undefined behavior.
     * @note If the LLMGenerator is destructed before generate() is called, all inputs written to it will be discarded.
     */
    hailo_status write(const std::vector<std::string> &prompt_json_strings);

    /**
     * Marks the end of input and initiates the generation process.
     * Returns an LLMGeneratorCompletion, which allows fetching generated tokens incrementally.
     *
     * @return Upon success, returns Expected of LLMGeneratorCompletion. Otherwise, returns Unexpected of ::hailo_status error.
     * @note Only one `LLMGeneratorCompletion` instance should exist at a time. Creating multiple instances concurrently may lead to undefined behavior.
     *       A subsequent LLMGeneratorCompletion must only be created once the previous one has been fully destructed.
     * @note If the LLMGenerator is destructed before generate() is called, all inputs written to it will be discarded.
     */
    Expected<LLMGeneratorCompletion> generate();

    class Impl;
    LLMGenerator(std::shared_ptr<Impl> pimpl);
private:
    std::shared_ptr<Impl> m_pimpl;
};

/*! Represents the LLM Model.
 *  Manages the lifecycle and configuration of a large language model instance.
 *  Provides methods to generate text, and manage the conversation context that is stored on the Hailo device.
 *  The entire LLM pipeline is offloaded to the Hailo device (tokenization, pre and post-process, etc), allowing for efficient processing of large language models.
 */
class HAILORTAPI LLM
{
public:

    /**
     * Creates an LLM model instance configured with the specified parameters.
     *
     * @param[in] vdevice           The VDevice object used to communicate with the Hailo device.
     * @param[in] llm_params        The LLMParams object used to configure the LLM model.
     *
     * @return Upon success, returns Expected of LLM. Otherwise, returns Unexpected of ::hailo_status error.
     */
    static Expected<LLM> create(std::shared_ptr<VDevice> vdevice, const LLMParams &llm_params);

    /**
     * Creates an LLMGeneratorParams object with the model's default values.
     *
     * @return Upon success, returns Expected of LLMGeneratorParams. Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<LLMGeneratorParams> create_generator_params();

    /**
     * Creates an LLMGenerator object from the provided generation parameters.
     *
     * @param[in] params            The LLMGeneratorParams used to set the generator parameters.
     * @return Upon success, returns Expected of LLMGenerator. Otherwise, returns Unexpected of ::hailo_status error.
     * @note Only one generator shall be active at any time to ensure precision in parameter handling.
     *       A subsequent generator must only be created once the previous one has been fully destructed.
     *       Creating two generators at the same time may lead to invalid usage and undefined behavior.
     */
    Expected<LLMGenerator> create_generator(const LLMGeneratorParams &params);

    /**
     * Creates an LLMGenerator object using the model's default generator parameters.
     *
     * @return Upon success, returns Expected of LLMGenerator. Otherwise, returns Unexpected of ::hailo_status error.
     * @note Only one generator shall be active at any time to ensure precision in parameter handling.
     *       A subsequent generator must only be created once the previous one has been fully destructed.
     *       Creating two generators at the same time may lead to invalid usage and undefined behavior.
     */
    Expected<LLMGenerator> create_generator();

    /**
     * Generates text directly using structured JSON prompts and default generation parameters without explicitly creating a generator.
     * This is a convenience method that creates a generator with default parameters, writes the structured prompt, and initiates generation.
     *
     * @param[in] prompt_json_strings    A vector of JSON strings representing the structured prompt.
     * @return Upon success, returns Expected of LLMGeneratorCompletion. Otherwise, returns Unexpected of ::hailo_status error.
     * @note This method uses the model's default generation parameters.
     * @note This method is equivalent to: create_generator() -> write(prompt_json_strings) -> generate().
     * @note Since this function creates a generator, and only one generator can be active at a time,
     *       no other generator can be created while this function is running.
     * @note Only one `LLMGeneratorCompletion` instance should exist at a time. Creating multiple instances concurrently may lead to undefined behavior.
     *       A subsequent LLMGeneratorCompletion must only be created once the previous one has been fully destructed.
     */
    Expected<LLMGeneratorCompletion> generate(const std::vector<std::string> &prompt_json_strings);

    /**
     * Generates text directly using structured JSON prompts without explicitly creating a generator.
     * This is a convenience method that creates a generator, writes the structured prompt, and initiates generation.
     *
     * @param[in] params                 The LLMGeneratorParams used to configure generation.
     * @param[in] prompt_json_strings    A vector of JSON strings representing the structured prompt.
     * @return Upon success, returns Expected of LLMGeneratorCompletion. Otherwise, returns Unexpected of ::hailo_status error.
     * @note This method is equivalent to: create_generator(params) -> write(prompt_json_strings) -> generate().
     * @note Since this function creates a generator, and only one generator can be active at a time,
     *       no other generator can be created while this function is running.
     * @note Only one `LLMGeneratorCompletion` instance should exist at a time. Creating multiple instances concurrently may lead to undefined behavior.
     *       A subsequent LLMGeneratorCompletion must only be created once the previous one has been fully destructed.
     */
    Expected<LLMGeneratorCompletion> generate(const LLMGeneratorParams &params, const std::vector<std::string> &prompt_json_strings);

    /**
     * Tokenizes a given string into a vector of integers representing the tokens.
     *
     * @param[in] prompt     The input string to tokenize.
     * @return Upon success, returns Expected of vector of integers. Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<std::vector<int>> tokenize(const std::string &prompt);

    /**
     * Gets the current context usage of the LLM model.
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
     * Obtains the maximum context capacity of the LLM model.
     *
     * @return Upon success, returns Expected of size_t representing the maximum number of tokens
     *         that can be stored in the model's context. Otherwise, returns Unexpected of ::hailo_status error.
     * @note This function returns the maximum number of tokens that can be stored in the model's context,
     *       which is determined in the model compilation, and is not configurable.
     */
    Expected<size_t> max_context_capacity();

    /**
     * Clears the conversation context of the LLM.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note This function must not be called while a generation is in progress
     */
    hailo_status clear_context();

    /**
     * Saves the current context of the LLM, as a binary blob that can be used to load the context later on.
     *
     * @return Upon success, returns Expected of BufferPtr. Otherwise, returns Unexpected of ::hailo_status error.
     * @note The context is unique for a specific LLM model, and can be used to set the context using load_context().
     *       Trying to set this context on a different LLM (e.g. QWEN context on a LLAMA model) will result in an error.
     */
    Expected<BufferPtr> save_context();

    /**
     * Loads a context to the LLM.
     *
     * @param[in] context     The context to set.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note The context is unique for a specific LLM model. It is retrieved using save_context().
     *       Trying to set a context from a different LLM (e.g. QWEN context on a LLAMA model) will result in an error.
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
     * Gets the generation recovery sequence of the LLM model.
     *
     * @return Upon success, returns Expected of std::string, representing the generation recovery sequence of the LLM model. Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<std::string> get_generation_recovery_sequence();

    /**
     * Returns the prompt template used by the LLM.
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
     * @note This replaces any previously set stop tokens, including the built-in end-of-sentence tokens.
     * @note Stop tokens are checked after each generated token, so very long sequences may impact performance.
     */
    hailo_status set_stop_tokens(const std::vector<std::string> &stop_tokens);

    /**
     * Gets the currently configured stop token sequences.
     *
     * @return Upon success, returns Expected of vector of strings representing the current stop token sequences.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<std::vector<std::string>> get_stop_tokens();

    LLM(LLM &&);
    LLM &operator=(LLM &&) = delete;
    LLM(const LLM &) = delete;
    LLM &operator=(const LLM &) = delete;
    virtual ~LLM();

    class Impl;
    LLM(std::unique_ptr<Impl> pimpl);
private:
    std::unique_ptr<Impl> m_pimpl;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_LLM_HPP_ */
