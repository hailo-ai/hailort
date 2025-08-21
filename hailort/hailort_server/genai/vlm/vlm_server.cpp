/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vlm_server.cpp
 * @brief Implementation for VLM server
 **/

#include "vlm_server.hpp"

#include "hailo/hailort_defaults.hpp"
#include "hailo/hailort_common.hpp"

#include "common/file_utils.hpp"

#include "utils.hpp"

#include <nlohmann/json.hpp>


using namespace hailort::genai;
using namespace hailort;

static const std::string PREFILL_MODEL_NAME_SUFFIX = "prefill";
static const std::string TBT_MODEL_NAME_SUFFIX = "tbt";
static const std::string FRAME_ENCODER_MODEL_NAME_SUFFIX = "encoder";
static const std::string VIS_SPECIAL_TOKEN_RESOURCE_NAME = "special_token__vision";

constexpr uint32_t VLMServer::MAX_FRAMES_IN_SINGLE_GENERATION;

Expected<std::unique_ptr<LLMServer>> VLMServer::create_unique(std::shared_ptr<Session> session)
{
    const uint32_t MAX_BACKLOG = 1024;
    TRY(auto shutdown_event, Event::create_shared(Event::State::not_signalled));
    auto generated_tokens_queue_exp = SpscQueue<std::pair<std::string, LLMGeneratorCompletion::Status>>::create(MAX_BACKLOG, shutdown_event, HAILO_INFINITE_TIMEOUT);
    CHECK_EXPECTED(generated_tokens_queue_exp);

    // generation-queue - size is 1 as only 1 generation in parallel is possible
    auto input_queue_exp = SpscQueue<std::pair<std::vector<int>, uint32_t>>::create(1, shutdown_event, HAILO_INFINITE_TIMEOUT);
    CHECK_EXPECTED(input_queue_exp);

    // Init with generation default params, will be overwritten by the params from the HEF
    auto post_process_params = LLMGeneratorParams(VLMServer::DEFAULT_GENERATION_TEMPERATURE, VLMServer::DEFAULT_GENERATION_TOP_P,
        VLMServer::DEFAULT_GENERATION_TOP_K, VLMServer::DEFAULT_GENERATION_FREQ_PENALTY, LLMServer::DEFAULT_GENERATION_MAX_GENERATED_TOKENS,
        VLMServer::DEFAULT_GENERATION_DO_SAMPLE, HAILO_RANDOM_SEED);

    auto ptr = std::make_unique<VLMServer>(session, generated_tokens_queue_exp.release(), input_queue_exp.release(),
        shutdown_event, std::move(post_process_params));
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY); // Consider returning different status

    ptr->init_generation_thread();

    return std::unique_ptr<LLMServer>(std::move(ptr));
}

VLMServer::VLMServer(std::shared_ptr<Session> session, SpscQueue<std::pair<std::string, LLMGeneratorCompletion::Status>> &&generated_tokens_queue,
    SpscQueue<std::pair<std::vector<int>, uint32_t>> &&input_queue, EventPtr shutdown_event, LLMGeneratorParams &&post_process_params) :
        LLMServer(session, std::move(generated_tokens_queue), {}, shutdown_event, std::move(post_process_params)),
        m_input_queue(std::move(input_queue))
{
}

VLMServer::~VLMServer()
{
    terminate_generation_thread();
};

hailo_status VLMServer::parse_config_json(const MemoryView &config_json)
{
    auto json_ptr = config_json.data();
    auto json_size = config_json.size();
    auto hailo_config_json = nlohmann::json::parse(json_ptr, json_ptr + json_size);

    CHECK(hailo_config_json.contains("image_pad"), HAILO_INVALID_ARGUMENT);
    m_image_pad_token_id = hailo_config_json["image_pad"].get<int>();

    return LLMServer::parse_config_json(hailo_config_json);
}

