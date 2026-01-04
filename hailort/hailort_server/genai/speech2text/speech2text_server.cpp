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

static const std::string ENCODER_MODEL_NAME_SUFFIX = "encoder";
static const std::string DECODER_MODEL_NAME_SUFFIX = "decoder";
static const std::string DECODER_INPUT_NAME_SUFFIX__COMMON_ENCODER_DECODER_LAYER = "input_layer1";
static const std::string DECODER_INPUT_NAME_SUFFIX__TOKENS_LAYER = "input_layer2";

constexpr size_t SAMPLE_RATE = 16000;
constexpr float32_t TIME_PRECISION = 0.02f;
constexpr int MAX_INITIAL_TIMESTAMP = static_cast<int>(1 / TIME_PRECISION);
constexpr int HOP_LENGTH = 160;
constexpr int PADDING_VALUE = 0;
constexpr int LANGUAGE_TOKEN_ID_OFFSET = 1;

// Support backward compatibility, TODO: HRT-19298
static const std::string OLD_HEF_CONFIG_JSON_PARSING_KEY = "embeddings_output_layers_names";

Speech2TextServer::Speech2TextServer(std::shared_ptr<Session> session, std::shared_ptr<VDeviceManager> vdevice_manager, Speech2TextGeneratorParams &&generator_params) :
    m_session(session),
    m_vdevice_manager(vdevice_manager),
    m_generator_params(std::move(generator_params))
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
    // Init with generation default params, will be overwritten by the params from the HEF
    auto generator_params = Speech2TextGeneratorParams(DEFAULT_SPEECH2TEXT_TASK, DEFAULT_SPEECH2TEXT_LANGUAGE, DEFAULT_SPEECH2TEXT_REPETITION_PENALTY);

    auto ptr = make_unique_nothrow<Speech2TextServer>(session, vdevice_manager, std::move(generator_params));
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::unique_ptr<Speech2TextServer>(std::move(ptr));
}

hailo_status Speech2TextServer::set_default_config_params()
{
    // Support backward compatibility. TODO: HRT-19298
    constexpr int N_MELS = 80;
    constexpr int CHUNK_SIZE_SEC = 10;
    constexpr int START_OF_TRANSCRIPT_TOKEN_ID = 50258;     // "<|startoftranscript|>"
    constexpr int TASK_TRANSCRIBE_TOKEN_ID = 50359;         // "<|transcribe|>"
    constexpr int TASK_TRANSLATE_TOKEN_ID = 50358;          // "<|translate|>"
    constexpr int EOT_TOKEN_ID = 50257;                     // "<|endoftext|>"
    constexpr int TIMESTAMP_BEGIN_TOKEN_ID = 50364;         // "<|0.00|>"
    constexpr int FIRST_LANGUAGE_TOKEN_ID = 50259;          // "<|en|>"
    constexpr int LAST_LANGUAGE_TOKEN_ID = 50357;           // "<|su|>"
    constexpr int N_AUDIO_CTX = 500;

    static const std::string SPEECH2TEXT_ENCODER_MODEL_NAME = "base-whisper-encoder-10s";
    static const std::string SPEECH2TEXT_DECODER_MODEL_NAME = "base-whisper-decoder-10s-out-seq-64";
    static const std::string COMMON_LAYER_INPUT_DECODER_NAME = "base-whisper-decoder-10s-out-seq-64/input_layer1";
    static const std::string TOKENS_LAYER_INPUT_DECODER_NAME = "base-whisper-decoder-10s-out-seq-64/input_layer2";
    static const std::vector<std::string> EMBEDDINGS_OUTPUT_LAYERS_NAMES =
        {"base-whisper-decoder-10s-out-seq-64/conv49", "base-whisper-decoder-10s-out-seq-64/conv50",
        "base-whisper-decoder-10s-out-seq-64/conv51", "base-whisper-decoder-10s-out-seq-64/conv52"};

    m_decoder_start_token_id = START_OF_TRANSCRIPT_TOKEN_ID;
    m_task_transcribe_token_id = TASK_TRANSCRIBE_TOKEN_ID;
    m_task_translate_token_id = TASK_TRANSLATE_TOKEN_ID;
    m_timestamp_begin_token_id = TIMESTAMP_BEGIN_TOKEN_ID;
    m_eos_token_id = EOT_TOKEN_ID;
    m_first_language_token_id = FIRST_LANGUAGE_TOKEN_ID;
    m_last_language_token_id = LAST_LANGUAGE_TOKEN_ID;
    m_chunk_size_seconds = CHUNK_SIZE_SEC;
    m_num_mel_bins = N_MELS;
    m_n_audio_ctx = N_AUDIO_CTX;
    m_decoder_ordered_outputs_names__embeddings = EMBEDDINGS_OUTPUT_LAYERS_NAMES;
    m_encoder_model_name = SPEECH2TEXT_ENCODER_MODEL_NAME;
    m_decoder_model_name = SPEECH2TEXT_DECODER_MODEL_NAME;
    m_decoder_input_name__common_encoder_decoder_layer = COMMON_LAYER_INPUT_DECODER_NAME;
    m_decoder_input_name__input_tokens_layer = TOKENS_LAYER_INPUT_DECODER_NAME;

    return HAILO_SUCCESS;
}

