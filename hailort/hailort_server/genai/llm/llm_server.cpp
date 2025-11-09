/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file llm_server.cpp
 * @brief Implementation for LLM server
 **/

#include "llm_server.hpp"

#include "hailo/hailort_defaults.hpp"
#include "hailo/hailort_common.hpp"
#include "hailo/quantization.hpp"

#include "common/file_utils.hpp"

#include "utils.hpp"

#include <nlohmann/json.hpp>


using namespace hailort::genai;
using namespace hailort;


Expected<std::string> get_path_from_lora_name(const std::string &lora_name)
{
    if (lora_name == std::string("LoRA-Adapter-0")) {
        return std::string("/data/qwen2/qwen2_call_auto_reply_combined.hef");
    } else if (lora_name == std::string("LoRA-Adapter-1")) {
        return std::string("/data/qwen2/qwen2_doc_summary_combined.hef");
    } else if (lora_name == std::string("LoRA-Adapter-2")) {
        return std::string("/data/qwen2/qwen2_call_summary_combined.hef");
    } else if (lora_name == std::string("LoRA-Adapter-3")) {
        return std::string("/data/qwen2/qwen2_recording_summary_combined.hef");
    } else if (lora_name == std::string("QWEN_2_5_coder")) {
        return std::string("/data/qwen2/qwen2_5_coder.hef");
    } else {
        LOGGER__ERROR("Invalid LoRA name '{}'", lora_name);
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }
}

Expected<std::string> get_prefill_model_name_suffix(const std::string &lora_name, size_t network_group_count)
{
    // TODO (HRT-18043): remove this hack. NG names should be deterministic and not depend on the number of NGs.
    if (network_group_count <= 2) {
        return std::string("prefill");
    }
    if (lora_name.empty()) {
        return std::string("base_model__prefill");
    }
    return lora_name + std::string("__prefill");
}

Expected<std::string> get_tbt_model_name_suffix(const std::string &lora_name, size_t network_group_count)
{
    // TODO (HRT-18043): remove this hack. NG names should be deterministic and not depend on the number of NGs.
    if (network_group_count == 2) {
        return std::string("tbt");
    }
    if (lora_name.empty()) {
        return std::string("base_model__tbt");
    }
    return lora_name + std::string("__tbt");
}

Expected<std::unique_ptr<LLMServer>> LLMServer::create_unique(std::shared_ptr<Session> session, std::shared_ptr<VDeviceManager> vdevice_manager)
{
    // Init with generation default params, will be overwritten by the params from the HEF
    auto post_process_params = LLMGeneratorParams(LLMServer::DEFAULT_GENERATION_TEMPERATURE, LLMServer::DEFAULT_GENERATION_TOP_P,
        LLMServer::DEFAULT_GENERATION_TOP_K, LLMServer::DEFAULT_GENERATION_FREQ_PENALTY,
        LLMServer::DEFAULT_GENERATION_MAX_GENERATED_TOKENS, LLMServer::DEFAULT_GENERATION_DO_SAMPLE,
        HAILO_RANDOM_SEED);

    auto ptr = std::make_unique<LLMServer>(session, vdevice_manager, std::move(post_process_params));
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY); // Consider returning different status

    return ptr;
}

LLMServer::LLMServer(std::shared_ptr<Session> session, std::shared_ptr<VDeviceManager> vdevice_manager, LLMGeneratorParams &&post_process_params) :
        m_session(SessionWrapper(session)), m_vdevice_manager(vdevice_manager), m_post_process(),
        m_post_process_params(std::move(post_process_params)),
        m_next_input_prompt_prefix_tokens(), m_generated_token_count(0),
        m_current_generation_params(m_post_process_params), m_abort_requested(false)
{}

LLMServer::~LLMServer()
{
    // Remove all references to local VDevice before marking it as removed
    m_inference_manager_prefill.reset();
    m_inference_manager_tbt.reset();

    m_vdevice_manager->remove_vdevice(DEFAULT_LLM_CONNECTION_PORT); // Use it as a unique client id
}

hailo_status LLMServer::parse_config_json(const MemoryView &config_json)
{
    auto json_ptr = config_json.data();
    auto json_size = config_json.size();
    auto hailo_config_json = nlohmann::json::parse(json_ptr, json_ptr + json_size);

    return parse_config_json(hailo_config_json);
}

hailo_status LLMServer::parse_config_json(const nlohmann::json &hailo_config_json)
{
    CHECK(hailo_config_json.contains("eos_token_id"), HAILO_INVALID_ARGUMENT);
    m_end_of_sentence_token_id = hailo_config_json["eos_token_id"].get<int>();

    m_chat_template = "";
    if (hailo_config_json.contains("chat_template")) {
        m_chat_template = hailo_config_json["chat_template"].get<std::string>();
    }

    if (hailo_config_json.contains("stop_token_id")) {
        auto stop_tokens = hailo_config_json["stop_token_id"].get<std::vector<int>>();
        for (const auto &stop_token : stop_tokens) {
            m_tokenized_stop_sequences.push_back({ stop_token });
        }
    }

    if (hailo_config_json.contains("begin_of_reasoning_token_id")) {
        m_reasoning.start_token_id = hailo_config_json["begin_of_reasoning_token_id"].get<int>();
    } else {
        m_reasoning.start_token_id = INVALID_TOKEN_VALUE;
    }
    if (hailo_config_json.contains("end_of_reasoning_token_id")) {
        m_reasoning.end_token_id = hailo_config_json["end_of_reasoning_token_id"].get<int>();
    } else {
        m_reasoning.end_token_id = INVALID_TOKEN_VALUE;
    }

    if (hailo_config_json.contains("default_generation_params")) {
        if (hailo_config_json["default_generation_params"].contains("max_new_tokens")) {
            m_post_process_params.set_max_generated_tokens(hailo_config_json["default_generation_params"]["max_new_tokens"].get<uint32_t>());
        }
        if (hailo_config_json["default_generation_params"].contains("temperature")) {
            m_post_process_params.set_temperature(hailo_config_json["default_generation_params"]["temperature"].get<float>());
        }
        if (hailo_config_json["default_generation_params"].contains("top_p")) {
            m_post_process_params.set_top_p(hailo_config_json["default_generation_params"]["top_p"].get<float>());
        }
        if (hailo_config_json["default_generation_params"].contains("top_k")) {
            m_post_process_params.set_top_k(hailo_config_json["default_generation_params"]["top_k"].get<uint32_t>());
        }
        if (hailo_config_json["default_generation_params"].contains("repetition_penalty")) {
            m_post_process_params.set_frequency_penalty(hailo_config_json["default_generation_params"]["repetition_penalty"].get<float>());
        }
        if (hailo_config_json["default_generation_params"].contains("do_sample")) {
            m_post_process_params.set_do_sample(hailo_config_json["default_generation_params"]["do_sample"].get<bool>());
        }
    }

    if (hailo_config_json.contains("pre_process_params")) {
        if (hailo_config_json["pre_process_params"].contains("kv_cache_size")) {
            m_pre_process_params.kv_cache_size = hailo_config_json["pre_process_params"]["kv_cache_size"].get<uint32_t>();
        }
        if (hailo_config_json["pre_process_params"].contains("num_attention_heads")) {
            m_pre_process_params.num_attention_heads = hailo_config_json["pre_process_params"]["num_attention_heads"].get<uint32_t>();
        }
        if (hailo_config_json["pre_process_params"].contains("num_key_value_heads")) {
            m_pre_process_params.num_key_value_heads = hailo_config_json["pre_process_params"]["num_key_value_heads"].get<uint32_t>();
        }
        if (hailo_config_json["pre_process_params"].contains("prefill_input_tokens_count")) {
            m_pre_process_params.prefill_input_tokens_count = hailo_config_json["pre_process_params"]["prefill_input_tokens_count"].get<uint32_t>();
        }
    }

    if (hailo_config_json.contains("input_layers_names_suffixes")) {
        if (hailo_config_json["input_layers_names_suffixes"].contains("embeddings")) {
            m_input_layers_names_suffixes.embeddings = hailo_config_json["input_layers_names_suffixes"]["embeddings"].get<std::string>();
        }
        if (hailo_config_json["input_layers_names_suffixes"].contains("attention_mask")) {
            m_input_layers_names_suffixes.attention_mask = hailo_config_json["input_layers_names_suffixes"]["attention_mask"].get<std::string>();
        }
        if (hailo_config_json["input_layers_names_suffixes"].contains("pe_q_cos")) {
            m_input_layers_names_suffixes.pe_q_cos = hailo_config_json["input_layers_names_suffixes"]["pe_q_cos"].get<std::string>();
        }
        if (hailo_config_json["input_layers_names_suffixes"].contains("pe_q_sin")) {
            m_input_layers_names_suffixes.pe_q_sin = hailo_config_json["input_layers_names_suffixes"]["pe_q_sin"].get<std::string>();
        }
        if (hailo_config_json["input_layers_names_suffixes"].contains("pe_k_cos")) {
            m_input_layers_names_suffixes.pe_k_cos = hailo_config_json["input_layers_names_suffixes"]["pe_k_cos"].get<std::string>();
        }
        if (hailo_config_json["input_layers_names_suffixes"].contains("pe_k_sin")) {
            m_input_layers_names_suffixes.pe_k_sin = hailo_config_json["input_layers_names_suffixes"]["pe_k_sin"].get<std::string>();
        }
    }
    return HAILO_SUCCESS;
}

