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

#include <filesystem>

#include "common/genai/serializer/serializer.hpp"
#include "common/genai/connection_ports.hpp"

#include <numeric>

namespace hailort
{
namespace genai
{

constexpr std::chrono::milliseconds LLMGeneratorCompletion::DEFAULT_READ_TIMEOUT;

LLMParams::LLMParams(const std::string &hef_path, const std::string &lora_name) :
    m_hef_path(hef_path), m_lora(lora_name)
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
    if (!is_builtin && !hef_exists) {
        // Get file size for chunked transfer
        TRY(auto file_size, SessionWrapper::get_file_size(hef_path));

        // Create request with file size for chunked transfer
        TRY(create_llm_request, LLMCreateSerializer::serialize_request(vdevice_params, llm_params, "", file_size));
        CHECK_SUCCESS(session_wrapper->write(MemoryView(create_llm_request)));
        CHECK_SUCCESS(session_wrapper->send_file_chunked(hef_path));
        TRY(create_llm_reply, session_wrapper->read());
    } else {
        std::string hef_path_to_send = hef_exists ? hef_path : is_builtin ? BUILTIN : "";
        TRY(create_llm_request, LLMCreateSerializer::serialize_request(vdevice_params, llm_params, hef_path_to_send));
        TRY(create_llm_reply, session_wrapper->execute(MemoryView(create_llm_request)));
    }
    TRY(auto prompt_template, LLMCreateSerializer::deserialize_reply(MemoryView(*create_llm_reply)), "Failed to create LLM");

    TRY(auto get_generator_default_params_request, LLMGetGeneratorParamsSerializer::serialize_request());
    TRY_V(auto get_generator_default_params, session_wrapper->execute(MemoryView(get_generator_default_params_request)));
    TRY(auto default_generator_params, LLMGetGeneratorParamsSerializer::deserialize_reply(MemoryView(*get_generator_default_params)));

    TRY(auto prompt_template_handler, PromptTemplateHandler::create(prompt_template));
    auto prompt_template_handler_ptr = make_shared_nothrow<PromptTemplateHandler>(std::move(prompt_template_handler));
    CHECK_NOT_NULL_AS_EXPECTED(prompt_template_handler_ptr, HAILO_OUT_OF_HOST_MEMORY);

