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
#include "hailo/quantization.hpp"

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

Expected<std::unique_ptr<LLMServer>> VLMServer::create_unique(std::shared_ptr<Session> session, std::shared_ptr<VDeviceManager> vdevice_manager)
{
    // Init with generation default params, will be overwritten by the params from the HEF
    auto post_process_params = LLMGeneratorParams(VLMServer::DEFAULT_GENERATION_TEMPERATURE, VLMServer::DEFAULT_GENERATION_TOP_P,
        VLMServer::DEFAULT_GENERATION_TOP_K, VLMServer::DEFAULT_GENERATION_FREQ_PENALTY, LLMServer::DEFAULT_GENERATION_MAX_GENERATED_TOKENS,
        VLMServer::DEFAULT_GENERATION_DO_SAMPLE, HAILO_RANDOM_SEED);

    auto ptr = std::make_unique<VLMServer>(session, vdevice_manager, std::move(post_process_params));
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY); // Consider returning different status

    return std::unique_ptr<LLMServer>(std::move(ptr));
}

VLMServer::VLMServer(std::shared_ptr<Session> session, std::shared_ptr<VDeviceManager> vdevice_manager, LLMGeneratorParams &&post_process_params) :
        LLMServer(session, vdevice_manager, std::move(post_process_params))
{
}

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
    TRY_AS_HRPC_STATUS(auto tuple, VLMCreateSerializer::deserialize_request(request), VLMCreateSerializer);
    auto &group_id = std::get<0>(tuple);
    auto &hef_path = std::get<1>(tuple);
    auto file_size = std::get<2>(tuple);
    auto tokenizer_on_host = std::get<3>(tuple);

    auto params = HailoRTDefaults::get_vdevice_params();
    if (!group_id.empty()) {
        params.group_id = group_id.c_str();
    }
    TRY_AS_HRPC_STATUS(auto vdevice, m_vdevice_manager->create_shared_vdevice(params, DEFAULT_LLM_CONNECTION_PORT), VLMCreateSerializer);
    LOGGER__GENAI_STATS_END("[create-vlm] create vdevice");

    LOGGER__GENAI_STATS_START("[create-vlm] transfer HEF");
    std::shared_ptr<Buffer> hef_buffer_ptr;
    if (!hef_path.empty()) { // hef path is not none only if hef exists locally, so no need to transfer it over the session
        TRY_AS_HRPC_STATUS(auto buff, read_binary_file(hef_path, BufferStorageParams::create_dma()), VLMCreateSerializer);
        hef_buffer_ptr = make_shared_nothrow<Buffer>(std::move(buff));
        CHECK_AS_HRPC_STATUS(nullptr != hef_buffer_ptr, HAILO_OUT_OF_HOST_MEMORY, VLMCreateSerializer); // Consider returning different status
    } else {
        // Empty string indicates that the HEF does not exist on the server
        TRY_AS_HRPC_STATUS(hef_buffer_ptr, m_session.receive_file_chunked(file_size), VLMCreateSerializer);
    }

    LOGGER__GENAI_STATS_END("[create-vlm] transfer HEF");
    LOGGER__INFO("hef buffer of size '{}'", hef_buffer_ptr->size());

    LOGGER__GENAI_STATS_START("[create-vlm] create HEF");
    TRY_AS_HRPC_STATUS(auto hef, Hef::create(hef_buffer_ptr), VLMCreateSerializer);
    hef.set_memory_footprint_optimization(true); // zero-copy configuration if possible
    LOGGER__GENAI_STATS_END("[create-vlm] create HEF");

    LOGGER__GENAI_STATS_START("[create-vlm] parse GenAI resources");
    TRY_AS_HRPC_STATUS(auto hailo_config_json_view, hef.get_external_resources(HAILO_CONFIG_JSON), VLMCreateSerializer);
    CHECK_SUCCESS_AS_HRPC_STATUS(parse_config_json(hailo_config_json_view), VLMCreateSerializer);

    TRY_AS_HRPC_STATUS(auto theta_view, hef.get_external_resources(THETA), VLMCreateSerializer);
    auto theta = LLMPreProcess::generate_theta_from_memview(theta_view);
    LOGGER__GENAI_STATS_END("[create-vlm] parse GenAI resources");

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
        if (LLMPreProcess::is_positional_embed_layer(input.name(), m_input_layers_names_suffixes)) {
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
    m_inference_manager_tbt = nullptr;
    auto inference_manager_tbt = LLMInferenceManager::create(vdevice, hef, TBT_MODEL_NAME_SUFFIX);
    if (inference_manager_tbt) {
        m_inference_manager_tbt = inference_manager_tbt.release();
    }
    LOGGER__GENAI_STATS_END("[create-vlm] create tbt model");

    LOGGER__GENAI_STATS_START("[create-vlm] configure tbt model");
    if (m_inference_manager_tbt) {
        auto model_tbt = m_inference_manager_tbt->get_model();
        for (auto input : model_tbt->inputs()) {
            if (LLMPreProcess::is_positional_embed_layer(input.name(), m_input_layers_names_suffixes)) {
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
    }
    LOGGER__GENAI_STATS_END("[create-vlm] configure tbt model");

    LOGGER__GENAI_STATS_START("[create-vlm] create PreProcess");
    auto prefill_inputs_frame_size = m_inference_manager_prefill->get_inputs_frame_size();
    auto tbt_inputs_frame_size = m_inference_manager_tbt ? m_inference_manager_tbt->get_inputs_frame_size() : std::map<std::string, size_t>();

    // Extract embeddings layer info
    // TODO: HRT-16646 - Add this to embeddings binary format in hef
    TRY_AS_HRPC_STATUS(auto embeddings_input_name, get_layer_name_from_suffix<size_t>(m_input_layers_names_suffixes.embeddings, prefill_inputs_frame_size),
        VLMCreateSerializer);
    TRY_AS_HRPC_STATUS(auto embeddings_input, m_inference_manager_prefill->get_model()->input(embeddings_input_name),
        VLMCreateSerializer);
    auto embeddings_features = embeddings_input.shape().features;

    // Get scaled-mask value
    TRY_AS_HRPC_STATUS(auto mask_input_name, get_layer_name_from_suffix<size_t>(m_input_layers_names_suffixes.attention_mask, prefill_inputs_frame_size),
        VLMCreateSerializer);
    TRY(auto mask_input, m_inference_manager_prefill->get_model()->input(mask_input_name));
    auto mask_quant_infos = mask_input.get_quant_infos();
    CHECK_AS_HRPC_STATUS(1 == mask_quant_infos.size(), HAILO_INTERNAL_FAILURE, VLMCreateSerializer);
    float32_t dequantized_mask_value = 1;
    uint8_t scaled_mask_value = 0;
    Quantization::quantize_input_buffer<float32_t, uint8_t>(&dequantized_mask_value, &scaled_mask_value, 1, mask_quant_infos[0]);

    TRY_AS_HRPC_STATUS(m_pre_process, VLMPreProcess::create(prefill_inputs_frame_size, tbt_inputs_frame_size,
        std::move(theta), embeddings_features, encoder_input_config.shape(), scaled_mask_value, m_input_layers_names_suffixes, m_pre_process_params),
        VLMCreateSerializer);
    LOGGER__GENAI_STATS_END("[create-vlm] create PreProcess");

    LOGGER__GENAI_STATS_START("[create-vlm] create tokenizer");
    const auto embeddings_per_frame = dynamic_cast<VLMPreProcess*>(m_pre_process.get())->embeddings_per_frame();
    if (!tokenizer_on_host) {
        // create token_embedder
        TRY_AS_HRPC_STATUS(auto embeddings_view, hef.get_external_resources(INPUT_EMB_BINARY), VLMCreateSerializer);
        TRY_AS_HRPC_STATUS(m_token_embedder, TokenEmbedder<uint16_t>::create(embeddings_view,
            embeddings_view.size() / (sizeof(uint16_t) * embeddings_features), embeddings_features,
            m_image_pad_token_id, embeddings_per_frame), VLMCreateSerializer);

        TRY_AS_HRPC_STATUS(auto tokenizer_view, hef.get_external_resources(TOKENIZER), VLMCreateSerializer);

        // HRT-16824 - HailoTokenizer should get memview in the c'tor and convert to string if neccesary inside
        std::string tokenizer_blob(tokenizer_view.size(), '\0');
        std::memcpy(const_cast<char*>(tokenizer_blob.data()), tokenizer_view.data(), tokenizer_blob.size());
        TRY_AS_HRPC_STATUS(m_tokenizer, HailoTokenizer::create(tokenizer_blob), VLMCreateSerializer);
    }
    LOGGER__GENAI_STATS_END("[create-vlm] create tokenizer");

    m_recovery.tokens = {m_end_of_sentence_token_id};

    hailo_format_t input_frame_format = encoder_input_config.format();
    hailo_3d_image_shape_t input_frame_shape = encoder_input_config.shape();

    TRY_AS_HRPC_STATUS(auto reply, VLMCreateSerializer::serialize_reply(HAILO_SUCCESS, input_frame_shape, input_frame_format, m_chat_template, embeddings_features,
        m_image_pad_token_id, embeddings_per_frame), VLMCreateSerializer);
    return reply;
}

Expected<Buffer> VLMServer::handle_vlm_generate_request(const MemoryView &request)
{
    TRY_AS_HRPC_STATUS(auto number_of_frames, VLMGeneratorGenerateSerializer::deserialize_request(request),
        VLMGeneratorGenerateSerializer);
    CHECK_AS_HRPC_STATUS(number_of_frames <= MAX_FRAMES_IN_SINGLE_GENERATION, HAILO_INVALID_ARGUMENT,
        VLMGeneratorGenerateSerializer);

    std::unique_lock<std::mutex> lock(m_generation_mutex);

    LOGGER__INFO("Generate request received with {} frames", number_of_frames);

    prepare_for_new_generation();

    for (uint32_t i = 0; i < number_of_frames; i++) {
        TRY_AS_HRPC_STATUS(auto size_read, m_session.read(MemoryView(*m_frame_encoder_input_buffers[i])),
            VLMGeneratorGenerateSerializer);
        CHECK_AS_HRPC_STATUS(size_read == m_frame_encoder_input_buffers[i]->size(), HAILO_INVALID_ARGUMENT,
            VLMGeneratorGenerateSerializer);
    }

    // Process frames to get frame embeddings - TODO: HRT-17264 - Move to async generation
    for (uint32_t i = 0; i < number_of_frames; i++) {
        m_current_frame_embeddings.push_back(MemoryView(m_frame_encoder_output_buffers[i]));
        LOGGER__GENAI_STATS_START("[vlm-generate-prefill] encode frame");
        auto status = m_inference_manager_frame_encoder->generate(
            MemoryView(m_frame_encoder_input_buffers[i]), MemoryView(m_frame_encoder_output_buffers[i]));
        LOGGER__GENAI_STATS_END("[vlm-generate-prefill] encode frame");
        CHECK_AS_HRPC_STATUS(status == HAILO_SUCCESS, status, VLMGeneratorGenerateSerializer);
    }

    TRY_AS_HRPC_STATUS(auto generator_generate_reply, VLMGeneratorGenerateSerializer::serialize_reply(HAILO_SUCCESS),
        VLMGeneratorGenerateSerializer);
    return generator_generate_reply;
}

hailo_status VLMServer::process_prefill_inputs_chunk(std::map<std::string, MemoryView> &prefill_inputs,
    std::map<std::string, MemoryView> &prefill_outputs, const std::vector<MemoryView> &input_embeddings, const std::vector<MemoryView> &frame_embeddings,
    uint32_t &current_frame_index, uint32_t &current_emb_index_in_frame)
{
    LOGGER__GENAI_STATS_START("[vlm-generate-prefill] pre process");
    CHECK_SUCCESS(dynamic_cast<VLMPreProcess*>(m_pre_process.get())->prepare_inputs_prefill(prefill_inputs, input_embeddings,
        frame_embeddings, current_frame_index, current_emb_index_in_frame));
    LOGGER__GENAI_STATS_END("[vlm-generate-prefill] pre process");

    LOGGER__GENAI_STATS_START("[vlm-generate-prefill] update cache offset");
    CHECK_SUCCESS(m_inference_manager_prefill->update_cache_offset(static_cast<int32_t>(input_embeddings.size())));

    LOGGER__GENAI_STATS_END("[vlm-generate-prefill] update cache offset");

    LOGGER__GENAI_STATS_START("[vlm-generate-prefill] hw-inference prefill");
    CHECK_SUCCESS(m_inference_manager_prefill->generate(prefill_inputs, prefill_outputs));
    LOGGER__GENAI_STATS_END("[vlm-generate-prefill] hw-inference prefill");

    return HAILO_SUCCESS;
}

Expected<int> VLMServer::get_next_token_prefill(std::map<std::string, MemoryView> &prefill_inputs,
    std::map<std::string, MemoryView> &prefill_outputs, const std::vector<MemoryView> &input_embeddings,
    const std::vector<MemoryView> &frame_embeddings,
    const LLMGeneratorParams &params)
{
    size_t num_full_chunks = input_embeddings.size() / m_pre_process_params.prefill_input_tokens_count;
    size_t remainder_size = input_embeddings.size() % m_pre_process_params.prefill_input_tokens_count;

    uint32_t current_frame_index = 0;
    uint32_t current_emb_index_in_frame = 0;

    // Process the remainder first, if any
    if (remainder_size > 0) {
        std::vector<MemoryView> first_prefill_embeddings(input_embeddings.begin(), input_embeddings.begin() + remainder_size);
        CHECK_SUCCESS(process_prefill_inputs_chunk(prefill_inputs, prefill_outputs, first_prefill_embeddings, frame_embeddings,
            current_frame_index, current_emb_index_in_frame));
    }

    // Process full prefill chunks
    size_t offset = remainder_size;
    for (size_t i = 0; i < num_full_chunks; ++i) {
        std::vector<MemoryView> input_embeddings_chunk(input_embeddings.begin() + offset, input_embeddings.begin() + offset + m_pre_process_params.prefill_input_tokens_count);
        CHECK_SUCCESS(process_prefill_inputs_chunk(prefill_inputs, prefill_outputs, input_embeddings_chunk, frame_embeddings,
            current_frame_index, current_emb_index_in_frame));
        offset += input_embeddings_chunk.size();
    }

    LOGGER__GENAI_STATS_START("[vlm-generate-prefill] post process");
    auto next_token = m_post_process.get_next_token(prefill_outputs.begin()->second, m_tokens_history, params);
    LOGGER__GENAI_STATS_END("[vlm-generate-prefill] post process");

    return next_token;
}


Expected<std::pair<int, LLMGeneratorCompletion::Status>> VLMServer::handle_prefill_phase(const std::vector<int> &tokens,
    const std::vector<MemoryView> &embeddings)
{
    // VLM prefill phase: process input embeddings WITH frame embeddings
    // Note: m_current_frame_embeddings should be already populated

    // Use provided embeddings if available (client-side tokenizer), otherwise tokenize on server
    // find the number of image pad tokens in the the tokens-vector
    auto number_of_image_pad_tokens = std::count(tokens.begin(), tokens.end(), m_image_pad_token_id);
    CHECK(static_cast<size_t>(number_of_image_pad_tokens) == m_current_frame_embeddings.size(), HAILO_INVALID_OPERATION);

    TRY(auto next_token, get_next_token_prefill(m_prefill_inputs, m_prefill_outputs,
        embeddings, m_current_frame_embeddings, m_current_generation_params));

    m_current_frame_embeddings.clear();
    m_generated_token_count++;

    auto generation_status = get_current_generation_status(next_token);
    if (generation_status != LLMGeneratorCompletion::Status::GENERATING) {
        // Handle end of generation - may return GENERATING if recovery tokens need delivery
        TRY(generation_status, handle_generation_completion(generation_status, next_token));
    }

    return std::make_pair(next_token, generation_status);
}

Expected<std::unique_ptr<LLMServerManager>> VLMServerManager::create(std::shared_ptr<Session> session, std::shared_ptr<VDeviceManager> vdevice_manager)
{
    auto server = VLMServer::create_unique(session, vdevice_manager);
    CHECK_EXPECTED(server);

    auto ptr = std::make_unique<VLMServerManager>(session, std::move(server.value()));
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY); // Consider returning different status

    return std::unique_ptr<LLMServerManager>(std::move(ptr));
}

VLMServerManager::VLMServerManager(std::shared_ptr<Session> session, std::unique_ptr<LLMServer> &&server) :
    LLMServerManager(session, std::move(server))
{
    m_dispatcher[HailoGenAIActionID::VLM__CREATE] =
        [&](const MemoryView &request) { return dynamic_cast<VLMServer*>(m_server.get())->handle_create_vlm_request(request); };
    m_dispatcher[HailoGenAIActionID::VLM__GENERATOR_GENERATE] =
        [&](const MemoryView &request) { return dynamic_cast<VLMServer*>(m_server.get())->handle_vlm_generate_request(request); };
}