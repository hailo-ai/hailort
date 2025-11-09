/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file llm.cpp
 * @brief LLM implementation
 **/

#include "llm_internal.hpp"
#include "vlm/vlm_internal.hpp"

#include "hailo/genai/llm/llm.hpp"
#include "genai/genai_common.hpp"

#include "hailo/hailort.h"
#include "common/filesystem.hpp"
#include "common/utils.hpp"
#include "common/file_utils.hpp"
#include "common/genai/session_wrapper/session_wrapper.hpp"

#include <filesystem>

#include "common/genai/constants.hpp"
#include "common/genai/serializer/serializer.hpp"
#include "common/genai/connection_ports.hpp"

#include <numeric>

namespace hailort
{
namespace genai
{


constexpr std::chrono::milliseconds LLMGeneratorCompletion::DEFAULT_READ_TIMEOUT;

LLMParams::LLMParams(const std::string &hef_path, const std::string &lora_name, bool optimize_memory_on_device) :
    m_hef_path(hef_path), m_lora(lora_name), m_optimize_memory_on_device(optimize_memory_on_device)
{}

hailo_status LLMParams::set_model(const std::string &hef_path, const std::string &lora_name)
{
    m_hef_path = hef_path;
    m_lora = lora_name;

    if (BUILTIN != hef_path) {
        CHECK(Filesystem::does_file_exists(hef_path), HAILO_OPEN_FILE_FAILURE,
            "HEF file '{}' does not exist.", hef_path);
    } else {
        // When using BUILTIN HEF, LoRA must be set
        CHECK(!(lora_name.empty()), HAILO_INVALID_OPERATION,
            "When using '{}' model, LoRA name must be set.", BUILTIN);
    }

    return HAILO_SUCCESS;
}

const std::string& LLMParams::hef() const
{
    return m_hef_path;
}

const std::string& LLMParams::lora() const
{
    return m_lora;
}

bool LLMParams::optimize_memory_on_device() const
{
    return m_optimize_memory_on_device;
}

void LLMParams::set_optimize_memory_on_device(bool optimize_memory_on_device)
{
    m_optimize_memory_on_device = optimize_memory_on_device;
}

hailo_status LLMGeneratorParams::set_temperature(float32_t temperature)
{
    m_temperature = temperature;

    return HAILO_SUCCESS;
}

float32_t LLMGeneratorParams::temperature() const
{
    return m_temperature;
}

hailo_status LLMGeneratorParams::set_top_p(float32_t top_p)
{
    m_top_p = top_p;

    return HAILO_SUCCESS;
}

float32_t LLMGeneratorParams::top_p() const
{
    return m_top_p;
}

hailo_status LLMGeneratorParams::set_top_k(uint32_t top_k)
{
    m_top_k = top_k;

    return HAILO_SUCCESS;
}

uint32_t LLMGeneratorParams::top_k() const
{
    return m_top_k;
}

hailo_status LLMGeneratorParams::set_frequency_penalty(float32_t frequency_penalty)
{
    m_frequency_penalty = frequency_penalty;

    return HAILO_SUCCESS;
}

float32_t LLMGeneratorParams::frequency_penalty() const
{
    return m_frequency_penalty;
}

hailo_status LLMGeneratorParams::set_max_generated_tokens(uint32_t max_generated_tokens)
{
    m_max_generated_tokens = max_generated_tokens;

    return HAILO_SUCCESS;
}

uint32_t LLMGeneratorParams::max_generated_tokens() const
{
    return m_max_generated_tokens;
}

hailo_status LLMGeneratorParams::set_do_sample(bool do_sample)
{
    m_do_sample = do_sample;

    return HAILO_SUCCESS;
}

bool LLMGeneratorParams::do_sample() const
{
    return m_do_sample;
}

hailo_status LLMGeneratorParams::set_seed(uint32_t seed)
{
    m_seed = seed;

    return HAILO_SUCCESS;
}

uint32_t LLMGeneratorParams::seed() const
{
    return m_seed;
}

Expected<LLM> LLM::create(std::shared_ptr<VDevice> vdevice, const LLMParams &llm_params)
{
    TRY(auto pimpl, Impl::create_unique(vdevice, llm_params));
    return LLM(std::move(pimpl));
}

Expected<std::unique_ptr<LLM::Impl>> LLM::Impl::create_unique(std::shared_ptr<VDevice> vdevice, const LLMParams &llm_params)
{
    CHECK(!llm_params.hef().empty(), HAILO_INVALID_OPERATION, "Failed to create LLM. HEF was not set.");

    // LoRA is supported only when working with BUILTIN HEF
    bool is_builtin = (BUILTIN == llm_params.hef());
    if (is_builtin) {
        // When using BUILTIN HEF, LoRA must be set
        CHECK(!llm_params.lora().empty(), HAILO_INVALID_OPERATION,
            "Failed to create LLM. When using '{}' model, LoRA name must be set.", BUILTIN);
    }
    // Check if HEF exists on the server
    std::string hef_hash = "";
    if (!is_builtin) {
        TRY(hef_hash, Hef::hash(llm_params.hef()));
    }

    auto vdevice_params = vdevice->get_params();
    TRY(auto session_wrapper, GenAICommon::create_session_wrapper(vdevice_params, DEFAULT_LLM_CONNECTION_PORT));

    // Translate llm_params.hef() to an absolute path if it is not already
    std::string hef_path = llm_params.hef();
    if (!std::filesystem::path(hef_path).is_absolute()) {
        hef_path = std::filesystem::absolute(hef_path).string();
    }

    TRY(auto check_hef_exists_on_server_request, GenAICheckHefExistsSerializer::serialize_request(hef_path, hef_hash));
    TRY(auto check_hef_exists_on_server_reply, session_wrapper->execute(MemoryView(check_hef_exists_on_server_request)));
    TRY(auto hef_exists, GenAICheckHefExistsSerializer::deserialize_reply(MemoryView(*check_hef_exists_on_server_reply)));

    Buffer create_llm_request;
    std::shared_ptr<Buffer> create_llm_reply;
    std::shared_ptr<HailoTokenizer> tokenizer;
    std::shared_ptr<TokenEmbedder<uint16_t>> token_embedder;
    BufferPtr token_embedder_buffer;
    if (!is_builtin && !hef_exists) {
        // Get file size for chunked transfer
        TRY(auto file_size, get_istream_size(hef_path));
        if (llm_params.optimize_memory_on_device()) {
#ifndef HAILO_CLIENT_TOKENIZER_ENABLED
            LOGGER__ERROR("Host-tokenization is not enabled in lib compilation");
            return make_unexpected(HAILO_NOT_IMPLEMENTED);
#else
            // Reduce external-info file-sizes form HEF file size
            TRY(auto external_resources, Hef::extract_hef_external_resources(hef_path));
            for (const auto &[name, resource_buffer] : external_resources) {
                // Counting on the fact that tokenizer and embeddings are always at the end of the HEF file
                if (name == TOKENIZER) {
                    std::string tokenizer_blob(resource_buffer->size(), '\0');
                    std::memcpy(const_cast<char*>(tokenizer_blob.data()), resource_buffer->data(), resource_buffer->size());
                    TRY(tokenizer, HailoTokenizer::create(tokenizer_blob));
                    file_size -= resource_buffer->size();
                    external_resources[name] = resource_buffer;
                } else if (name == INPUT_EMB_BINARY) {
                    // Create token_embedder after getting the embedding features from the server
                    token_embedder_buffer = resource_buffer;
                    file_size -= resource_buffer->size();
                }
            }
#endif // HAILO_CLIENT_TOKENIZER_ENABLED
        }

        // Create request with file size for chunked transfer
        TRY(create_llm_request, LLMCreateSerializer::serialize_request(vdevice_params, llm_params, "", file_size));
        CHECK_SUCCESS(session_wrapper->write(MemoryView(create_llm_request)));
        CHECK_SUCCESS(session_wrapper->send_file_chunked(hef_path, file_size));
        TRY(create_llm_reply, session_wrapper->read());
    } else {
        std::string hef_path_to_send = hef_exists ? hef_path : is_builtin ? BUILTIN : "";
        TRY(create_llm_request, LLMCreateSerializer::serialize_request(vdevice_params, llm_params, hef_path_to_send));
        TRY(create_llm_reply, session_wrapper->execute(MemoryView(create_llm_request)));
    }
    TRY(auto reply_tuple, LLMCreateSerializer::deserialize_reply(MemoryView(*create_llm_reply)), "Failed to create LLM");
    auto prompt_template = std::get<0>(reply_tuple);
    auto embedding_features = std::get<1>(reply_tuple);

    if (llm_params.optimize_memory_on_device()) {
#ifndef HAILO_CLIENT_TOKENIZER_ENABLED
        (void)embedding_features;
        LOGGER__ERROR("Host-tokenization is not enabled in lib compilation");
        return make_unexpected(HAILO_NOT_IMPLEMENTED);
#else
        CHECK_AS_EXPECTED(nullptr != token_embedder_buffer, HAILO_NOT_AVAILABLE, "Token embedder buffer is not available");
        TRY(token_embedder, TokenEmbedder<uint16_t>::create(token_embedder_buffer,
            token_embedder_buffer->size() / (sizeof(uint16_t) * embedding_features), embedding_features));
#endif // HAILO_CLIENT_TOKENIZER_ENABLED
    }

    TRY(auto get_generator_default_params_request, LLMGetGeneratorParamsSerializer::serialize_request());
    TRY_V(auto get_generator_default_params, session_wrapper->execute(MemoryView(get_generator_default_params_request)));
    TRY(auto default_generator_params, LLMGetGeneratorParamsSerializer::deserialize_reply(MemoryView(*get_generator_default_params)));

    TRY(auto prompt_template_handler, PromptTemplateHandler::create(prompt_template));
    auto prompt_template_handler_ptr = make_shared_nothrow<PromptTemplateHandler>(std::move(prompt_template_handler));
    CHECK_NOT_NULL_AS_EXPECTED(prompt_template_handler_ptr, HAILO_OUT_OF_HOST_MEMORY);

    auto llm_ptr = make_unique_nothrow<Impl>(session_wrapper, llm_params, default_generator_params, prompt_template_handler_ptr,
        tokenizer, token_embedder);
    CHECK_NOT_NULL_AS_EXPECTED(llm_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return llm_ptr;
}

LLM::Impl::Impl(std::shared_ptr<SessionWrapper> session, const LLMParams &llm_params,
    const LLMGeneratorParams &default_generator_params, std::shared_ptr<PromptTemplateHandler> prompt_template_handler,
    std::shared_ptr<HailoTokenizer> tokenizer, std::shared_ptr<TokenEmbedder<uint16_t>> token_embedder) :
        m_session(session), m_llm_params(llm_params), m_default_generator_params(default_generator_params),
        m_prompt_template_handler(prompt_template_handler),
        m_tokenizer(tokenizer),
        m_token_embedder(token_embedder)
{}

LLM::Impl::~Impl()
{
    auto release_request = LLMReleaseSerializer::serialize_request();
    if (!release_request) {
        LOGGER__CRITICAL("Failed to serialize LLM release request with status {}", release_request.status());
        return;
    }
    auto release_reply = m_session->execute(MemoryView(release_request.value()));
    if (!release_reply) {
        LOGGER__CRITICAL("Failed to execute LLM release request with status {}", release_reply.status());
        return;
    }
    auto status = LLMReleaseSerializer::deserialize_reply(MemoryView(*release_reply));
    if (HAILO_SUCCESS != status) {
        LOGGER__CRITICAL("Failed to deserialize LLM release reply with status {}", status);
        return;
    }
}

LLM::LLM(std::unique_ptr<Impl> pimpl) :
    m_pimpl(std::move(pimpl))
{}

Expected<std::vector<int>> LLM::tokenize(const std::string &prompt)
{
    return m_pimpl->tokenize(prompt);
}

Expected<std::vector<int>> LLM::Impl::tokenize(const std::string &prompt)
{
#ifdef HAILO_CLIENT_TOKENIZER_ENABLED
    if (m_tokenizer) {
        TRY(auto tokens, m_tokenizer->text_to_tokens(prompt));
        return tokens;
    }
#endif // HAILO_CLIENT_TOKENIZER_ENABLED

    TRY(auto request, LLMTokenizeSerializer::serialize_request(prompt));
    TRY(auto reply, m_session->execute(MemoryView(request)));
    TRY(auto tokens, LLMTokenizeSerializer::deserialize_reply(MemoryView(*reply)));
    return tokens;
}

Expected<size_t> LLM::get_context_usage_size()
{
    return m_pimpl->get_context_usage_size();
}

Expected<size_t> LLM::Impl::get_context_usage_size()
{
    TRY(auto request, LLMGetContextUsageSizeSerializer::serialize_request());
    TRY(auto reply, m_session->execute(MemoryView(request)));
    TRY(auto context_usage, LLMGetContextUsageSizeSerializer::deserialize_reply(MemoryView(*reply)));
    return context_usage;
}

Expected<size_t> LLM::max_context_capacity()
{
    return m_pimpl->max_context_capacity();
}

Expected<size_t> LLM::Impl::max_context_capacity()
{
    TRY(auto request, LLMGetMaxContextCapacitySerializer::serialize_request());
    TRY(auto reply, m_session->execute(MemoryView(request)));
    TRY(auto max_context_capacity, LLMGetMaxContextCapacitySerializer::deserialize_reply(MemoryView(*reply)));
    return max_context_capacity;
}

hailo_status LLM::clear_context()
{
    return m_pimpl->clear_context();
}

hailo_status LLM::Impl::clear_context()
{
    TRY(auto request, LLMClearContextSerializer::serialize_request());
    TRY(auto reply, m_session->execute(MemoryView(request)));
    CHECK_SUCCESS(LLMClearContextSerializer::deserialize_reply(MemoryView(*reply)),
        "Failed to clear context. Make sure there is no other generation in progress");
    m_prompt_template_handler->reset_state();

    return HAILO_SUCCESS;
}

Expected<BufferPtr> LLM::save_context()
{
    return m_pimpl->save_context();
}

Expected<BufferPtr> LLM::Impl::save_context()
{
    TRY(auto request, LLMGetContextSerializer::serialize_request());
    CHECK_SUCCESS(m_session->write(MemoryView(request)), "Failed to write request");
    TRY(auto context_shared, m_session->read());
    TRY(auto reply, m_session->read());
    CHECK_SUCCESS(LLMGetContextSerializer::deserialize_reply(MemoryView(*reply)), "Failed to get context");
    return context_shared;
}

hailo_status LLM::load_context(const MemoryView &context)
{
    return m_pimpl->load_context(context);
}

hailo_status LLM::Impl::load_context(const MemoryView &context)
{
    TRY(auto request, LLMSetContextSerializer::serialize_request());
    CHECK_SUCCESS(m_session->write(MemoryView(request)), "Failed to write request");
    CHECK_SUCCESS(m_session->write(context), "Failed to write context");
    TRY(auto reply, m_session->read());
    CHECK_SUCCESS(LLMSetContextSerializer::deserialize_reply(MemoryView(*reply)), "Failed to set context");
    return HAILO_SUCCESS;
}

Expected<std::string> LLM::prompt_template()
{
    return m_pimpl->prompt_template();
}

Expected<std::string> LLM::Impl::prompt_template()
{
    TRY(auto prompt_template, m_prompt_template_handler->prompt_template());
    CHECK(!prompt_template.empty(), HAILO_NOT_AVAILABLE,
        "Prompt template is not set for this LLM");

    return prompt_template;
}


hailo_status LLM::set_generation_recovery_sequence(const std::string &abort_sequence)
{
    return m_pimpl->set_generation_recovery_sequence(abort_sequence);
}

hailo_status LLM::Impl::set_generation_recovery_sequence(const std::string &abort_sequence)
{
    TRY(auto tokens, tokenize(abort_sequence));
    TRY(auto set_end_of_generation_sequence_request, LLMSetEndOfGenerationSequenceSerializer::serialize_request(tokens));
    TRY(auto set_end_of_generation_sequence_reply, m_session->execute(MemoryView(set_end_of_generation_sequence_request)));
    CHECK_SUCCESS(LLMSetEndOfGenerationSequenceSerializer::deserialize_reply(MemoryView(*set_end_of_generation_sequence_reply)), "Failed to set generation recovery sequence");
    return HAILO_SUCCESS;
}

Expected<std::string> LLM::get_generation_recovery_sequence()
{

    return m_pimpl->get_generation_recovery_sequence();
}

Expected<std::string> LLM::Impl::get_generation_recovery_sequence()
{
    TRY(auto get_end_of_generation_sequence_request, LLMGetEndOfGenerationSequenceSerializer::serialize_request());
    TRY(auto get_end_of_generation_sequence_reply, m_session->execute(MemoryView(get_end_of_generation_sequence_request)));
    TRY(auto end_of_gen_sequence_pair, LLMGetEndOfGenerationSequenceSerializer::deserialize_reply(MemoryView(*get_end_of_generation_sequence_reply)));
    auto &[abort_sequence_str, end_of_gen_sequence_tokens] = end_of_gen_sequence_pair;
    if (!abort_sequence_str.empty()) {
        return std::string(abort_sequence_str);
    } else {
#ifdef HAILO_CLIENT_TOKENIZER_ENABLED
        if (m_tokenizer) {
            return m_tokenizer->tokens_to_text(end_of_gen_sequence_tokens, true);
        }
#endif // HAILO_CLIENT_TOKENIZER_ENABLED
        // If tokenizer not on host, then the empty-string returned by the server is actually the abort sequence
        return std::string();
    }
}


hailo_status LLM::set_stop_tokens(const std::vector<std::string> &stop_tokens)
{
    return m_pimpl->set_stop_tokens(stop_tokens);
}

hailo_status LLM::Impl::set_stop_tokens(const std::vector<std::string> &stop_tokens)
{
    // Tokenize the stop tokens before sending to server
    std::vector<std::vector<int>> tokenized_sequences;
    for (const auto &stop_token : stop_tokens) {
        TRY(auto tokens, tokenize(stop_token));
        if (!tokens.empty()) {
            tokenized_sequences.push_back(tokens);
        }
    }

    TRY(auto set_stop_tokens_request, LLMSetStopTokensSerializer::serialize_request(tokenized_sequences));
    TRY(auto set_stop_tokens_reply, m_session->execute(MemoryView(set_stop_tokens_request)));
    CHECK_SUCCESS(LLMSetStopTokensSerializer::deserialize_reply(MemoryView(*set_stop_tokens_reply)), "Failed to set stop tokens");
    return HAILO_SUCCESS;
}

Expected<std::vector<std::string>> LLM::get_stop_tokens()
{
    return m_pimpl->get_stop_tokens();
}

Expected<std::vector<std::string>> LLM::Impl::get_stop_tokens()
{
    TRY(auto get_stop_tokens_request, LLMGetStopTokensSerializer::serialize_request());
    TRY(auto get_stop_tokens_reply, m_session->execute(MemoryView(get_stop_tokens_request)));
    TRY(auto response_data, LLMGetStopTokensSerializer::deserialize_reply(MemoryView(*get_stop_tokens_reply)));
    auto &[stop_tokens_str, stop_tokens_tokenized] = response_data;

    std::vector<std::string> stop_tokens_results;
    if (!stop_tokens_str.empty()) {
        stop_tokens_results = stop_tokens_str;
    } else {
#ifdef HAILO_CLIENT_TOKENIZER_ENABLED
        if (m_tokenizer) {
            std::vector<std::string> stop_tokens_results;
            for (const auto &tokenized_sequence : stop_tokens_tokenized) {
                TRY(auto stop_token_str, m_tokenizer->tokens_to_text(tokenized_sequence));
                stop_tokens_results.push_back(stop_token_str);
            }
        }
#endif // HAILO_CLIENT_TOKENIZER_ENABLED
        // If tokenizer not on host, then the empty-vector returned by the server is actually the stop tokens
    }
    return stop_tokens_results;
}

Expected<LLMGenerator> LLM::create_generator(const LLMGeneratorParams &params)
{
    return m_pimpl->create_generator(params);
}

Expected<LLMGenerator> LLM::create_generator()
{
    TRY(auto generator_params, create_generator_params());
    return m_pimpl->create_generator(generator_params);
}

Expected<LLMGeneratorParams> LLM::create_generator_params()
{
    return m_pimpl->create_generator_params();
}

Expected<LLMGeneratorParams> LLM::Impl::create_generator_params()
{
    auto generator_params = m_default_generator_params;

    return generator_params;
}

Expected<LLMGenerator> LLM::Impl::create_generator(const LLMGeneratorParams &params)
{
    CHECK_SUCCESS(validate_generator_params(params));

    TRY(auto create_generator_request, LLMGeneratorCreateSerializer::serialize_request(params));
    TRY(auto create_generator_reply, m_session->execute(MemoryView(create_generator_request)));
    CHECK_SUCCESS(LLMGeneratorCreateSerializer::deserialize_reply(MemoryView(*create_generator_reply)), "Failed to create LLM generator");

    auto pimpl = make_shared_nothrow<LLMGenerator::Impl>(m_session, m_prompt_template_handler,
        m_tokenizer, m_token_embedder);
    CHECK_NOT_NULL_AS_EXPECTED(pimpl, HAILO_OUT_OF_HOST_MEMORY);
    return LLMGenerator(pimpl);
}

hailo_status LLM::Impl::validate_generator_params(const LLMGeneratorParams &params)
{
    CHECK_AS_EXPECTED(0 < params.temperature(), HAILO_INVALID_ARGUMENT,
        "Temperature should be higher than '0'. received: '{}'", params.temperature());
    CHECK_AS_EXPECTED((0 <= params.top_p()) && (params.top_p() <= 1), HAILO_INVALID_ARGUMENT,
        "top_p should be in range [0, 1]. received: '{}'", params.top_p());
    CHECK_AS_EXPECTED(1 <= params.top_k(), HAILO_INVALID_ARGUMENT,
        "top_k should be greater than or equal to '1'. received: '{}'", params.top_k());
    CHECK_AS_EXPECTED(0 != params.frequency_penalty(), HAILO_INVALID_ARGUMENT,
        "frequency_penalty must be a nonzero value. received: '{}'", params.frequency_penalty());
    CHECK_AS_EXPECTED(1 <= params.max_generated_tokens(), HAILO_INVALID_ARGUMENT,
        "max_generated_tokens should be greater than or equal to '1'. received: '{}'", params.max_generated_tokens());
    return HAILO_SUCCESS;
}

Expected<LLMGeneratorCompletion> LLM::generate(const LLMGeneratorParams &params, const std::vector<std::string> &prompt_json_strings)
{
    return m_pimpl->generate(params, prompt_json_strings);
}

Expected<LLMGeneratorCompletion> LLM::Impl::generate(const LLMGeneratorParams &params, const std::vector<std::string> &prompt_json_strings)
{
    TRY(auto generator, create_generator(params));
    generator.write(prompt_json_strings);

    TRY(auto completion, generator.generate());
    // Generator is kept alive via shared_from_this() in LLMGenerator::Impl::generate()
    return completion;
}

Expected<LLMGeneratorCompletion> LLM::generate(const std::vector<std::string> &prompt_json_strings)
{
    TRY(auto generator_params, create_generator_params());
    return m_pimpl->generate(generator_params, prompt_json_strings);
}

LLMGenerator::LLMGenerator(std::shared_ptr<Impl> pimpl) :
    m_pimpl(pimpl)
{}

LLMGenerator::Impl::Impl(std::shared_ptr<SessionWrapper> session, std::shared_ptr<PromptTemplateHandler> prompt_template_handler,
    std::shared_ptr<HailoTokenizer> tokenizer, std::shared_ptr<TokenEmbedder<uint16_t>> token_embedder) :
        m_session(session), m_prompt_template_handler(prompt_template_handler),
        m_tokenizer(tokenizer),
        m_token_embedder(token_embedder)
{}

LLMGenerator::Impl::~Impl()
{
    auto release_request = LLMGeneratorReleaseSerializer::serialize_request();
    if (!release_request) {
        LOGGER__CRITICAL("Failed to serialize LLM generator release request with status {}", release_request.status());
        return;
    }
    auto release_reply = m_session->execute(MemoryView(release_request.value()));
    if (!release_reply) {
        LOGGER__CRITICAL("Failed to execute LLM generator release request with status {}", release_reply.status());
        return;
    }
    auto status = LLMGeneratorReleaseSerializer::deserialize_reply(MemoryView(*release_reply));
    if (HAILO_SUCCESS != status) {
        LOGGER__CRITICAL("Failed to deserialize LLM generator release reply with status {}", status);
    }
}

hailo_status LLMGenerator::write(const std::vector<std::string> &prompt_json_strings)
{
    return m_pimpl->write(prompt_json_strings);
}

hailo_status LLMGenerator::Impl::write(const std::vector<std::string> &prompt_json_strings)
{
    CHECK_AS_EXPECTED(!prompt_json_strings.empty(), HAILO_INVALID_ARGUMENT, "Prompt cannot be empty");

    TRY(auto updated_prompt, apply_prompt_tempalate_from_json(prompt_json_strings));
    return write(updated_prompt);
}

hailo_status LLMGenerator::write(const std::string &prompt)
{
    CHECK(!prompt.empty(), HAILO_INVALID_ARGUMENT, "Prompt cannot be empty");
    return m_pimpl->write(prompt);
}

hailo_status LLMGenerator::Impl::write(const std::string &prompt)
{
    TRY(auto generator_write_request, LLMGeneratorWriteSerializer::serialize_request());
    TRY(auto generator_write_reply, m_session->execute(MemoryView(generator_write_request)));
    CHECK_SUCCESS(LLMGeneratorWriteSerializer::deserialize_reply(MemoryView(*generator_write_reply)), "Failed to write prompt");

    m_aggregated_prompt += prompt;

    return HAILO_SUCCESS;
}

Expected<std::string> LLMGenerator::Impl::apply_prompt_tempalate_from_json(const std::vector<std::string> &prompt_json_strings)
{
    return m_prompt_template_handler->render(prompt_json_strings);
}

Expected<LLMGeneratorCompletion> LLMGenerator::generate()
{
    return m_pimpl->generate();
}

Expected<LLMGeneratorCompletion> LLMGenerator::Impl::generate()
{
    CHECK(!m_aggregated_prompt.empty(), HAILO_INVALID_OPERATION, "Prompt cannot be empty! Make sure 'write()' was called");

    TRY(auto generator_generate_request, LLMGeneratorGenerateSerializer::serialize_request());
    TRY(auto generator_generate_reply, m_session->execute(MemoryView(generator_generate_request)));
    TRY(auto initial_prefix_tokens, LLMGeneratorGenerateSerializer::deserialize_reply(MemoryView(*generator_generate_reply)),
        "Failed to generate. Make sure there is no other generation in progress");

    TRY(auto shutdown_event, Event::create_shared(Event::State::not_signalled));
    const auto TOKENS_BACKLOG = 1024;
    TRY(auto client_token_queue, SpscQueue<LLMTokenPair>::create(
        TOKENS_BACKLOG, shutdown_event, HAILO_INFINITE_TIMEOUT));

    auto pimpl = make_unique_nothrow<LLMGeneratorCompletion::Impl>(m_session, shared_from_this(),
        std::move(client_token_queue), shutdown_event, m_tokenizer, m_token_embedder,
        m_aggregated_prompt, initial_prefix_tokens);
    CHECK_NOT_NULL_AS_EXPECTED(pimpl, HAILO_OUT_OF_HOST_MEMORY);

    m_aggregated_prompt.clear();

    return LLMGeneratorCompletion(std::move(pimpl));
}

LLMGeneratorCompletion::LLMGeneratorCompletion(std::unique_ptr<Impl> pimpl) :
    m_pimpl(std::move(pimpl))
{}


LLMGeneratorCompletion::Impl::Impl(std::shared_ptr<SessionWrapper> session, std::shared_ptr<TextGeneratorBase> generator,
    SpscQueue<LLMTokenPair> &&client_token_queue, EventPtr shutdown_event,
    std::shared_ptr<HailoTokenizer> tokenizer, std::shared_ptr<TokenEmbedder<uint16_t>> token_embedder,
    const std::string &aggregated_prompt, const std::vector<int> &initial_prefix_tokens) :
        m_generator_scope_guard(generator),
        m_session(session),
        m_generation_status(Status::GENERATING),
        m_client_token_queue(std::move(client_token_queue)),
        m_shutdown_event(shutdown_event),
        m_context_full_warning_issued(false),
        m_tokenizer(tokenizer),
        m_token_embedder(token_embedder)
{
    m_token_reader_thread = std::thread(&LLMGeneratorCompletion::Impl::token_reader_thread, this, aggregated_prompt, initial_prefix_tokens);
}

LLMGeneratorCompletion::Impl::~Impl()
{
    // If generation is still in progress, abort it (like origin/develop)
    if (m_generation_status == Status::GENERATING) {
        auto status = abort();
        if (HAILO_SUCCESS != status) {
            LOGGER__CRITICAL("Failed to release LLMGeneratorCompletion. Failed to abort LLM generator completion with status {}", status);
        }
        
        // abort() already joined the thread, no need for additional cleanup
    } else {
        // If not generating, ensure proper thread shutdown and cleanup
        stop_token_reader_thread();
    }
}

void LLMGeneratorCompletion::Impl::stop_token_reader_thread()
{
    // Signal shutdown to stop the token reader thread
    if (m_shutdown_event) {
        m_shutdown_event->signal();
    }

    if (m_token_reader_thread.joinable()) {
        m_token_reader_thread.join();
    }
}

void LLMGeneratorCompletion::Impl::token_reader_thread(const std::string &aggregated_prompt, const std::vector<int> &initial_prefix_tokens)
{
    LLMGeneratorReadSerializer::TextGenerationInput input = {};
    input.initial_prompt = aggregated_prompt;
    input.tokens = initial_prefix_tokens;

    while (true) {
        TimeoutGuard timeout_guard(LONG_TIMEOUT);

        // Handle client-side tokenizer embeddings if needed
        if (m_tokenizer && m_token_embedder) {
            auto status = prepare_client_side_embeddings(input);
            if (HAILO_SUCCESS != status) {
                LOGGER__ERROR("Failed to prepare client-side embeddings: {}", status);
                break;
            }
        }

        // Send request and get response
        auto response_exp = send_read_request(input, timeout_guard.get_remaining_timeout());
        if (!response_exp) {
            LOGGER__ERROR("Failed to send read request: {}", response_exp.status());
            break;
        }

        auto [output_info, status] = response_exp.value();

        if (output_info.is_context_full && !m_context_full_warning_issued) {
            LOGGER__WARNING("Conversation context is full. It is adivsable to clear context as cache size was reached");
            m_context_full_warning_issued = true;
        }

#ifdef HAILO_CLIENT_TOKENIZER_ENABLED

        if (m_tokenizer) {
            auto token_str = m_tokenizer->tokens_to_text({output_info.output_token_id}, true);
            output_info.output_token_str = (token_str) ? token_str.value() : "";
        }
#endif // HAILO_CLIENT_TOKENIZER_ENABLED

        // Enqueue token for client consumption
        auto enqueue_result = m_client_token_queue.enqueue(std::make_pair(output_info.output_token_str, status), timeout_guard.get_remaining_timeout());
        if (HAILO_SUCCESS != enqueue_result) {
            if (HAILO_SHUTDOWN_EVENT_SIGNALED == enqueue_result) {
                break; // Normal shutdown
            } else if (HAILO_TIMEOUT == enqueue_result) {
                LOGGER__WARNING("Client token queue is full, dropping token");
                continue;
            } else {
                LOGGER__ERROR("Failed to enqueue token to client queue: {}", enqueue_result);
                break;
            }
        }

        // Check if generation is complete
        if (status != Status::GENERATING) {
            break;
        }

        // Prepare for next iteration
        prepare_next_iteration(input, output_info.output_token_id);
    }

    // Signal shutdown for any blocking user's read()
    if (m_shutdown_event && (0 == m_client_token_queue.size_approx())) {
        m_shutdown_event->signal();
    }
}

hailo_status LLMGeneratorCompletion::Impl::prepare_client_side_embeddings(LLMGeneratorReadSerializer::TextGenerationInput &input)
{
#ifndef HAILO_CLIENT_TOKENIZER_ENABLED
    (void)input;
    LOGGER__ERROR("Host-tokenization is not enabled in lib compilation");
    return HAILO_NOT_IMPLEMENTED;
#else
    std::vector<int> tokens_to_embed = input.tokens; // Either prefix-tokens for first iteration, or single input token for subsequent iterations


    if (!input.initial_prompt.empty()) {
        // First iteration - tokenize prompt and combine with prefix tokens
        TRY(auto prompt_tokens, m_tokenizer->text_to_tokens(input.initial_prompt));
        tokens_to_embed.insert(tokens_to_embed.end(), prompt_tokens.begin(), prompt_tokens.end());

        // Update input.tokens and clear prompt for consistency
        input.tokens = tokens_to_embed;
        input.initial_prompt.clear();
    }

    // Convert tokens to embeddings
    input.embeddings.clear();
    auto embeddings = m_token_embedder->tokens_to_embeddings(tokens_to_embed);
    for (auto &embedding : embeddings) {
        TRY(auto buffer, Buffer::create_shared(embedding.data(), embedding.size(), BufferStorageParams::create_dma()));
        input.embeddings.push_back(buffer);
    }

    return HAILO_SUCCESS;
#endif // HAILO_CLIENT_TOKENIZER_ENABLED
}

Expected<std::pair<LLMGeneratorReadSerializer::TextGenerationOutput, LLMGeneratorCompletion::Status>>
LLMGeneratorCompletion::Impl::send_read_request(const LLMGeneratorReadSerializer::TextGenerationInput &input, std::chrono::milliseconds timeout)
{
    TRY(auto read_request, LLMGeneratorReadSerializer::serialize_request(timeout, input));
    TRY(auto read_reply, m_session->execute(MemoryView(read_request)));
    TRY(auto response_pair, LLMGeneratorReadSerializer::deserialize_reply(MemoryView(*read_reply)));
    
    return response_pair;
}

void LLMGeneratorCompletion::Impl::prepare_next_iteration(LLMGeneratorReadSerializer::TextGenerationInput &input, int next_token_id)
{
    input.tokens = { next_token_id };
    input.initial_prompt.clear();
    input.embeddings.clear();
}

Expected<size_t> LLMGeneratorCompletion::read(char *output, size_t output_size, std::chrono::milliseconds timeout)
{
    return m_pimpl->read(output, output_size, timeout);
}

Expected<size_t> LLMGeneratorCompletion::Impl::read(char *output, size_t output_size, std::chrono::milliseconds timeout)
{
    TRY(auto str, read(timeout));
    CHECK(output_size > str.size(), HAILO_INSUFFICIENT_BUFFER, "Output buffer is too small. received token: '{}' is too large.", str);
    std::strncpy(output, str.c_str(), str.size());

    return str.size();
}

Expected<std::string> LLMGeneratorCompletion::read(std::chrono::milliseconds timeout)
{
    return m_pimpl->read(timeout);
}

Expected<std::string> LLMGeneratorCompletion::Impl::read(std::chrono::milliseconds timeout)
{
    // Thread-safe status check
    {
        std::lock_guard<std::mutex> lock(m_status_mutex);
        CHECK((m_generation_status == Status::GENERATING), HAILO_INVALID_OPERATION,
            "read() cannot be called after generation completed!");
    }

    // Read from the client-side queue instead of making server requests
    auto token_pair_exp = m_client_token_queue.dequeue(timeout);
    if (!token_pair_exp) {
        if (HAILO_TIMEOUT == token_pair_exp.status()) {
            return make_unexpected(HAILO_TIMEOUT);
        } else if (HAILO_SHUTDOWN_EVENT_SIGNALED == token_pair_exp.status()) {
            return make_unexpected(HAILO_INVALID_OPERATION);
        } else {
            return make_unexpected(token_pair_exp.status());
        }
    }

    auto [token, status] = token_pair_exp.value();
    {
        std::lock_guard<std::mutex> lock(m_status_mutex);
        m_generation_status = status;
    }

    return std::string(token);
}

Expected<std::string> LLMGeneratorCompletion::read_all(std::chrono::milliseconds timeout)
{
    TimeoutGuard timeout_guard(timeout);
    std::string all_tokens;
    while (generation_status() == Status::GENERATING) {
        TRY(auto token, read(timeout_guard.get_remaining_timeout()));
        all_tokens += token;
    }
    return all_tokens;
}

LLMGeneratorCompletion::Status LLMGeneratorCompletion::generation_status() const
{
    return m_pimpl->generation_status();
}

LLMGeneratorCompletion::Status LLMGeneratorCompletion::Impl::generation_status() const
{
    std::lock_guard<std::mutex> lock(m_status_mutex);
    return m_generation_status;
}

hailo_status LLMGeneratorCompletion::abort()
{
    return m_pimpl->abort();
}

hailo_status LLMGeneratorCompletion::Impl::abort()
{
    // Send abort request to server
    TRY(auto abort_request, LLMGeneratorAbortSerializer::serialize_request());
    TRY(auto abort_reply, m_session->execute(MemoryView(abort_request)));
    CHECK_SUCCESS(LLMGeneratorAbortSerializer::deserialize_reply(MemoryView(*abort_reply)), "Failed to abort generation");

    // The server will abort the generation, and return the recovery sequence with ABORTED status on last token
    // Wait for token reader thread to finish naturally (don't signal shutdown - let recovery tokens flush)
    if (m_token_reader_thread.joinable()) {
        m_token_reader_thread.join();
    }

    return HAILO_SUCCESS;
}

// https://stackoverflow.com/questions/71104545/constructor-and-destructor-in-c-when-using-the-pimpl-idiom
// All member functions shoud be implemented in the cpp module
LLM::~LLM() = default;
LLM::LLM(LLM &&) = default;

LLMGenerator::~LLMGenerator() = default;
LLMGenerator::LLMGenerator(LLMGenerator &&) = default;

LLMGeneratorCompletion::~LLMGeneratorCompletion() = default;
LLMGeneratorCompletion::LLMGeneratorCompletion(LLMGeneratorCompletion &&) = default;


} /* namespace genai */
} /* namespace hailort */