Expected<Buffer> LLMServer::handle_create_llm_request(const MemoryView &request)
{
    LOGGER__GENAI_STATS_START("[create-llm] create vdevice");
    TRY_AS_HRPC_STATUS(auto request_info, LLMCreateSerializer::deserialize_request(request), LLMCreateSerializer);
    auto &lora_name = request_info.lora_name;
    auto &hef_path = request_info.hef_path;
    auto &group_id = request_info.group_id;
    auto &file_size = request_info.file_size;
    auto &tokenizer_on_host = request_info.tokenizer_on_host;

    auto params = HailoRTDefaults::get_vdevice_params();
    if (!group_id.empty()) {
        params.group_id = group_id.c_str();
    }
    TRY_AS_HRPC_STATUS(auto vdevice, m_vdevice_manager->create_shared_vdevice(params, DEFAULT_LLM_CONNECTION_PORT), LLMCreateSerializer);
    LOGGER__GENAI_STATS_END("[create-llm] create vdevice");

    LOGGER__GENAI_STATS_START("[create-llm] transfer HEF");
    std::shared_ptr<Buffer> hef_buffer_ptr;
    if (!hef_path.empty()) { // hef path is not none only if hef exists locally, so no need to transfer it over the session
        if (BUILTIN == hef_path) {
            TRY_AS_HRPC_STATUS(hef_path, get_path_from_lora_name(lora_name), LLMCreateSerializer);
        }
        TRY_AS_HRPC_STATUS(auto buff, read_binary_file(hef_path, BufferStorageParams::create_dma()), LLMCreateSerializer);
        hef_buffer_ptr = make_shared_nothrow<Buffer>(std::move(buff));
        CHECK_AS_HRPC_STATUS(nullptr != hef_buffer_ptr, HAILO_OUT_OF_HOST_MEMORY, LLMCreateSerializer); // Consider returning different status
    } else {
        TRY_AS_HRPC_STATUS(hef_buffer_ptr, m_session.receive_file_chunked(file_size), LLMCreateSerializer);
    }
    LOGGER__GENAI_STATS_END("[create-llm] transfer HEF");
    LOGGER__INFO("hef buffer of size '{}', lora: '{}'", hef_buffer_ptr->size(), lora_name);

    LOGGER__GENAI_STATS_START("[create-llm] create HEF");
    TRY_AS_HRPC_STATUS(auto hef, Hef::create(hef_buffer_ptr), LLMCreateSerializer);
    hef.set_memory_footprint_optimization(true); // zero-copy configuration if possible
    LOGGER__GENAI_STATS_END("[create-llm] create HEF");

    LOGGER__GENAI_STATS_START("[create-llm] parse GenAI resources");
    TRY_AS_HRPC_STATUS(auto hailo_config_json_view, hef.get_external_resources(HAILO_CONFIG_JSON),
        LLMCreateSerializer);
    CHECK_SUCCESS_AS_HRPC_STATUS(parse_config_json(hailo_config_json_view), LLMCreateSerializer);

    TRY_AS_HRPC_STATUS(auto theta_view, hef.get_external_resources(THETA),
        LLMCreateSerializer);
    auto theta = LLMPreProcess::generate_theta_from_memview(theta_view);
    LOGGER__GENAI_STATS_END("[create-llm] parse GenAI resources");

    LOGGER__GENAI_STATS_START("[create-llm] create prefill model");
    auto network_group_names = hef.get_network_groups_names();
    TRY_AS_HRPC_STATUS(auto prefill_model_suffix, get_prefill_model_name_suffix(lora_name, network_group_names.size()), LLMCreateSerializer);
    TRY_AS_HRPC_STATUS(m_inference_manager_prefill, LLMInferenceManager::create(vdevice, hef, prefill_model_suffix),
        LLMCreateSerializer);
    LOGGER__GENAI_STATS_END("[create-llm] create prefill model");

    LOGGER__GENAI_STATS_START("[create-llm] configure prefill model");
    auto model_prefill = m_inference_manager_prefill->get_model();
    for (auto input : model_prefill->inputs()) {
        if (LLMPreProcess::is_positional_embed_layer(input.name(), m_input_layers_names_suffixes)) {
            input.set_format_type(HAILO_FORMAT_TYPE_FLOAT32);
        }
    }
    for (auto output : model_prefill->outputs()) {
        output.set_format_type(HAILO_FORMAT_TYPE_FLOAT32);
    }
    TRY_AS_HRPC_STATUS(m_prefill_buffers, m_inference_manager_prefill->allocate_buffers(), LLMCreateSerializer);
    m_prefill_inputs = buffers_to_memviews(m_prefill_buffers.first);
    m_prefill_outputs = buffers_to_memviews(m_prefill_buffers.second);

    CHECK_SUCCESS_AS_HRPC_STATUS(m_inference_manager_prefill->configure(), LLMCreateSerializer);
    LOGGER__GENAI_STATS_END("[create-llm] configure prefill model");

    LOGGER__GENAI_STATS_START("[create-llm] create tbt model");
    // If no tbt model is available, continue without it
    m_inference_manager_tbt = nullptr;
    TRY_AS_HRPC_STATUS(auto tbt_model_suffix, get_tbt_model_name_suffix(lora_name, network_group_names.size()), LLMCreateSerializer);
    auto inference_manager_tbt = LLMInferenceManager::create(vdevice, hef, tbt_model_suffix);
    if (inference_manager_tbt) {
        m_inference_manager_tbt = inference_manager_tbt.release();
    }
    LOGGER__GENAI_STATS_END("[create-llm] create tbt model");

    LOGGER__GENAI_STATS_START("[create-llm] configure tbt model");
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
        TRY_AS_HRPC_STATUS(m_tbt_buffers, m_inference_manager_tbt->allocate_buffers(), LLMCreateSerializer);
        m_tbt_inputs = buffers_to_memviews(m_tbt_buffers.first);
        m_tbt_outputs = buffers_to_memviews(m_tbt_buffers.second);
        CHECK_SUCCESS_AS_HRPC_STATUS(m_inference_manager_tbt->configure(), LLMCreateSerializer);
    }
    LOGGER__GENAI_STATS_END("[create-llm] configure tbt model");

    LOGGER__GENAI_STATS_START("[create-llm] create PreProcess");
    auto prefill_inputs_frame_size = m_inference_manager_prefill->get_inputs_frame_size();
    auto tbt_inputs_frame_size = m_inference_manager_tbt ? m_inference_manager_tbt->get_inputs_frame_size() : std::map<std::string, size_t>();

    // Extract embeddings layer info
    // TODO: HRT-16646 - Add this to embeddings binary format in hef
    TRY_AS_HRPC_STATUS(auto embeddings_input_name, get_layer_name_from_suffix<size_t>(m_input_layers_names_suffixes.embeddings, prefill_inputs_frame_size),
        LLMCreateSerializer);
    TRY_AS_HRPC_STATUS(auto embeddings_input, m_inference_manager_prefill->get_model()->input(embeddings_input_name),
        LLMCreateSerializer);
    auto embeddings_features = embeddings_input.shape().features;
    auto embeddings_dtype = embeddings_input.format().type;

    // Get scaled-mask value
    TRY_AS_HRPC_STATUS(auto mask_input_name, get_layer_name_from_suffix<size_t>(m_input_layers_names_suffixes.attention_mask, prefill_inputs_frame_size),
        LLMCreateSerializer);
    TRY(auto mask_input, m_inference_manager_prefill->get_model()->input(mask_input_name));
    auto mask_quant_infos = mask_input.get_quant_infos();
    CHECK_AS_HRPC_STATUS(1 == mask_quant_infos.size(), HAILO_INTERNAL_FAILURE, LLMCreateSerializer);
    float32_t dequantized_mask_value = 1;
    uint8_t scaled_mask_value = 0;
    Quantization::quantize_input_buffer<float32_t, uint8_t>(&dequantized_mask_value, &scaled_mask_value, 1, mask_quant_infos[0]);

    TRY_AS_HRPC_STATUS(m_pre_process, LLMPreProcess::create(prefill_inputs_frame_size, tbt_inputs_frame_size,
        std::move(theta), embeddings_features, embeddings_dtype, scaled_mask_value, m_input_layers_names_suffixes, m_pre_process_params), LLMCreateSerializer);
    LOGGER__GENAI_STATS_END("[create-llm] create PreProcess");

    LOGGER__GENAI_STATS_START("[create-llm] create tokenizer");
    if (!tokenizer_on_host) {
        TRY_AS_HRPC_STATUS(auto embeddings_view, hef.get_external_resources(INPUT_EMB_BINARY),
            LLMCreateSerializer);
        TRY_AS_HRPC_STATUS(m_token_embedder, TokenEmbedder<uint16_t>::create(embeddings_view,
            embeddings_view.size() / (sizeof(uint16_t) * embeddings_features), embeddings_features), LLMCreateSerializer);

        TRY_AS_HRPC_STATUS(auto tokenizer_view, hef.get_external_resources(TOKENIZER),
            LLMCreateSerializer);

        // HRT-16824 - HailoTokenizer should get memview in the c'tor and convert to string if neccesary inside
        std::string tokenizer_blob(tokenizer_view.size(), '\0');
        std::memcpy(const_cast<char*>(tokenizer_blob.data()), tokenizer_view.data(), tokenizer_blob.size());
        TRY_AS_HRPC_STATUS(m_tokenizer, HailoTokenizer::create(tokenizer_blob),
            LLMCreateSerializer);
    }
    LOGGER__GENAI_STATS_END("[create-llm] create tokenizer");

    m_recovery.tokens = {m_end_of_sentence_token_id};

    TRY_AS_HRPC_STATUS(auto reply, LLMCreateSerializer::serialize_reply(HAILO_SUCCESS, m_chat_template, embeddings_features), LLMCreateSerializer);
    return reply;
}