Expected<Buffer> VLMServer::handle_create_vlm_request(const MemoryView &request)
{
    LOGGER__GENAI_STATS_START("[create-vlm] create vdevice");
    TRY_AS_HRPC_STATUS(auto pair, VLMCreateSerializer::deserialize_request(request), VLMCreateSerializer);
    auto &group_id = pair.first;
    auto &hef_path = pair.second;

    auto params = HailoRTDefaults::get_vdevice_params();
    if (!group_id.empty()) {
        params.group_id = group_id.c_str();
    }
    TRY_AS_HRPC_STATUS(auto vdevice, hailort::VDevice::create_shared(params), VLMCreateSerializer);
    LOGGER__GENAI_STATS_END("[create-vlm] create vdevice");

    LOGGER__GENAI_STATS_START("[create-vlm] transfer HEF");
    std::shared_ptr<Buffer> hef_buffer_ptr;
    if (!hef_path.empty()) {
        TRY_AS_HRPC_STATUS(auto buff, read_binary_file(hef_path, BufferStorageParams::create_dma()), VLMCreateSerializer);
        hef_buffer_ptr = make_shared_nothrow<Buffer>(std::move(buff));
        CHECK_AS_HRPC_STATUS(nullptr != hef_buffer_ptr, HAILO_OUT_OF_HOST_MEMORY, VLMCreateSerializer); // Consider returning different status
    } else {
        // Empty string indicates that the HEF does not exist on the server
        TRY_AS_HRPC_STATUS(hef_buffer_ptr, m_session.read(), VLMCreateSerializer);
    }

    LOGGER__GENAI_STATS_END("[create-vlm] transfer HEF");
    LOGGER__INFO("hef buffer of size '{}'", hef_buffer_ptr->size());

    LOGGER__GENAI_STATS_START("[create-vlm] create HEF");
    TRY_AS_HRPC_STATUS(auto hef, Hef::create(hef_buffer_ptr), VLMCreateSerializer);
    hef.set_memory_footprint_optimization(true); // zero-copy configuration if possible
    LOGGER__GENAI_STATS_END("[create-vlm] create HEF");

    LOGGER__GENAI_STATS_START("[create-vlm] create vision encoder model");
    TRY_AS_HRPC_STATUS(auto frame_encoder_model_name, get_model_name_from_suffix(hef, FRAME_ENCODER_MODEL_NAME_SUFFIX),
        VLMCreateSerializer);
    TRY_AS_HRPC_STATUS(m_inference_manager_frame_encoder, InferenceManager::create(vdevice, hef, frame_encoder_model_name),
        VLMCreateSerializer);
    LOGGER__GENAI_STATS_END("[create-vlm] create vision encoder model");

    LOGGER__GENAI_STATS_START("[create-vlm] configure vision encoder model");
    auto model_encoder = m_inference_manager_frame_encoder->get_model();
    TRY_AS_HRPC_STATUS(auto encoder_output_config, model_encoder->output(),
        VLMCreateSerializer);
    m_frame_encoder_output_buffers.reserve(MAX_FRAMES_IN_SINGLE_GENERATION);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_SINGLE_GENERATION; ++i) {
        TRY_AS_HRPC_STATUS(auto buffer, Buffer::create_shared(encoder_output_config.get_frame_size(), BufferStorageParams::create_dma()),
            VLMCreateSerializer);
        m_frame_encoder_output_buffers.push_back(buffer);
    }
    TRY_AS_HRPC_STATUS(auto encoder_input_config, model_encoder->input(), VLMCreateSerializer);
    m_frame_encoder_input_buffers.reserve(MAX_FRAMES_IN_SINGLE_GENERATION);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_SINGLE_GENERATION; ++i) {
        TRY_AS_HRPC_STATUS(auto buffer, Buffer::create_shared(encoder_input_config.get_frame_size(), BufferStorageParams::create_dma()),
            VLMCreateSerializer);
        m_frame_encoder_input_buffers.push_back(buffer);
    }

    CHECK_SUCCESS_AS_HRPC_STATUS(m_inference_manager_frame_encoder->configure(), VLMCreateSerializer);
    LOGGER__GENAI_STATS_END("[create-vlm] configure vision encoder model");

    LOGGER__GENAI_STATS_START("[create-vlm] create prefill model");
    TRY_AS_HRPC_STATUS(m_inference_manager_prefill, LLMInferenceManager::create(vdevice, hef, PREFILL_MODEL_NAME_SUFFIX),
        VLMCreateSerializer);
    LOGGER__GENAI_STATS_END("[create-vlm] create prefill model");

    LOGGER__GENAI_STATS_START("[create-vlm] configure prefill model");
    auto model_prefill = m_inference_manager_prefill->get_model();
    for (auto input : model_prefill->inputs()) {
        if (LLMPreProcess::is_positional_embed_layer(input.name())) {
            input.set_format_type(HAILO_FORMAT_TYPE_FLOAT32);
        }
    }
    for (auto output : model_prefill->outputs()) {
        output.set_format_type(HAILO_FORMAT_TYPE_FLOAT32);
    }
    TRY_AS_HRPC_STATUS(m_prefill_buffers, m_inference_manager_prefill->allocate_buffers(),
        VLMCreateSerializer);
    m_prefill_inputs = buffers_to_memviews(m_prefill_buffers.first);
    m_prefill_outputs = buffers_to_memviews(m_prefill_buffers.second);
    CHECK_SUCCESS_AS_HRPC_STATUS(m_inference_manager_prefill->configure(), VLMCreateSerializer);
    LOGGER__GENAI_STATS_END("[create-vlm] configure prefill model");

    LOGGER__GENAI_STATS_START("[create-vlm] create tbt model");
    TRY_AS_HRPC_STATUS(m_inference_manager_tbt, LLMInferenceManager::create(vdevice, hef, TBT_MODEL_NAME_SUFFIX),
        VLMCreateSerializer);
    LOGGER__GENAI_STATS_END("[create-vlm] create tbt model");

    LOGGER__GENAI_STATS_START("[create-vlm] configure tbt model");
    auto model_tbt = m_inference_manager_tbt->get_model();
    for (auto input : model_tbt->inputs()) {
        if (LLMPreProcess::is_positional_embed_layer(input.name())) {
            input.set_format_type(HAILO_FORMAT_TYPE_FLOAT32);
        }
    }
    for (auto output : model_tbt->outputs()) {
        output.set_format_type(HAILO_FORMAT_TYPE_FLOAT32);
    }
    TRY_AS_HRPC_STATUS(m_tbt_buffers, m_inference_manager_tbt->allocate_buffers(), VLMCreateSerializer);
    m_tbt_inputs = buffers_to_memviews(m_tbt_buffers.first);
    m_tbt_outputs = buffers_to_memviews(m_tbt_buffers.second);
    CHECK_SUCCESS_AS_HRPC_STATUS(m_inference_manager_tbt->configure(), VLMCreateSerializer);
    LOGGER__GENAI_STATS_END("[create-vlm] configure tbt model");

    LOGGER__GENAI_STATS_START("[create-vlm] parse GenAI resources");
    auto prefill_inputs_frame_size = m_inference_manager_prefill->get_inputs_frame_size();
    auto tbt_inputs_frame_size = m_inference_manager_tbt->get_inputs_frame_size();

    TRY_AS_HRPC_STATUS(auto external_resources, m_inference_manager_tbt->get_hef().get_external_resources(),
        VLMCreateSerializer);

    CHECK_AS_HRPC_STATUS(contains(external_resources, HAILO_CONFIG_JSON), HAILO_INVALID_ARGUMENT, LLMCreateSerializer);
    CHECK_SUCCESS_AS_HRPC_STATUS(parse_config_json(external_resources.at(HAILO_CONFIG_JSON)), VLMCreateSerializer);

    // TODO: HRT-16646 - Remove default flow, always get the theta from hef
    auto theta = contains(external_resources, THETA) ?
        LLMPreProcess::generate_theta_from_memview(external_resources.at(THETA)) : LLMPreProcess::generate_default_theta();

    // Extract embeddings layer info
    // TODO: HRT-16646 - Add this to embeddings binary format in hef
    TRY_AS_HRPC_STATUS(auto embeddings_input_name, get_layer_name_from_suffix<size_t>(INPUT_LAYER_EMBEDDINGS_SUFF, prefill_inputs_frame_size),
        VLMCreateSerializer);
    TRY_AS_HRPC_STATUS(auto embeddings_input, m_inference_manager_prefill->get_model()->input(embeddings_input_name),
        VLMCreateSerializer);
    auto embeddings_features = embeddings_input.shape().features;
    auto embeddings_dtype = embeddings_input.format().type;

    CHECK_AS_HRPC_STATUS(contains(external_resources, INPUT_EMB_BINARY), HAILO_INVALID_HEF, VLMCreateSerializer);
    LOGGER__GENAI_STATS_END("[create-vlm] parse GenAI resources");

    LOGGER__GENAI_STATS_START("[create-vlm] create PreProcess");
    TRY_AS_HRPC_STATUS(m_pre_process, VLMPreProcess::create(external_resources.at(INPUT_EMB_BINARY), prefill_inputs_frame_size, tbt_inputs_frame_size,
        std::move(theta), embeddings_features, embeddings_dtype, m_image_pad_token_id, encoder_input_config.shape()),
        VLMCreateSerializer);
    LOGGER__GENAI_STATS_END("[create-vlm] create PreProcess");

    LOGGER__GENAI_STATS_START("[create-vlm] create tokenizer");
    CHECK_AS_HRPC_STATUS(contains(external_resources, TOKENIZER), HAILO_INVALID_HEF, VLMCreateSerializer);
    // HRT-16824 - HailoTokenizer should get memview in the c'tor and convert to string if neccesary inside
    std::string tokenizer_blob(external_resources.at(TOKENIZER).size(), '\0');
    std::memcpy(const_cast<char*>(tokenizer_blob.data()), external_resources[TOKENIZER].data(), tokenizer_blob.size());
    TRY_AS_HRPC_STATUS(m_tokenizer, HailoTokenizer::create(tokenizer_blob), VLMCreateSerializer);
    LOGGER__GENAI_STATS_END("[create-vlm] create tokenizer");

    TRY_AS_HRPC_STATUS(m_generation_recovery_sequence, m_tokenizer->tokens_to_text({m_end_of_sentence_token_id}, true), VLMCreateSerializer);
    LOGGER__INFO("Default end of generation sequence: '{}'", m_generation_recovery_sequence);

    hailo_format_t input_frame_format = encoder_input_config.format();
    hailo_3d_image_shape_t input_frame_shape = encoder_input_config.shape();

    TRY_AS_HRPC_STATUS(auto reply, VLMCreateSerializer::serialize_reply(HAILO_SUCCESS, input_frame_shape, input_frame_format, m_chat_template),
        VLMCreateSerializer);
    return reply;
}

