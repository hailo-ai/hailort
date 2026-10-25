/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
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
#include "common/genai/constants.hpp"

#include "pre_process_config.hpp"
#include "reasoning.hpp"
#include "utils.hpp"

#include <nlohmann/json.hpp>
#include <future>
#include <utility>


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

Expected<std::string> get_prefill_model_name_suffix(const std::string &lora_name)
{
    // TODO (HRT-18043): Instead of using suffixes, NG names for base model (lora_name is not given) should be well defined in 'hailo-config.json'
    if (lora_name.empty()) {
        return std::string("__prefill");
    }
    return lora_name + std::string("__prefill");
}

Expected<std::string> get_tbt_model_name_suffix(const std::string &lora_name)
{
    // TODO (HRT-18043): Instead of using suffixes, NG names for base model (lora_name is not given) should be well defined in 'hailo-config.json'
    if (lora_name.empty()) {
        return std::string("__tbt");
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
    // Mark before HW teardown so concurrent new connections aren't blocked during the slow reset.
    m_vdevice_manager->mark_vdevice_for_close(DEFAULT_LLM_CONNECTION_PORT);

    m_inference_manager_prefill.reset();
    m_inference_manager_tbt.reset();

    m_vdevice_manager->remove_vdevice(DEFAULT_LLM_CONNECTION_PORT);

    m_kv_cache_guard.reset();
}

hailo_status LLMServer::parse_config_json(const MemoryView &config_json)
{
    auto hailo_config_json = parse_json(config_json);
    return parse_config_json(hailo_config_json, RopeLayout::TextOnly);
}

hailo_status LLMServer::parse_config_json(const nlohmann::json &hailo_config_json, RopeLayout rope_layout)
{
    CHECK(hailo_config_json.contains("eos_token_id"), HAILO_INVALID_ARGUMENT);
    m_end_of_sentence_token_id = hailo_config_json["eos_token_id"].get<int>();

    if (hailo_config_json.contains("bos_token_str") && hailo_config_json["bos_token_str"].is_string()) {
        m_bos_token = hailo_config_json["bos_token_str"].get<std::string>();
    }
    if (hailo_config_json.contains("eos_token_str") && hailo_config_json["eos_token_str"].is_string()) {
        m_eos_token = hailo_config_json["eos_token_str"].get<std::string>();
    }

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
    if (hailo_config_json.contains("tool_call_start_token_id")) {
        m_reasoning.tool_call_start_token_id = hailo_config_json["tool_call_start_token_id"].get<int>();
    } else {
        m_reasoning.tool_call_start_token_id = INVALID_TOKEN_VALUE;
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

    TRY(auto pre_process_config, parse_pre_process_config(hailo_config_json, rope_layout));
    m_input_layers_names_suffixes = std::move(pre_process_config.suffixes);
    m_embedding_tables = std::move(pre_process_config.embedding_tables);
    m_pre_process_params = std::move(pre_process_config.params);
    m_attention_groups = build_attention_groups(m_input_layers_names_suffixes, m_pre_process_params);
    TRY(m_context_capacity, context_capacity(m_attention_groups));

    return HAILO_SUCCESS;
}

static Expected<AttentionMaskValues> resolve_phase_mask_values(const AttentionGroupParams &group, InferModel &model,
    const std::map<std::string, size_t> &inputs_frame_size)
{
    TRY(const auto mask_input_name, get_layer_name_from_suffix<size_t>(group.suffixes.mask, inputs_frame_size));
    TRY(auto mask_input, model.input(mask_input_name));
    const auto mask_quant_infos = mask_input.get_quant_infos();
    CHECK(1 == mask_quant_infos.size(), HAILO_INVALID_ARGUMENT,
        "Attention mask layer '{}' carries {} quantization infos, pre-process needs exactly one",
        mask_input_name, mask_quant_infos.size());
    TRY(auto mask_values, quantize_attention_mask_values(mask_input.format().type, mask_quant_infos[0]),
        "Failed to quantize the mask values of attention mask layer '{}'", mask_input_name);

    return mask_values;
}

Expected<std::vector<AttentionGroupMaskValues>> LLMServer::resolve_group_mask_values(
    const std::map<std::string, size_t> &prefill_inputs_frame_size,
    const std::map<std::string, size_t> &tbt_inputs_frame_size) const
{
    CHECK(nullptr != m_inference_manager_tbt, HAILO_INVALID_ARGUMENT, "{}", MISSING_TBT_NETWORK_ERROR);

    std::vector<AttentionGroupMaskValues> group_mask_values;
    group_mask_values.reserve(m_attention_groups.size());
    for (const auto &group : m_attention_groups) {
        AttentionGroupMaskValues mask_values;
        TRY(mask_values.prefill, resolve_phase_mask_values(group, *m_inference_manager_prefill->get_model(),
            prefill_inputs_frame_size));
        TRY(mask_values.tbt, resolve_phase_mask_values(group, *m_inference_manager_tbt->get_model(),
            tbt_inputs_frame_size));
        group_mask_values.push_back(mask_values);
    }

    return group_mask_values;
}

std::future<hailo_status> LLMServer::create_inference_managers_future(std::shared_ptr<VDevice> vdevice, const Hef &hef,
    const std::string &lora_name, std::shared_ptr<Event> external_resources_created_event,
    std::shared_ptr<Event> inference_models_created_event, StatusEventPtr ccws_ready_event,
    std::shared_ptr<Event> shutdown_event)
{
    return std::async(std::launch::async, [this, vdevice, hef, lora_name, external_resources_created_event,
        inference_models_created_event, ccws_ready_event, shutdown_event]() -> hailo_status {

        LOGGER__GENAI_STATS_START("[create] create prefill model");
        TRY(auto prefill_model_name_suffix, get_prefill_model_name_suffix(lora_name));
        TRY(m_inference_manager_prefill, LLMInferenceManager::create(vdevice, hef, prefill_model_name_suffix));
        CHECK_SUCCESS(WaitOrShutdown(external_resources_created_event, shutdown_event).wait(WAIT_FOR_OPERATION_TIMEOUT));
        m_inference_manager_prefill->set_ccws_ready_event(ccws_ready_event);
        auto model_prefill = m_inference_manager_prefill->get_model();
        for (auto input : model_prefill->inputs()) {
            if (LLMPreProcess::is_positional_embed_layer(input.name(), m_attention_groups) ||
                LLMPreProcess::is_deepstack_layer(input.name(), m_input_layers_names_suffixes)) {
                input.set_format_type(HAILO_FORMAT_TYPE_FLOAT32);
            }
        }
        for (auto output : model_prefill->outputs()) {
            output.set_format_type(HAILO_FORMAT_TYPE_FLOAT32);
        }
        LOGGER__GENAI_STATS_END("[create] create prefill model");

        LOGGER__GENAI_STATS_START("[create] create tbt model");
        m_inference_manager_tbt = nullptr;
        TRY(auto tbt_model_name_suffix, get_tbt_model_name_suffix(lora_name));
        auto inference_manager_tbt = LLMInferenceManager::create(vdevice, hef, tbt_model_name_suffix);
        if (inference_manager_tbt) {
            m_inference_manager_tbt = inference_manager_tbt.release();
            m_inference_manager_tbt->set_ccws_ready_event(ccws_ready_event);
            auto model_tbt = m_inference_manager_tbt->get_model();
            for (auto input : model_tbt->inputs()) {
                if (LLMPreProcess::is_positional_embed_layer(input.name(), m_attention_groups) ||
                    LLMPreProcess::is_deepstack_layer(input.name(), m_input_layers_names_suffixes)) {
                    input.set_format_type(HAILO_FORMAT_TYPE_FLOAT32);
                }
            }
            for (auto output : model_tbt->outputs()) {
                output.set_format_type(HAILO_FORMAT_TYPE_FLOAT32);
            }
        }
        inference_models_created_event->signal();
        LOGGER__GENAI_STATS_END("[create] create tbt model");

        auto configure_prefill_future = std::async(std::launch::async, [this]() -> hailo_status {
            LOGGER__GENAI_STATS_START("[create] configure prefill model");
            TRY(m_prefill_buffers, m_inference_manager_prefill->allocate_buffers());
            m_prefill_inputs = buffers_to_memviews(m_prefill_buffers.first);
            m_prefill_outputs = buffers_to_memviews(m_prefill_buffers.second);
            CHECK_SUCCESS(m_inference_manager_prefill->configure());
            LOGGER__GENAI_STATS_END("[create] configure prefill model");

            return HAILO_SUCCESS;
        });

        if (m_inference_manager_tbt) {
            LOGGER__GENAI_STATS_START("[create] configure tbt model");
            TRY(m_tbt_buffers, m_inference_manager_tbt->allocate_buffers());
            m_tbt_inputs = buffers_to_memviews(m_tbt_buffers.first);
            m_tbt_outputs = buffers_to_memviews(m_tbt_buffers.second);
            CHECK_SUCCESS(m_inference_manager_tbt->configure());
            LOGGER__GENAI_STATS_END("[create] configure tbt model");
        }
        CHECK_SUCCESS(wait_for_future_status_or_shutdown(configure_prefill_future, shutdown_event));

        return HAILO_SUCCESS;
    });
}

static Expected<std::vector<Eigen::VectorXf>> read_group_thetas(const Hef &hef,
    const std::vector<AttentionGroupParams> &attention_groups, ResourceArrivals &resource_arrivals,
    EventPtr shutdown_event)
{
    std::vector<Eigen::VectorXf> group_thetas;
    group_thetas.reserve(attention_groups.size());
    for (size_t group_index = 0; group_index < attention_groups.size(); group_index++) {
        const auto &group = attention_groups[group_index];
        CHECK_SUCCESS(resource_arrivals.wait_for(group.rope_theta, LONG_TIMEOUT, shutdown_event),
            "Rope theta resource '{}' of attention group '{}' did not arrive", group.rope_theta,
            attention_group_name(group, group_index));
        TRY(auto theta_view, hef.get_external_resources(group.rope_theta),
            "Failed to read rope theta resource '{}' of attention group '{}'", group.rope_theta,
            attention_group_name(group, group_index));
        group_thetas.push_back(LLMPreProcess::generate_theta_from_memview(theta_view));
    }

    return group_thetas;
}

std::future<Expected<std::vector<Eigen::VectorXf>>> LLMServer::parse_external_resources_future(const Hef &hef,
    ResourceArrivalsPtr resource_arrivals, std::shared_ptr<Event> external_resources_created_event,
    std::shared_ptr<Event> shutdown_event)
{
    return std::async(std::launch::async, [this, hef, resource_arrivals, external_resources_created_event,
        shutdown_event]() -> Expected<std::vector<Eigen::VectorXf>> {
        CHECK_SUCCESS(resource_arrivals->wait_for(HAILO_CONFIG_JSON, LONG_TIMEOUT, shutdown_event));

        LOGGER__GENAI_STATS_START("[create] parse GenAI resources");
        TRY(auto hailo_config_json_view, hef.get_external_resources(HAILO_CONFIG_JSON));
        CHECK_SUCCESS(parse_config_json(hailo_config_json_view));

        TRY(auto group_thetas, read_group_thetas(hef, m_attention_groups, *resource_arrivals, shutdown_event));
        LOGGER__GENAI_STATS_END("[create] parse GenAI resources");

        external_resources_created_event->signal();
        return group_thetas;
    });
}

std::future<hailo_status> LLMServer::create_tokenizer_future(const Hef &hef, ResourceArrivalsPtr resource_arrivals,
    std::shared_ptr<Event> shutdown_event)
{
    return std::async(std::launch::async, [this, hef, resource_arrivals, shutdown_event]() -> hailo_status {
        CHECK_SUCCESS(resource_arrivals->wait_for(TOKENIZER, LONG_TIMEOUT, shutdown_event));

        LOGGER__GENAI_STATS_START("[create] create tokenizer");
        TRY(auto tokenizer_view, hef.get_external_resources(TOKENIZER));
        TRY(m_tokenizer, HailoTokenizer::create(tokenizer_view));
        LOGGER__GENAI_STATS_END("[create] create tokenizer");

        return HAILO_SUCCESS;
    });
}

std::future<hailo_status> LLMServer::create_pre_process_future(const Hef &hef,
    std::shared_ptr<Event> inference_models_created_event,
    std::future<Expected<std::vector<Eigen::VectorXf>>> &external_resources_future,
    std::shared_ptr<Event> pre_process_created_event, std::shared_ptr<Event> shutdown_event)
{
    return std::async(std::launch::async, [this, hef, inference_models_created_event, &external_resources_future,
        pre_process_created_event, shutdown_event]() -> hailo_status {
        CHECK_SUCCESS(WaitOrShutdown(inference_models_created_event, shutdown_event).wait(WAIT_FOR_OPERATION_TIMEOUT));
        TRY(auto group_thetas, wait_for_future_value(external_resources_future));

        LOGGER__GENAI_STATS_START("[create] create PreProcess");
        auto prefill_inputs_frame_size = m_inference_manager_prefill->get_inputs_frame_size();
        auto tbt_inputs_frame_size = m_inference_manager_tbt ? m_inference_manager_tbt->get_inputs_frame_size() : std::map<std::string, size_t>();

        // Extract embeddings layer info
        // TODO: HRT-16646 - Add this to embeddings binary format in hef
        TRY(auto embeddings_input_name, get_layer_name_from_suffix<size_t>(
            m_embedding_tables.token_embeddings.input_layer_suffix, prefill_inputs_frame_size));
        TRY(auto embeddings_input, m_inference_manager_prefill->get_model()->input(embeddings_input_name));
        m_embeddings_features = embeddings_input.shape().features;
        auto embeddings_dtype = embeddings_input.format().type;

        if (m_embedding_tables.per_layer_inputs) {
            TRY(const auto per_layer_inputs_name, get_layer_name_from_suffix<size_t>(
                m_embedding_tables.per_layer_inputs->input_layer_suffix, prefill_inputs_frame_size));
            TRY(const auto per_layer_inputs_input, m_inference_manager_prefill->get_model()->input(
                per_layer_inputs_name));
            m_per_layer_inputs_features = per_layer_inputs_input.shape().features;
        }

        TRY(const auto group_mask_values, resolve_group_mask_values(prefill_inputs_frame_size, tbt_inputs_frame_size));

        TRY(m_pre_process, LLMPreProcess::create(prefill_inputs_frame_size, tbt_inputs_frame_size, group_thetas,
            m_embeddings_features, embeddings_dtype, m_embedding_tables, m_per_layer_inputs_features,
            group_mask_values, m_input_layers_names_suffixes, m_pre_process_params, m_attention_groups));
        LOGGER__GENAI_STATS_END("[create] create PreProcess");

        pre_process_created_event->signal();
        return HAILO_SUCCESS;
    });
}

hailo_status LLMServer::wait_for_table(ResourceArrivals &resource_arrivals, const EmbeddingTableParams &table,
    const std::string &table_description, EventPtr shutdown_event)
{
    CHECK_SUCCESS(resource_arrivals.wait_for(table.resource, LONG_TIMEOUT, shutdown_event),
        "The {} resource '{}' did not arrive", table_description, table.resource);
    if (table.codebook) {
        CHECK_SUCCESS(resource_arrivals.wait_for(table.codebook->resource, LONG_TIMEOUT, shutdown_event),
            "The codebook '{}' of the {} resource '{}' did not arrive", table.codebook->resource, table_description,
            table.resource);
    }

    return HAILO_SUCCESS;
}

Expected<std::unique_ptr<TokenEmbedder<uint16_t>>> LLMServer::read_table_embedder(const Hef &hef,
    const EmbeddingTableParams &table, uint32_t features, const std::string &table_description,
    int image_pad_token_id, int video_pad_token_id)
{
    TRY(const auto table_view, hef.get_external_resources(table.resource),
        "Failed to read {} resource '{}'", table_description, table.resource);
    MemoryView codebook_view;
    if (table.codebook) {
        TRY(codebook_view, hef.get_external_resources(table.codebook->resource),
            "Failed to read the codebook '{}' of the {} resource '{}'", table.codebook->resource, table_description,
            table.resource);
    }

    return create_table_embedder(table, table_view, codebook_view, features, image_pad_token_id, video_pad_token_id);
}

std::future<hailo_status> LLMServer::create_token_embedder_future(const Hef &hef, ResourceArrivalsPtr resource_arrivals,
    std::shared_ptr<Event> pre_process_created_event, std::shared_ptr<Event> shutdown_event)
{
    return std::async(std::launch::async, [this, hef, resource_arrivals, pre_process_created_event,
        shutdown_event]() -> hailo_status {
        CHECK_SUCCESS(WaitOrShutdown(pre_process_created_event, shutdown_event).wait(WAIT_FOR_OPERATION_TIMEOUT));
        const auto &token_embeddings = m_embedding_tables.token_embeddings;
        CHECK_SUCCESS(wait_for_table(*resource_arrivals, token_embeddings, TOKEN_EMBEDDINGS_TABLE_DESCRIPTION,
            shutdown_event));
        LOGGER__GENAI_STATS_START("[create] create token embedder");
        TRY(m_token_embedder, read_table_embedder(hef, token_embeddings, m_embeddings_features,
            TOKEN_EMBEDDINGS_TABLE_DESCRIPTION));
        LOGGER__GENAI_STATS_END("[create] create token embedder");

        if (m_embedding_tables.per_layer_inputs) {
            const auto &per_layer_inputs = *m_embedding_tables.per_layer_inputs;
            CHECK_SUCCESS(wait_for_table(*resource_arrivals, per_layer_inputs, PER_LAYER_INPUTS_TABLE_DESCRIPTION,
                shutdown_event));
            LOGGER__GENAI_STATS_START("[create] create per-layer inputs embedder");
            TRY(m_per_layer_inputs_embedder, read_table_embedder(hef, per_layer_inputs,
                m_per_layer_inputs_features, PER_LAYER_INPUTS_TABLE_DESCRIPTION));
            LOGGER__GENAI_STATS_END("[create] create per-layer inputs embedder");
        }

        return HAILO_SUCCESS;
    });
}

Expected<std::future<hailo_status>> LLMServer::create_resources_async(std::shared_ptr<VDevice> vdevice, std::shared_ptr<Buffer> hef_buffer, const std::string lora_name,
    bool tokenizer_on_host, HefSource hef_source, ResourceArrivalsPtr resource_arrivals,
    StatusEventPtr ccws_ready_event, std::shared_ptr<Event> shutdown_event)
{
    CHECK((HefSource::Streamed != hef_source) || (nullptr != resource_arrivals), HAILO_INVALID_ARGUMENT,
        "A streamed HEF needs a resource arrivals registry");
    TRY(auto inference_models_created_event, Event::create_shared(Event::State::not_signalled));
    TRY(auto external_resources_created_event, Event::create_shared(Event::State::not_signalled));
    TRY(auto pre_process_created_event, Event::create_shared(Event::State::not_signalled));

    return std::async(std::launch::async, [this, vdevice, hef_buffer, lora_name, tokenizer_on_host, hef_source,
        resource_arrivals, inference_models_created_event, external_resources_created_event, pre_process_created_event,
        ccws_ready_event, shutdown_event]() mutable -> hailo_status {

        // Create HEF
        LOGGER__GENAI_STATS_START("[create] create HEF");
        TRY(auto hef, Hef::create(hef_buffer));
        hef.set_memory_footprint_optimization(true);
        LOGGER__GENAI_STATS_END("[create] create HEF");

        if (HefSource::Local == hef_source) {
            TRY(resource_arrivals, ResourceArrivals::create_all_arrived(hef.get_external_resource_names()));
        }

        // Spawn all async creation tasks
        auto external_resources_future = parse_external_resources_future(hef, resource_arrivals,
            external_resources_created_event, shutdown_event);

        auto inference_managers_future = create_inference_managers_future(vdevice, hef, lora_name,
            external_resources_created_event, inference_models_created_event, ccws_ready_event, shutdown_event);

        auto pre_process_future = create_pre_process_future(hef, inference_models_created_event, external_resources_future,
            pre_process_created_event, shutdown_event);

        if (!tokenizer_on_host) {
            auto tokenizer_future = create_tokenizer_future(hef, resource_arrivals, shutdown_event);
            auto token_embedder_future = create_token_embedder_future(hef,
                resource_arrivals, pre_process_created_event, shutdown_event);
            CHECK_SUCCESS(wait_for_future_status_or_shutdown(tokenizer_future, shutdown_event));
            CHECK_SUCCESS(wait_for_future_status_or_shutdown(token_embedder_future, shutdown_event));
        }

        // Wait for all async tasks to complete
        CHECK_SUCCESS(wait_for_future_status_or_shutdown(pre_process_future, shutdown_event));
        CHECK_SUCCESS(wait_for_future_status_or_shutdown(inference_managers_future, shutdown_event));

        return HAILO_SUCCESS;
    });
}

Expected<Buffer> LLMServer::handle_create_llm_request(const MemoryView &request)
{
    LOGGER__GENAI_STATS_START("[create] create vdevice");
    TRY_AS_HRPC_STATUS(auto request_info, LLMCreateSerializer::deserialize_request(request), LLMCreateSerializer);
    auto &lora_name = request_info.lora_name;
    auto &hef_path = request_info.hef_path;
    auto &group_id = request_info.group_id;
    auto &chunks_to_transfer = request_info.chunks_to_transfer;
    auto &tokenizer_on_host = request_info.tokenizer_on_host;
    auto total_hef_size = request_info.total_hef_size;

    auto params = HailoRTDefaults::get_vdevice_params();
    if (!group_id.empty()) {
        params.group_id = group_id.c_str();
    }
    TRY_AS_HRPC_STATUS(auto vdevice, m_vdevice_manager->create_vdevice_for_genai(params, DEFAULT_LLM_CONNECTION_PORT),
        LLMCreateSerializer);
    LOGGER__GENAI_STATS_END("[create] create vdevice");

    LOGGER__GENAI_STATS_START("[create] transfer HEF");
    std::shared_ptr<Buffer> hef_buffer_ptr;
    std::future<hailo_status> resources_creation_future;
    std::future<hailo_status> ccws_future;
    ResourceArrivalsPtr resource_arrivals;
    HefSource hef_source = HefSource::Local;
    TRY_AS_HRPC_STATUS(auto ccws_ready_event, StatusEvent::create_shared(), LLMCreateSerializer);
    TRY_AS_HRPC_STATUS(auto shutdown_event, Event::create_shared(Event::State::not_signalled), LLMCreateSerializer);

    // On error: unblock async config tasks and free VDevice, before std::async future destructors block.
    // Must be declared after futures so it fires before they block on destruction.
    bool is_error_cleanup_needed = true;
    DEFER_IF({
        auto status = shutdown_event->signal();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to signal shutdown event: {}", status);
        }
        (void)ccws_ready_event->signal_failure(HAILO_SHUTDOWN_EVENT_SIGNALED);
        m_vdevice_manager->mark_vdevice_for_close(DEFAULT_LLM_CONNECTION_PORT);
    }, is_error_cleanup_needed);

    if (!hef_path.empty()) { // hef path is not none only if hef exists locally, so no need to transfer it over the session
        if (BUILTIN == hef_path) {
            TRY_AS_HRPC_STATUS(hef_path, get_path_from_lora_name(lora_name), LLMCreateSerializer);
        }
        TRY_AS_HRPC_STATUS(auto buff, read_binary_file(hef_path, BufferStorageParams::create_dma()), LLMCreateSerializer);
        hef_buffer_ptr = make_shared_nothrow<Buffer>(std::move(buff));
        CHECK_AS_HRPC_STATUS(nullptr != hef_buffer_ptr, HAILO_OUT_OF_HOST_MEMORY, LLMCreateSerializer);

        // For local HEF, create resources immediately in async task
        TRY_AS_HRPC_STATUS(resources_creation_future, create_resources_async(vdevice, hef_buffer_ptr, lora_name,
            tokenizer_on_host, hef_source, nullptr, ccws_ready_event, shutdown_event), LLMCreateSerializer);
        ccws_ready_event->signal_success();
    } else {
        hef_source = HefSource::Streamed;
        // Use total HEF size from the request
        LOGGER__INFO("hef buffer of size '{}', lora: '{}'", total_hef_size, lora_name);
        TRY_AS_HRPC_STATUS(hef_buffer_ptr, Buffer::create_shared(total_hef_size, BufferStorageParams::create_dma()), LLMCreateSerializer);
        TRY_AS_HRPC_STATUS(resource_arrivals, ResourceArrivals::create(get_chunk_names(chunks_to_transfer)),
            LLMCreateSerializer);

        // Receive all chunks synchronously, spawning async creation tasks after key chunks arrive
        for (const auto &chunk : chunks_to_transfer) {
            // Receive all chunks synchronously, except for CCWs
            if (chunk.name != CCWS) {
                CHECK_SUCCESS_AS_HRPC_STATUS(receive_hef_chunk_sync(m_session, chunk, hef_buffer_ptr),
                    LLMCreateSerializer);
                CHECK_SUCCESS_AS_HRPC_STATUS(resource_arrivals->mark_arrived(chunk.name), LLMCreateSerializer);
            } else {
                ccws_future = std::async(std::launch::async,
                    [this, chunk, hef_buffer_ptr, ccws_ready_event, resource_arrivals]() -> hailo_status {
                    LOGGER__INFO("Receiving CCWs chunk '{}' (offset: {}, size: {} bytes) [ASYNC]", chunk.name, chunk.offset, chunk.size);
                    auto status = receive_hef_chunk_sync(m_session, chunk, hef_buffer_ptr);
                    if (HAILO_SUCCESS != status) {
                        const auto signal_status = ccws_ready_event->signal_failure(status);
                        if (HAILO_SUCCESS != signal_status) {
                            LOGGER__ERROR("Failed to signal CCWs ready event, status={}", signal_status);
                        }
                        return status;
                    }
                    CHECK_SUCCESS(ccws_ready_event->signal_success());
                    CHECK_SUCCESS(resource_arrivals->mark_arrived(chunk.name));
                    LOGGER__GENAI_STATS_END("[create] transfer HEF");
                    return HAILO_SUCCESS;
                });
            }
            // After receiving HEADER_PROTO_PADDING, start HEF creation asynchronously
            if (chunk.name == HEADER_PROTO_PADDING) {
                LOGGER__INFO("HEADER_PROTO_PADDING received, starting async resources creation");
                TRY_AS_HRPC_STATUS(resources_creation_future, create_resources_async(vdevice, hef_buffer_ptr, lora_name,
                    tokenizer_on_host, hef_source, resource_arrivals, ccws_ready_event, shutdown_event),
                    LLMCreateSerializer);
            }
        }
    }

    CHECK_SUCCESS_AS_HRPC_STATUS(wait_for_future_status(resources_creation_future), LLMCreateSerializer);
    if (ccws_future.valid()) { // In case HEF exists locally, dont need to wait for CCWs
        CHECK_SUCCESS_AS_HRPC_STATUS(wait_for_future_status(ccws_future), LLMCreateSerializer);
    }
    if (HefSource::Streamed == hef_source) { // Only a transferred HEF is tracked chunk by chunk
        CHECK_SUCCESS_AS_HRPC_STATUS(resource_arrivals->check_all_arrived(), LLMCreateSerializer);
    }

    m_recovery.tokens = {m_end_of_sentence_token_id};

    LLMCreateSerializer::ReplyInfo reply_info {};
    reply_info.prompt_template = m_chat_template;
    reply_info.embedding_features = m_embeddings_features;
    reply_info.bos_token = m_bos_token;
    reply_info.eos_token = m_eos_token;
    reply_info.per_layer_inputs_features = m_per_layer_inputs_features;

    // Success — skip DEFER cleanup (mark_vdevice_for_close will be called in ~LLMServer on normal shutdown)
    is_error_cleanup_needed = false;

    TRY_AS_HRPC_STATUS(auto reply, LLMCreateSerializer::serialize_reply(HAILO_SUCCESS, reply_info),
        LLMCreateSerializer);
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
    TRY_AS_HRPC_STATUS(auto input, LLMGeneratorReadSerializer::deserialize_request(request),
        LLMGeneratorReadSerializer);

    std::unique_lock<std::mutex> lock(m_generation_mutex);
    LLMGeneratorCompletion::Status gen_status = LLMGeneratorCompletion::Status::GENERATING;
    int next_token_id = INVALID_TOKEN_VALUE;

    // Check if there are recovery tokens to deliver one by one
    if (m_recovery.has_pending_tokens()) {
        std::tie(next_token_id, gen_status) = pop_recovery_token(m_recovery.termination_status);
    } else {
        std::vector<EmbeddingViewWrapper> embeddings_views;
        std::vector<EmbeddingViewWrapper> per_layer_inputs_views;
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
            TRY_AS_HRPC_STATUS(embeddings_views, m_token_embedder->tokens_to_embeddings(combined_tokens),
                LLMGeneratorReadSerializer);
            TRY_AS_HRPC_STATUS(per_layer_inputs_views, gather_declared_per_layer_inputs(combined_tokens),
                LLMGeneratorReadSerializer);
        } else if (!input.tokens.empty() && input.embeddings.empty()) {
            // Subsequent iterations OR client-side tokenizer without embeddings
            // Just use tokens as-is (single token for TBT, or combined tokens from client)
            assert(m_token_embedder);
            TRY_AS_HRPC_STATUS(embeddings_views, m_token_embedder->tokens_to_embeddings(input.tokens),
                LLMGeneratorReadSerializer);
            TRY_AS_HRPC_STATUS(per_layer_inputs_views, gather_declared_per_layer_inputs(input.tokens),
                LLMGeneratorReadSerializer);
        } else {
            // Client-side tokenizer with embeddings
            // Embeddings already contain prefix+input combined (first iter) or single token (subsequent)
            std::transform(input.embeddings.begin(), input.embeddings.end(), std::back_inserter(embeddings_views),
                [](const auto &emb) { return emb; });
            per_layer_inputs_views = std::move(input.per_layer_inputs);
        }

        TRY_AS_HRPC_STATUS(auto token_result,
            generate_next_token_on_demand(input.tokens, embeddings_views, per_layer_inputs_views),
            LLMGeneratorReadSerializer);

        std::tie(next_token_id, gen_status) = token_result;
    }

    LLMGeneratorReadSerializer::TextGenerationOutput output = {};
    output.output_token_id = next_token_id;
    output.output_token_str = handle_next_token(next_token_id);

    const auto is_context_full = (m_pre_process->cache_usage_size() >= m_context_capacity);
    TRY_AS_HRPC_STATUS(auto generator_read_reply,
        LLMGeneratorReadSerializer::serialize_reply(HAILO_SUCCESS, output, gen_status, is_context_full),
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
    CHECK_SUCCESS_AS_HRPC_STATUS(reset_conversation_context(), LLMClearContextSerializer);
    TRY_AS_HRPC_STATUS(auto reply, LLMClearContextSerializer::serialize_reply(HAILO_SUCCESS),
        LLMClearContextSerializer);
    return reply;
}

Expected<Buffer> LLMServer::handle_get_context_request(const MemoryView &request)
{
    CHECK_SUCCESS_AS_HRPC_STATUS(LLMGetContextSerializer::deserialize_request(request), LLMGetContextSerializer);
    // Wait on a lock to make sure that no generation is in progress
    std::unique_lock<std::mutex> lock(m_generation_mutex);
    auto context_buffer = get_context();
    // The client always reads a context frame before the reply, so an error still sends an empty one
    const auto context_frame = context_buffer ? MemoryView(context_buffer.value()) : MemoryView();
    CHECK_SUCCESS_AS_HRPC_STATUS(m_session.write(context_frame), LLMGetContextSerializer);
    CHECK_EXPECTED_AS_HRPC_STATUS(context_buffer, LLMGetContextSerializer);
    TRY_AS_HRPC_STATUS(auto reply, LLMGetContextSerializer::serialize_reply(HAILO_SUCCESS), LLMGetContextSerializer);
    return reply;
}

Expected<Buffer> LLMServer::handle_set_context_request(const MemoryView &request)
{
    CHECK_SUCCESS_AS_HRPC_STATUS(LLMSetContextSerializer::deserialize_request(request), LLMSetContextSerializer);
    TRY_AS_HRPC_STATUS(auto context_buffer, m_session.read(), LLMSetContextSerializer);
    // Wait on a lock to make sure that no generation is in progress
    std::unique_lock<std::mutex> lock(m_generation_mutex);
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

    auto max_context_capacity = m_context_capacity;
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

Expected<Buffer> LLMServer::handle_acquire_kv_cache_request(const MemoryView &)
{
    auto guard = KvCacheFlag::acquire();
    auto status = guard.status();
    if (guard) {
        m_kv_cache_guard = std::move(guard.release());
    }
    return LLMAcquireKvCacheSerializer::serialize_reply(status);
}

Expected<Buffer> LLMServer::handle_check_hef_exists_request(const MemoryView &request)
{
    TRY_AS_HRPC_STATUS(auto pair, GenAICheckHefExistsSerializer::deserialize_request(request),
        GenAICheckHefExistsSerializer);
    const auto &hef_path = pair.first;
    const auto &hef_hash = pair.second;

    if (!Filesystem::does_file_exists(hef_path)) {
        LOGGER__INFO("HEF file '{}' does not exist on device", hef_path);
        TRY_AS_HRPC_STATUS(auto reply, GenAICheckHefExistsSerializer::serialize_reply(HAILO_SUCCESS, false),
            GenAICheckHefExistsSerializer);
        return reply;
    }

    TRY_AS_HRPC_STATUS(auto local_hef_hash, Hef::hash(hef_path), GenAICheckHefExistsSerializer);
    if (local_hef_hash != hef_hash) {
        LOGGER__INFO("HEF file '{}' exists on device, but hash '{}' does not match expected hash '{}'", hef_path, local_hef_hash, hef_hash);
        TRY_AS_HRPC_STATUS(auto reply, GenAICheckHefExistsSerializer::serialize_reply(HAILO_SUCCESS, false),
            GenAICheckHefExistsSerializer);
        return reply;
    }

    LOGGER__INFO("HEF file '{}' exists on device and hash '{}' matches expected hash '{}'", hef_path, local_hef_hash, hef_hash);
    TRY_AS_HRPC_STATUS(auto reply, GenAICheckHefExistsSerializer::serialize_reply(HAILO_SUCCESS, true),
        GenAICheckHefExistsSerializer);
    return reply;
}

hailo_status LLMServer::process_prefill_inputs_chunk(std::map<std::string, MemoryView> &prefill_inputs,
    std::map<std::string, MemoryView> &prefill_outputs, const std::vector<EmbeddingViewWrapper> &input_embeddings,
    const std::vector<EmbeddingViewWrapper> &per_layer_inputs)
{
    LOGGER__GENAI_STATS_START("[generate-prefill] pre process");
    CHECK_SUCCESS(m_pre_process->prepare_inputs_prefill(prefill_inputs, input_embeddings, per_layer_inputs));
    LOGGER__GENAI_STATS_END("[generate-prefill] pre process");

    LOGGER__GENAI_STATS_START("[generate-prefill] update cache offset");
    CHECK_SUCCESS(m_inference_manager_prefill->update_cache_offset(static_cast<int32_t>(input_embeddings.size())));
    LOGGER__GENAI_STATS_END("[generate-prefill] update cache offset");

    LOGGER__GENAI_STATS_START("[generate-prefill] hw-inference prefill");
    CHECK_SUCCESS(m_inference_manager_prefill->generate(prefill_inputs, prefill_outputs));
    LOGGER__GENAI_STATS_END("[generate-prefill] hw-inference prefill");

    return HAILO_SUCCESS;
}

static std::vector<EmbeddingViewWrapper> slice_per_layer_inputs(
    const std::vector<EmbeddingViewWrapper> &per_layer_inputs, size_t offset, size_t count)
{
    if (per_layer_inputs.empty()) {
        return {};
    }

    return std::vector<EmbeddingViewWrapper>(per_layer_inputs.begin() + offset,
        per_layer_inputs.begin() + offset + count);
}

Expected<int> LLMServer::get_next_token_prefill(std::map<std::string, MemoryView> &prefill_inputs,
    std::map<std::string, MemoryView> &prefill_outputs, const std::vector<EmbeddingViewWrapper> &input_embeddings,
    const std::vector<EmbeddingViewWrapper> &per_layer_inputs, const LLMGeneratorParams &params)
{
    size_t num_full_chunks = input_embeddings.size() / m_pre_process_params.prefill_input_tokens_count;
    size_t remainder_size = input_embeddings.size() % m_pre_process_params.prefill_input_tokens_count;
    // Process the remainder first, if any
    if (remainder_size > 0) {
        std::vector<EmbeddingViewWrapper> first_prefill_embeddings(input_embeddings.begin(), input_embeddings.begin() + remainder_size);
        CHECK_SUCCESS(process_prefill_inputs_chunk(prefill_inputs, prefill_outputs, first_prefill_embeddings,
            slice_per_layer_inputs(per_layer_inputs, 0, remainder_size)));
    }

    // Process full prefill chunks
    size_t offset = remainder_size;
    for (size_t i = 0; i < num_full_chunks; ++i) {
        std::vector<EmbeddingViewWrapper> input_embeddings_chunk(input_embeddings.begin() + offset, input_embeddings.begin() + offset + m_pre_process_params.prefill_input_tokens_count);
        CHECK_SUCCESS(process_prefill_inputs_chunk(prefill_inputs, prefill_outputs, input_embeddings_chunk,
            slice_per_layer_inputs(per_layer_inputs, offset, input_embeddings_chunk.size())));
        offset += input_embeddings_chunk.size();
    }

    LOGGER__GENAI_STATS_START("[generate-prefill] post process");
    auto next_token = m_post_process.get_next_token(prefill_outputs.begin()->second, m_tokens_history, params);
    LOGGER__GENAI_STATS_END("[generate-prefill] post process");

    return next_token;
}

hailo_status LLMServer::reset_conversation_context()
{
    LOGGER__INFO("Resetting conversation context");

    // Nothing is cleared unless the device cache can be reset too, so a failure leaves the conversation intact
    CHECK_SUCCESS(m_inference_manager_prefill->init_cache(0));
    clear_host_conversation();

    return HAILO_SUCCESS;
}

void LLMServer::clear_host_conversation()
{
    m_tokens_history.clear();
    m_pre_process->reset_local_cache();
    m_post_process.reset_random_generator();
    prepare_for_new_generation();
}

void LLMServer::reset_conversation_after_device_cache_failure(hailo_status failure_status)
{
    LOGGER__ERROR("Resetting the conversation, as updating the device cache failed with status {}", failure_status);
    clear_host_conversation();
    const auto reset_status = m_inference_manager_prefill->init_cache(0);
    if (HAILO_SUCCESS != reset_status) {
        LOGGER__ERROR("Resetting the device cache failed with status {}", reset_status);
    }
}

std::optional<size_t> LLMServer::check_stop_sequences(int latest_token)
{
    if (m_tokenized_stop_sequences.empty()) {
        return std::nullopt;
    }

    // Add the latest token to our ordered sequence
    m_recent_tokens_sequence.push_back({latest_token, m_reasoning.is_inside_reasoning});

    // Keep the sequence size manageable
    if (m_recent_tokens_sequence.size() > MAX_STOP_SEQUENCE_LENGTH) {
        m_recent_tokens_sequence.pop_front();
    }

    const auto matched_length = find_matched_stop_sequence_length();
    if (matched_length) {
        LOGGER__INFO("Custom stop sequence matched: {} tokens ending with {}", *matched_length, latest_token);
    }
    return matched_length;
}

std::optional<size_t> LLMServer::find_matched_stop_sequence_length() const
{
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
            if (m_recent_tokens_sequence[recent_len - seq_len + i].token_id != stop_sequence[i]) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return seq_len;
        }
    }
    return std::nullopt;
}

