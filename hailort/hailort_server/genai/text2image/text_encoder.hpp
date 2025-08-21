/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file text_encoder.hpp
 * @brief Text2Image embeddings encoder
 **/

#ifndef _HAILO_ENCODER_HPP_
#define _HAILO_ENCODER_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/vdevice.hpp"
#include "common/utils.hpp"

#include "inference_manager.hpp"
#include "hailo_tokenizer.hpp"
#include "token_embedder.hpp"
#include "single_io_model.hpp"
#include "utils.hpp"

namespace hailort
{
namespace genai
{

// TODO: HRT-15972 - When supporting `samples_count`, should multiply this number with batch_size
static const uint16_t INPUT_PROMPTS_BATCH_SIZE = 2; // positive prompt + negative prompt

class TextEncoder
{
public:
    // Note: `output_buffer` is used in cases where we want the output buffer to be allocated from outside this model.
    // For example, using the next model's input as the output of this model.
    static Expected<std::unique_ptr<TextEncoder>> create(Hef text_encoder_hef, std::shared_ptr<hailort::VDevice> vdevice);

    hailo_status encode(const std::string &positive_prompt, const std::string &negative_prompt);

    Expected<std::vector<int>> tokenize(const std::string &prompt);

    TextEncoder(std::unique_ptr<InferenceManager> &&inference_manager_text_encoder,
        std::unique_ptr<HailoTokenizer> &&tokenizer, std::unique_ptr<TokenEmbedder<uint16_t>> &&token_embedder,
        uint32_t model_max_tokens);

    Expected<size_t> get_output_frame_size();
    void set_output_pos_buffer(MemoryView output_buffer);
    void set_output_neg_buffer(MemoryView output_buffer);
    hailo_status allocate_inputs_buffers();

    TextEncoder(TextEncoder &&) = delete;
    TextEncoder(const TextEncoder &) = delete;
    TextEncoder &operator=(TextEncoder &&) = delete;
    TextEncoder &operator=(const TextEncoder &) = delete;
    virtual ~TextEncoder() = default;

private:
    Expected<AsyncInferJob> encode_prompt(const std::string &prompt, MemoryView input_buffer, MemoryView output_buffer);

    std::unique_ptr<InferenceManager> m_inference_manager;
    std::unique_ptr<HailoTokenizer> m_tokenizer;
    std::unique_ptr<TokenEmbedder<uint16_t>> m_token_embedder;
    uint32_t m_model_max_tokens;

    BufferPtr m_input_positive_buffer;
    BufferPtr m_input_negative_buffer;

    MemoryView m_output_positive_buffer;
    MemoryView m_output_negative_buffer;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_ENCODER_HPP_ */
