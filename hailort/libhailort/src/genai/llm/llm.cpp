/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file llm.cpp
 * @brief LLM implementation
 **/

#include "llm_internal.hpp"

#include "hailo/genai/llm/llm.hpp"
#include "genai/genai_common.hpp"

#include "hailo/hailort.h"
#include "common/filesystem.hpp"
#include "common/utils.hpp"
#include "common/file_utils.hpp"

#include "common/genai/serializer/serializer.hpp"
#include "common/genai/connection_ports.hpp"

#include <numeric>

namespace hailort
{
namespace genai
{

constexpr std::chrono::milliseconds LLMGeneratorCompletion::DEFAULT_READ_TIMEOUT;

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
    if (BUILTIN == llm_params.hef()) {
        // When using BUILTIN HEF, LoRA must be set
        CHECK(!llm_params.lora().empty(), HAILO_INVALID_OPERATION,
            "Failed to create LLM. When using '{}' model, LoRA name must be set.", BUILTIN);
    }

    auto vdevice_params = vdevice->get_params();
    CHECK_SUCCESS(GenAICommon::validate_genai_vdevice_params(vdevice_params));

    std::string device_id = (nullptr != vdevice_params.device_ids) ? vdevice_params.device_ids[0].id : "";
    TRY(auto hailo_session, Session::connect(DEFAULT_LLM_CONNECTION_PORT, device_id));
    auto session_wrapper = make_shared_nothrow<SessionWrapper>(hailo_session);
    CHECK_NOT_NULL_AS_EXPECTED(session_wrapper, HAILO_OUT_OF_HOST_MEMORY);

    TRY(auto create_llm_request, LLMCreateSerializer::serialize_request(vdevice_params, llm_params));
    CHECK_SUCCESS(session_wrapper->write(MemoryView(create_llm_request)), "Failed to load LLM hef");
    // If HEF is not builtin, write it to the server
    if (BUILTIN != llm_params.hef()) {
        TRY(auto file_data, read_binary_file(llm_params.hef(), BufferStorageParams::create_dma()));
        CHECK_SUCCESS(session_wrapper->write(MemoryView(file_data), LONG_TIMEOUT));
    }
    TRY(auto create_llm_reply, session_wrapper->read(LONG_TIMEOUT));
    CHECK_SUCCESS(LLMCreateSerializer::deserialize_reply(MemoryView(*create_llm_reply)), "Failed to create LLM");

    TRY(auto get_generator_default_params_request, LLMGetGeneratorParamsSerializer::serialize_request());
    CHECK_SUCCESS(session_wrapper->write(MemoryView(get_generator_default_params_request)), "Failed to get default generator params");
    TRY_V(auto get_generator_default_params, session_wrapper->read());
    TRY(auto default_generator_params, LLMGetGeneratorParamsSerializer::deserialize_reply(MemoryView(*get_generator_default_params)));

