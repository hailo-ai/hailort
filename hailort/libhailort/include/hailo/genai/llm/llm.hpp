/**
 * Copyright (c) 2019-2024 Hailo Technologies Ltd. All rights reserved.
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
    std::string hef() const;

    /**
     * @return The LoRA name of the LLM model.
     */
    std::string lora() const;

    /**
     * Sets the LLM vocabulary file path.
     *
     * @param[in] path            The path of the vocabulary file.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    hailo_status set_vocabulary(const std::string &path);

    /**
     * @return The vocabulary file path.
     */
    std::string vocabulary() const;

private:
    hailo_status set_hef(const std::string &path);

    std::string m_hef_path;
    std::string m_lora;
    std::string m_vocabulary_path;
};

/*! The LLMGeneratorParams represents the parameters for text generation, which can be changed during runtime for each generator. */
class HAILORTAPI LLMGeneratorParams
{
public:
    LLMGeneratorParams() = default;

    /**
     * Sets the sampling temperature of the LLM model.
     *
     * @param[in] temperature           The sampling temperature.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note: Currently not implemented.
     */
    hailo_status set_temperature(float32_t temperature);

    /**
     * @return The sampling temperature.
     */
    float32_t temperature() const;

    /**
     * Sets the top_p parameter of the LLM model.
     *
     * @param[in] top_p           The top_p sampling value.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note: Currently not implemented.
     */
    hailo_status set_top_p(float32_t top_p);

    /**
     * @return The top_p sampling value.
     */
    float32_t top_p() const;

private:
    float32_t m_temperature;
    float32_t m_top_p;
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
     * @return Upon success, returns Expected of std::string, represnting the next token completion. Otherwise, returns Unexpected of ::hailo_status error.
     * @note The returned output is a UTF-8 encoded string.
     */
    Expected<std::string> read(std::chrono::milliseconds timeout = DEFAULT_READ_TIMEOUT);

    /**
     * Check if the token completion has finished.
     *
     * @return True if the generation has finished, false otherwise.
     * @note Once this function returns true, no further reads should be attempted on this LLMGeneratorCompletion object,
     * as it indicates the end of available token completions.
     */
    bool end_of_generation() const;

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
     * Returns a LLMGeneratorCompletion, which allows fetching generated tokens incrementally.
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
     * Creates a LLMGenerator object from the provided generation parameters or defaults if none are specified.
     *
     * @param[in] params            The LLMGeneratorParams used to set the generator parameters.
     *                              If not provided, the model will use it's default params.
     * @return Upon success, returns Expected of LLMGenerator. Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<LLMGenerator> create_generator(const LLMGeneratorParams &params = LLMGeneratorParams());

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
