/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file speech2text_server.cpp
 * @brief Speech2Text server implementation
 **/

#include "speech2text_server.hpp"
#include "common/logger_macros.hpp"
#include "common/utils.hpp"
#include "common/genai/constants.hpp"
#include "hailo/genai/speech2text/speech2text.hpp"
#include "hailo/hailort.h"
#include "hailo/hailort_defaults.hpp"
#include "speech2text/post_process.hpp"
#include "speech2text/pre_process.hpp"
#include "utils.hpp"

namespace hailort
{
namespace genai
{

// TODO: HRT-18577 - Adjust names
static const std::string SPEECH2TEXT_ENCODER_MODEL_NAME = "base-whisper-encoder-10s";
static const std::string SPEECH2TEXT_DECODER_MODEL_NAME = "base-whisper-decoder-10s-out-seq-64";

// TODO: HRT-18577 - Get params from hef
constexpr size_t SAMPLE_RATE = 16000;
constexpr float32_t TIME_PRECISION = 0.02f;
constexpr int MAX_INITIAL_TIMESTAMP = static_cast<int>(1 / TIME_PRECISION);
constexpr int CHUNK_SIZE_SEC = 10;
constexpr int HOP_LENGTH = 160;
constexpr int START_OF_TRANSCRIPT_TOKEN_ID = 50258;     // "<|startoftranscript|>"
constexpr int TASK_TRANSCRIBE_TOKEN_ID = 50359;         // "<|transcribe|>"
constexpr int TASK_TRANSLATE_TOKEN_ID = 50358;          // "<|translate|>"
constexpr int EOT_TOKEN_ID = 50257;                     // "<|endoftext|>"
constexpr int PADDING_VALUE = 0;
constexpr int N_AUDIO_CTX = 500;

// TODO: HRT-18577 - Get from hef
static const std::unordered_set<std::string> SUPPORTED_LANGUAGE_CODES = {
    "af", "am", "ar", "as", "az", "ba", "be", "bg", "bn", "bo", "br", "bs", "ca", "cs", "cy", "da", "de", "el", "en", "es", "et", "eu", "fa", "fi", "fo", "fr", "gl", "gu", "ha", "haw", "he", "hi", "hr", "ht", "hu", "hy", "id", "is", "it", "ja", "jw", "ka", "kk", "km", "kn", "ko", "la", "lb", "ln", "lo", "lt", "lv", "mg", "mi", "mk", "ml", "mn", "mr", "ms", "mt", "my", "ne", "nl", "nn", "no", "oc", "pa", "pl", "ps", "pt", "ro", "ru", "sa", "sd", "si", "sk", "sl", "sn", "so", "sq", "sr", "su", "sv", "sw", "ta", "te", "tg", "th", "tk", "tl", "tr", "tt", "uk", "ur", "uz", "vi", "yi", "yo", "zh"
};

// TODO: HRT-18789 - Get from hef
// Output layers to concatenate
static const std::vector<std::string> EMBEDDINGS_OUTPUT_LAYERS_NAMES =
    {"base-whisper-decoder-10s-out-seq-64/conv49", "base-whisper-decoder-10s-out-seq-64/conv50",
    "base-whisper-decoder-10s-out-seq-64/conv51", "base-whisper-decoder-10s-out-seq-64/conv52"};

// TODO: HRT-18789 - Get from hef
// Common layer of decoder input - encoder output
static const std::string COMMON_LAYER_INPUT_DECODER_NAME = "base-whisper-decoder-10s-out-seq-64/input_layer1";

// TODO: HRT-18789 - Get from hef
static const std::string TOKENS_LAYER_INPUT_DECODER_NAME = "base-whisper-decoder-10s-out-seq-64/input_layer2";

Speech2TextServer::Speech2TextServer(std::shared_ptr<Session> session, std::shared_ptr<VDeviceManager> vdevice_manager) :
    m_session(session),
    m_vdevice_manager(vdevice_manager),
    m_generator_params(Speech2TextTask::TRANSCRIBE, "")
{}

Speech2TextServer::~Speech2TextServer()
{
    // Remove all references to local VDevice before marking it as removed
    m_inference_manager_decoder.reset();
    m_inference_manager_encoder.reset();

    m_vdevice_manager->remove_vdevice(DEFAULT_SPEECH2TEXT_CONNECTION_PORT); // Use it as a unique client id
}

Expected<std::unique_ptr<Speech2TextServer>> Speech2TextServer::create_unique(std::shared_ptr<Session> session, std::shared_ptr<VDeviceManager> vdevice_manager)
{
    auto ptr = make_unique_nothrow<Speech2TextServer>(session, vdevice_manager);
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::unique_ptr<Speech2TextServer>(std::move(ptr));
}

Expected<Buffer> Speech2TextServer::handle_create_speech2text_request(const MemoryView &request)
{
    LOGGER__GENAI_STATS_START("[create-speech2text] create vdevice");
    TRY_AS_HRPC_STATUS(auto group_id, Speech2TextCreateSerializer::deserialize_request(request), Speech2TextCreateSerializer);

    auto params = HailoRTDefaults::get_vdevice_params();
    if (!group_id.empty()) {
        params.group_id = group_id.c_str();
    }
    TRY_AS_HRPC_STATUS(auto vdevice, m_vdevice_manager->create_shared_vdevice(params, DEFAULT_SPEECH2TEXT_CONNECTION_PORT), Speech2TextCreateSerializer);
    LOGGER__GENAI_STATS_END("[create-speech2text] create vdevice");

    LOGGER__GENAI_STATS_START("[create-speech2text] transfer HEF");
    TRY_AS_HRPC_STATUS(auto hef_buffer, m_session.read(), Speech2TextCreateSerializer);
    LOGGER__GENAI_STATS_END("[create-speech2text] transfer HEF");

    LOGGER__GENAI_STATS_START("[create-speech2text] create HEF");
    TRY_AS_HRPC_STATUS(auto hef, Hef::create(hef_buffer), Speech2TextCreateSerializer);
    // TODO: HRT-18577 - Enable this
    // hef.set_memory_footprint_optimization(true); // zero-copy configuration if possible
    LOGGER__GENAI_STATS_END("[create-speech2text] create HEF");

    LOGGER__GENAI_STATS_START("[create-speech2text] create speech2text encoder model");
    TRY_AS_HRPC_STATUS(m_inference_manager_encoder, InferenceManager::create(vdevice, hef, SPEECH2TEXT_ENCODER_MODEL_NAME), Speech2TextCreateSerializer);
    LOGGER__GENAI_STATS_END("[create-speech2text] create speech2text encoder model");

    LOGGER__GENAI_STATS_START("[create-speech2text] configure speech2text encoder model");

    TRY_AS_HRPC_STATUS(auto encoder_input, m_inference_manager_encoder->get_model()->input(), Speech2TextCreateSerializer);
    encoder_input.set_format_type(HAILO_FORMAT_TYPE_FLOAT32);
    encoder_input.set_format_order(HAILO_FORMAT_ORDER_NCHW); // TODO: HRT-18868 - Remove
    TRY_AS_HRPC_STATUS(auto encoder_output, m_inference_manager_encoder->get_model()->output(), Speech2TextCreateSerializer);
    auto encoder_output_shape = encoder_output.shape();
    encoder_output.set_format_order(HAILO_FORMAT_ORDER_NHCW);

    TRY_AS_HRPC_STATUS(auto encoder_buffers, m_inference_manager_encoder->allocate_buffers(), Speech2TextCreateSerializer);
    assert(encoder_buffers.first.size() == 1);
    assert(encoder_buffers.second.size() == 1);
    m_encoder_input_buffer = encoder_buffers.first.begin()->second;
    m_encoder_output_buffer = encoder_buffers.second.begin()->second;
    CHECK_SUCCESS_AS_HRPC_STATUS(m_inference_manager_encoder->configure(), Speech2TextCreateSerializer);
    LOGGER__GENAI_STATS_END("[create-speech2text] configure speech2text encoder model");

    LOGGER__GENAI_STATS_START("[create-speech2text] create speech2text decoder model");
    TRY_AS_HRPC_STATUS(m_inference_manager_decoder, InferenceManager::create(vdevice, hef, SPEECH2TEXT_DECODER_MODEL_NAME), Speech2TextCreateSerializer);
    LOGGER__GENAI_STATS_END("[create-speech2text] create speech2text decoder model");

    LOGGER__GENAI_STATS_START("[create-speech2text] configure speech2text decoder model");

    // Check that the decoder input shape is the same as the encoder output shape
    TRY_AS_HRPC_STATUS(auto decoder_input, m_inference_manager_decoder->get_model()->input(COMMON_LAYER_INPUT_DECODER_NAME), Speech2TextCreateSerializer);
    auto decoder_input_shape = decoder_input.shape();
    decoder_input.set_format_order(HAILO_FORMAT_ORDER_NHCW);
    CHECK_AS_HRPC_STATUS(((decoder_input_shape.height == encoder_output_shape.height) && (decoder_input_shape.width == encoder_output_shape.width) &&
        (decoder_input_shape.features == encoder_output_shape.features)), HAILO_INVALID_ARGUMENT, Speech2TextCreateSerializer);

    // Set the format type of the decoder outputs
    auto decoder_outputs_names = m_inference_manager_decoder->get_model()->get_output_names();
    m_embeddings_outputs_row_size = 0;
    for (auto &output_name : decoder_outputs_names) {
        TRY_AS_HRPC_STATUS(auto output, m_inference_manager_decoder->get_model()->output(output_name), Speech2TextCreateSerializer);
        if (contains(EMBEDDINGS_OUTPUT_LAYERS_NAMES, output_name)) {
            const auto &quant_infos = output.get_quant_infos();
            CHECK_AS_HRPC_STATUS(quant_infos.size() == 1, HAILO_INVALID_ARGUMENT, Speech2TextCreateSerializer);
            m_embeddings_outputs_info.emplace_back(output_name, output.shape().features, quant_infos[0]);

            if (m_embeddings_outputs_row_size == 0) {
                m_embeddings_outputs_row_size = output.shape().width;
            } else {
                // All embeddings outputs should have the same width
                CHECK_AS_HRPC_STATUS(m_embeddings_outputs_row_size == output.shape().width, HAILO_INVALID_ARGUMENT, Speech2TextCreateSerializer);
            }
        }
    }
    m_vocab_size = std::accumulate(m_embeddings_outputs_info.begin(), m_embeddings_outputs_info.end(), 0, [](size_t acc, const auto &output_info) {
        return acc + std::get<1>(output_info);
    });

    std::unordered_set<std::string> layers_not_to_allocate = {COMMON_LAYER_INPUT_DECODER_NAME};
    TRY_AS_HRPC_STATUS(m_decoder_buffers, m_inference_manager_decoder->allocate_buffers(layers_not_to_allocate), Speech2TextCreateSerializer);
    m_decoder_buffers.first[COMMON_LAYER_INPUT_DECODER_NAME] = m_encoder_output_buffer; // Using the same buffer for encoder output and decoder input
    m_decoder_inputs = buffers_to_memviews(m_decoder_buffers.first);
    m_decoder_outputs = buffers_to_memviews(m_decoder_buffers.second);

    // Buffer used to store the next token scores after extracting and dequantizing the decoder outputs' embeddings to float32
    TRY_AS_HRPC_STATUS(m_next_token_scores_buffer, Buffer::create(m_vocab_size * sizeof(float32_t), BufferStorageParams::create_dma()), Speech2TextCreateSerializer);

    CHECK_SUCCESS_AS_HRPC_STATUS(m_inference_manager_decoder->configure(), Speech2TextCreateSerializer);
    LOGGER__GENAI_STATS_END("[create-speech2text] configure speech2text decoder model");

    LOGGER__GENAI_STATS_START("[create-speech2text] parse GenAI resources");
    // Init tokenizer
    TRY_AS_HRPC_STATUS(auto tokenizer_view, hef.get_external_resources(TOKENIZER), Speech2TextCreateSerializer);
    std::string tokenizer_blob(tokenizer_view.size(), '\0');
    std::memcpy(const_cast<char*>(tokenizer_blob.data()), tokenizer_view.data(), tokenizer_blob.size());
    TRY_AS_HRPC_STATUS(m_tokenizer, HailoTokenizer::create(tokenizer_blob), Speech2TextCreateSerializer);

    // Init embeddings
    TRY_AS_HRPC_STATUS(auto input, m_inference_manager_decoder->get_model()->input(TOKENS_LAYER_INPUT_DECODER_NAME), Speech2TextCreateSerializer);
    m_decoder_seq_length = input.shape().width;

    auto embeddings_dtype = input.format().type;
    CHECK_AS_HRPC_STATUS(embeddings_dtype == HAILO_FORMAT_TYPE_UINT8, HAILO_INVALID_OPERATION, Speech2TextCreateSerializer);
    TRY_AS_HRPC_STATUS(auto embeddings_view, hef.get_external_resources(INPUT_EMB_BINARY), Speech2TextCreateSerializer);
    size_t cols = input.shape().features;
    size_t rows = embeddings_view.size() / cols / sizeof(uint8_t);
    TRY_AS_HRPC_STATUS(m_token_embedder, TokenEmbedder<uint8_t>::create(embeddings_view, rows, cols), Speech2TextCreateSerializer);

    // TODO: HRT-18577 - Get the tokens from the hef
    m_task_to_token_id_map = {
        {Speech2TextTask::TRANSCRIBE, TASK_TRANSCRIBE_TOKEN_ID},
        {Speech2TextTask::TRANSLATE,  TASK_TRANSLATE_TOKEN_ID}
    };
    LOGGER__GENAI_STATS_END("[create-speech2text] parse GenAI resources");

    // TODO: HRT-18577 - Get from hef
    std::vector<int> special_tokens = {1, 2, 7, 8, 9, 10, 14, 25, 26, 27, 28, 29, 31, 58, 59, 60, 61, 62, 63, 90, 91, 92, 93, 359, 503, 522, 542, 873, 893, 902, 918, 922, 931, 1350, 1853, 1982, 2460, 2627, 3246, 3253, 3268, 3536, 3846, 3961, 4183, 4667, 6585, 6647, 7273, 9061, 9383, 10428, 10929, 11938, 12033, 12331, 12562, 13793, 14157, 14635, 15265, 15618, 16553, 16604, 18362, 18956, 20075, 21675, 22520, 26130, 26161, 26435, 28279, 29464, 31650, 32302, 32470, 36865, 42863, 47425, 49870, 50254, 50258, 50358, 50359, 50360, 50361, 50362};
    TRY_AS_HRPC_STATUS(auto blank_tokens, m_tokenizer->text_to_tokens(" "), Speech2TextCreateSerializer);
    m_post_process = Speech2TextPostProcess(CHUNK_SIZE_SEC, TIMESTAMP_BEGIN_TOKEN_ID, EOT_TOKEN_ID, std::move(blank_tokens), std::move(special_tokens), TIME_PRECISION, MAX_INITIAL_TIMESTAMP);

    return Speech2TextCreateSerializer::serialize_reply(HAILO_SUCCESS);
}

Expected<Buffer> Speech2TextServer::handle_tokenize_request(const MemoryView &request)
{
    TRY_AS_HRPC_STATUS(auto prompt, Speech2TextTokenizeSerializer::deserialize_request(request), Speech2TextTokenizeSerializer);
    TRY_AS_HRPC_STATUS(auto tokens, m_tokenizer->text_to_tokens(prompt), Speech2TextTokenizeSerializer);
    TRY_AS_HRPC_STATUS(auto reply, Speech2TextTokenizeSerializer::serialize_reply(HAILO_SUCCESS, tokens), Speech2TextTokenizeSerializer);
    return reply;
}

Expected<int> Speech2TextServer::language_to_token_id(const std::string &language)
{
    CHECK(contains(SUPPORTED_LANGUAGE_CODES, language), HAILO_INVALID_ARGUMENT, "Language code '{}' is not supported", language);

    std::string wrapped_language = "<|" + language + "|>";
    TRY(auto language_tokens, m_tokenizer->text_to_tokens(wrapped_language));
    CHECK(language_tokens.size() == 1, HAILO_INVALID_ARGUMENT, "Language must be 1 token, got multiple tokens");
    int token_id = language_tokens[0];

    return token_id;
}

Expected<Buffer> Speech2TextServer::handle_generate_request(const MemoryView &request)
{
    TRY_AS_HRPC_STATUS(m_generator_params, Speech2TextGenerateSerializer::deserialize_request(request), Speech2TextGenerateSerializer);

    LOGGER__GENAI_STATS_START("[generate-speech2text] audio buffer transfer");
    TRY_AS_HRPC_STATUS(auto audio_buffer, m_session.read(), Speech2TextGenerateSerializer);
    LOGGER__GENAI_STATS_END("[generate-speech2text] audio buffer transfer");
    CHECK_AS_HRPC_STATUS(audio_buffer->size() > 0, HAILO_INVALID_ARGUMENT, Speech2TextGenerateSerializer);

    std::string language(m_generator_params.language());
    TRY_AS_HRPC_STATUS(auto language_token_id, language_to_token_id(language), Speech2TextGenerateSerializer);
    CHECK_AS_HRPC_STATUS(contains(m_task_to_token_id_map, m_generator_params.task()), HAILO_INVALID_ARGUMENT, Speech2TextGenerateSerializer);
    auto task_token_id = m_task_to_token_id_map.at(m_generator_params.task());
    std::vector<int> context_tokens = {START_OF_TRANSCRIPT_TOKEN_ID, language_token_id, task_token_id};

    TRY(auto segments_infos, process_input_audio(MemoryView(audio_buffer), context_tokens));

    return Speech2TextGenerateSerializer::serialize_reply(HAILO_SUCCESS, segments_infos);
}

Expected<std::vector<Speech2Text::SegmentInfo>> Speech2TextServer::process_input_audio(const MemoryView &audio, const std::vector<int> &context_tokens)
{
    std::vector<Speech2Text::SegmentInfo> segments_infos;
    LOGGER__GENAI_STATS_START("[generate-speech2text] pre-process - convert to mel spectrogram");
    // TODO: HRT-18595 - Check if possible to compute in chunks and re-use input buffer
    auto mel_spectrogram = Speech2TextPreProcess::compute_log_mel(MemoryView(audio), CHUNK_SIZE_SEC, SAMPLE_RATE, HOP_LENGTH);
    LOGGER__GENAI_STATS_END("[generate-speech2text] pre-process - convert to mel spectrogram");

    int seek = 0;
    static const int n_frames = (CHUNK_SIZE_SEC * SAMPLE_RATE) / HOP_LENGTH;
    int content_frames = static_cast<int>(mel_spectrogram.rows()) - n_frames;
    int input_stride = n_frames / N_AUDIO_CTX;

    CHECK(n_frames * mel_spectrogram.cols() * sizeof(float32_t) == m_encoder_input_buffer->size(), HAILO_INTERNAL_FAILURE, "Encoder input buffer size mismatch");
    Eigen::Map<Eigen::MatrixXf> encoder_input(reinterpret_cast<float32_t*>(m_encoder_input_buffer->data()), n_frames, mel_spectrogram.cols());

    while (seek < content_frames) {
        uint32_t segment_size = std::min(n_frames, content_frames - seek);
        float32_t segment_duration_sec = static_cast<float32_t>(segment_size * HOP_LENGTH) / static_cast<float32_t>(SAMPLE_RATE);
        float32_t time_offset = static_cast<float32_t>(seek * HOP_LENGTH) / static_cast<float32_t>(SAMPLE_RATE);
        Eigen::Block<Eigen::MatrixXf> curr_chunk_input = mel_spectrogram.middleRows(seek, segment_size); // Eigen::Block is a memory view

        LOGGER__GENAI_STATS_START("[generate-speech2text] pre-process - pad or trim");
        Speech2TextPreProcess::pad_or_trim(curr_chunk_input, encoder_input);
        LOGGER__GENAI_STATS_END("[generate-speech2text] pre-process - pad or trim");

        LOGGER__GENAI_STATS_START("[generate-speech2text] hw-inference encoder model");
        CHECK_SUCCESS(m_inference_manager_encoder->generate(MemoryView(m_encoder_input_buffer), MemoryView(m_encoder_output_buffer)));
        LOGGER__GENAI_STATS_END("[generate-speech2text] hw-inference encoder model");

        TRY(auto pair, decoder_loop(context_tokens));
        auto &[tokens, sum_logprobs] = pair;

        LOGGER__GENAI_STATS_START("[generate-speech2text] update segments and calc seek");
        TRY(seek, update_segments_and_calc_seek(tokens, sum_logprobs, seek, segment_duration_sec, segment_size, time_offset, input_stride, segments_infos));
        LOGGER__GENAI_STATS_END("[generate-speech2text] update segments and calc seek");
    }

    return segments_infos;
}

// TODO: HRT-18933 - Refactor this function
Expected<int> Speech2TextServer::update_segments_and_calc_seek(const std::vector<int> &tokens, float32_t sum_logprobs, int seek, float32_t segment_duration_sec,
    uint32_t segment_size, float32_t time_offset, int input_stride, std::vector<Speech2Text::SegmentInfo> &segments_infos)
{
    // TODO: HRT-18934
    (void)sum_logprobs;

    int last_timestamp = INVALID_TOKEN_VALUE;
    std::vector<size_t> segments_end_timestamp_indexes;
    for (size_t i = 0; i < tokens.size(); i++) {
        if (is_timestamp_token(tokens[i])) {
            last_timestamp = tokens[i];
        }

        // check consecutive pair (i > 0)
        if (i > 0 && is_timestamp_token(tokens[i]) && is_timestamp_token(tokens[i-1])) {
            segments_end_timestamp_indexes.push_back(i);
        }
    }

    // `single_end` flag is used to determine how to calculate the next seek.
    // If true → segment ends cleanly with one timestamp → jump to next chunk.
    // If false:
    //      1. If we have multiple segments (consecutive timestamps) → use last timestamp to decide where to continue.
    //      2. No timestamps at all → fallback → jump by full chunk size.
    bool single_end = (tokens.size() >= 2 && !is_timestamp_token(tokens[tokens.size()-2]) && is_timestamp_token(tokens[tokens.size()-1]));

    // Multiple segments
    if (!segments_end_timestamp_indexes.empty()) {
        if (single_end) {
            segments_end_timestamp_indexes.push_back(tokens.size() - 1); // cut at the very end
        }

        size_t curr_segment_start_ts_index = 0;
        for (size_t curr_segment_end_index : segments_end_timestamp_indexes) {
            Speech2Text::SegmentInfo segment_info;
            std::vector<int> text_tokens(tokens.begin() + curr_segment_start_ts_index + 1, tokens.begin() + curr_segment_end_index - 1); // the plus 1 is to exclude the timestamp tokens

            TRY(segment_info.text, m_tokenizer->tokens_to_text(text_tokens));
            segment_info.start_sec = time_offset + static_cast<float32_t>(tokens[curr_segment_start_ts_index] - TIMESTAMP_BEGIN_TOKEN_ID) * TIME_PRECISION;
            segment_info.end_sec = time_offset + static_cast<float32_t>(tokens[curr_segment_end_index] - TIMESTAMP_BEGIN_TOKEN_ID) * TIME_PRECISION;
            segments_infos.push_back(segment_info);
            curr_segment_start_ts_index = curr_segment_end_index;
        }

        if (single_end) {
            seek += segment_size;
        } else {
            int last_timestamp_pos = tokens[segments_end_timestamp_indexes.back()] - TIMESTAMP_BEGIN_TOKEN_ID;
            seek += last_timestamp_pos * input_stride;
        }
    } else {
        // No consecutive timestamps - single segment
        Speech2Text::SegmentInfo segment_info;
        float32_t duration_sec = segment_duration_sec;

        if (last_timestamp != INVALID_TOKEN_VALUE && last_timestamp != TIMESTAMP_BEGIN_TOKEN_ID) {
            duration_sec = static_cast<float32_t>(last_timestamp - TIMESTAMP_BEGIN_TOKEN_ID) * TIME_PRECISION;
        }

        auto tokens_end_iter = (tokens.size() > 1) && is_timestamp_token(tokens[tokens.size() - 1]) ?
            tokens.end() - 1 : // if the last token is a timestamp, exclude it from the text
            tokens.end();
        std::vector<int> text_tokens(tokens.begin() + 1, tokens_end_iter); // the plus 1 is to exclude the timestamp tokens from the text
        TRY(auto text, m_tokenizer->tokens_to_text(text_tokens));
        segment_info.text = std::move(text);
        segment_info.start_sec = time_offset;
        segment_info.end_sec = time_offset + duration_sec;
        segments_infos.push_back(segment_info);

        seek += segment_size;
    }

    return seek;
}

Eigen::Map<Eigen::VectorXf> Speech2TextServer::get_next_token_scores(int next_token_idx)
{
    float32_t *dst_ptr = reinterpret_cast<float32_t*>(m_next_token_scores_buffer.data());
    Eigen::Map<Eigen::VectorXf> dst_row(dst_ptr, m_vocab_size);

    size_t dst_col_offset = 0;
    const size_t row_idx = static_cast<size_t>(next_token_idx - 1);
    for (auto &output_info : m_embeddings_outputs_info) {
        auto &output_name = std::get<0>(output_info);
        const size_t current_cols = std::get<1>(output_info);
        auto &quant_info = std::get<2>(output_info);

        uint8_t *src_ptr = reinterpret_cast<uint8_t*>(m_decoder_outputs[output_name].data());

        auto scale = quant_info.qp_scale;
        auto zero_point = quant_info.qp_zp;

        const size_t row_offset = row_idx * current_cols;
        const uint8_t *src_row_ptr = src_ptr + row_offset;

        // Dequantize this row segment into the correct region of dst_row
        Eigen::Map<Eigen::Array<uint8_t, Eigen::Dynamic, 1>> quant_row(const_cast<uint8_t*>(src_row_ptr), current_cols);
        dst_row.segment(dst_col_offset, current_cols) = (quant_row.cast<float32_t>().array() - zero_point) * scale;

        dst_col_offset += current_cols;
    }

    return dst_row;
}

Expected<std::pair<std::vector<int>, float32_t>> Speech2TextServer::decoder_loop(const std::vector<int> &context_tokens)
{
    float32_t sum_logprobs = 0.0f;
    int next_token_idx = CONTEXT_TOKENS_COUNT;

    // NOTE: Openai is looping over `m_decoder_seq_length / 2` instead of just `m_decoder_seq_length` because they use previous context tokens.
    // Since we don't use previous context tokens, we probably can loop over the full sequence length.
    // TODO: HRT-18595 - Remove it, and check accuracy and performance.
    auto sample_len = m_decoder_seq_length / 2;

    // TODO HRT-18570 - Use `tokens` directly, need to adjust the post-process
    std::vector<int> generated_tokens = context_tokens;
    std::vector<int> padded_input_tokens(m_decoder_seq_length, PADDING_VALUE);
    std::copy(generated_tokens.begin(), generated_tokens.end(), padded_input_tokens.begin());
    for (int i = 0; i < sample_len; i++) {

        LOGGER__GENAI_STATS_START("[generate-speech2text] decoder - tokens to embeddings");
        m_token_embedder->tokens_to_embeddings(MemoryView(m_decoder_inputs[TOKENS_LAYER_INPUT_DECODER_NAME]), padded_input_tokens);
        LOGGER__GENAI_STATS_END("[generate-speech2text] decoder - tokens to embeddings");

        LOGGER__GENAI_STATS_START("[generate-speech2text] hw-inference decoder model");
        CHECK_SUCCESS(m_inference_manager_decoder->generate(m_decoder_inputs, m_decoder_outputs));
        LOGGER__GENAI_STATS_END("[generate-speech2text] hw-inference decoder model");

        LOGGER__GENAI_STATS_START("[generate-speech2text] decoder - post-process");
        Eigen::Map<Eigen::VectorXf> next_token_scores = get_next_token_scores(next_token_idx);

        auto [next_token, logprob] = m_post_process.get_next_token(next_token_scores, generated_tokens);
        padded_input_tokens[next_token_idx] = next_token;
        generated_tokens.push_back(next_token);
        sum_logprobs += logprob;
        LOGGER__GENAI_STATS_END("[generate-speech2text] decoder - post-process");

        if (next_token == EOT_TOKEN_ID) {
            // Stop if we reached the end of the segment
            break;
        }
        next_token_idx++;
    }

    // Make sure each sequence has at least one EOT token at the end
    if (generated_tokens.back() != EOT_TOKEN_ID) {
        generated_tokens.push_back(EOT_TOKEN_ID);
    }

    // Slice from the actual start of the sample (without context tokens) to the first EOT token
    auto it = std::find(generated_tokens.begin(), generated_tokens.end(), EOT_TOKEN_ID);
    assert(it != generated_tokens.end());
    size_t eot_index = std::distance(generated_tokens.begin(), it);
    std::vector<int> result_tokens(generated_tokens.begin() + CONTEXT_TOKENS_COUNT, generated_tokens.begin() + eot_index);

    return std::make_pair(std::move(result_tokens), sum_logprobs);
}

Expected<std::unique_ptr<Speech2TextServerManager>> Speech2TextServerManager::create(std::shared_ptr<Session> session,
    std::shared_ptr<VDeviceManager> vdevice_manager)
{
    TRY(auto server, Speech2TextServer::create_unique(session, vdevice_manager));

    auto ptr = make_unique_nothrow<Speech2TextServerManager>(session, std::move(server));
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY); // Consider returning different status

    return std::unique_ptr<Speech2TextServerManager>(std::move(ptr));
}

Speech2TextServerManager::Speech2TextServerManager(std::shared_ptr<Session> session, std::unique_ptr<Speech2TextServer> &&server) :
    GenAIServerManager(session), m_server(std::move(server))
{
    m_dispatcher[HailoGenAIActionID::SPEECH2TEXT__CREATE] =
        [&](const MemoryView &request) { return m_server->handle_create_speech2text_request(request); };
    m_dispatcher[HailoGenAIActionID::SPEECH2TEXT__GENERATE] =
        [&](const MemoryView &request) { return m_server->handle_generate_request(request); };
    m_dispatcher[HailoGenAIActionID::SPEECH2TEXT__TOKENIZE] =
        [&](const MemoryView &request) { return m_server->handle_tokenize_request(request); };
    m_dispatcher[HailoGenAIActionID::SPEECH2TEXT__RELEASE] =
        [&](const MemoryView &request) { (void)request; m_server.reset(); return Speech2TextReleaseSerializer::serialize_reply(HAILO_SUCCESS); };
}

} /* namespace genai */
} /* namespace hailort */