Expected<Buffer> VLMServer::handle_vlm_generate_request(const MemoryView &request)
{
    TRY_AS_HRPC_STATUS(auto number_of_frames, VLMGeneratorGenerateSerializer::deserialize_request(request),
        VLMGeneratorGenerateSerializer);
    CHECK_AS_HRPC_STATUS(number_of_frames <= MAX_FRAMES_IN_SINGLE_GENERATION, HAILO_INVALID_ARGUMENT,
        VLMGeneratorGenerateSerializer);

    std::unique_lock<std::mutex> lock(m_generation_mutex);
    CHECK_AS_HRPC_STATUS(m_state == State::READY, HAILO_INVALID_OPERATION,
        VLMGeneratorGenerateSerializer);
    m_state = State::GENERATING;

    TRY_AS_HRPC_STATUS(auto prompt_buffer, m_session.read(), VLMGeneratorGenerateSerializer);
    std::string prompt = prompt_buffer->to_string();

    LOGGER__INFO("Generate request received. number_of_frames - '{}', input prompt - '{}'", number_of_frames, prompt);
    for (uint32_t i = 0; i < number_of_frames; i++) {
        TRY_AS_HRPC_STATUS(auto size_read, m_session.read(MemoryView(*m_frame_encoder_input_buffers[i])),
            VLMGeneratorGenerateSerializer);
        CHECK_AS_HRPC_STATUS(size_read == m_frame_encoder_input_buffers[i]->size(), HAILO_INVALID_ARGUMENT,
            VLMGeneratorGenerateSerializer);
    }

    LOGGER__GENAI_STATS_START("[vlm-generate-prefill] prepare input tokens");
    TRY_AS_HRPC_STATUS(auto pair, get_input_tokens(prompt), VLMGeneratorGenerateSerializer);
    LOGGER__GENAI_STATS_END("[vlm-generate-prefill] prepare input tokens");

    auto &input_tokens = pair.first;
    auto &number_of_vision_tokens = pair.second;

    CHECK_AS_HRPC_STATUS(number_of_vision_tokens == number_of_frames, HAILO_INVALID_ARGUMENT,
        VLMGeneratorGenerateSerializer);

    m_input_queue.enqueue(std::make_pair(input_tokens, number_of_frames));

    TRY_AS_HRPC_STATUS(auto generator_generate_reply, VLMGeneratorGenerateSerializer::serialize_reply(HAILO_SUCCESS),
        VLMGeneratorGenerateSerializer);
    return generator_generate_reply;
}

