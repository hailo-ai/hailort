/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file speech2text.hpp
 * @brief HailoRT GenAI Speech2Text API.
 * This API is currently in preview and may undergo further changes.
 **/

#ifndef _HAILO_GENAI_SPEECH2TEXT_HPP_
#define _HAILO_GENAI_SPEECH2TEXT_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/vdevice.hpp"
#include <string_view>


namespace hailort
{
namespace genai
{

/*! Parameters to configure the Speech2Text model */
class HAILORTAPI Speech2TextParams
{
public:
    Speech2TextParams() = default;

    /**
     * Creates Speech2TextParams with optional model configuration.
     *
     * @param[in] hef_path        The path of the Hef file. If empty, set_model() must be called later.
     */
     Speech2TextParams(std::string_view hef_path);

    /**
     * Sets Speech2Text model.
     *
     * @param[in] hef_path        The path of the Hef file.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    hailo_status set_model(std::string_view hef_path);

    /**
     * @return The Hef path of the Speech2Text model.
     */
    std::string_view hef() const;

private:
    std::string m_hef_path;
};

/*! The task to perform in the Speech2Text model. */
enum class Speech2TextTask {
    TRANSCRIBE = 0, // Default option
    TRANSLATE
};

/*! The Speech2TextGeneratorParams represents the parameters for speech-to-text generation, which can be changed during runtime in each generation. */
class HAILORTAPI Speech2TextGeneratorParams
{
public:
    /**
     * Sets the task for the Speech2Text model to perform.
     *
     * @param[in] task      The task to perform.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    hailo_status set_task(Speech2TextTask task);

    /**
     * Sets the language to use in the generation process. In the format of ISO-639-1 two-letter code, for example: "en", "fr", etc.
     *
     * @param[in] language          The language to use in the generation process.
     *                              In the format of ISO-639-1 two-letter code, for example: "en", "fr", etc.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    hailo_status set_language(std::string_view language);

    /**
     * @return The task for the Speech2Text model to perform.
     */
    Speech2TextTask task() const;

    /**
     * @return The language to use in the generation process.
     */
    std::string_view language() const;


    Speech2TextGeneratorParams(Speech2TextTask task, std::string_view language);
private:
    Speech2TextGeneratorParams();
    friend class Speech2Text;

    Speech2TextTask m_task;
    std::string m_language;
};

/*! Represents the Speech2Text Model.
*  Manages the lifecycle and configuration of a speech-to-text model instance.
*  Provides methods to transcribe and translate speech to text.
*  The entire speech-to-text pipeline is offloaded to the Hailo device (tokenization, pre and post-process, etc), allowing for efficient processing of speech-to-text.
*/
class HAILORTAPI Speech2Text
{
public:

    /**
    * Holds information about a single segment of transcribed audio.
    */
    struct SegmentInfo
    {
        /** Start timestamp of the segment relative to the start of the audio, in seconds */
        float32_t start_sec;

        /** End timestamp of the segment relative to the start of the audio, in seconds */
        float32_t end_sec;

        /** Transcribed text corresponding to this segment */
        std::string text;
    };

    /**
    * Creates an Speech2Text model instance configured with the specified parameters.
    *
    * @param[in] vdevice                    The VDevice object used to communicate with the Hailo device.
    * @param[in] speech2text_params         The Speech2TextParams object used to configure the Speech2Text model.
    *
    * @return Upon success, returns Expected of Speech2Text. Otherwise, returns Unexpected of ::hailo_status error.
    */
    static Expected<Speech2Text> create(std::shared_ptr<VDevice> vdevice, const Speech2TextParams &speech2text_params);

    /**
    * Creates an Speech2TextGeneratorParams object with the model's default values.
    *
    * @return Upon success, returns Expected of Speech2TextGeneratorParams. Otherwise, returns Unexpected of ::hailo_status error.
    */
    Expected<Speech2TextGeneratorParams> create_generator_params();

    /**
    * Generates a full transcription from the given audio data and returns it as a single string.
    *
    * @param[in] audio_buffer       The audio data to process. Must be PCM float32 normalized to [-1.0, 1.0), mono, little-endian, at 16 kHz.
    * @param[in] generator_params   The generator parameters to use for the generation.
    * @param[in] timeout            The timeout for the operation.
    * @return Upon success, returns Expected of std::string. Otherwise, returns Unexpected of ::hailo_status error.
    */
    Expected<std::string> generate_all_text(MemoryView audio_buffer, const Speech2TextGeneratorParams &generator_params, std::chrono::milliseconds timeout = DEFAULT_OPERATION_TIMEOUT);

    /**
    * Generates a full transcription from the given audio data and returns it as a single string.
    *
    * @param[in] audio_buffer       The audio data to process. Must be PCM float32 normalized to [-1.0, 1.0), mono, little-endian, at 16 kHz.
    * @param[in] timeout            The timeout for the operation.
    * @return Upon success, returns Expected of std::string. Otherwise, returns Unexpected of ::hailo_status error.
    * @note The default generator parameters are used in this method, as returned by create_generator_params().
    */
    Expected<std::string> generate_all_text(MemoryView audio_buffer, std::chrono::milliseconds timeout = DEFAULT_OPERATION_TIMEOUT);

    /**
    * Generates transcription segments from audio data, and returns them as a vector of SegmentInfo.
    *
    * @param[in] audio_buffer       The audio data to process. Must be PCM float32 normalized to [-1.0, 1.0), mono, little-endian, at 16 kHz.
    * @param[in] generator_params   The generator parameters to use for the generation.
    * @param[in] timeout            The timeout for the operation.
    * @return Upon success, returns Expected of std::vector<Speech2Text::SegmentInfo> containing
    *         the transcription segments. Otherwise, returns Unexpected of ::hailo_status error.
    */
    Expected<std::vector<SegmentInfo>> generate_all_segments(MemoryView audio_buffer,
        const Speech2TextGeneratorParams &generator_params, std::chrono::milliseconds timeout = DEFAULT_OPERATION_TIMEOUT);

    /**
    * Generates transcription segments from audio data, and returns them as a vector of SegmentInfo.
    *
    * @param[in] audio_buffer       The audio data to process. Must be PCM float32 normalized to [-1.0, 1.0), mono, little-endian, at 16 kHz.
    * @param[in] timeout            The timeout for the operation.
    * @return Upon success, returns Expected of std::vector<Speech2Text::SegmentInfo> containing
    *         the transcription segments. Otherwise, returns Unexpected of ::hailo_status error.
    * @note The default generator parameters are used in this method, as returned by create_generator_params().
    */
    Expected<std::vector<SegmentInfo>> generate_all_segments(MemoryView audio_buffer, std::chrono::milliseconds timeout = DEFAULT_OPERATION_TIMEOUT);

    /**
    * Tokenizes a given text into a vector of integers representing the tokens.
    *
    * @param[in] text     The input text to tokenize.
    * @return Upon success, returns Expected of vector of integers. Otherwise, returns Unexpected of ::hailo_status error.
    */
    Expected<std::vector<int>> tokenize(const std::string &text);

    static constexpr std::chrono::milliseconds DEFAULT_OPERATION_TIMEOUT = std::chrono::seconds(10);

    Speech2Text(Speech2Text &&);
    Speech2Text &operator=(Speech2Text &&) = delete;
    Speech2Text(const Speech2Text &) = delete;
    Speech2Text &operator=(const Speech2Text &) = delete;
    virtual ~Speech2Text();

    class Impl;
    Speech2Text(std::unique_ptr<Impl> pimpl);
private:
    std::unique_ptr<Impl> m_pimpl;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_SPEECH2TEXT_HPP_ */