Expected<std::pair<int, LLMGeneratorCompletion::Status>> LLMServer::generate_next_token_on_demand(const std::vector<int> &tokens,
    const std::vector<EmbeddingViewWrapper> &embeddings, const std::vector<EmbeddingViewWrapper> &per_layer_inputs)
{
    // This method assumes m_generation_mutex is already held by the caller

    assert(!tokens.empty());
    if ((tokens.size() == 1) && m_inference_manager_tbt) {
        CHECK(1 == embeddings.size(), HAILO_INVALID_ARGUMENT,
            "Got {} embedding rows for a token-by-token step, expected one", embeddings.size());
        return handle_tbt_phase(embeddings, per_layer_inputs);
    } else {
        CHECK(0 == m_generated_token_count, HAILO_INVALID_OPERATION,
            "Got a prompt after {} generated tokens; a generation's prompt must be its first read",
            m_generated_token_count);
        return handle_prefill_phase(tokens, embeddings, per_layer_inputs);
    }
}

Expected<std::vector<EmbeddingViewWrapper>> LLMServer::gather_declared_per_layer_inputs(
    const std::vector<int> &tokens) const
{
    if (!m_per_layer_inputs_embedder) {
        return std::vector<EmbeddingViewWrapper>();
    }

    return gather_per_layer_inputs(*m_per_layer_inputs_embedder, tokens);
}