    auto llm_ptr = make_unique_nothrow<Impl>(session_wrapper, llm_params, default_generator_params);
    CHECK_NOT_NULL_AS_EXPECTED(llm_ptr, HAILO_OUT_OF_HOST_MEMORY);
    return llm_ptr;
}

LLM::Impl::Impl(std::shared_ptr<SessionWrapper> session, const LLMParams &llm_params,
    const LLMGeneratorParams &default_generator_params) :
        m_session(session), m_llm_params(llm_params), m_default_generator_params(default_generator_params)
{}

LLM::Impl::~Impl()
{
    auto release_request = LLMReleaseSerializer::serialize_request();
    if (!release_request) {
        LOGGER__CRITICAL("Failed to serialize LLM release request with status {}", release_request.status());
        return;
    }
    auto status = m_session->write(MemoryView(*release_request));
    if (HAILO_SUCCESS != status) {
        LOGGER__CRITICAL("Failed to write LLM release request with status {}", status);
        return;
    }
    auto release_reply = m_session->read();
    if (!release_reply) {
        LOGGER__CRITICAL("Failed to read LLM release reply with status {}", release_reply.status());
        return;
    }
    status = LLMReleaseSerializer::deserialize_reply(MemoryView(*release_reply));
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
    CHECK_SUCCESS(m_session->write(MemoryView(request)), "Failed to tokenize prompt");
    TRY(auto reply, m_session->read());
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
    CHECK_SUCCESS(m_session->write(MemoryView(request)), "Failed to clear context");
    TRY(auto reply, m_session->read());
    CHECK_SUCCESS(LLMClearContextSerializer::deserialize_reply(MemoryView(*reply)),
        "Failed to clear context. Make sure there is no other generation in progress");
    return HAILO_SUCCESS;
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
    CHECK_SUCCESS(m_session->write(MemoryView(create_generator_request)), "Failed to create LLM generator");
    TRY(auto create_generator_reply, m_session->read());
    CHECK_SUCCESS(LLMGeneratorCreateSerializer::deserialize_reply(MemoryView(*create_generator_reply)), "Failed to create LLM generator");

    auto pimpl = std::make_unique<LLMGenerator::Impl>(m_session);
    CHECK_NOT_NULL_AS_EXPECTED(pimpl, HAILO_OUT_OF_HOST_MEMORY);
    return LLMGenerator(std::move(pimpl));
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

LLMGenerator::LLMGenerator(std::unique_ptr<Impl> pimpl) :
    m_pimpl(std::move(pimpl))
{}

LLMGenerator::Impl::Impl(std::shared_ptr<SessionWrapper> session) :
    m_session(session)
{}

hailo_status LLMGenerator::write(const char *prompt, size_t prompt_size)
{
    return write(std::string(prompt, prompt_size));
}

hailo_status LLMGenerator::write(const std::string &prompt)
{
    return m_pimpl->write(prompt);
}

hailo_status LLMGenerator::Impl::write(const std::string &prompt)
{
    TRY(auto generator_write_request, LLMGeneratorWriteSerializer::serialize_request());
    CHECK_SUCCESS(m_session->write(MemoryView(generator_write_request)), "Failed to write prompt");
    CHECK_SUCCESS(m_session->write(MemoryView(prompt)), "Failed to write prompt");
    TRY(auto generator_write_reply, m_session->read());
    CHECK_SUCCESS(LLMGeneratorWriteSerializer::deserialize_reply(MemoryView(*generator_write_reply)), "Failed to write prompt");

    return HAILO_SUCCESS;
}

Expected<LLMGeneratorCompletion> LLMGenerator::generate()
{
    return m_pimpl->generate();
}

Expected<LLMGeneratorCompletion> LLMGenerator::Impl::generate()
{
    TRY(auto generator_generate_request, LLMGeneratorGenerateSerializer::serialize_request());
    CHECK_SUCCESS(m_session->write(MemoryView(generator_generate_request)), "Failed to generate");
    TRY(auto generator_generate_reply, m_session->read());
    CHECK_SUCCESS(LLMGeneratorGenerateSerializer::deserialize_reply(MemoryView(*generator_generate_reply)),
        "Failed to generate. Make sure there is no other generation in progress");

    auto pimpl = make_unique_nothrow<LLMGeneratorCompletion::Impl>(m_session);
    CHECK_NOT_NULL_AS_EXPECTED(pimpl, HAILO_OUT_OF_HOST_MEMORY);
    return LLMGeneratorCompletion(std::move(pimpl));
}

LLMGeneratorCompletion::LLMGeneratorCompletion(std::unique_ptr<Impl> pimpl) :
    m_pimpl(std::move(pimpl))
{}

LLMGeneratorCompletion::Impl::Impl(std::shared_ptr<SessionWrapper> session) :
    m_session(session),
    m_generation_status(Status::GENERATING)
{}

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
    CHECK_SUCCESS(m_session->write(MemoryView(read_request)), "Failed to read");
    TRY(auto read_reply, m_session->read());
    TRY(auto pair, LLMGeneratorReadSerializer::deserialize_reply(MemoryView(*read_reply)));
    auto next_token = pair.first;
    m_generation_status = pair.second;

    return next_token;
}

LLMGeneratorCompletion::Status LLMGeneratorCompletion::generation_status() const
{
    return m_pimpl->generation_status();
}

LLMGeneratorCompletion::Status LLMGeneratorCompletion::Impl::generation_status() const
{
    return m_generation_status;
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