hailo_status VLMServer::process_prefill_inputs_chunk(std::map<std::string, MemoryView> &prefill_inputs,
    std::map<std::string, MemoryView> &prefill_outputs, std::vector<int> &input_tokens, const std::vector<MemoryView> &frame_embeddings,
    uint32_t &current_frame_index, uint32_t &current_emb_index_in_frame)
{
    LOGGER__GENAI_STATS_START("[vlm-generate-prefill] pre process");
    CHECK_SUCCESS(dynamic_cast<VLMPreProcess*>(m_pre_process.get())->prepare_inputs_prefill(prefill_inputs, input_tokens,
        frame_embeddings, current_frame_index, current_emb_index_in_frame));
    LOGGER__GENAI_STATS_END("[vlm-generate-prefill] pre process");

    LOGGER__GENAI_STATS_START("[vlm-generate-prefill] update cache offset");
    CHECK_SUCCESS(m_inference_manager_prefill->update_cache_offset(static_cast<int32_t>(input_tokens.size())));
    LOGGER__GENAI_STATS_END("[vlm-generate-prefill] update cache offset");

    LOGGER__GENAI_STATS_START("[vlm-generate-prefill] hw-inference prefill");
    CHECK_SUCCESS(m_inference_manager_prefill->generate(prefill_inputs, prefill_outputs));
    LOGGER__GENAI_STATS_END("[vlm-generate-prefill] hw-inference prefill");

    return HAILO_SUCCESS;
}