Expected<Buffer> LLMServer::handle_get_generator_params_request(const MemoryView &request)
{
    CHECK_SUCCESS_AS_HRPC_STATUS(LLMGetGeneratorParamsSerializer::deserialize_request(request), LLMGetGeneratorParamsSerializer);
    TRY_AS_HRPC_STATUS(auto reply, LLMGetGeneratorParamsSerializer::serialize_reply(HAILO_SUCCESS, m_post_process_params), LLMGetGeneratorParamsSerializer);
    return reply;
}

Expected<Buffer> LLMServer::handle_create_generator_request(const MemoryView &request)
{
    TRY_AS_HRPC_STATUS(m_post_process_params, LLMGeneratorCreateSerializer::deserialize_request(request), LLMGeneratorCreateSerializer);
    m_post_process.set_seed(m_post_process_params.seed());
    TRY_AS_HRPC_STATUS(auto generator_create_reply, LLMGeneratorCreateSerializer::serialize_reply(HAILO_SUCCESS), LLMGeneratorCreateSerializer);
    return generator_create_reply;
}

Expected<Buffer> LLMServer::handle_write_request(const MemoryView &request)
{
    CHECK_SUCCESS_AS_HRPC_STATUS(LLMGeneratorWriteSerializer::deserialize_request(request), LLMGeneratorWriteSerializer);
    TRY_AS_HRPC_STATUS(auto generator_write_reply, LLMGeneratorWriteSerializer::serialize_reply(HAILO_SUCCESS), LLMGeneratorWriteSerializer);
    return generator_write_reply;
}