hailo_status Speech2TextServer::parse_config_json(const nlohmann::json &hailo_config_json)
{
    CHECK(hailo_config_json.contains("decoder_start_token_id"), HAILO_INVALID_ARGUMENT);
    m_decoder_start_token_id = hailo_config_json["decoder_start_token_id"].get<int>();

    CHECK(hailo_config_json.contains("task_transcribe_token_id"), HAILO_INVALID_ARGUMENT);
    m_task_transcribe_token_id = hailo_config_json["task_transcribe_token_id"].get<int>();

    CHECK(hailo_config_json.contains("task_translate_token_id"), HAILO_INVALID_ARGUMENT);
    m_task_translate_token_id = hailo_config_json["task_translate_token_id"].get<int>();

    CHECK(hailo_config_json.contains("timestamp_begin_token_id"), HAILO_INVALID_ARGUMENT);
    m_timestamp_begin_token_id = hailo_config_json["timestamp_begin_token_id"].get<int>();

    CHECK(hailo_config_json.contains("eos_token_id"), HAILO_INVALID_ARGUMENT);
    m_eos_token_id = hailo_config_json["eos_token_id"].get<int>();

    CHECK(hailo_config_json.contains("first_language_token_id"), HAILO_INVALID_ARGUMENT);
    m_first_language_token_id = hailo_config_json["first_language_token_id"].get<int>();

    CHECK(hailo_config_json.contains("last_language_token_id"), HAILO_INVALID_ARGUMENT);
    m_last_language_token_id = hailo_config_json["last_language_token_id"].get<int>();

    CHECK(hailo_config_json.contains("chunk_size_seconds"), HAILO_INVALID_ARGUMENT);
    m_chunk_size_seconds = hailo_config_json["chunk_size_seconds"].get<int>();

    CHECK(hailo_config_json.contains("num_mel_bins"), HAILO_INVALID_ARGUMENT);
    m_num_mel_bins = hailo_config_json["num_mel_bins"].get<int>();

    CHECK(hailo_config_json.contains("n_audio_ctx"), HAILO_INVALID_ARGUMENT);
    m_n_audio_ctx = hailo_config_json["n_audio_ctx"].get<int>();

    CHECK(hailo_config_json.contains("repetition_penalty_window_size"), HAILO_INVALID_ARGUMENT);
    m_repetition_penalty_window_size = hailo_config_json["repetition_penalty_window_size"].get<int>();

    CHECK(hailo_config_json.contains("repetition_penalty_default_value"), HAILO_INVALID_ARGUMENT);
    CHECK_SUCCESS(m_generator_params.set_repetition_penalty(hailo_config_json["repetition_penalty_default_value"].get<float32_t>()));

    CHECK(hailo_config_json.contains("special_tokens_ids_to_suppress"), HAILO_INVALID_ARGUMENT);
    m_special_tokens_ids_to_suppress = hailo_config_json["special_tokens_ids_to_suppress"].get<std::vector<int>>();

    CHECK(hailo_config_json.contains("decoder_ordered_outputs_names__embeddings"), HAILO_INVALID_ARGUMENT);
    m_decoder_ordered_outputs_names__embeddings = hailo_config_json["decoder_ordered_outputs_names__embeddings"].get<std::vector<std::string>>();

    return HAILO_SUCCESS;
}