Expected<std::pair<std::vector<int>, uint32_t>> VLMServer::get_input_tokens(const std::string &input_prompt)
{
    TRY(auto input_tokens, m_tokenizer->text_to_tokens(input_prompt));

    auto number_of_vision_tokens = static_cast<uint32_t>(std::count(input_tokens.begin(), input_tokens.end(), m_image_pad_token_id));
    auto updated_input_tokens = adjust_vision_token(input_tokens);

    return std::make_pair(updated_input_tokens, number_of_vision_tokens);
}

std::vector<int> VLMServer::adjust_vision_token(const std::vector<int> &tokens)
{
    const auto embeddings_per_frame = dynamic_cast<VLMPreProcess*>(m_pre_process.get())->embeddings_per_frame();

    std::vector<int> res;
    for (auto token : tokens) {
        if (m_image_pad_token_id == token) {
            res.insert(res.end(), embeddings_per_frame, token);
        } else {
            res.push_back(token);
        }
    }

    return res;
}

Expected<int> VLMServer::get_next_token_prefill(std::map<std::string, MemoryView> &prefill_inputs,
    std::map<std::string, MemoryView> &prefill_outputs, std::vector<int> &input_tokens,
    const std::vector<MemoryView> &frame_embeddings, const LLMGeneratorParams &params)
{
    size_t num_full_chunks = input_tokens.size() / PREFILL_INPUT_TOKENS_SIZE;
    size_t remainder_size = input_tokens.size() % PREFILL_INPUT_TOKENS_SIZE;

    uint32_t current_frame_index = 0;
    uint32_t current_emb_index_in_frame = 0;

    // Process the remainder first, if any
    if (remainder_size > 0) {
        std::vector<int> first_prefill_tokens(input_tokens.begin(), input_tokens.begin() + remainder_size);
        CHECK_SUCCESS(process_prefill_inputs_chunk(prefill_inputs, prefill_outputs, first_prefill_tokens, frame_embeddings,
            current_frame_index, current_emb_index_in_frame));
    }

    // Process full prefill chunks
    size_t offset = remainder_size;
    for (size_t i = 0; i < num_full_chunks; ++i) {
        std::vector<int> input_tokens_chunk(input_tokens.begin() + offset, input_tokens.begin() + offset + PREFILL_INPUT_TOKENS_SIZE);
        CHECK_SUCCESS(process_prefill_inputs_chunk(prefill_inputs, prefill_outputs, input_tokens_chunk, frame_embeddings,
            current_frame_index, current_emb_index_in_frame));
        offset += input_tokens_chunk.size();
    }

    LOGGER__GENAI_STATS_START("[vlm-generate-prefill] post process");
    auto next_token = m_post_process.get_next_token(prefill_outputs.begin()->second, m_tokens_history, params);
    LOGGER__GENAI_STATS_END("[vlm-generate-prefill] post process");

    return next_token;
}