void LLMServer::prepare_for_new_generation()
{
    m_current_generation_params = m_post_process_params;  // Copy current params

    // Initialize on-demand generation state - combine token vectors directly

    m_generated_token_count = 0;
    m_abort_requested = false;  // Reset abort flag for new generation
    m_next_input_prompt_prefix_tokens.clear();
    m_reasoning.clear();
    m_recent_tokens_sequence.clear();

    if (m_tokenizer) {
        m_tokenizer->clear_buffered_tokens();
    }

    // Clear any leftover recovery tokens from previous generation
    m_recovery.clear();
}

Expected<Buffer> LLMServer::handle_generate_request(const MemoryView &request)
{
    CHECK_SUCCESS_AS_HRPC_STATUS(LLMGeneratorGenerateSerializer::deserialize_request(request), LLMGeneratorGenerateSerializer);
    std::unique_lock<std::mutex> lock(m_generation_mutex);

    // Return current prefix tokens from conversation context to client
    TRY_AS_HRPC_STATUS(auto generator_generate_reply, LLMGeneratorGenerateSerializer::serialize_reply(HAILO_SUCCESS, m_next_input_prompt_prefix_tokens),
        LLMGeneratorGenerateSerializer);

    prepare_for_new_generation();

    return generator_generate_reply;
}

Expected<Buffer> LLMServer::handle_read_request(const MemoryView &request)
{
    TRY_AS_HRPC_STATUS(auto pair, LLMGeneratorReadSerializer::deserialize_request(request),
        LLMGeneratorReadSerializer);
    auto &[timeout, input] = pair;

    std::unique_lock<std::mutex> lock(m_generation_mutex);
    LLMGeneratorCompletion::Status gen_status = LLMGeneratorCompletion::Status::GENERATING;
    int next_token_id = INVALID_TOKEN_VALUE;

    // Check if there are recovery tokens to deliver one by one
    if (m_recovery.has_pending_tokens()) {
        next_token_id = m_recovery.delivery_queue.front();
        m_recovery.delivery_queue.pop();

        if (!m_recovery.has_pending_tokens()) {
            // Last recovery token - deliver the actual termination status
            gen_status = m_recovery.termination_status;
        }
    } else {
        std::vector<MemoryView> embeddings_views;
        std::vector<int> combined_tokens;

        if (!input.initial_prompt.empty()) {
            // First iteration - Server-side tokenizer
            // input.tokens contains prefix tokens, input.initial_prompt contains new user input
            assert(m_tokenizer);
            TRY_AS_HRPC_STATUS(auto prompt_tokens, m_tokenizer->text_to_tokens(input.initial_prompt),
                LLMGeneratorReadSerializer);

            // Combine prefix tokens (in input.tokens) with new prompt tokens
            combined_tokens = input.tokens;  // These are the prefix tokens
            combined_tokens.insert(combined_tokens.end(), prompt_tokens.begin(), prompt_tokens.end());
            input.tokens = combined_tokens;  // Update for consistency

            // TODO: (HRT-18669) think how to manage memory better to prevent allocations
            assert(m_token_embedder);
            embeddings_views = m_token_embedder->tokens_to_embeddings(combined_tokens);
        } else if (!input.tokens.empty() && input.embeddings.empty()) {
            // Subsequent iterations OR client-side tokenizer without embeddings
            // Just use tokens as-is (single token for TBT, or combined tokens from client)
            assert(m_token_embedder);
            embeddings_views = m_token_embedder->tokens_to_embeddings(input.tokens);
        } else {
            // Client-side tokenizer with embeddings
            // Embeddings already contain prefix+input combined (first iter) or single token (subsequent)
            std::transform(input.embeddings.begin(), input.embeddings.end(), std::back_inserter(embeddings_views),
                [](const BufferPtr &buffer) { return MemoryView(buffer); });
        }

        TRY_AS_HRPC_STATUS(auto token_result, generate_next_token_on_demand(input.tokens, embeddings_views),
            LLMGeneratorReadSerializer);

        std::tie(next_token_id, gen_status) = token_result;
    }

    LLMGeneratorReadSerializer::TextGenerationOutput output = {};
    output.output_token_id = next_token_id;
    output.output_token_str = handle_next_token(next_token_id);

    TRY_AS_HRPC_STATUS(auto generator_read_reply,
        LLMGeneratorReadSerializer::serialize_reply(HAILO_SUCCESS, output, gen_status, (m_pre_process->cache_usage_size() >= m_pre_process_params.kv_cache_size)),
        LLMGeneratorReadSerializer);

    return generator_read_reply;
}

Expected<Buffer> LLMServer::handle_tokenize_request(const MemoryView &request)
{
    TRY_AS_HRPC_STATUS(auto prompt, LLMTokenizeSerializer::deserialize_request(request),
        LLMTokenizeSerializer);
    assert(m_tokenizer);
    TRY_AS_HRPC_STATUS(auto tokens, m_tokenizer->text_to_tokens(prompt),
        LLMTokenizeSerializer);
    TRY_AS_HRPC_STATUS(auto reply, LLMTokenizeSerializer::serialize_reply(HAILO_SUCCESS, tokens),
        LLMTokenizeSerializer);
    return reply;
}

Expected<Buffer> LLMServer::handle_clear_context_request(const MemoryView &request)
{
    CHECK_SUCCESS_AS_HRPC_STATUS(LLMClearContextSerializer::deserialize_request(request), LLMClearContextSerializer);
    // Wait on a lock to make sure that no generation is in progress
    std::unique_lock<std::mutex> lock(m_generation_mutex);
    reset_cnversation_context();
    TRY_AS_HRPC_STATUS(auto reply, LLMClearContextSerializer::serialize_reply(HAILO_SUCCESS),
        LLMClearContextSerializer);
    return reply;
}

Expected<Buffer> LLMServer::handle_get_context_request(const MemoryView &request)
{
    CHECK_SUCCESS_AS_HRPC_STATUS(LLMGetContextSerializer::deserialize_request(request), LLMGetContextSerializer);
    // Wait on a lock to make sure that no generation is in progress
    std::unique_lock<std::mutex> lock(m_generation_mutex);
    TRY_AS_HRPC_STATUS(auto context_buffer, get_context(), LLMGetContextSerializer);
    CHECK_SUCCESS_AS_HRPC_STATUS(m_session.write(MemoryView(context_buffer)), LLMGetContextSerializer);
    TRY_AS_HRPC_STATUS(auto reply, LLMGetContextSerializer::serialize_reply(HAILO_SUCCESS), LLMGetContextSerializer);
    return reply;
}

Expected<Buffer> LLMServer::handle_set_context_request(const MemoryView &request)
{
    CHECK_SUCCESS_AS_HRPC_STATUS(LLMSetContextSerializer::deserialize_request(request), LLMSetContextSerializer);
    TRY_AS_HRPC_STATUS(auto context_buffer, m_session.read(), LLMSetContextSerializer);
    // Wait on a lock to make sure that no generation is in progress
    std::unique_lock<std::mutex> lock(m_generation_mutex);
    prepare_for_new_generation();
    CHECK_SUCCESS_AS_HRPC_STATUS(set_context(MemoryView(context_buffer)), LLMSetContextSerializer);
    TRY_AS_HRPC_STATUS(auto reply, LLMSetContextSerializer::serialize_reply(HAILO_SUCCESS),
        LLMSetContextSerializer);
    return reply;
}

