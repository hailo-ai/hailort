/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file text_encoder.cpp
 * @brief Text2Image encoder implementation
 **/

#include "text_encoder.hpp"
#include "hailo/hailort.h"

namespace hailort
{
namespace genai
{

static const std::string INPUT_EMB_BINARY = "embeddings.bin";
static const std::string TOKENIZER = "tokenizer.json";

// TODO: HRT-16738 - Get these tokens from hef
static const int EOF_TOKEN_ID = 49407; // <|endoftext|>
static const int BOS_TOKEN_ID = 49406; // <|startoftext|>

Expected<std::unique_ptr<TextEncoder>> TextEncoder::create(Hef text_encoder_hef, std::shared_ptr<hailort::VDevice> vdevice)
{
    TRY(auto inference_manager_text_encoder, InferenceManager::create(vdevice, text_encoder_hef));
    inference_manager_text_encoder->get_model()->set_batch_size(INPUT_PROMPTS_BATCH_SIZE);

    TRY(auto external_resources, inference_manager_text_encoder->get_hef().get_external_resources());
    CHECK(contains(external_resources, INPUT_EMB_BINARY), HAILO_INVALID_ARGUMENT,
        "Failed to create 'TextEncoder'. 'external_resources' should contain embeddings in binary format in hef");
    CHECK(contains(external_resources, TOKENIZER), HAILO_INVALID_ARGUMENT,
        "Failed to create 'TextEncoder'. 'external_resources' should contain tokenizer in hef");

    // Init tokenizer
    // HRT-16824 - HailoTokenizer should get memview in the c'tor and convert to string if neccesary inside
    std::string tokenizer_blob(external_resources.at(TOKENIZER).size(), '\0');
    std::memcpy(const_cast<char*>(tokenizer_blob.data()), external_resources[TOKENIZER].data(), tokenizer_blob.size());
    TRY(auto tokenizer, HailoTokenizer::create(tokenizer_blob));

    // Init embeddings
    TRY(auto input, inference_manager_text_encoder->get_model()->input());
    auto embeddings_dtype = input.format().type;
    CHECK(embeddings_dtype == HAILO_FORMAT_TYPE_UINT16, HAILO_INVALID_OPERATION,
        "Expected dtype of embeddings to be uint16, instead got {}", HailoRTCommon::get_format_type_str(embeddings_dtype));

    size_t cols = input.shape().features;
    size_t rows = external_resources.at(INPUT_EMB_BINARY).size() / cols / sizeof(uint16_t);
    TRY(auto token_embedder, TokenEmbedder<uint16_t>::create(external_resources.at(INPUT_EMB_BINARY), rows, cols));

    TRY(auto output, inference_manager_text_encoder->get_model()->output());
    output.set_format_type(HAILO_FORMAT_TYPE_FLOAT32);
    CHECK_SUCCESS(inference_manager_text_encoder->configure());

    auto model_max_tokens = input.shape().width;
    auto encoder_ptr = make_unique_nothrow<TextEncoder>(std::move(inference_manager_text_encoder),
        std::move(tokenizer), std::move(token_embedder), model_max_tokens);
    CHECK_NOT_NULL_AS_EXPECTED(encoder_ptr, HAILO_OUT_OF_HOST_MEMORY); // Consider returning different status

    return encoder_ptr;
}

TextEncoder::TextEncoder(std::unique_ptr<InferenceManager> &&inference_manager_text_encoder,
    std::unique_ptr<HailoTokenizer> &&tokenizer, std::unique_ptr<TokenEmbedder<uint16_t>> &&token_embedder,
    uint32_t model_max_tokens) :
        m_inference_manager(std::move(inference_manager_text_encoder)),
        m_tokenizer(std::move(tokenizer)),
        m_token_embedder(std::move(token_embedder)),
        m_model_max_tokens(model_max_tokens)
{}

Expected<std::vector<int>> TextEncoder::tokenize(const std::string &prompt)
{
    return m_tokenizer->text_to_tokens(prompt);
}

hailo_status TextEncoder::encode(const std::string &positive_prompt, const std::string &negative_prompt)
{
    if (negative_prompt.empty()) {
        // If negative prompt is empty, we infer only once on the positive prompt
        CHECK_SUCCESS(m_inference_manager->get_configured_model().set_scheduler_threshold(DEFAULT_SCHEDULER_THRESHOLD));
        CHECK_SUCCESS(m_inference_manager->get_configured_model().set_scheduler_timeout(DEFAULT_SCHEDULER_TIMEOUT));
    } else {
        // If negative prompt is not empty, we infer on both the positive and negative prompts
        CHECK_SUCCESS(m_inference_manager->get_configured_model().set_scheduler_threshold(INPUT_PROMPTS_BATCH_SIZE));
        CHECK_SUCCESS(m_inference_manager->get_configured_model().set_scheduler_timeout(SCHEDULER_TIMEOUT));
    }

    TRY(auto async_job, encode_prompt(positive_prompt, MemoryView(m_input_positive_buffer), m_output_positive_buffer));
    async_job.detach();

    if (!negative_prompt.empty()) {
        TRY(async_job, encode_prompt(negative_prompt, MemoryView(m_input_negative_buffer), m_output_negative_buffer));
    }

    CHECK_SUCCESS(async_job.wait(JOB_WAIT_TIMEOUT));
    return HAILO_SUCCESS;
}

Expected<AsyncInferJob> TextEncoder::encode_prompt(const std::string &prompt, MemoryView input_buffer, MemoryView output_buffer)
{
    TRY(auto tokens, m_tokenizer->text_to_tokens(prompt));
    LOGGER__INFO("Parsed {} tokens of prompt", tokens.size());

    // The check is low then (m_model_max_tokens - 1) because there is also the bos token
    CHECK((tokens.size() < static_cast<size_t>(m_model_max_tokens - 1)), HAILO_INVALID_ARGUMENT,
        "The number of tokens must be lower then {}, got {}",  static_cast<size_t>(m_model_max_tokens - 1), tokens.size());
    auto input_tokens_size = std::min(static_cast<size_t>(m_model_max_tokens - 1), tokens.size());

    // TODO: HRT-16824 - Check Tokenizer lib to set the special token bos / Get from hef
    std::vector<int> padded_tokens(m_model_max_tokens, EOF_TOKEN_ID);
    padded_tokens[0] = BOS_TOKEN_ID; 
    for (size_t i = 0; i < input_tokens_size; i++) {
        padded_tokens[i+1] = tokens[i];
    }
    m_token_embedder->tokens_to_embeddings(input_buffer, padded_tokens);

    return m_inference_manager->generate_async(input_buffer, output_buffer);
}

void TextEncoder::set_output_pos_buffer(MemoryView output_buffer)
{
    m_output_positive_buffer = output_buffer;
}

void TextEncoder::set_output_neg_buffer(MemoryView output_buffer)
{
    m_output_negative_buffer = output_buffer;
}

hailo_status TextEncoder::allocate_inputs_buffers()
{
    TRY(auto input, m_inference_manager->get_model()->input());
    TRY(m_input_positive_buffer, Buffer::create_shared(input.get_frame_size(), BufferStorageParams::create_dma()));
    TRY(m_input_negative_buffer, Buffer::create_shared(input.get_frame_size(), BufferStorageParams::create_dma()));

    return HAILO_SUCCESS;
}

Expected<size_t> TextEncoder::get_output_frame_size()
{
    TRY(auto output, m_inference_manager->get_model()->output());
    return output.get_frame_size();
}

} /* namespace genai */
} /* namespace hailort */