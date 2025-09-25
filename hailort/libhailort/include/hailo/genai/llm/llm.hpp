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
#include "hailo/genai/vdevice_genai.hpp"
#include "hailo/genai/common.hpp"

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
     * Sets LLM model.
     *
     * @param[in] hef_path        The path of the Hef file.
     * @param[in] lora_name       The name of the chosen LoRA. Currently not implemented.
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

private:
    std::string m_hef_path;
    std::string m_lora;
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
     * @note If the seed remains unchanged between calls, the generator will continue producing values from its
     *       current state. If a new seed is provided, the generator is reinitialized.
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
     */
    Expected<size_t> read(char *output, size_t output_size, std::chrono::milliseconds timeout = DEFAULT_READ_TIMEOUT);

    /**
     * Reads the next token completion.
     *
     * @param[in] timeout          The timeout for the read operation.
     * @return Upon success, returns Expected of std::string, representing the next token completion. Otherwise, returns Unexpected of ::hailo_status error.
     * @note The returned output is a UTF-8 encoded string.
     */
    Expected<std::string> read(std::chrono::milliseconds timeout = DEFAULT_READ_TIMEOUT);

    /**
     * Returns the current generation status.
     *
     * @return Status - The current generation status, which can indicate:
     *         - GENERATING: The generation process is ongoing.
     *         - MAX_TOKENS_REACHED: The maximum number of tokens has been generated.
     *         - LOGICAL_END_OF_GENERATION: The generation reached its logical end.
     * @note Once this function returns a status indicating the end of generation  (e.g., MAX_TOKENS_REACHED or LOGICAL_END_OF_GENERATION),
     *  no further reads should be attempted on this LLMGeneratorCompletion object,
     *  as it indicates that all token completions have been provided.
     */
    Status generation_status() const;

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
     * Writes the input prompt to the LLM model.
     *
     * @param[in] prompt        The prompt to be sent to the LLM model
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note Multiple sequential writes are supported. Prompts are aggregated as-is without separators.
     */
    hailo_status write(const std::string &prompt);

    /**
     * Writes the input prompt to the LLM model.
     *
     * @param[in] prompt        The prompt to be sent to the LLM model.
     * @param[in] prompt_size   The prompt size.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note Multiple sequential writes are supported. Prompts are aggregated as-is without separators.
     */
    hailo_status write(const char *prompt, size_t prompt_size);

    /**
     * Marks the end of input and initiates the generation process.
     * Returns an LLMGeneratorCompletion, which allows fetching generated tokens incrementally.
     *
     * @return Upon success, returns Expected of LLMGeneratorCompletion. Otherwise, returns Unexpected of ::hailo_status error.
     * @note Once this function is called, the LLMGenerator is no longer functional.
     */
    Expected<LLMGeneratorCompletion> generate();

    class Impl;
    LLMGenerator(std::unique_ptr<Impl> pimpl);
private:
    std::unique_ptr<Impl> m_pimpl;
};

/*! Represents the LLM Model.
 *  Manages the lifecycle and configuration of a large language model instance.
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
     */
    Expected<LLMGenerator> create_generator(const LLMGeneratorParams &params);

    /**
     * Creates an LLMGenerator object using the model's default generator parameters.
     *
     * @return Upon success, returns Expected of LLMGenerator. Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<LLMGenerator> create_generator();

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