Expected<Buffer> Speech2TextServer::handle_create_speech2text_request(const MemoryView &request)
{
    LOGGER__GENAI_STATS_START("[create] create vdevice");
    TRY_AS_HRPC_STATUS(auto group_id, Speech2TextCreateSerializer::deserialize_request(request), Speech2TextCreateSerializer);

    auto params = HailoRTDefaults::get_vdevice_params();
    if (!group_id.empty()) {
        params.group_id = group_id.c_str();
    }
    TRY_AS_HRPC_STATUS(auto vdevice, m_vdevice_manager->create_shared_vdevice(params, DEFAULT_SPEECH2TEXT_CONNECTION_PORT), Speech2TextCreateSerializer);
    LOGGER__GENAI_STATS_END("[create] create vdevice");

    LOGGER__GENAI_STATS_START("[create] transfer HEF");
    TRY_AS_HRPC_STATUS(auto hef_buffer, m_session.read(), Speech2TextCreateSerializer);
    LOGGER__GENAI_STATS_END("[create] transfer HEF");

    LOGGER__GENAI_STATS_START("[create] create HEF");
    TRY_AS_HRPC_STATUS(auto hef, Hef::create(hef_buffer), Speech2TextCreateSerializer);
    LOGGER__GENAI_STATS_END("[create] create HEF");

    LOGGER__GENAI_STATS_START("[create] parse GenAI config json");
    TRY_AS_HRPC_STATUS(auto hailo_config_json_view, hef.get_external_resources(HAILO_CONFIG_JSON), Speech2TextCreateSerializer);
    auto hailo_config_json = parse_json(hailo_config_json_view);

    if (!hailo_config_json.contains(OLD_HEF_CONFIG_JSON_PARSING_KEY)) {
        CHECK_SUCCESS_AS_HRPC_STATUS(parse_config_json(hailo_config_json), Speech2TextCreateSerializer);
        hef.set_memory_footprint_optimization(true); // zero-copy configuration if possible (was not supported in the old HEF)
        TRY_AS_HRPC_STATUS(m_encoder_model_name, get_model_name_from_suffix(hef, ENCODER_MODEL_NAME_SUFFIX), Speech2TextCreateSerializer);
        TRY_AS_HRPC_STATUS(m_decoder_model_name, get_model_name_from_suffix(hef, DECODER_MODEL_NAME_SUFFIX), Speech2TextCreateSerializer);
    } else {
        // Support backward compatibility, TODO: HRT-19298
        CHECK_SUCCESS_AS_HRPC_STATUS(set_default_config_params(), Speech2TextCreateSerializer);
    }
    LOGGER__GENAI_STATS_END("[create] parse GenAI config json");

    LOGGER__GENAI_STATS_START("[create] create speech2text encoder model");
    TRY_AS_HRPC_STATUS(m_inference_manager_encoder, InferenceManager::create(vdevice, hef, m_encoder_model_name), Speech2TextCreateSerializer);
    LOGGER__GENAI_STATS_END("[create] create speech2text encoder model");

    LOGGER__GENAI_STATS_START("[create] configure speech2text encoder model");

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
    LOGGER__GENAI_STATS_END("[create] configure speech2text encoder model");

    LOGGER__GENAI_STATS_START("[create] create speech2text decoder model");
    TRY_AS_HRPC_STATUS(m_inference_manager_decoder, InferenceManager::create(vdevice, hef, m_decoder_model_name), Speech2TextCreateSerializer);
    for (auto input_name : m_inference_manager_decoder->get_model()->get_input_names()) {
        if (has_suffix(input_name, DECODER_INPUT_NAME_SUFFIX__COMMON_ENCODER_DECODER_LAYER)) {
            m_decoder_input_name__common_encoder_decoder_layer = input_name;
        }
        if (has_suffix(input_name, DECODER_INPUT_NAME_SUFFIX__TOKENS_LAYER)) {
            m_decoder_input_name__input_tokens_layer = input_name;
        }
    }
    CHECK_AS_HRPC_STATUS(!m_decoder_input_name__common_encoder_decoder_layer.empty(), HAILO_INVALID_ARGUMENT, Speech2TextCreateSerializer);
    CHECK_AS_HRPC_STATUS(!m_decoder_input_name__input_tokens_layer.empty(), HAILO_INVALID_ARGUMENT, Speech2TextCreateSerializer);
    LOGGER__GENAI_STATS_END("[create] create speech2text decoder model");

    LOGGER__GENAI_STATS_START("[create] configure speech2text decoder model");

    // Check that the decoder input shape is the same as the encoder output shape
    TRY_AS_HRPC_STATUS(auto decoder_input, m_inference_manager_decoder->get_model()->input(m_decoder_input_name__common_encoder_decoder_layer), Speech2TextCreateSerializer);
    auto decoder_input_shape = decoder_input.shape();
    decoder_input.set_format_order(HAILO_FORMAT_ORDER_NHCW);
    CHECK_AS_HRPC_STATUS(((decoder_input_shape.height == encoder_output_shape.height) && (decoder_input_shape.width == encoder_output_shape.width) &&
        (decoder_input_shape.features == encoder_output_shape.features)), HAILO_INVALID_ARGUMENT, Speech2TextCreateSerializer);

    // Set the format type of the decoder outputs
    m_embeddings_outputs_row_size = 0;
    for (auto &output_name : m_decoder_ordered_outputs_names__embeddings) {
        TRY_AS_HRPC_STATUS(auto output, m_inference_manager_decoder->get_model()->output(output_name), Speech2TextCreateSerializer);
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
    m_vocab_size = std::accumulate(m_embeddings_outputs_info.begin(), m_embeddings_outputs_info.end(), 0, [](size_t acc, const auto &output_info) {
        return acc + std::get<1>(output_info);
    });

    std::unordered_set<std::string> layers_not_to_allocate = {m_decoder_input_name__common_encoder_decoder_layer};
    TRY_AS_HRPC_STATUS(m_decoder_buffers, m_inference_manager_decoder->allocate_buffers(layers_not_to_allocate), Speech2TextCreateSerializer);
    m_decoder_buffers.first[m_decoder_input_name__common_encoder_decoder_layer] = m_encoder_output_buffer; // Using the same buffer for encoder output and decoder input
    m_decoder_inputs = buffers_to_memviews(m_decoder_buffers.first);
    m_decoder_outputs = buffers_to_memviews(m_decoder_buffers.second);

    // Buffer used to store the next token scores after extracting and dequantizing the decoder outputs' embeddings to float32
    TRY_AS_HRPC_STATUS(m_next_token_scores_buffer, Buffer::create(m_vocab_size * sizeof(float32_t), BufferStorageParams::create_dma()), Speech2TextCreateSerializer);

    CHECK_SUCCESS_AS_HRPC_STATUS(m_inference_manager_decoder->configure(), Speech2TextCreateSerializer);
    LOGGER__GENAI_STATS_END("[create] configure speech2text decoder model");

    LOGGER__GENAI_STATS_START("[create] parse GenAI resources");
    // Init tokenizer
    TRY_AS_HRPC_STATUS(auto tokenizer_view, hef.get_external_resources(TOKENIZER), Speech2TextCreateSerializer);
    TRY_AS_HRPC_STATUS(m_tokenizer, HailoTokenizer::create(tokenizer_view), Speech2TextCreateSerializer);

    // Init embeddings
    TRY_AS_HRPC_STATUS(auto input, m_inference_manager_decoder->get_model()->input(m_decoder_input_name__input_tokens_layer), Speech2TextCreateSerializer);
    m_decoder_seq_length = input.shape().width;

    auto embeddings_dtype = input.format().type;
    CHECK_AS_HRPC_STATUS(embeddings_dtype == HAILO_FORMAT_TYPE_UINT8, HAILO_INVALID_OPERATION, Speech2TextCreateSerializer);
    TRY_AS_HRPC_STATUS(auto embeddings_view, hef.get_external_resources(INPUT_EMB_BINARY), Speech2TextCreateSerializer);
    size_t cols = input.shape().features;
    size_t rows = embeddings_view.size() / cols / sizeof(uint8_t);
    TRY_AS_HRPC_STATUS(m_token_embedder, TokenEmbedder<uint8_t>::create(embeddings_view, rows, cols), Speech2TextCreateSerializer);

    m_task_to_token_id_map = {
        {Speech2TextTask::TRANSCRIBE, m_task_transcribe_token_id},
        {Speech2TextTask::TRANSLATE,  m_task_translate_token_id}
    };
    LOGGER__GENAI_STATS_END("[create] parse GenAI resources");

    TRY_AS_HRPC_STATUS(auto blank_tokens, m_tokenizer->text_to_tokens(" "), Speech2TextCreateSerializer);
    m_post_process = Speech2TextPostProcess(m_chunk_size_seconds, m_timestamp_begin_token_id, m_eos_token_id, std::move(blank_tokens), m_special_tokens_ids_to_suppress, TIME_PRECISION, MAX_INITIAL_TIMESTAMP);

    return Speech2TextCreateSerializer::serialize_reply(HAILO_SUCCESS, m_generator_params);
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
    std::string wrapped_language = "<|" + language + "|>";
    TRY(auto language_tokens, m_tokenizer->text_to_tokens(wrapped_language));
    CHECK(language_tokens.size() == 1, HAILO_INVALID_ARGUMENT, "Language must be 1 token, got multiple tokens");
    int token_id = language_tokens[0];

    return token_id;
}

Expected<Buffer> Speech2TextServer::handle_generate_request(const MemoryView &request)
{
    TRY_AS_HRPC_STATUS(m_generator_params, Speech2TextGenerateSerializer::deserialize_request(request), Speech2TextGenerateSerializer);

    LOGGER__GENAI_STATS_START("[generate] audio buffer transfer");
    TRY_AS_HRPC_STATUS(auto audio_buffer, m_session.read(), Speech2TextGenerateSerializer);
    LOGGER__GENAI_STATS_END("[generate] audio buffer transfer");
    CHECK_AS_HRPC_STATUS(audio_buffer->size() > 0, HAILO_INVALID_ARGUMENT, Speech2TextGenerateSerializer);

    TRY_AS_HRPC_STATUS(auto segments_infos, process_input_audio(MemoryView(audio_buffer)), Speech2TextGenerateSerializer);

    return Speech2TextGenerateSerializer::serialize_reply(HAILO_SUCCESS, segments_infos);
}

Expected<std::vector<Speech2Text::SegmentInfo>> Speech2TextServer::process_input_audio(const MemoryView &audio)
{
    std::vector<Speech2Text::SegmentInfo> segments_infos;
    LOGGER__GENAI_STATS_START("[generate] pre-process - convert to mel spectrogram");
    // TODO: HRT-18595 - Check if possible to compute in chunks and re-use input buffer
    auto mel_spectrogram = Speech2TextPreProcess::compute_log_mel(MemoryView(audio), m_chunk_size_seconds, SAMPLE_RATE, HOP_LENGTH, m_num_mel_bins);
    LOGGER__GENAI_STATS_END("[generate] pre-process - convert to mel spectrogram");

    int seek = 0;
    static const int n_frames = static_cast<int>((m_chunk_size_seconds * SAMPLE_RATE) / HOP_LENGTH);
    int content_frames = static_cast<int>(mel_spectrogram.rows()) - n_frames;
    int input_stride = n_frames / m_n_audio_ctx;

    CHECK(n_frames * mel_spectrogram.cols() * sizeof(float32_t) == m_encoder_input_buffer->size(), HAILO_INTERNAL_FAILURE, "Encoder input buffer size mismatch");
    Eigen::Map<Eigen::MatrixXf> encoder_input(reinterpret_cast<float32_t*>(m_encoder_input_buffer->data()), n_frames, mel_spectrogram.cols());

    // Prepare context tokens
    // Task token
    CHECK(contains(m_task_to_token_id_map, m_generator_params.task()), HAILO_INVALID_ARGUMENT);
    auto task_token_id = m_task_to_token_id_map.at(m_generator_params.task());

    // Language token
    int language_token_id = INVALID_TOKEN_VALUE;
    if (!m_generator_params.language().empty()) {
        std::string language(m_generator_params.language());
        TRY(language_token_id, language_to_token_id(language));
    }

    std::vector<int> context_tokens = {m_decoder_start_token_id, language_token_id, task_token_id};

    while (seek < content_frames) {
        uint32_t segment_size = std::min(n_frames, content_frames - seek);
        float32_t segment_duration_sec = static_cast<float32_t>(segment_size * HOP_LENGTH) / static_cast<float32_t>(SAMPLE_RATE);
        float32_t time_offset = static_cast<float32_t>(seek * HOP_LENGTH) / static_cast<float32_t>(SAMPLE_RATE);
        Eigen::Block<Eigen::MatrixXf> curr_chunk_input = mel_spectrogram.middleRows(seek, segment_size); // Eigen::Block is a memory view

        LOGGER__GENAI_STATS_START("[generate] pre-process - pad or trim");
        Speech2TextPreProcess::pad_or_trim(curr_chunk_input, encoder_input);
        LOGGER__GENAI_STATS_END("[generate] pre-process - pad or trim");

        LOGGER__GENAI_STATS_START("[generate] hw-inference encoder model");
        CHECK_SUCCESS(m_inference_manager_encoder->generate(MemoryView(m_encoder_input_buffer), MemoryView(m_encoder_output_buffer)));
        LOGGER__GENAI_STATS_END("[generate] hw-inference encoder model");

        if (seek == 0 && (language_token_id == INVALID_TOKEN_VALUE)) {
            TRY(language_token_id, detect_language());
            context_tokens[LANGUAGE_TOKEN_ID_OFFSET] = language_token_id;
        }

        TRY(auto pair, decoder_loop(context_tokens));
        auto &[tokens, sum_logprobs] = pair;

        LOGGER__GENAI_STATS_START("[generate] update segments and calc seek");
        TRY(seek, update_segments_and_calc_seek(tokens, sum_logprobs, seek, segment_duration_sec, segment_size, time_offset, input_stride, segments_infos));
        LOGGER__GENAI_STATS_END("[generate] update segments and calc seek");
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
    static const bool force_printable = false;
    static const bool skip_special_tokens = true;
    if (!segments_end_timestamp_indexes.empty()) {
        if (single_end) {
            segments_end_timestamp_indexes.push_back(tokens.size() - 1); // cut at the very end
        }

        size_t curr_segment_start_ts_index = 0;
        for (size_t curr_segment_end_index : segments_end_timestamp_indexes) {
            Speech2Text::SegmentInfo segment_info;
            std::vector<int> text_tokens(tokens.begin() + curr_segment_start_ts_index + 1, tokens.begin() + curr_segment_end_index - 1); // the plus 1 is to exclude the timestamp tokens

            TRY(segment_info.text, m_tokenizer->tokens_to_text(text_tokens, force_printable, skip_special_tokens));
            segment_info.start_sec = time_offset + static_cast<float32_t>(tokens[curr_segment_start_ts_index] - m_timestamp_begin_token_id) * TIME_PRECISION;
            segment_info.end_sec = time_offset + static_cast<float32_t>(tokens[curr_segment_end_index] - m_timestamp_begin_token_id) * TIME_PRECISION;
            segments_infos.push_back(segment_info);
            curr_segment_start_ts_index = curr_segment_end_index;
        }

        if (single_end) {
            seek += segment_size;
        } else {
            int last_timestamp_pos = tokens[segments_end_timestamp_indexes.back()] - m_timestamp_begin_token_id;
            seek += last_timestamp_pos * input_stride;
        }
    } else {
        // No consecutive timestamps - single segment
        Speech2Text::SegmentInfo segment_info;
        float32_t duration_sec = segment_duration_sec;

        if (last_timestamp != INVALID_TOKEN_VALUE && last_timestamp != m_timestamp_begin_token_id) {
            duration_sec = static_cast<float32_t>(last_timestamp - m_timestamp_begin_token_id) * TIME_PRECISION;
        }

        auto tokens_end_iter = (tokens.size() > 1) && is_timestamp_token(tokens[tokens.size() - 1]) ?
            tokens.end() - 1 : // if the last token is a timestamp, exclude it from the text
            tokens.end();
        std::vector<int> text_tokens(tokens.begin() + 1, tokens_end_iter); // the plus 1 is to exclude the timestamp tokens from the text
        TRY(auto text, m_tokenizer->tokens_to_text(text_tokens, force_printable, skip_special_tokens));
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

Expected<int> Speech2TextServer::detect_language()
{
    std::vector<int> tokens = {m_decoder_start_token_id};
    m_token_embedder->tokens_to_embeddings(MemoryView(m_decoder_inputs[m_decoder_input_name__input_tokens_layer]), tokens);
    CHECK_SUCCESS(m_inference_manager_decoder->generate(m_decoder_inputs, m_decoder_outputs));
    Eigen::Map<Eigen::VectorXf> token_scores = get_next_token_scores(LANGUAGE_TOKEN_ID_OFFSET);

    auto num_language_tokens = m_last_language_token_id - m_first_language_token_id + 1;
    Eigen::Map<Eigen::VectorXf> language_scores(token_scores.data() + m_first_language_token_id, num_language_tokens);

    Eigen::Index max_idx;
    language_scores.maxCoeff(&max_idx);
    int language_token_id = m_first_language_token_id + static_cast<int>(max_idx);

    LOGGER__INFO("Detected language token ID: {}", language_token_id);

    return language_token_id;
}

Expected<std::pair<std::vector<int>, float32_t>> Speech2TextServer::decoder_loop(const std::vector<int> &context_tokens)
{
    float32_t sum_logprobs = 0.0f;
    int next_token_idx = static_cast<int>(context_tokens.size());

    // NOTE: Openai is looping over `m_decoder_seq_length / 2` instead of just `m_decoder_seq_length` because they use previous context tokens.
    // Since we don't use previous context tokens, we probably can loop over the full sequence length.
    // TODO: HRT-18595 - Remove it, and check accuracy and performance.
    auto sample_len = m_decoder_seq_length / 2;

    // TODO HRT-18570 - Use `tokens` directly, need to adjust the post-process
    std::vector<int> generated_tokens = context_tokens;
    std::vector<int> padded_input_tokens(m_decoder_seq_length, PADDING_VALUE);
    std::copy(generated_tokens.begin(), generated_tokens.end(), padded_input_tokens.begin());
    for (int i = 0; i < sample_len; i++) {

        LOGGER__GENAI_STATS_START("[generate] decoder - tokens to embeddings");
        m_token_embedder->tokens_to_embeddings(MemoryView(m_decoder_inputs[m_decoder_input_name__input_tokens_layer]), padded_input_tokens);
        LOGGER__GENAI_STATS_END("[generate] decoder - tokens to embeddings");

        LOGGER__GENAI_STATS_START("[generate] hw-inference decoder model");
        CHECK_SUCCESS(m_inference_manager_decoder->generate(m_decoder_inputs, m_decoder_outputs));
        LOGGER__GENAI_STATS_END("[generate] hw-inference decoder model");

        LOGGER__GENAI_STATS_START("[generate] decoder - post-process");
        Eigen::Map<Eigen::VectorXf> next_token_scores = get_next_token_scores(next_token_idx);

        auto [next_token, logprob] = m_post_process.get_next_token(next_token_scores, generated_tokens, m_generator_params.repetition_penalty(), m_repetition_penalty_window_size);
        padded_input_tokens[next_token_idx] = next_token;
        generated_tokens.push_back(next_token);
        sum_logprobs += logprob;
        LOGGER__GENAI_STATS_END("[generate] decoder - post-process");

        if (next_token == m_eos_token_id) {
            // Stop if we reached the end of the segment
            break;
        }
        next_token_idx++;
    }

    // Make sure each sequence has at least one EOT token at the end
    if (generated_tokens.back() != m_eos_token_id) {
        generated_tokens.push_back(m_eos_token_id);
    }

    // Slice from the actual start of the sample (without context tokens) to the first EOT token
    auto it = std::find(generated_tokens.begin(), generated_tokens.end(), m_eos_token_id);
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