Expected<Buffer> LLMServer::handle_abort_request(const MemoryView &request)
{
    CHECK_SUCCESS_AS_HRPC_STATUS(LLMGeneratorAbortSerializer::deserialize_request(request), LLMGeneratorAbortSerializer);

    std::unique_lock<std::mutex> lock(m_generation_mutex);

    m_abort_requested = true;
    LOGGER__INFO("Abort requested during generation");

    TRY_AS_HRPC_STATUS(auto reply, LLMGeneratorAbortSerializer::serialize_reply(HAILO_SUCCESS),
        LLMGeneratorAbortSerializer);
    return reply;
}

Expected<Buffer> LLMServer::handle_set_generation_recovery_sequence_request(const MemoryView &request)
{
    TRY_AS_HRPC_STATUS(m_recovery.tokens, LLMSetEndOfGenerationSequenceSerializer::deserialize_request(request),
        LLMSetEndOfGenerationSequenceSerializer);
    TRY_AS_HRPC_STATUS(auto reply, LLMSetEndOfGenerationSequenceSerializer::serialize_reply(HAILO_SUCCESS),
        LLMSetEndOfGenerationSequenceSerializer);
    return reply;
}

Expected<Buffer> LLMServer::handle_get_generation_recovery_sequence_request(const MemoryView &request)
{
    CHECK_SUCCESS_AS_HRPC_STATUS(LLMGetEndOfGenerationSequenceSerializer::deserialize_request(request), LLMGetEndOfGenerationSequenceSerializer);

    // Convert tokens to string on demand (rare operation)
    std::string recovery_sequence_str = "";
    if (m_tokenizer) {
        TRY_AS_HRPC_STATUS(recovery_sequence_str, m_tokenizer->tokens_to_text(m_recovery.tokens, true), LLMGetEndOfGenerationSequenceSerializer);
    }

    TRY_AS_HRPC_STATUS(auto reply, LLMGetEndOfGenerationSequenceSerializer::serialize_reply(HAILO_SUCCESS, recovery_sequence_str, m_recovery.tokens),
        LLMGetEndOfGenerationSequenceSerializer);
    return reply;
}

Expected<Buffer> LLMServer::handle_set_stop_tokens_request(const MemoryView &request)
{
    TRY_AS_HRPC_STATUS(auto stop_sequences, LLMSetStopTokensSerializer::deserialize_request(request), LLMSetStopTokensSerializer);

    // Clear existing stop sequences and set new ones
    m_tokenized_stop_sequences = stop_sequences;

    TRY_AS_HRPC_STATUS(auto reply, LLMSetStopTokensSerializer::serialize_reply(HAILO_SUCCESS), LLMSetStopTokensSerializer);
    return reply;
}

Expected<Buffer> LLMServer::handle_get_stop_tokens_request(const MemoryView &request)
{
    CHECK_SUCCESS_AS_HRPC_STATUS(LLMGetStopTokensSerializer::deserialize_request(request), LLMGetStopTokensSerializer);

    std::vector<std::string> stop_tokens;
    if (m_tokenizer) {
        for (const auto &sequence : m_tokenized_stop_sequences) {
            TRY_AS_HRPC_STATUS(auto stop_token_str, m_tokenizer->tokens_to_text(sequence), LLMGetStopTokensSerializer);
            stop_tokens.push_back(stop_token_str);
        }
    }

    TRY_AS_HRPC_STATUS(auto reply, LLMGetStopTokensSerializer::serialize_reply(HAILO_SUCCESS, stop_tokens, m_tokenized_stop_sequences),
        LLMGetStopTokensSerializer);
    return reply;
}

Expected<Buffer> LLMServer::handle_get_context_usage_size(const MemoryView &request)
{
    CHECK_SUCCESS_AS_HRPC_STATUS(LLMGetContextUsageSizeSerializer::deserialize_request(request), LLMGetContextUsageSizeSerializer);

    auto context_usage = m_pre_process->cache_usage_size() + m_next_input_prompt_prefix_tokens.size();
    TRY_AS_HRPC_STATUS(auto reply, LLMGetContextUsageSizeSerializer::serialize_reply(HAILO_SUCCESS, context_usage),
        LLMGetContextUsageSizeSerializer);
    return reply;
}

Expected<Buffer> LLMServer::handle_get_max_context_capacity(const MemoryView &request)
{
    CHECK_SUCCESS_AS_HRPC_STATUS(LLMGetMaxContextCapacitySerializer::deserialize_request(request), LLMGetMaxContextCapacitySerializer);

    auto max_context_capacity = m_pre_process_params.kv_cache_size;
    TRY_AS_HRPC_STATUS(auto reply, LLMGetMaxContextCapacitySerializer::serialize_reply(HAILO_SUCCESS, max_context_capacity),
        LLMGetMaxContextCapacitySerializer);
    return reply;
}

Expected<Buffer> LLMServer::handle_generator_release_request(const MemoryView &request)
{
    CHECK_SUCCESS_AS_HRPC_STATUS(LLMGeneratorReleaseSerializer::deserialize_request(request), LLMGeneratorReleaseSerializer);
    TRY_AS_HRPC_STATUS(auto reply, LLMGeneratorReleaseSerializer::serialize_reply(HAILO_SUCCESS),
        LLMGeneratorReleaseSerializer);
    return reply;
}

hailo_status LLMServer::process_prefill_inputs_chunk(std::map<std::string, MemoryView> &prefill_inputs,
    std::map<std::string, MemoryView> &prefill_outputs, const std::vector<MemoryView> &input_embeddings)
{
    LOGGER__GENAI_STATS_START("[llm-generate-prefill] pre process");
    CHECK_SUCCESS(m_pre_process->prepare_inputs_prefill(prefill_inputs, input_embeddings));
    LOGGER__GENAI_STATS_END("[llm-generate-prefill] pre process");

    LOGGER__GENAI_STATS_START("[llm-generate-prefill] update cache offset");
    CHECK_SUCCESS(m_inference_manager_prefill->update_cache_offset(static_cast<int32_t>(input_embeddings.size())));
    LOGGER__GENAI_STATS_END("[llm-generate-prefill] update cache offset");

    LOGGER__GENAI_STATS_START("[llm-generate-prefill] hw-inference prefill");
    CHECK_SUCCESS(m_inference_manager_prefill->generate(prefill_inputs, prefill_outputs));
    LOGGER__GENAI_STATS_END("[llm-generate-prefill] hw-inference prefill");

    return HAILO_SUCCESS;
}