    auto llm_ptr = make_unique_nothrow<Impl>(session_wrapper, llm_params, default_generator_params, prompt_template_handler_ptr);
    CHECK_NOT_NULL_AS_EXPECTED(llm_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return llm_ptr;
}

LLM::Impl::Impl(std::shared_ptr<SessionWrapper> session, const LLMParams &llm_params,
    const LLMGeneratorParams &default_generator_params, std::shared_ptr<PromptTemplateHandler> prompt_template_handler) :
        m_session(session), m_llm_params(llm_params), m_default_generator_params(default_generator_params),
        m_prompt_template_handler(prompt_template_handler)
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
    TRY(auto request, LLMTokenizeSerializer::serialize_request(prompt));
    TRY(auto reply, m_session->execute(MemoryView(request)));
    TRY(auto tokens, LLMTokenizeSerializer::deserialize_reply(MemoryView(*reply)));
    return tokens;
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
    TRY(auto set_end_of_generation_sequence_request, LLMSetEndOfGenerationSequenceSerializer::serialize_request(abort_sequence));
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
    TRY(auto abort_sequence, LLMGetEndOfGenerationSequenceSerializer::deserialize_reply(MemoryView(*get_end_of_generation_sequence_reply)));
    return abort_sequence;
}

hailo_status LLM::set_stop_tokens(const std::vector<std::string> &stop_tokens)
{
    return m_pimpl->set_stop_tokens(stop_tokens);
}

hailo_status LLM::Impl::set_stop_tokens(const std::vector<std::string> &stop_tokens)
{
    TRY(auto set_stop_tokens_request, LLMSetStopTokensSerializer::serialize_request(stop_tokens));
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
    TRY(auto stop_tokens, LLMGetStopTokensSerializer::deserialize_reply(MemoryView(*get_stop_tokens_reply)));
    return stop_tokens;
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

    auto pimpl = make_shared_nothrow<LLMGenerator::Impl>(m_session, m_prompt_template_handler);
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
    CHECK_AS_EXPECTED(1 < params.max_generated_tokens(), HAILO_INVALID_ARGUMENT,
        "max_generated_tokens should be greater than '1'. received: '{}'", params.max_generated_tokens());
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

LLMGenerator::Impl::Impl(std::shared_ptr<SessionWrapper> session, std::shared_ptr<PromptTemplateHandler> prompt_template_handler) :
    m_session(session), m_prompt_template_handler(prompt_template_handler)
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
    std::vector<MemoryView> write_buffers = {MemoryView(generator_write_request), MemoryView(prompt)};
    TRY(auto generator_write_reply, m_session->execute(write_buffers));
    CHECK_SUCCESS(LLMGeneratorWriteSerializer::deserialize_reply(MemoryView(*generator_write_reply)), "Failed to write prompt");

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
    TRY(auto generator_generate_request, LLMGeneratorGenerateSerializer::serialize_request());
    TRY(auto generator_generate_reply, m_session->execute(MemoryView(generator_generate_request)));
    CHECK_SUCCESS(LLMGeneratorGenerateSerializer::deserialize_reply(MemoryView(*generator_generate_reply)),
        "Failed to generate. Make sure there is no other generation in progress");

    auto pimpl = make_unique_nothrow<LLMGeneratorCompletion::Impl>(m_session, shared_from_this());
    CHECK_NOT_NULL_AS_EXPECTED(pimpl, HAILO_OUT_OF_HOST_MEMORY);
    return LLMGeneratorCompletion(std::move(pimpl));
}

LLMGeneratorCompletion::LLMGeneratorCompletion(std::unique_ptr<Impl> pimpl) :
    m_pimpl(std::move(pimpl))
{}


LLMGeneratorCompletion::Impl::Impl(std::shared_ptr<SessionWrapper> session, std::shared_ptr<TextGeneratorBase> generator) :
    m_generator_scope_guard(generator),
    m_session(session),
    m_generation_status(Status::GENERATING)
{}

LLMGeneratorCompletion::Impl::~Impl()
{
    if (m_generation_status == Status::GENERATING) {
        auto status = abort();
        if (HAILO_SUCCESS != status) {
            LOGGER__CRITICAL("Failed to release LLMGeneratorCompletion. Failed to abort LLM generator completion with status {}", status);
        }
        while (m_generation_status == Status::GENERATING) {
            auto token = read(DEFAULT_READ_TIMEOUT);
            if (token.status() != HAILO_SUCCESS) {
                LOGGER__CRITICAL("Failed to release LLMGeneratorCompletion. Failed to read flushed token with status {}", token.status());
                break;
            }
        }
    }
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
    TimeoutGuard timeout_guard(timeout);
    CHECK((m_generation_status == Status::GENERATING), HAILO_INVALID_OPERATION,
        "read() cannot be called after generation completed!");

    // Consider setting shorter timeout in the request, to leave space for comunication overhead
    TRY(auto read_request, LLMGeneratorReadSerializer::serialize_request(timeout_guard.get_remaining_timeout()));
    TRY(auto read_reply, m_session->execute(MemoryView(read_request)));
    TRY(auto pair, LLMGeneratorReadSerializer::deserialize_reply(MemoryView(*read_reply)));
    auto next_token = pair.first;
    m_generation_status = pair.second;

    return next_token;
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
    return m_generation_status;
}

hailo_status LLMGeneratorCompletion::abort()
{
    return m_pimpl->abort();
}

hailo_status LLMGeneratorCompletion::Impl::abort()
{
    TRY(auto abort_request, LLMGeneratorAbortSerializer::serialize_request());
    TRY(auto abort_reply, m_session->execute(MemoryView(abort_request)));
    CHECK_SUCCESS(LLMGeneratorAbortSerializer::deserialize_reply(MemoryView(*abort_reply)), "Failed to abort generation");
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
