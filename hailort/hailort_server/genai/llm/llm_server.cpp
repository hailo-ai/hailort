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
    if (network_group_count == 2) {
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

Expected<std::unique_ptr<LLMServer>> LLMServer::create_unique(std::shared_ptr<Session> session)
{
    const uint32_t MAX_BACKLOG = 1024;
    TRY(auto shutdown_event, Event::create_shared(Event::State::not_signalled));
    auto generated_tokens_queue_exp = SpscQueue<std::pair<std::string, LLMGeneratorCompletion::Status>>::create(MAX_BACKLOG, shutdown_event, HAILO_INFINITE_TIMEOUT);
    CHECK_EXPECTED(generated_tokens_queue_exp);

    // generation-queue - size is 1 as only 1 generation in parallel is possible
    auto input_prompt_queue_exp = SpscQueue<std::string>::create(1, shutdown_event, HAILO_INFINITE_TIMEOUT);
    CHECK_EXPECTED(input_prompt_queue_exp);

    auto input_prompt_queue_ptr = make_unique_nothrow<SpscQueue<std::string>>(input_prompt_queue_exp.release());
    CHECK_NOT_NULL_AS_EXPECTED(input_prompt_queue_ptr, HAILO_OUT_OF_HOST_MEMORY); // Consider returning different status

    // Init with generation default params, will be overwritten by the params from the HEF
    auto post_process_params = LLMGeneratorParams(LLMServer::DEFAULT_GENERATION_TEMPERATURE, LLMServer::DEFAULT_GENERATION_TOP_P,
        LLMServer::DEFAULT_GENERATION_TOP_K, LLMServer::DEFAULT_GENERATION_FREQ_PENALTY,
        LLMServer::DEFAULT_GENERATION_MAX_GENERATED_TOKENS, LLMServer::DEFAULT_GENERATION_DO_SAMPLE,
        HAILO_RANDOM_SEED);

    auto ptr = std::make_unique<LLMServer>(session, generated_tokens_queue_exp.release(), std::move(input_prompt_queue_ptr), shutdown_event,
        std::move(post_process_params));
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY); // Consider returning different status

    ptr->init_generation_thread();

    return ptr;
}

LLMServer::LLMServer(std::shared_ptr<Session> session, SpscQueue<std::pair<std::string, LLMGeneratorCompletion::Status>> &&generated_tokens_queue,
    std::unique_ptr<SpscQueue<std::string>> &&input_prompt_queue, EventPtr shutdown_event, LLMGeneratorParams &&post_process_params) :
        m_session(SessionWrapper(session)), m_post_process(),
        // TODO (HRT-16824): default post-process params should come from HEF?
        m_post_process_params(std::move(post_process_params)),
        m_aggregated_input_prompt(), m_next_input_prompt_prefix(), m_input_prompt_queue(std::move(input_prompt_queue)),
        m_generated_tokens_queue(std::move(generated_tokens_queue)), m_shutdown_event(shutdown_event), m_state(State::READY)
{}

void LLMServer::init_generation_thread()
{
    m_async_generation_thread = std::thread(&LLMServer::async_generate_into_internal_db, this);
}

LLMServer::~LLMServer()
{
    terminate_generation_thread();
}

void LLMServer::terminate_generation_thread()
{
    m_shutdown_event->signal();
    if (m_async_generation_thread.joinable()) {
        m_async_generation_thread.join();
    }
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
        m_start_of_reasoning_token_id = hailo_config_json["begin_of_reasoning_token_id"].get<int>();
    } else {
        m_start_of_reasoning_token_id = INVALID_TOKEN_VALUE;
    }
    if (hailo_config_json.contains("end_of_reasoning_token_id")) {
        m_end_of_reasoning_token_id = hailo_config_json["end_of_reasoning_token_id"].get<int>();
    } else {
        m_end_of_reasoning_token_id = INVALID_TOKEN_VALUE;
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

    return HAILO_SUCCESS;
}