Expected<int> LLMServer::get_next_token_prefill(std::map<std::string, MemoryView> &prefill_inputs,
    std::map<std::string, MemoryView> &prefill_outputs, const std::vector<MemoryView> &input_embeddings,
    const LLMGeneratorParams &params)
{
    size_t num_full_chunks = input_embeddings.size() / m_pre_process_params.prefill_input_tokens_count;
    size_t remainder_size = input_embeddings.size() % m_pre_process_params.prefill_input_tokens_count;
    // Process the remainder first, if any
    if (remainder_size > 0) {
        std::vector<MemoryView> first_prefill_embeddings(input_embeddings.begin(), input_embeddings.begin() + remainder_size);
        CHECK_SUCCESS(process_prefill_inputs_chunk(prefill_inputs, prefill_outputs, first_prefill_embeddings));
    }

    // Process full prefill chunks
    size_t offset = remainder_size;
    for (size_t i = 0; i < num_full_chunks; ++i) {
        std::vector<MemoryView> input_embeddings_chunk(input_embeddings.begin() + offset, input_embeddings.begin() + offset + m_pre_process_params.prefill_input_tokens_count);
        CHECK_SUCCESS(process_prefill_inputs_chunk(prefill_inputs, prefill_outputs, input_embeddings_chunk));
        offset += input_embeddings_chunk.size();
    }

    LOGGER__GENAI_STATS_START("[llm-generate-prefill] post process");
    auto next_token = m_post_process.get_next_token(prefill_outputs.begin()->second, m_tokens_history, params);
    LOGGER__GENAI_STATS_END("[llm-generate-prefill] post process");

    return next_token;
}

void LLMServer::reset_cnversation_context()
{
    LOGGER__INFO("Resetting conversation context");

    m_tokens_history.clear();
    m_pre_process->reset_local_cache();
    m_post_process.reset_random_generator();
    m_inference_manager_prefill->init_cache(0); // TODO (HRT-16833): Check if required

    prepare_for_new_generation();
}

bool LLMServer::check_stop_sequences(int latest_token)
{
    if (m_tokenized_stop_sequences.empty()) {
        return false;
    }

    // Add the latest token to our ordered sequence
    m_recent_tokens_sequence.push_back(latest_token);

    // Keep the sequence size manageable
    if (m_recent_tokens_sequence.size() > MAX_STOP_SEQUENCE_LENGTH) {
        m_recent_tokens_sequence.pop_front();
    }
    // Check each stop sequence against the end of our recent tokens
    for (const auto &stop_sequence : m_tokenized_stop_sequences) {
        if (stop_sequence.empty() || stop_sequence.size() > m_recent_tokens_sequence.size()) {
            continue;
        }
        // Check if the stop sequence matches the end of our recent tokens
        bool matches = true;
        size_t seq_len = stop_sequence.size();
        size_t recent_len = m_recent_tokens_sequence.size();
        for (size_t i = 0; i < seq_len; ++i) {
            if (m_recent_tokens_sequence[recent_len - seq_len + i] != stop_sequence[i]) {
                matches = false;
                break;
            }
        }
        if (matches) {
            LOGGER__INFO("Custom stop sequence matched: {} tokens ending with {}", seq_len, latest_token);
            return true;
        }
    }
    return false;
}

Expected<std::pair<int, LLMGeneratorCompletion::Status>> LLMServer::generate_next_token_on_demand(const std::vector<int> &tokens,
    const std::vector<MemoryView> &embeddings)
{
    // This method assumes m_generation_mutex is already held by the caller

    assert(!tokens.empty());
    assert(tokens.size() == embeddings.size());
    if ((tokens.size() == 1) && m_inference_manager_tbt) {
        return handle_tbt_phase(embeddings);
    } else {
        return handle_prefill_phase(tokens, embeddings);
    }
}

std::string LLMServer::handle_next_token(int next_token)
{
    m_tokens_history.insert(next_token);

    // For reasoning models, track tokens generated after reasoning (not including reasoning tokens)
    if (m_reasoning.generation_with_reasoning) {
        handle_reasoning_token_tracking(next_token);
    }

    if (m_tokenizer) {
        auto next_tokn_str = m_tokenizer->tokens_to_text({next_token}, true);
        if (next_tokn_str) {
            LOGGER__INFO("Next token: '{}'", next_tokn_str.value());
            return next_tokn_str.value();
        }
    }

    return "";
}

Expected<std::pair<int, LLMGeneratorCompletion::Status>> LLMServer::handle_prefill_phase(const std::vector<int> &tokens,
    const std::vector<MemoryView> &embeddings)
{
    int next_token = INVALID_TOKEN_VALUE;

    // Perform prefill phase - process all input embeddings

    // Check if this is a reasoining generation only on first token generation
    if (0 == m_generated_token_count) {
        m_reasoning.generation_with_reasoning = contains(tokens, m_reasoning.start_token_id);
    }

    if (m_reasoning.generation_with_reasoning) {
        // Handle reasoning model prefill
        auto start_of_reasoning_token_iter = std::find(tokens.begin(),
            tokens.end(), m_reasoning.start_token_id);
        auto start_of_reasoning_token_index = std::distance(tokens.begin(), start_of_reasoning_token_iter);
        std::vector<MemoryView> before_reasoning_embeddings(embeddings.begin(),
            embeddings.begin() + start_of_reasoning_token_index);
        std::vector<MemoryView> after_reasoning_embeddings(embeddings.begin() + start_of_reasoning_token_index + 1,
            embeddings.end());

        // Generate tokens before reasoning (without exporting)
        TRY(auto ignored_next_token, get_next_token_prefill(m_prefill_inputs, m_prefill_outputs,
            before_reasoning_embeddings, m_current_generation_params));
        (void)ignored_next_token;

        // Save cache context before reasoning for later recovery
        LOGGER__GENAI_STATS_START("[llm-generate-reasoning] get local cache for reasoning");
        m_reasoning.tokens_history_before_reasoning = m_tokens_history;
        m_reasoning.pre_process_cache_before_reasoning = m_pre_process->get_local_cache();
        // Track how many tokens we'll process in reasoning section for accurate rollback
        m_reasoning.reasoning_section_token_count = after_reasoning_embeddings.size();
        LOGGER__GENAI_STATS_END("[llm-generate-reasoning] get local cache for reasoning");

        // Generate tokens after reasoning
        TRY(next_token, get_next_token_prefill(m_prefill_inputs, m_prefill_outputs,
            after_reasoning_embeddings, m_current_generation_params));

        // Initialize after_reasoning_tokens collection and state
        m_reasoning.after_reasoning_tokens.clear();
        m_reasoning.past_end_of_reasoning = false;  // Reset for this generation
    } else {
        // Non-reasoning model prefill
        TRY(next_token, get_next_token_prefill(m_prefill_inputs, m_prefill_outputs,
            embeddings, m_current_generation_params));
    }

    m_generated_token_count++;

    auto generation_status = get_current_generation_status(next_token);
    if (generation_status != LLMGeneratorCompletion::Status::GENERATING) {
        // Handle end of generation - may return GENERATING if recovery tokens need delivery
        TRY(generation_status, handle_generation_completion(generation_status, next_token));
    }

    return std::make_pair(next_token, generation_status);
}

LLMGeneratorCompletion::Status LLMServer::get_current_generation_status(int next_token)
{
    if (m_generated_token_count >= m_current_generation_params.max_generated_tokens()) {
        return LLMGeneratorCompletion::Status::MAX_TOKENS_REACHED;
    } else if (check_stop_sequences(next_token)) {
        return LLMGeneratorCompletion::Status::LOGICAL_END_OF_GENERATION;
    } else if (m_abort_requested) {
        return LLMGeneratorCompletion::Status::ABORTED;
    }
    return LLMGeneratorCompletion::Status::GENERATING;
}

