/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pre_process.cpp
 * @brief Speech2Text pre-process implementation
 **/

#include "pre_process.hpp"
#include "hailo/hailort.h"
#include "librosa.h"

namespace hailort
{
namespace genai
{
 
// TODO: HRT-18577 - Take params from hef
constexpr int N_MELS = 80;
constexpr int N_FFT = 400;
constexpr float32_t FMIN = 0.0f;

constexpr float32_t MIN_CLIP_VALUE = 1e-10f;
constexpr float32_t DB_DYNAMIC_RANGE = 8.0f;
constexpr float32_t NORMALIZATION_BIAS = 4.0f;
constexpr float32_t POWER = 2.0f;
constexpr bool HANN_CENTERED = true;
const std::string HANN_WINDOW = "hann";
const std::string REFLECT_MODE = "reflect";

// TODO: HRT-18595 - Optimizations (Avoid runtime allocations, smaller padding size)
Eigen::Matrix<float32_t, 1, Eigen::Dynamic, Eigen::RowMajor> pad_audio(MemoryView audio_chunk, size_t padding_element_count)
{
    auto chunk_element_count = audio_chunk.size() / sizeof(float32_t);
    const float32_t* chunk_data = reinterpret_cast<const float32_t*>(audio_chunk.data());
    Eigen::Map<const Eigen::Matrix<float32_t, 1, Eigen::Dynamic, Eigen::RowMajor>> map_chunk =
        Eigen::Map<const Eigen::Matrix<float32_t, 1, Eigen::Dynamic, Eigen::RowMajor>>(chunk_data, chunk_element_count);

    size_t total_length = chunk_element_count + padding_element_count;
    Eigen::Matrix<float32_t, 1, Eigen::Dynamic, Eigen::RowMajor> padded_audio(total_length);
    padded_audio.setZero();
    padded_audio.leftCols(chunk_element_count) = map_chunk;

    return padded_audio;
}

// TODO: HRT-18595 - (use multiple threads, avoid runtime allocations if possible)
Eigen::MatrixXf Speech2TextPreProcess::compute_log_mel(const MemoryView audio_chunk, size_t chunk_size, int sample_rate, int hop_length)
{
    // The padding is a "silent chunk" for the model's productivity
    // ref: https://github.com/openai/whisper/blob/main/whisper/audio.py#L146
    // ref: https://github.com/openai/whisper/blob/main/whisper/audio.py#L170
    auto padding_element_count = chunk_size * sample_rate;
    auto padded_audio = pad_audio(audio_chunk, padding_element_count);
    Eigen::Map<const Eigen::Matrix<float32_t, 1, Eigen::Dynamic, Eigen::RowMajor>> padded_audio_map(padded_audio.data(), 1, padded_audio.cols());

    auto max_freq = sample_rate / 2;
    auto mel = librosa::Feature::melspectrogram(padded_audio_map, sample_rate, N_FFT, hop_length, HANN_WINDOW, HANN_CENTERED,
        REFLECT_MODE, POWER, N_MELS, FMIN, max_freq);

    mel = mel.array().max(MIN_CLIP_VALUE);
    mel = mel.array().log10();
    float32_t max_val = mel.maxCoeff();
    mel = mel.array().max(max_val - DB_DYNAMIC_RANGE);
    mel = (mel.array() + NORMALIZATION_BIAS) / NORMALIZATION_BIAS;

    return mel;
}

void Speech2TextPreProcess::pad_or_trim(const Eigen::MatrixXf &input, Eigen::Map<Eigen::MatrixXf> &output)
{
    assert(output.cols() == input.cols());

    // Copy the necessary rows (trim if needed)
    auto rows_to_copy = std::min(input.rows(), output.rows());
    output.topRows(rows_to_copy) = input.topRows(rows_to_copy);

    // Pad if needed
    if (input.rows() < output.rows()) {
        output.middleRows(rows_to_copy, output.rows() - rows_to_copy).setZero();
    }
}

} /* namespace genai */
} /* namespace hailort */
 