std::string LLMServer::handle_next_token(int next_token)
{
    if (INVALID_TOKEN_VALUE == next_token) {
        return "";
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
    const std::vector<EmbeddingViewWrapper> &embeddings, const std::vector<EmbeddingViewWrapper> &per_layer_inputs)
{
    // The reasoning split indexes the embedding and per-layer rows by token position
    CHECK(tokens.size() == embeddings.size(), HAILO_INVALID_ARGUMENT,
        "Got {} embedding rows for {} tokens, expected one per token", embeddings.size(), tokens.size());
    // Each prefill chunk enters the cache before the next one is prepared, so a bad row must fail before the first
    CHECK_SUCCESS(m_pre_process->validate_input_rows(embeddings, per_layer_inputs));

    int next_token = INVALID_TOKEN_VALUE;

    // Perform prefill phase - process all input embeddings

    const auto unclosed_reasoning_start = find_unclosed_reasoning_start(tokens, m_reasoning.start_token_id,
        m_reasoning.end_token_id);
    if (unclosed_reasoning_start) {
        const auto start_of_reasoning_token_index = static_cast<std::ptrdiff_t>(*unclosed_reasoning_start);
        std::vector<EmbeddingViewWrapper> before_reasoning_embeddings(embeddings.begin(),
            embeddings.begin() + start_of_reasoning_token_index);
        std::vector<EmbeddingViewWrapper> after_reasoning_embeddings(embeddings.begin() + start_of_reasoning_token_index + 1,
            embeddings.end());
        const auto before_reasoning_per_layer_inputs = slice_per_layer_inputs(per_layer_inputs, 0,
            before_reasoning_embeddings.size());
        const auto after_reasoning_per_layer_inputs = slice_per_layer_inputs(per_layer_inputs,
            static_cast<size_t>(start_of_reasoning_token_index) + 1, after_reasoning_embeddings.size());

        // Generate tokens before reasoning (without exporting)
        TRY(auto ignored_next_token, get_next_token_prefill(m_prefill_inputs, m_prefill_outputs,
            before_reasoning_embeddings, before_reasoning_per_layer_inputs, m_current_generation_params));
        (void)ignored_next_token;

        save_reasoning_rewind_point(ReasoningOpener::Prompt);
        TRY(const auto generation_end_before_write, prepare_device_cache_for_write(after_reasoning_embeddings.size()));
        if (generation_end_before_write.has_value()) {
            return GenerationEndBeforeWrite::value_type(*generation_end_before_write);
        }

        // Generate tokens after reasoning
        TRY(next_token, get_next_token_prefill(m_prefill_inputs, m_prefill_outputs,
            after_reasoning_embeddings, after_reasoning_per_layer_inputs, m_current_generation_params));
    } else {
        TRY(next_token, get_next_token_prefill(m_prefill_inputs, m_prefill_outputs,
            embeddings, per_layer_inputs, m_current_generation_params));
    }

    m_generated_token_count++;
    handle_generated_reasoning_token(next_token);

    TRY(const auto generation_status, resolve_generation_status(next_token));

    // If no tokens are expected back, override the generated token with 'INVALID_TOKEN_VALUE' - which will be ignored along the way
    if (0 == m_current_generation_params.max_generated_tokens()) {
        next_token = INVALID_TOKEN_VALUE;
    }
    return std::make_pair(next_token, generation_status);
}

Expected<LLMGeneratorCompletion::Status> LLMServer::resolve_generation_status(int next_token)
{
    const auto generation_end = get_current_generation_end(next_token);
    if (LLMGeneratorCompletion::Status::GENERATING == generation_end.status) {
        record_token_fed_back(next_token);
        return LLMGeneratorCompletion::Status::GENERATING;
    }
    return handle_generation_completion(generation_end, next_token);
}

void LLMServer::record_token_fed_back(int next_token)
{
    m_tokens_history.insert(next_token);
    if (m_reasoning.has_rewind_point) {
        collect_token_if_outside_reasoning(next_token);
    }
}

Expected<std::pair<int, LLMGeneratorCompletion::Status>> LLMServer::end_generation_without_new_token()
{
    if (m_tokenizer) {
        m_tokenizer->clear_buffered_tokens();
    }
    m_recent_tokens_sequence.clear();

    if (0 == m_current_generation_params.max_generated_tokens()) {
        return std::make_pair(INVALID_TOKEN_VALUE, LLMGeneratorCompletion::Status::MAX_TOKENS_REACHED);
    }
    CHECK_SUCCESS(apply_recovery_sequence(LLMGeneratorCompletion::Status::ABORTED));
    return pop_recovery_token(LLMGeneratorCompletion::Status::ABORTED);
}

std::pair<int, LLMGeneratorCompletion::Status> LLMServer::pop_recovery_token(
    LLMGeneratorCompletion::Status termination_status)
{
    auto delivery = deliver_next_recovery_token(std::move(m_recovery.delivery_queue), termination_status);
    m_recovery.delivery_queue = std::move(delivery.remaining_recovery_tokens);
    return std::make_pair(delivery.token, delivery.status);
}

LLMServer::GenerationEnd LLMServer::get_current_generation_end(int next_token)
{
    if (m_generated_token_count >= m_current_generation_params.max_generated_tokens()) {
        return {LLMGeneratorCompletion::Status::MAX_TOKENS_REACHED, std::nullopt};
    }
    const auto matched_stop_sequence_length = check_stop_sequences(next_token);
    if (matched_stop_sequence_length) {
        return {LLMGeneratorCompletion::Status::LOGICAL_END_OF_GENERATION, matched_stop_sequence_length};
    }
    if (m_abort_requested) {
        return {LLMGeneratorCompletion::Status::ABORTED, std::nullopt};
    }
    return {LLMGeneratorCompletion::Status::GENERATING, std::nullopt};
}

Expected<std::pair<int, LLMGeneratorCompletion::Status>> LLMServer::handle_tbt_phase(
    const std::vector<EmbeddingViewWrapper> &embeddings, const std::vector<EmbeddingViewWrapper> &per_layer_inputs)
{
    assert(1 == embeddings.size());
    assert(m_inference_manager_tbt);

    std::map<std::string, MemoryView> tbt_inputs = m_tbt_inputs;
    std::map<std::string, MemoryView> tbt_outputs = m_tbt_outputs;

    TRY(const auto generation_end_before_write, prepare_device_cache_for_write(embeddings.size()));
    if (generation_end_before_write.has_value()) {
        return GenerationEndBeforeWrite::value_type(*generation_end_before_write);
    }

    // TBT preprocessing (restore missing logic from old tbt_generation_loop)
    LOGGER__GENAI_STATS_START("[generate-tbt] pre process");
    CHECK_SUCCESS(m_pre_process->prepare_inputs_tbt(tbt_inputs, embeddings, per_layer_inputs));
    LOGGER__GENAI_STATS_END("[generate-tbt] pre process");

    LOGGER__GENAI_STATS_START("[generate-tbt] update cache offset");
    CHECK_SUCCESS(m_inference_manager_tbt->update_cache_offset(1));
    LOGGER__GENAI_STATS_END("[generate-tbt] update cache offset");

    LOGGER__GENAI_STATS_START("[generate-tbt] hw-inference tbt");
    CHECK_SUCCESS(m_inference_manager_tbt->generate(tbt_inputs, tbt_outputs));
    LOGGER__GENAI_STATS_END("[generate-tbt] hw-inference tbt");

    LOGGER__GENAI_STATS_START("[generate-tbt] post process");
    int next_token = m_post_process.get_next_token(tbt_outputs.begin()->second,
        m_tokens_history, m_current_generation_params);
    LOGGER__GENAI_STATS_END("[generate-tbt] post process");

    m_generated_token_count++;
    handle_generated_reasoning_token(next_token);

    TRY(const auto generation_status, resolve_generation_status(next_token));

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
    m_tokens_history.insert(m_recovery.tokens.begin(), m_recovery.tokens.end());

    return HAILO_SUCCESS;
}

Expected<LLMGeneratorCompletion::Status> LLMServer::handle_generation_completion(
    const GenerationEnd &generation_end, int next_token)
{
    if (m_tokenizer) {
        m_tokenizer->clear_buffered_tokens();
    }

    auto completion_status = generation_end.status;
    const auto kept_last_tokens = last_tokens_kept_in_context(generation_end.matched_stop_sequence_length,
        next_token);
    if (m_reasoning.has_rewind_point) {
        CHECK_SUCCESS(handle_reasoning_generation_completion());
    }

    bool zero_tokens_back = (0 == m_current_generation_params.max_generated_tokens());

    // The last generated tokens are not in the cache yet, so the next prompt carries the ones the context keeps
    if (!zero_tokens_back) {
        m_tokens_history.insert(kept_last_tokens.begin(), kept_last_tokens.end());
        append_tokens_to_next_generation_prefix(kept_last_tokens);
    }

    // After finishing a generation, reset the recent tokens sequence to avoid false stop-token matches
    m_recent_tokens_sequence.clear();

    // Apply recovery sequence for ungraceful endings (MAX_TOKENS_REACHED or ABORTED)
    bool is_ungraceful_ending = (completion_status == LLMGeneratorCompletion::Status::ABORTED) ||
                               (completion_status == LLMGeneratorCompletion::Status::MAX_TOKENS_REACHED);

    if ((is_ungraceful_ending) && !zero_tokens_back) { // In case no tokens are expected back, recovery sequence is not applied
        CHECK_SUCCESS(apply_recovery_sequence(completion_status));
        if (!m_recovery.tokens.empty()) {
            // Recovery tokens will be delivered first, so return GENERATING for now
            LOGGER__INFO("Applied recovery sequence for ungraceful ending");
            return LLMGeneratorCompletion::Status::GENERATING;
        }
    }

    return completion_status;
}

std::vector<int> LLMServer::last_tokens_kept_in_context(std::optional<size_t> matched_stop_sequence_length,
    int last_token) const
{
    if (!m_reasoning.has_rewind_point) {
        return {last_token};
    }

    if (matched_stop_sequence_length) {
        // The matched stop sequence closes the model turn, so it stays even inside an unclosed thought
        const auto matched_begin = m_recent_tokens_sequence.end() -
            static_cast<std::ptrdiff_t>(*matched_stop_sequence_length);
        std::vector<int> stop_sequence_tokens;
        std::transform(matched_begin, m_recent_tokens_sequence.end(), std::back_inserter(stop_sequence_tokens),
            [](const RecentToken &recent_token) { return recent_token.token_id; });
        return stop_sequence_tokens_kept_in_context(stop_sequence_tokens, matched_begin->is_inside_reasoning,
            m_reasoning.start_token_id, m_reasoning.end_token_id);
    }

    if (is_reasoning_span_token(last_token, m_reasoning.is_inside_reasoning, m_reasoning.start_token_id)) {
        return {};
    }
    return {last_token};
}

void LLMServer::save_reasoning_rewind_point(ReasoningOpener opener)
{
    LOGGER__GENAI_STATS_START("[generate-reasoning] get local cache for reasoning");
    m_reasoning.has_rewind_point = true;
    m_reasoning.is_opened_by_prompt = (ReasoningOpener::Prompt == opener);
    m_reasoning.is_inside_reasoning = m_reasoning.is_opened_by_prompt;
    m_reasoning.tokens_outside_reasoning.clear();
    m_reasoning.tokens_history_before_reasoning = m_tokens_history;
    m_reasoning.pre_process_cache_before_reasoning = m_pre_process->get_local_cache();
    LOGGER__GENAI_STATS_END("[generate-reasoning] get local cache for reasoning");
}

Expected<LLMServer::GenerationEndBeforeWrite> LLMServer::prepare_device_cache_for_write(size_t written_tokens_count)
{
    if (!m_reasoning.has_rewind_point || m_reasoning.device_cache_snapshot.has_value()) {
        return GenerationEndBeforeWrite();
    }

    const auto rollback_tokens_count = reasoning_rollback_tokens_count();
    const auto exceeded_limit = find_reasoning_rollback_limit(rollback_tokens_count + written_tokens_count);
    if (!exceeded_limit) {
        return GenerationEndBeforeWrite();
    }

    LOGGER__INFO("Saving the device cache before the rollback reaches {} tokens - attention group '{}' absorbs at most "
        "{} rolled-back tokens", rollback_tokens_count + written_tokens_count, exceeded_limit->group_name,
        exceeded_limit->absorbable_rollback_tokens_count);
    LOGGER__GENAI_STATS_START("[generate-reasoning] get device cache for reasoning");
    auto cache_buffers = m_inference_manager_prefill->get_cache_buffers();
    LOGGER__GENAI_STATS_END("[generate-reasoning] get device cache for reasoning");
    if (!cache_buffers) {
        // The context has lost no entry yet, so rewinding the offset alone drops the thought exactly
        LOGGER__ERROR("Saving the device cache failed with status {}, so the generation ends and drops its thought",
            cache_buffers.status());
        CHECK_SUCCESS(handle_reasoning_generation_completion());
        TRY(const auto generation_end, end_generation_without_new_token());
        return GenerationEndBeforeWrite(generation_end);
    }
    m_reasoning.device_cache_snapshot = cache_buffers.release();

    return GenerationEndBeforeWrite();
}

size_t LLMServer::reasoning_rollback_tokens_count() const
{
    return m_pre_process->cache_usage_size() - m_reasoning.pre_process_cache_before_reasoning.usage_size;
}

std::optional<CacheRollbackLimit> LLMServer::find_reasoning_rollback_limit(size_t rollback_tokens_count) const
{
    return find_exceeded_cache_rollback_limit(m_attention_groups, m_pre_process_params.prefill_input_tokens_count,
        m_reasoning.pre_process_cache_before_reasoning.usage_size, rollback_tokens_count);
}

void LLMServer::handle_generated_reasoning_token(int next_token)
{
    if (m_reasoning.is_tool_call_turn ||
            !is_reasoning_configured(m_reasoning.start_token_id, m_reasoning.end_token_id)) {
        return;
    }
    if (is_tool_call_keeping_thoughts(next_token, m_reasoning.tool_call_start_token_id,
            m_reasoning.is_inside_reasoning, m_reasoning.is_opened_by_prompt)) {
        LOGGER__INFO("Tool call token {} generated outside a thought, so the generation keeps its thoughts",
            next_token);
        m_reasoning.is_tool_call_turn = true;
        m_reasoning.release_rewind_point();
        return;
    }
    if (m_reasoning.has_rewind_point || (m_reasoning.start_token_id != next_token)) {
        return;
    }

    save_reasoning_rewind_point(ReasoningOpener::Generation);
}

void LLMServer::collect_token_if_outside_reasoning(int next_token)
{
    const auto is_outside_reasoning = is_outside_reasoning_and_advance_span(next_token, m_reasoning.start_token_id,
        m_reasoning.end_token_id, m_reasoning.is_inside_reasoning);
    if (is_outside_reasoning) {
        m_reasoning.tokens_outside_reasoning.push_back(next_token);
    }
}

hailo_status LLMServer::rewind_device_cache_before_reasoning(const DeviceCacheRewindPlan &rewind_plan,
    size_t rollback_tokens_count)
{
    CHECK(DeviceCacheRewind::MissingSnapshot != rewind_plan.action, HAILO_INTERNAL_FAILURE,
        "No device cache snapshot to roll back {} tokens", rollback_tokens_count);
    if (DeviceCacheRewind::RestoreThenOffset == rewind_plan.action) {
        LOGGER__INFO("Restoring the device cache to roll back {} tokens - attention group '{}' absorbs at most {} "
            "rolled-back tokens", rollback_tokens_count, rewind_plan.exceeded_limit->group_name,
            rewind_plan.exceeded_limit->absorbable_rollback_tokens_count);
        LOGGER__GENAI_STATS_START("[generate-reasoning] restore device cache for reasoning");
        for (const auto &[cache_id, buffer] : *m_reasoning.device_cache_snapshot) {
            CHECK_SUCCESS(m_inference_manager_prefill->update_cache_buffer(cache_id, MemoryView(*buffer)));
        }
        LOGGER__GENAI_STATS_END("[generate-reasoning] restore device cache for reasoning");
    }

    LOGGER__GENAI_STATS_START("[generate-reasoning] update cache offset for reasoning");
    CHECK_SUCCESS(m_inference_manager_prefill->update_cache_offset(-static_cast<int32_t>(rollback_tokens_count)));
    LOGGER__GENAI_STATS_END("[generate-reasoning] update cache offset for reasoning");

    return HAILO_SUCCESS;
}

hailo_status LLMServer::handle_reasoning_generation_completion()
{
    const auto rollback_tokens_count = reasoning_rollback_tokens_count();
    const auto rewind_plan = plan_device_cache_rewind(m_attention_groups,
        m_pre_process_params.prefill_input_tokens_count, m_reasoning.pre_process_cache_before_reasoning.usage_size,
        rollback_tokens_count, m_reasoning.device_cache_snapshot.has_value());
    if (DeviceCacheRewind::None != rewind_plan.action) {
        const auto status = rewind_device_cache_before_reasoning(rewind_plan, rollback_tokens_count);
        if (HAILO_SUCCESS != status) {
            reset_conversation_after_device_cache_failure(status);
            return status;
        }
    }

    LOGGER__GENAI_STATS_START("[generate-reasoning] set local cache for reasoning");
    const auto &kept_tokens = m_reasoning.tokens_outside_reasoning;
    m_tokens_history = m_reasoning.tokens_history_before_reasoning;
    m_tokens_history.insert(kept_tokens.begin(), kept_tokens.end());
    m_pre_process->set_local_cache(m_reasoning.pre_process_cache_before_reasoning);
    LOGGER__GENAI_STATS_END("[generate-reasoning] set local cache for reasoning");

    // Directly prepend tokens instead of converting to text and back to tokens
    append_tokens_to_next_generation_prefix(kept_tokens);

    m_reasoning.clear();

    return HAILO_SUCCESS;
}

Expected<Buffer> LLMServer::get_context() const
{
    TRY(auto cache_buffers, m_inference_manager_prefill->get_cache_buffers());
    const auto pre_process_cache = m_pre_process->get_local_cache();

    // Calculate the required payload size
    TRY(auto payload_size, LLMContextUtils::calculate_payload_size(pre_process_cache, m_tokens_history,
        m_next_input_prompt_prefix_tokens, cache_buffers));

    // Create buffer with header + dynamic payload
    TRY(auto context_buffer, Buffer::create(sizeof(LLMContextHeader) + payload_size, BufferStorageParams::create_dma()));

    // Create and set header
    TRY(auto header, LLMContextUtils::create_header(m_inference_manager_prefill->get_hef(), payload_size));
    std::memcpy(context_buffer.data(), &header, sizeof(LLMContextHeader));

    // Serialize payload data
    CHECK_SUCCESS(LLMContextUtils::serialize_payload(context_buffer.from(sizeof(LLMContextHeader)), pre_process_cache,
        m_tokens_history, m_next_input_prompt_prefix_tokens, cache_buffers));

    return context_buffer;
}

hailo_status LLMServer::set_context(const MemoryView &context_buffer)
{
    CHECK(context_buffer.size() >= sizeof(LLMContextHeader), HAILO_INVALID_ARGUMENT,
        "The context is {} bytes, shorter than its {} byte header", context_buffer.size(), sizeof(LLMContextHeader));

    const LLMContextHeader *header = reinterpret_cast<const LLMContextHeader*>(context_buffer.data());
    CHECK_SUCCESS(LLMContextUtils::validate_header(*header, m_inference_manager_prefill->get_hef()));

    const auto received_payload_size = context_buffer.size() - sizeof(LLMContextHeader);
    CHECK(header->payload_size == received_payload_size, HAILO_INVALID_ARGUMENT,
        "The context header declares a {} byte payload, but {} bytes follow it", header->payload_size,
        received_payload_size);

    // Deserialize payload data
    auto model_shaped_cache = m_pre_process->get_local_cache();
    std::unordered_set<int32_t> tokens_history;
    std::vector<int32_t> next_prompt_prefix;
    std::unordered_map<uint32_t, MemoryView> cache_buffers;

    const auto payload = MemoryView::create_const(context_buffer.data() + sizeof(LLMContextHeader),
        header->payload_size);
    CHECK_SUCCESS(LLMContextUtils::deserialize_payload(payload, model_shaped_cache, tokens_history, next_prompt_prefix,
        cache_buffers));

    prepare_for_new_generation();
    m_pre_process->set_local_cache(model_shaped_cache);
    m_tokens_history = std::move(tokens_history);
    m_next_input_prompt_prefix_tokens = std::move(next_prompt_prefix);

    const auto load_device_cache = [&]() -> hailo_status {
        for (const auto &[cache_id, buffer] : cache_buffers) {
            CHECK_SUCCESS(m_inference_manager_prefill->update_cache_buffer(cache_id, buffer));
        }
        return m_inference_manager_prefill->init_cache(static_cast<uint32_t>(model_shaped_cache.usage_size));
    };
    const auto status = load_device_cache();
    if (HAILO_SUCCESS != status) {
        reset_conversation_after_device_cache_failure(status);
        return status;
    }

    return HAILO_SUCCESS;
}

Expected<std::unique_ptr<LLMServerManager>> LLMServerManager::create(std::shared_ptr<Session> session, std::shared_ptr<VDeviceManager> vdevice_manager)
{
    TRY(auto server, LLMServer::create_unique(session, vdevice_manager));

    auto ptr = std::make_unique<LLMServerManager>(session, std::move(server));
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
        [&](const MemoryView &request) { return m_server->handle_check_hef_exists_request(request); };
    m_dispatcher[HailoGenAIActionID::LLM__GENERATOR_RELEASE] =
        [&](const MemoryView &request) { return m_server->handle_generator_release_request(request); };
    m_dispatcher[HailoGenAIActionID::LLM__ACQUIRE_KV_CACHE] =
        [&](const MemoryView &request) { return m_server->handle_acquire_kv_cache_request(request); };
}