Expected<std::pair<int, LLMGeneratorCompletion::Status>> LLMServer::handle_tbt_phase(const std::vector<MemoryView> &embeddings)
{
    assert(1 == embeddings.size());
    assert(m_inference_manager_tbt);

    std::map<std::string, MemoryView> tbt_inputs = m_tbt_inputs;
    std::map<std::string, MemoryView> tbt_outputs = m_tbt_outputs;

    // TBT preprocessing (restore missing logic from old tbt_generation_loop)
    LOGGER__GENAI_STATS_START("[llm-generate-tbt] pre process");
    CHECK_SUCCESS(m_pre_process->prepare_inputs_tbt(tbt_inputs, embeddings));
    LOGGER__GENAI_STATS_END("[llm-generate-tbt] pre process");

    LOGGER__GENAI_STATS_START("[llm-generate-tbt] update cache offset");
    CHECK_SUCCESS(m_inference_manager_tbt->update_cache_offset(1));
    LOGGER__GENAI_STATS_END("[llm-generate-tbt] update cache offset");

    LOGGER__GENAI_STATS_START("[llm-generate-tbt] hw-inference tbt");
    CHECK_SUCCESS(m_inference_manager_tbt->generate(tbt_inputs, tbt_outputs));
    LOGGER__GENAI_STATS_END("[llm-generate-tbt] hw-inference tbt");

    LOGGER__GENAI_STATS_START("[llm-generate-tbt] post process");
    int next_token = m_post_process.get_next_token(tbt_outputs.begin()->second,
        m_tokens_history, m_current_generation_params);
    LOGGER__GENAI_STATS_END("[llm-generate-tbt] post process");

    m_generated_token_count++;

    auto generation_status = get_current_generation_status(next_token);
    if (generation_status != LLMGeneratorCompletion::Status::GENERATING) {
        // Handle end of generation - may return GENERATING if recovery tokens need delivery
        TRY(generation_status, handle_generation_completion(generation_status, next_token));
    }

    return std::make_pair(next_token, generation_status);
}

void LLMServer::append_tokens_to_next_generation_prefix(const std::vector<int> &tokens_to_prepend)
{
    if (tokens_to_prepend.empty()) {
        return;
    }

    std::vector<int> new_prefix;
    new_prefix.reserve(tokens_to_prepend.size() + m_next_input_prompt_prefix_tokens.size());
    new_prefix.insert(new_prefix.end(), m_next_input_prompt_prefix_tokens.begin(), m_next_input_prompt_prefix_tokens.end());
    new_prefix.insert(new_prefix.end(), tokens_to_prepend.begin(), tokens_to_prepend.end());
    m_next_input_prompt_prefix_tokens = std::move(new_prefix);
}

hailo_status LLMServer::apply_recovery_sequence(LLMGeneratorCompletion::Status termination_status)
{
    if (!m_recovery.tokens.empty()) {
        // Queue recovery tokens for delivery and store the termination status
        m_recovery.termination_status = termination_status;
    }
    for (int token_id : m_recovery.tokens) {
        m_recovery.delivery_queue.push(token_id);
    }

    // Add recovery tokens to prefix for next generation
    append_tokens_to_next_generation_prefix(m_recovery.tokens);

    return HAILO_SUCCESS;
}

Expected<LLMGeneratorCompletion::Status> LLMServer::handle_generation_completion(LLMGeneratorCompletion::Status completion_status, int next_token)
{
    if (m_tokenizer) {
        m_tokenizer->clear_buffered_tokens();
    }

    // For reasoning models, handle cache restoration and after-reasoning tokens
    if (m_reasoning.generation_with_reasoning) {
        CHECK_SUCCESS(handle_reasoning_generation_completion());
    }

    // since this is not in cache yet, we need to append the current token to the next generation prefix
    append_tokens_to_next_generation_prefix({next_token});

    // After finishing a generation, reset the recent tokens sequence to avoid false stop-token matches
    m_recent_tokens_sequence.clear();

    // Apply recovery sequence for ungraceful endings (MAX_TOKENS_REACHED or ABORTED)
    bool is_ungraceful_ending = (completion_status == LLMGeneratorCompletion::Status::ABORTED) ||
                               (completion_status == LLMGeneratorCompletion::Status::MAX_TOKENS_REACHED);

    if (is_ungraceful_ending) {
        CHECK_SUCCESS(apply_recovery_sequence(completion_status));
        if (!m_recovery.tokens.empty()) {
            // Recovery tokens will be delivered first, so return GENERATING for now
            LOGGER__INFO("Applied recovery sequence for ungraceful ending");
            return LLMGeneratorCompletion::Status::GENERATING;
        }
    }

    return completion_status;
}

void LLMServer::handle_reasoning_token_tracking(int next_token)
{
    if (m_reasoning.end_token_id != INVALID_TOKEN_VALUE) {
        if (m_reasoning.end_token_id == next_token) {
            // We just hit the end-of-reasoning token, start collecting after-reasoning tokens
            m_reasoning.past_end_of_reasoning = true;
        } else if (m_reasoning.past_end_of_reasoning) {
            // We're past the end-of-reasoning token, collect this token
            m_reasoning.after_reasoning_tokens.push_back(next_token);
        }
    }
}

hailo_status LLMServer::handle_reasoning_generation_completion()
{
    LOGGER__GENAI_STATS_START("[llm-generate-reasoning] set local cache for reasoning");
    m_tokens_history = m_reasoning.tokens_history_before_reasoning;
    m_pre_process->set_local_cache(std::get<0>(m_reasoning.pre_process_cache_before_reasoning),
        std::get<1>(m_reasoning.pre_process_cache_before_reasoning),
        std::get<2>(m_reasoning.pre_process_cache_before_reasoning),
        std::get<3>(m_reasoning.pre_process_cache_before_reasoning));
    LOGGER__GENAI_STATS_END("[llm-generate-reasoning] set local cache for reasoning");

    LOGGER__GENAI_STATS_START("[llm-generate-reasoning] update cache offset for reasoning");
    // Roll back by: reasoning tokens + generated tokens
    const auto total_rollback = m_reasoning.reasoning_section_token_count + m_generated_token_count;
    CHECK_SUCCESS(m_inference_manager_prefill->update_cache_offset(-static_cast<int32_t>(total_rollback)));
    LOGGER__GENAI_STATS_END("[llm-generate-reasoning] update cache offset for reasoning");

    // Directly prepend tokens instead of converting to text and back to tokens
    append_tokens_to_next_generation_prefix(m_reasoning.after_reasoning_tokens);

    m_reasoning.clear();

    return HAILO_SUCCESS;
}