void VLMServer::flush_internal_queues()
{
    m_input_queue.clear();
    return LLMServer::flush_internal_queues();
}

void VLMServer::async_generate_into_internal_db()
{
    while (HAILO_TIMEOUT == m_shutdown_event->wait(std::chrono::milliseconds(0))) {
        auto input_pair = m_input_queue.dequeue();
        std::unique_lock<std::mutex> lock(m_generation_mutex);

        /*
            Create local copy so if user changes generation-params (by creating another generator) it won't affect the current generation
        */
        auto local_post_process_params = m_post_process_params;
        CHECK_SUCCESS_OR_DO_AND_RETURN(input_pair.status(), m_state = State::ERROR,
            "Failed to dequeue input prompt with status '{}'", input_pair.status());

        auto &input_tokens = input_pair->first;
        auto &num_frames = input_pair->second;

        LOGGER__INFO("Parsed {} tokens", input_tokens.size());

        // TODO (HRT-17264): Optimize, perform in parallel
        std::vector<MemoryView> frames_embeddings;
        for (uint32_t i = 0; i < num_frames; i++) {
            frames_embeddings.push_back(MemoryView(m_frame_encoder_output_buffers[i]));
            LOGGER__GENAI_STATS_START("[vlm-generate-prefill] encode frame");
            auto status = m_inference_manager_frame_encoder->generate(
                MemoryView(m_frame_encoder_input_buffers[i]), MemoryView(m_frame_encoder_output_buffers[i]));
            LOGGER__GENAI_STATS_END("[vlm-generate-prefill] encode frame");
            CHECK_SUCCESS_OR_DO_AND_RETURN(status, m_state = State::ERROR,
                "Failed to generate frame embeddings with status '{}'", status);
        }

        auto status = process_model(input_tokens, local_post_process_params, frames_embeddings);
        CHECK_SUCCESS_OR_DO_AND_RETURN(status, m_state = State::ERROR,
            "Failed to generate tokens with status '{}'", status);

        // Finished current generation, remove potintially buffered-tokens from tokenizer
        m_tokenizer->clear_buffered_tokens();

        m_state = State::READY;
    }
}

hailo_status VLMServer::process_model(std::vector<int> &input_tokens, const LLMGeneratorParams &local_post_process_params,
    const std::vector<MemoryView> &frame_embeddings)
{
    TRY(auto next_token, get_next_token_prefill(m_prefill_inputs, m_prefill_outputs, input_tokens, frame_embeddings, local_post_process_params));
    CHECK_SUCCESS(handle_next_token(next_token, LLMGeneratorCompletion::Status::GENERATING));
    TRY(auto generated_tokens, tbt_generation_loop(m_tbt_inputs, m_tbt_outputs, next_token, local_post_process_params));
    (void)generated_tokens;

    return HAILO_SUCCESS;
}

Expected<std::unique_ptr<LLMServerManager>> VLMServerManager::create(std::shared_ptr<Session> session)
{
    auto server = VLMServer::create_unique(session);
    CHECK_EXPECTED(server);

    auto ptr = std::make_unique<VLMServerManager>(session, std::move(server.value()));
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY); // Consider returning different status

    return std::unique_ptr<LLMServerManager>(std::move(ptr));
}

VLMServerManager::VLMServerManager(std::shared_ptr<Session> session, std::unique_ptr<LLMServer> &&server) :
    LLMServerManager(session, std::move(server))
{
    m_dispatcher[static_cast<int>(HailoGenAIActionID::VLM__CREATE)] =
        [&](const MemoryView &request) { return dynamic_cast<VLMServer*>(m_server.get())->handle_create_vlm_request(request); };
    m_dispatcher[static_cast<int>(HailoGenAIActionID::VLM__GENERATOR_GENERATE)] =
        [&](const MemoryView &request) { return dynamic_cast<VLMServer*>(m_server.get())->handle_vlm_generate_request(request); };
}