Expected<Buffer> LLMServer::handle_create_llm_request(const MemoryView &request)
{
    LOGGER__GENAI_STATS_START("[create-llm] create vdevice");
    TRY_AS_HRPC_STATUS(auto tuple, LLMCreateSerializer::deserialize_request(request), LLMCreateSerializer);
    auto &lora_name = std::get<0>(tuple);
    auto hef_path = std::get<1>(tuple);
    auto &group_id = std::get<2>(tuple);
    auto file_size = std::get<3>(tuple);

    auto params = HailoRTDefaults::get_vdevice_params();
    if (!group_id.empty()) {
        params.group_id = group_id.c_str();
    }
    TRY_AS_HRPC_STATUS(auto vdevice, hailort::VDevice::create_shared(params), LLMCreateSerializer);
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

    LOGGER__GENAI_STATS_START("[create-llm] create prefill model");
    auto network_group_names = hef.get_network_groups_names();
    TRY_AS_HRPC_STATUS(auto prefill_model_suffix, get_prefill_model_name_suffix(lora_name, network_group_names.size()), LLMCreateSerializer);
    TRY_AS_HRPC_STATUS(m_inference_manager_prefill, LLMInferenceManager::create(vdevice, hef, prefill_model_suffix),
        LLMCreateSerializer);
    LOGGER__GENAI_STATS_END("[create-llm] create prefill model");

    LOGGER__GENAI_STATS_START("[create-llm] configure prefill model");
    auto model_prefill = m_inference_manager_prefill->get_model();
    for (auto input : model_prefill->inputs()) {
        if (LLMPreProcess::is_positional_embed_layer(input.name())) {
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
    TRY_AS_HRPC_STATUS(auto tbt_model_suffix, get_tbt_model_name_suffix(lora_name, network_group_names.size()), LLMCreateSerializer);
    TRY_AS_HRPC_STATUS(m_inference_manager_tbt, LLMInferenceManager::create(vdevice, hef, tbt_model_suffix),
        LLMCreateSerializer);
    LOGGER__GENAI_STATS_END("[create-llm] create tbt model");

    LOGGER__GENAI_STATS_START("[create-llm] configure tbt model");
    auto model_tbt = m_inference_manager_tbt->get_model();
    for (auto input : model_tbt->inputs()) {
        if (LLMPreProcess::is_positional_embed_layer(input.name())) {
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
    LOGGER__GENAI_STATS_END("[create-llm] configure tbt model");

    LOGGER__GENAI_STATS_START("[create-llm] parse GenAI resources");
    TRY_AS_HRPC_STATUS(auto external_resources, m_inference_manager_tbt->get_hef().get_external_resources(),
        LLMCreateSerializer);

    CHECK_AS_HRPC_STATUS(contains(external_resources, HAILO_CONFIG_JSON), HAILO_INVALID_ARGUMENT, LLMCreateSerializer);
    CHECK_SUCCESS_AS_HRPC_STATUS(parse_config_json(external_resources.at(HAILO_CONFIG_JSON)), LLMCreateSerializer);

    // TODO: HRT-16646 - Remove default flow, always get the theta from hef
    auto theta = contains(external_resources, THETA) ?
        LLMPreProcess::generate_theta_from_memview(external_resources.at(THETA)) : LLMPreProcess::generate_default_theta();
    LOGGER__GENAI_STATS_END("[create-llm] parse GenAI resources");

    LOGGER__GENAI_STATS_START("[create-llm] create PreProcess");
    auto prefill_inputs_frame_size = m_inference_manager_prefill->get_inputs_frame_size();
    auto tbt_inputs_frame_size = m_inference_manager_tbt->get_inputs_frame_size();

    // Extract embeddings layer info
    // TODO: HRT-16646 - Add this to embeddings binary format in hef
    TRY_AS_HRPC_STATUS(auto embeddings_input_name, get_layer_name_from_suffix<size_t>(INPUT_LAYER_EMBEDDINGS_SUFF, prefill_inputs_frame_size),
        LLMCreateSerializer);
    TRY_AS_HRPC_STATUS(auto embeddings_input, m_inference_manager_prefill->get_model()->input(embeddings_input_name),
        LLMCreateSerializer);
    auto embeddings_features = embeddings_input.shape().features;
    auto embeddings_dtype = embeddings_input.format().type;

    CHECK_AS_HRPC_STATUS(contains(external_resources, INPUT_EMB_BINARY), HAILO_INVALID_ARGUMENT, LLMCreateSerializer);
    TRY_AS_HRPC_STATUS(m_pre_process, LLMPreProcess::create(external_resources.at(INPUT_EMB_BINARY), prefill_inputs_frame_size, tbt_inputs_frame_size,
        std::move(theta), embeddings_features, embeddings_dtype), LLMCreateSerializer);
    LOGGER__GENAI_STATS_END("[create-llm] create PreProcess");

    LOGGER__GENAI_STATS_START("[create-llm] create tokenizer");
    CHECK_AS_HRPC_STATUS(contains(external_resources, TOKENIZER), HAILO_INVALID_ARGUMENT, LLMCreateSerializer);

    // HRT-16824 - HailoTokenizer should get memview in the c'tor and convert to string if neccesary inside
    std::string tokenizer_blob(external_resources.at(TOKENIZER).size(), '\0');
    std::memcpy(const_cast<char*>(tokenizer_blob.data()), external_resources[TOKENIZER].data(), tokenizer_blob.size());
    TRY_AS_HRPC_STATUS(m_tokenizer, HailoTokenizer::create(tokenizer_blob),
        LLMCreateSerializer);
    LOGGER__GENAI_STATS_END("[create-llm] create tokenizer");

    TRY_AS_HRPC_STATUS(m_generation_recovery_sequence, m_tokenizer->tokens_to_text({m_end_of_sentence_token_id}, true), LLMCreateSerializer);
    LOGGER__INFO("Default generation recovery sequence: '{}'", m_generation_recovery_sequence);

    // Populate custom stop tokens after tokenizer is created
    for (auto stop_token : m_tokenized_stop_sequences) {
        TRY_AS_HRPC_STATUS(auto stop_token_str, m_tokenizer->tokens_to_text(stop_token), LLMCreateSerializer);
        m_stop_tokens.push_back(stop_token_str);
        LOGGER__INFO("Defualt Stop token: '{}'", stop_token_str);
    }

    TRY_AS_HRPC_STATUS(auto reply, LLMCreateSerializer::serialize_reply(HAILO_SUCCESS, m_chat_template), LLMCreateSerializer);
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
    std::unique_lock<std::mutex> lock(m_generation_mutex);
    TRY_AS_HRPC_STATUS(auto prompt_buffer, m_session.read(), LLMGeneratorWriteSerializer);
    m_aggregated_input_prompt += prompt_buffer->to_string();
    LOGGER__INFO("Write request received. updated input prompt - '{}'", m_aggregated_input_prompt);
    TRY_AS_HRPC_STATUS(auto generator_write_reply, LLMGeneratorWriteSerializer::serialize_reply(HAILO_SUCCESS), LLMGeneratorWriteSerializer);
    return generator_write_reply;
}

Expected<Buffer> LLMServer::handle_generate_request(const MemoryView &request)
{
    CHECK_SUCCESS_AS_HRPC_STATUS(LLMGeneratorGenerateSerializer::deserialize_request(request), LLMGeneratorGenerateSerializer);
    std::unique_lock<std::mutex> lock(m_generation_mutex);
    CHECK_AS_HRPC_STATUS(m_state == State::READY, HAILO_INVALID_OPERATION, LLMGeneratorGenerateSerializer);
    m_state = State::GENERATING;
    CHECK_AS_HRPC_STATUS(!m_aggregated_input_prompt.empty(), HAILO_INVALID_OPERATION, LLMGeneratorGenerateSerializer);
    LOGGER__INFO("Generate request received. updated prompt - '{}'", m_next_input_prompt_prefix + m_aggregated_input_prompt);
    m_input_prompt_queue->enqueue(m_next_input_prompt_prefix + m_aggregated_input_prompt);
    m_aggregated_input_prompt = "";
    m_next_input_prompt_prefix = "";
    TRY_AS_HRPC_STATUS(auto generator_generate_reply, LLMGeneratorGenerateSerializer::serialize_reply(HAILO_SUCCESS),
        LLMGeneratorGenerateSerializer);
    return generator_generate_reply;
}

Expected<Buffer> LLMServer::handle_read_request(const MemoryView &request)
{
    TRY_AS_HRPC_STATUS(auto timeout, LLMGeneratorReadSerializer::deserialize_request(request),
        LLMGeneratorReadSerializer);
    TRY_AS_HRPC_STATUS(auto pair, m_generated_tokens_queue.dequeue(timeout),
        LLMGeneratorReadSerializer);
    auto &next_tokn_str = pair.first;
    auto generation_status = pair.second;

    TRY_AS_HRPC_STATUS(auto generator_read_reply, LLMGeneratorReadSerializer::serialize_reply(HAILO_SUCCESS, next_tokn_str, generation_status),
        LLMGeneratorReadSerializer);
    return generator_read_reply;
}

Expected<Buffer> LLMServer::handle_tokenize_request(const MemoryView &request)
{
    TRY_AS_HRPC_STATUS(auto prompt, LLMTokenizeSerializer::deserialize_request(request),
        LLMTokenizeSerializer);
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
    CHECK_AS_HRPC_STATUS(m_state == State::READY, HAILO_INVALID_OPERATION, LLMClearContextSerializer);
    reset_cnversation_context();
    TRY_AS_HRPC_STATUS(auto reply, LLMClearContextSerializer::serialize_reply(HAILO_SUCCESS),
        LLMClearContextSerializer);
    return reply;
}

Expected<Buffer> LLMServer::handle_abort_request(const MemoryView &request)
{
    CHECK_SUCCESS_AS_HRPC_STATUS(LLMGeneratorAbortSerializer::deserialize_request(request), LLMGeneratorAbortSerializer);
    CHECK_AS_HRPC_STATUS(m_state != State::ERROR, HAILO_INTERNAL_FAILURE, LLMGeneratorAbortSerializer);

    if (State::GENERATING == m_state) {
        m_state = State::ABORTING;

        // Wait for the generation thread to finish abort and return the state to ready
        std::unique_lock<std::mutex> lock(m_generation_mutex);
        CHECK_AS_HRPC_STATUS(m_cv.wait_for(lock, LONG_TIMEOUT, [this] { return (State::READY == m_state) || (State::ERROR == m_state); }),
            HAILO_TIMEOUT, LLMGeneratorAbortSerializer);
        CHECK_AS_HRPC_STATUS(m_state != State::ERROR, HAILO_INTERNAL_FAILURE, LLMGeneratorAbortSerializer);
    }

    TRY_AS_HRPC_STATUS(auto reply, LLMGeneratorAbortSerializer::serialize_reply(HAILO_SUCCESS),
        LLMGeneratorAbortSerializer);
    return reply;
}

Expected<Buffer> LLMServer::handle_set_generation_recovery_sequence_request(const MemoryView &request)
{
    TRY_AS_HRPC_STATUS(m_generation_recovery_sequence, LLMSetEndOfGenerationSequenceSerializer::deserialize_request(request), LLMSetEndOfGenerationSequenceSerializer);
    TRY_AS_HRPC_STATUS(auto reply, LLMSetEndOfGenerationSequenceSerializer::serialize_reply(HAILO_SUCCESS),
        LLMSetEndOfGenerationSequenceSerializer);
    return reply;
}

Expected<Buffer> LLMServer::handle_get_generation_recovery_sequence_request(const MemoryView &request)
{
    CHECK_SUCCESS_AS_HRPC_STATUS(LLMGetEndOfGenerationSequenceSerializer::deserialize_request(request), LLMGetEndOfGenerationSequenceSerializer);
    TRY_AS_HRPC_STATUS(auto reply, LLMGetEndOfGenerationSequenceSerializer::serialize_reply(HAILO_SUCCESS, m_generation_recovery_sequence),
        LLMGetEndOfGenerationSequenceSerializer);
    return reply;
}

Expected<Buffer> LLMServer::handle_set_stop_tokens_request(const MemoryView &request)
{
    TRY_AS_HRPC_STATUS(auto stop_tokens, LLMSetStopTokensSerializer::deserialize_request(request), LLMSetStopTokensSerializer);
    
    // Store the custom stop tokens
    m_stop_tokens = stop_tokens;

    // Tokenize all stop sequences for efficient matching during generation
    m_tokenized_stop_sequences.clear();
    for (const auto &stop_token : stop_tokens) {
        TRY_AS_HRPC_STATUS(auto tokenized_sequence, m_tokenizer->text_to_tokens(stop_token), LLMSetStopTokensSerializer);
        if (!tokenized_sequence.empty()) {
            m_tokenized_stop_sequences.push_back(tokenized_sequence);
        }
    }
    
    TRY_AS_HRPC_STATUS(auto reply, LLMSetStopTokensSerializer::serialize_reply(HAILO_SUCCESS), LLMSetStopTokensSerializer);
    return reply;
}

Expected<Buffer> LLMServer::handle_get_stop_tokens_request(const MemoryView &request)
{
    CHECK_SUCCESS_AS_HRPC_STATUS(LLMGetStopTokensSerializer::deserialize_request(request), LLMGetStopTokensSerializer);
    TRY_AS_HRPC_STATUS(auto reply, LLMGetStopTokensSerializer::serialize_reply(HAILO_SUCCESS, m_stop_tokens),
        LLMGetStopTokensSerializer);
    return reply;
}

Expected<Buffer> LLMServer::handle_generator_release_request(const MemoryView &request)
{
    CHECK_SUCCESS_AS_HRPC_STATUS(LLMGeneratorReleaseSerializer::deserialize_request(request), LLMGeneratorReleaseSerializer);
    m_aggregated_input_prompt = "";
    TRY_AS_HRPC_STATUS(auto reply, LLMGeneratorReleaseSerializer::serialize_reply(HAILO_SUCCESS),
        LLMGeneratorReleaseSerializer);
    return reply;
}

hailo_status LLMServer::process_prefill_inputs_chunk(std::map<std::string, MemoryView> &prefill_inputs,
    std::map<std::string, MemoryView> &prefill_outputs, std::vector<int> &input_tokens)
{
    LOGGER__GENAI_STATS_START("[llm-generate-prefill] pre process");
    CHECK_SUCCESS(m_pre_process->prepare_inputs_prefill(prefill_inputs, input_tokens));
    LOGGER__GENAI_STATS_END("[llm-generate-prefill] pre process");

    LOGGER__GENAI_STATS_START("[llm-generate-prefill] update cache offset");
    CHECK_SUCCESS(m_inference_manager_prefill->update_cache_offset(static_cast<int32_t>(input_tokens.size())));
    LOGGER__GENAI_STATS_END("[llm-generate-prefill] update cache offset");

    LOGGER__GENAI_STATS_START("[llm-generate-prefill] hw-inference prefill");
    CHECK_SUCCESS(m_inference_manager_prefill->generate(prefill_inputs, prefill_outputs));
    LOGGER__GENAI_STATS_END("[llm-generate-prefill] hw-inference prefill");

    return HAILO_SUCCESS;
}

Expected<int> LLMServer::get_next_token_prefill(std::map<std::string, MemoryView> &prefill_inputs,
    std::map<std::string, MemoryView> &prefill_outputs, std::vector<int> &input_tokens,
    const LLMGeneratorParams &params)
{
    size_t num_full_chunks = input_tokens.size() / PREFILL_INPUT_TOKENS_SIZE;
    size_t remainder_size = input_tokens.size() % PREFILL_INPUT_TOKENS_SIZE;
    // Process the remainder first, if any
    if (remainder_size > 0) {
        std::vector<int> first_prefill_tokens(input_tokens.begin(), input_tokens.begin() + remainder_size);
        CHECK_SUCCESS(process_prefill_inputs_chunk(prefill_inputs, prefill_outputs, first_prefill_tokens));
    }

    // Process full prefill chunks
    size_t offset = remainder_size;
    for (size_t i = 0; i < num_full_chunks; ++i) {
        std::vector<int> input_tokens_chunk(input_tokens.begin() + offset, input_tokens.begin() + offset + PREFILL_INPUT_TOKENS_SIZE);
        CHECK_SUCCESS(process_prefill_inputs_chunk(prefill_inputs, prefill_outputs, input_tokens_chunk));
        offset += input_tokens_chunk.size();
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
    m_recent_tokens_sequence.clear();
    m_pre_process->reset_local_cache();
    m_post_process.reset_random_generator();
    m_inference_manager_prefill->init_cache(0); // TODO (HRT-16833): Check if required
    m_aggregated_input_prompt = "";
    m_next_input_prompt_prefix = "";
    m_tokenizer->clear_buffered_tokens();
}

hailo_status LLMServer::handle_generation_completion(LLMGeneratorCompletion::Status final_generation_status)
{
    // Only apply recovery sequence for ungraceful generation endings (not LOGICAL_END_OF_GENERATION)
    bool is_ungraceful_ending = (final_generation_status == LLMGeneratorCompletion::Status::ABORTED) ||
                               (final_generation_status == LLMGeneratorCompletion::Status::MAX_TOKENS_REACHED);

    if (is_ungraceful_ending) {
        TRY(auto recovery_sequence_tokens, m_tokenizer->text_to_tokens(m_generation_recovery_sequence));
        if (recovery_sequence_tokens.empty()) {
            CHECK_SUCCESS(m_generated_tokens_queue.enqueue({"", final_generation_status}));
        } else {
            for (size_t i = 0; i < recovery_sequence_tokens.size(); ++i) {
                auto status = (i + 1 == recovery_sequence_tokens.size()) ?
                    final_generation_status :
                    LLMGeneratorCompletion::Status::GENERATING;
                CHECK_SUCCESS(handle_next_token(recovery_sequence_tokens[i], status));
            }
        }
        // After ungraceful ending, use recovery sequence as prefix to make the model usable again
        m_next_input_prompt_prefix = m_generation_recovery_sequence;
    } else {
        // For graceful ending (LOGICAL_END_OF_GENERATION), just signal completion without recovery sequence
        CHECK_SUCCESS(m_generated_tokens_queue.enqueue({"", final_generation_status}));
    }

    // After finishing a generation, we want to reset 'm_recent_tokens_sequence' to avoid false stop-token matches
    m_recent_tokens_sequence.clear();

    return HAILO_SUCCESS;
}

void LLMServer::flush_internal_queues()
{
    // Drop all generated tokens, and un-generated writes
    m_generated_tokens_queue.clear();
    if (m_input_prompt_queue) {
        m_input_prompt_queue->clear();
    }
    m_aggregated_input_prompt = "";
}

Expected<uint32_t> LLMServer::tbt_generation_loop(std::map<std::string, MemoryView> &tbt_inputs,
    std::map<std::string, MemoryView> &tbt_outputs, int next_token, const LLMGeneratorParams &params)
{
    uint32_t generated_tokens = 1; // The first token is generated at the end of the prefill phase
    auto generation_status = LLMGeneratorCompletion::Status::GENERATING;
    bool reached_end_of_reasoning = false;
    std::vector<int> after_resoning_tokens; // Not including 'm_end_of_reasoning_token_id'

    // Check custom stop sequences for the output of the prefill phase
    if (check_stop_sequences(next_token)) {
        generation_status = LLMGeneratorCompletion::Status::LOGICAL_END_OF_GENERATION;
    }

    while (HAILO_TIMEOUT == m_shutdown_event->wait(std::chrono::milliseconds(0))) {

        if (State::ABORTING == m_state) {
            LOGGER__INFO("Generation aborted by the client");
            flush_internal_queues();
            generation_status = LLMGeneratorCompletion::Status::ABORTED;
            break;
        }

        LOGGER__GENAI_STATS_START("[llm-generate-tbt] pre process");
        CHECK_SUCCESS(m_pre_process->prepare_inputs_tbt(tbt_inputs, next_token));
        LOGGER__GENAI_STATS_END("[llm-generate-tbt] pre process");

        LOGGER__GENAI_STATS_START("[llm-generate-tbt] update cache offset");
        CHECK_SUCCESS(m_inference_manager_tbt->update_cache_offset(1));
        LOGGER__GENAI_STATS_END("[llm-generate-tbt] update cache offset");

        LOGGER__GENAI_STATS_START("[llm-generate-tbt] hw-inference tbt");
        CHECK_SUCCESS(m_inference_manager_tbt->generate(tbt_inputs, tbt_outputs));
        LOGGER__GENAI_STATS_END("[llm-generate-tbt] hw-inference tbt");

        if (LLMGeneratorCompletion::Status::GENERATING != generation_status) {
            break;
        }

        LOGGER__GENAI_STATS_START("[llm-generate-tbt] post process");
        next_token = m_post_process.get_next_token(tbt_outputs.begin()->second, m_tokens_history, params);
        LOGGER__GENAI_STATS_END("[llm-generate-tbt] post process");

        // Check stopping conditions. Actually stop in the next iteration to update kv-cache
        if (params.max_generated_tokens() == ++generated_tokens) {
            generation_status = LLMGeneratorCompletion::Status::MAX_TOKENS_REACHED;
        }

        // Check custom stop sequences
        if (check_stop_sequences(next_token)) {
            generation_status = LLMGeneratorCompletion::Status::LOGICAL_END_OF_GENERATION;
        }

        CHECK_SUCCESS(handle_next_token(next_token, LLMGeneratorCompletion::Status::GENERATING));

        if (reached_end_of_reasoning) {
            after_resoning_tokens.push_back(next_token);
        }
        if (m_end_of_reasoning_token_id == next_token) {
            reached_end_of_reasoning = true;
        }
    }
    CHECK_SUCCESS(handle_generation_completion(generation_status));

    if (INVALID_TOKEN_VALUE != m_start_of_reasoning_token_id) { // This is a reasoning model
        // Chain the generated output without resoning to the start of the next input prompt
        TRY(auto after_reasoning_tokens_str, m_tokenizer->tokens_to_text(after_resoning_tokens));
        m_next_input_prompt_prefix = after_reasoning_tokens_str + m_next_input_prompt_prefix;
    }

    return generated_tokens;
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

hailo_status LLMServer::handle_next_token(int next_token, LLMGeneratorCompletion::Status generation_status)
{
    m_tokens_history.insert(next_token);
    auto next_tokn_str = m_tokenizer->tokens_to_text({next_token}, true);
    if (next_tokn_str) {
        LOGGER__INFO("Next token: '{}'", next_tokn_str.value());
        CHECK_SUCCESS(m_generated_tokens_queue.enqueue({next_tokn_str.value(), generation_status}));
    }

    return HAILO_SUCCESS;
}

void LLMServer::async_generate_into_internal_db()
{
    while (HAILO_TIMEOUT == m_shutdown_event->wait(std::chrono::milliseconds(0))) {

        auto input_prompt = m_input_prompt_queue->dequeue();
        std::unique_lock<std::mutex> lock(m_generation_mutex);

        /*
            Create local copy so if user changes generation-params (by creating another generator) it won't affect the current generation
        */
        auto local_post_process_params = m_post_process_params;
        CHECK_SUCCESS_OR_DO_AND_RETURN(input_prompt.status(), m_state = State::ERROR,
            "Failed to dequeue input prompt with status '{}'", input_prompt.status());

        auto input_tokens = m_tokenizer->text_to_tokens(*input_prompt);
        CHECK_SUCCESS_OR_DO_AND_RETURN(input_tokens.status(), m_state = State::ERROR,
            "Failed to tokenize input prompt with status '{}'", input_tokens.status());

        LOGGER__INFO("Parsed {} tokens", input_tokens->size());
        bool contains_reasoning = contains(*input_tokens, m_start_of_reasoning_token_id);
        auto status = HAILO_UNINITIALIZED;

        if (contains_reasoning) {
            status = process_reasoning_model(*input_tokens, local_post_process_params);
        } else {
            status = process_non_reasoning_model(*input_tokens, local_post_process_params);
        }
        CHECK_SUCCESS_OR_DO_AND_RETURN(status, m_state = State::ERROR,
            "Failed to generate tokens with status '{}'", status);

        // Finished current generation, remove potintially buffered-tokens from tokenizer
        m_tokenizer->clear_buffered_tokens();

        m_state = State::READY;
        m_cv.notify_all();
    }
}

hailo_status LLMServer::process_non_reasoning_model(std::vector<int> &input_tokens, const LLMGeneratorParams &local_post_process_params)
{
    TRY(auto next_token, get_next_token_prefill(m_prefill_inputs, m_prefill_outputs, input_tokens, local_post_process_params));
    CHECK_SUCCESS(handle_next_token(next_token, LLMGeneratorCompletion::Status::GENERATING));
    TRY(auto generated_tokens, tbt_generation_loop(m_tbt_inputs, m_tbt_outputs, next_token, local_post_process_params));
    (void)generated_tokens;

    return HAILO_SUCCESS;
}

hailo_status LLMServer::process_reasoning_model(std::vector<int> &input_tokens, const LLMGeneratorParams &local_post_process_params)
{
    assert(contains(input_tokens, m_start_of_reasoning_token_id));
    auto it = std::find(input_tokens.begin(), input_tokens.end(), m_start_of_reasoning_token_id);

    std::vector<int> before_reasoning_tokens(input_tokens.begin(), it); // before m_start_of_reasoning_token_id
    std::vector<int> after_reasoning_tokens(it + 1, input_tokens.end()); // after m_start_of_reasoning_token_id, icluding BOR

    // Generate the tokens before reasoning, without exporting the generated token
    TRY(auto ignored_next_token, get_next_token_prefill(m_prefill_inputs, m_prefill_outputs, before_reasoning_tokens, local_post_process_params));
    (void)ignored_next_token;

    // save cache context from before reasoning
    LOGGER__GENAI_STATS_START("[llm-generate-reasoning] get local cache for reasoning");
    auto tokens_history_before_reasoning = m_tokens_history;
    auto pre_process_cache_before_reasoning = m_pre_process->get_local_cache();
    LOGGER__GENAI_STATS_END("[llm-generate-reasoning] get local cache for reasoning");

    // Generate the tokens after reasoning and continue to TBT
    TRY(auto next_token, get_next_token_prefill(m_prefill_inputs, m_prefill_outputs, after_reasoning_tokens, local_post_process_params));
    CHECK_SUCCESS(handle_next_token(next_token, LLMGeneratorCompletion::Status::GENERATING));
    TRY(auto generated_tokens, tbt_generation_loop(m_tbt_inputs, m_tbt_outputs, next_token, local_post_process_params));

    // restore cache context from before reasoning
    LOGGER__GENAI_STATS_START("[llm-generate-reasoning] set local cache for reasoning");
    m_tokens_history = tokens_history_before_reasoning;
    m_pre_process->set_local_cache(std::get<0>(pre_process_cache_before_reasoning), std::get<1>(pre_process_cache_before_reasoning),
        std::get<2>(pre_process_cache_before_reasoning), std::get<3>(pre_process_cache_before_reasoning));
    LOGGER__GENAI_STATS_END("[llm-generate-reasoning] set local cache for reasoning");

    LOGGER__GENAI_STATS_START("[llm-generate-reasoning] update cache offset for reasoning");
    m_inference_manager_prefill->update_cache_offset(-(generated_tokens + static_cast<int32_t>(after_reasoning_tokens.size())));
    LOGGER__GENAI_STATS_END("[llm-generate-reasoning] update cache offset for reasoning");

    return HAILO_SUCCESS;
}

Expected<std::unique_ptr<LLMServerManager>> LLMServerManager::create(std::shared_ptr<Session> session)
{
    auto server = LLMServer::create_unique(session);
    CHECK_EXPECTED(server);

    auto ptr = std::make_unique<LLMServerManager>(session, std::move(server.value()));
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY); // Consider returning different status

    return ptr;
}

LLMServerManager::LLMServerManager(std::shared_ptr<Session> session, std::unique_ptr<LLMServer> &&server) :
    m_session(session), m_server(std::move(server))
{
    m_dispatcher[static_cast<int>(HailoGenAIActionID::LLM__CREATE)] =
        [&](const MemoryView &request) { return m_server->handle_create_llm_request(request); };
    m_dispatcher[static_cast<int>(HailoGenAIActionID::LLM__GET_GENERATOR_PARAMS)] =
        [&](const MemoryView &request) { return m_server->handle_get_generator_params_request(request); };
    m_dispatcher[static_cast<int>(HailoGenAIActionID::LLM__GENERATOR_CREATE)] =
        [&](const MemoryView &request) { return m_server->handle_create_generator_request(request); };
    m_dispatcher[static_cast<int>(HailoGenAIActionID::LLM__GENERATOR_WRITE)] =
        [&](const MemoryView &request) { return m_server->handle_write_request(request); };
    m_dispatcher[static_cast<int>(HailoGenAIActionID::LLM__GENERATOR_GENERATE)] =
        [&](const MemoryView &request) { return m_server->handle_generate_request(request); };
    m_dispatcher[static_cast<int>(HailoGenAIActionID::LLM__GENERATOR_READ)] =
        [&](const MemoryView &request) { return m_server->handle_read_request(request); };
    m_dispatcher[static_cast<int>(HailoGenAIActionID::LLM__TOKENIZE)] =
        [&](const MemoryView &request) { return m_server->handle_tokenize_request(request); };
    m_dispatcher[static_cast<int>(HailoGenAIActionID::LLM_CLEAR_CONTEXT)] =
        [&](const MemoryView &request) { return m_server->handle_clear_context_request(request); };
    m_dispatcher[static_cast<int>(HailoGenAIActionID::LLM_RELEASE)] =
        [&](const MemoryView &request) { (void)request; m_server.reset(); return LLMReleaseSerializer::serialize_reply(HAILO_SUCCESS); };
    m_dispatcher[static_cast<int>(HailoGenAIActionID::LLM__GENERATOR_ABORT)] =
        [&](const MemoryView &request) { return m_server->handle_abort_request(request); };
    m_dispatcher[static_cast<int>(HailoGenAIActionID::LLM__SET_END_OF_GENERATION_SEQUENCE)] =
        [&](const MemoryView &request) { return m_server->handle_set_generation_recovery_sequence_request(request); };
    m_dispatcher[static_cast<int>(HailoGenAIActionID::LLM__GET_END_OF_GENERATION_SEQUENCE)] =
        [&](const MemoryView &request) { return m_server->handle_get_generation_recovery_sequence_request(request); };
    m_dispatcher[static_cast<int>(HailoGenAIActionID::LLM__SET_STOP_TOKENS)] =
        [&](const MemoryView &request) { return m_server->handle_set_stop_tokens_request(request); };
    m_dispatcher[static_cast<int>(HailoGenAIActionID::LLM__GET_STOP_TOKENS)] =
        [&](const MemoryView &request) { return m_server->handle_get_stop_tokens_request(request); };
    m_dispatcher[static_cast<int>(HailoGenAIActionID::GENAI__CHECK_HEF_EXISTS)] =
        [&](const MemoryView &request) { return handle_check_hef_exists_request(request); };
    m_dispatcher[static_cast<int>(HailoGenAIActionID::LLM__GENERATOR_RELEASE)] =
        [&](const MemoryView &request) { return m_server->handle_generator_release_request(request); };
}

hailo_status LLMServerManager::flow()
{
    while (true) {
        TRY(auto request, m_session.read());
        TRY(auto action_id, GenAISerializerUtils::get_action_id(MemoryView(*request)));
        TRY(auto reply, m_dispatcher[action_id](MemoryView(*request)));
        CHECK_SUCCESS(m_session.write(MemoryView(reply)));
    }

    return HAILO_SUCCESS;
}