Expected<Buffer> LLMServer::get_context() const
{
    TRY(auto cache_buffers, m_inference_manager_prefill->get_cache_buffers());
    auto [cache_usage_size, cached_embeddings, cached_pos_ids, timestamp_value] = m_pre_process->get_local_cache();

    // Calculate the required payload size
    TRY(auto payload_size, LLMContextUtils::calculate_payload_size(cached_embeddings, cached_pos_ids,
        m_tokens_history, m_next_input_prompt_prefix_tokens, cache_buffers));

    // Create buffer with header + dynamic payload
    TRY(auto context_buffer, Buffer::create(sizeof(LLMContextHeader) + payload_size, BufferStorageParams::create_dma()));

    // Create and set header
    TRY(auto header, LLMContextUtils::create_header(m_inference_manager_prefill->get_hef(), payload_size));
    std::memcpy(context_buffer.data(), &header, sizeof(LLMContextHeader));

    // Serialize payload data
    CHECK_SUCCESS(LLMContextUtils::serialize_payload(context_buffer.from(sizeof(LLMContextHeader)),
        cache_usage_size, cached_embeddings, cached_pos_ids, timestamp_value, m_tokens_history,
        m_next_input_prompt_prefix_tokens, cache_buffers));

    return context_buffer;
}

hailo_status LLMServer::set_context(const MemoryView &context_buffer)
{
    if (context_buffer.size() < sizeof(LLMContextHeader)) {
        return HAILO_INVALID_ARGUMENT;
    }

    const LLMContextHeader *header = reinterpret_cast<const LLMContextHeader*>(context_buffer.data());
    CHECK_SUCCESS(LLMContextUtils::validate_header(*header, m_inference_manager_prefill->get_hef()));

    if (context_buffer.size() < sizeof(LLMContextHeader) + header->payload_size) {
        return HAILO_INVALID_ARGUMENT;
    }

    // Deserialize payload data
    auto [cache_usage_size, cached_embeddings, cached_pos_ids, timestamp_value_from_cache] = m_pre_process->get_local_cache(); // initializing the eigen arrays properly
    std::unordered_map<uint32_t, MemoryView> cache_buffers;
    uint32_t timestamp_value;

    CHECK_SUCCESS(LLMContextUtils::deserialize_payload(context_buffer.from(sizeof(LLMContextHeader)),
        cache_usage_size, cached_embeddings, cached_pos_ids, timestamp_value, m_tokens_history,
        m_next_input_prompt_prefix_tokens, cache_buffers));

    // Set local cache
    m_pre_process->set_local_cache(cache_usage_size, cached_embeddings, cached_pos_ids, timestamp_value);

    // Update cache buffers
    for (const auto &[cache_id, buffer] : cache_buffers) {
        CHECK_SUCCESS(m_inference_manager_prefill->update_cache_buffer(cache_id, buffer));
    }
    CHECK_SUCCESS(m_inference_manager_prefill->init_cache(static_cast<uint32_t>(cache_usage_size)));

    return HAILO_SUCCESS;
}

Expected<std::unique_ptr<LLMServerManager>> LLMServerManager::create(std::shared_ptr<Session> session, std::shared_ptr<VDeviceManager> vdevice_manager)
{
    auto server = LLMServer::create_unique(session, vdevice_manager);
    CHECK_EXPECTED(server);

    auto ptr = std::make_unique<LLMServerManager>(session, std::move(server.value()));
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY); // Consider returning different status

    return ptr;
}

LLMServerManager::LLMServerManager(std::shared_ptr<Session> session, std::unique_ptr<LLMServer> &&server) :
    GenAIServerManager(session), m_server(std::move(server))
{
    m_dispatcher[HailoGenAIActionID::LLM__CREATE] =
        [&](const MemoryView &request) { return m_server->handle_create_llm_request(request); };
    m_dispatcher[HailoGenAIActionID::LLM__GET_GENERATOR_PARAMS] =
        [&](const MemoryView &request) { return m_server->handle_get_generator_params_request(request); };
    m_dispatcher[HailoGenAIActionID::LLM__GENERATOR_CREATE] =
        [&](const MemoryView &request) { return m_server->handle_create_generator_request(request); };
    m_dispatcher[HailoGenAIActionID::LLM__GENERATOR_WRITE] =
        [&](const MemoryView &request) { return m_server->handle_write_request(request); };
    m_dispatcher[HailoGenAIActionID::LLM__GENERATOR_GENERATE] =
        [&](const MemoryView &request) { return m_server->handle_generate_request(request); };
    m_dispatcher[HailoGenAIActionID::LLM__GENERATOR_READ] =
        [&](const MemoryView &request) { return m_server->handle_read_request(request); };
    m_dispatcher[HailoGenAIActionID::LLM__TOKENIZE] =
        [&](const MemoryView &request) { return m_server->handle_tokenize_request(request); };
    m_dispatcher[HailoGenAIActionID::LLM_CLEAR_CONTEXT] =
        [&](const MemoryView &request) { return m_server->handle_clear_context_request(request); };
    m_dispatcher[HailoGenAIActionID::LLM__GET_CONTEXT] =
        [&](const MemoryView &request) { return m_server->handle_get_context_request(request); };
    m_dispatcher[HailoGenAIActionID::LLM__SET_CONTEXT] =
        [&](const MemoryView &request) { return m_server->handle_set_context_request(request); };
        m_dispatcher[HailoGenAIActionID::LLM_RELEASE] =
        [&](const MemoryView &request) { (void)request; m_server.reset(); return LLMReleaseSerializer::serialize_reply(HAILO_SUCCESS); };
    m_dispatcher[HailoGenAIActionID::LLM__GENERATOR_ABORT] =
        [&](const MemoryView &request) { return m_server->handle_abort_request(request); };
    m_dispatcher[HailoGenAIActionID::LLM__SET_END_OF_GENERATION_SEQUENCE] =
        [&](const MemoryView &request) { return m_server->handle_set_generation_recovery_sequence_request(request); };
    m_dispatcher[HailoGenAIActionID::LLM__GET_END_OF_GENERATION_SEQUENCE] =
        [&](const MemoryView &request) { return m_server->handle_get_generation_recovery_sequence_request(request); };
    m_dispatcher[HailoGenAIActionID::LLM__SET_STOP_TOKENS] =
        [&](const MemoryView &request) { return m_server->handle_set_stop_tokens_request(request); };
    m_dispatcher[HailoGenAIActionID::LLM__GET_STOP_TOKENS] =
        [&](const MemoryView &request) { return m_server->handle_get_stop_tokens_request(request); };
    m_dispatcher[HailoGenAIActionID::LLM__GET_CONTEXT_USAGE_SIZE] =
        [&](const MemoryView &request) { return m_server->handle_get_context_usage_size(request); };
    m_dispatcher[HailoGenAIActionID::LLM__GET_MAX_CONTEXT_CAPACITY] =
        [&](const MemoryView &request) { return m_server->handle_get_max_context_capacity(request); };
    m_dispatcher[HailoGenAIActionID::GENAI__CHECK_HEF_EXISTS] =
        [&](const MemoryView &request) { return handle_check_hef_exists_request(request); };
    m_dispatcher[HailoGenAIActionID::LLM__GENERATOR_RELEASE] =
        [&](const MemoryView &request) { return m_server->handle_generator_release_request(request); };
}