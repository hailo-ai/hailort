/**
 * Copyright (c) 2019-2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file llm.cpp
 * @brief LLM implementation
 **/

#include "llm_internal.hpp"

#include "hailo/genai/llm/llm.hpp"

#include "hailo/hailort.h"
#include "common/filesystem.hpp"
#include "common/utils.hpp"
#include "common/file_utils.hpp"
#include "common/utils.hpp"

#include <numeric>

namespace hailort
{
namespace genai
{

constexpr std::chrono::milliseconds LLMGeneratorCompletion::DEFAULT_READ_TIMEOUT;
const uint16_t DEFAULT_LLM_CONNECTION_PORT = 12145;
const std::string FILE_NOT_FOUND = "<file_not_found>";

// TODO (HRT-15334): Move the logic to server side
const std::string EOF_TOEKN = "<|endoftext|>";
const std::string IM_END_TOEKN = "<|im_end|>";

// TODO (HRT-15334): - adjusting all ack's once server is written in cpp
const size_t SERVER_ACK_SIZE = 32;

hailo_status LLMParams::set_model(const std::string &hef_path, const std::string &lora_name)
{
    auto status = set_hef(hef_path);
    CHECK_SUCCESS(status);

    m_lora = lora_name;
    CHECK(lora_name.empty(), HAILO_NOT_IMPLEMENTED, "Setting LoRA is not implemented.");

    return HAILO_SUCCESS;
}

hailo_status LLMParams::set_hef(const std::string &path)
{
    m_hef_path = path;

    CHECK(((BUILTIN == path) || Filesystem::does_file_exists(path)), HAILO_OPEN_FILE_FAILURE,
        "Hef file {} does not exist", path);

    return HAILO_SUCCESS;
}

std::string LLMParams::hef() const
{
    return m_hef_path;
}

std::string LLMParams::lora() const
{
    return m_lora;
}

hailo_status LLMParams::set_vocabulary(const std::string &vocabulary_path)
{
    m_vocabulary_path = vocabulary_path;

    CHECK((BUILTIN == vocabulary_path) || Filesystem::does_file_exists(vocabulary_path), HAILO_OPEN_FILE_FAILURE,
        "vocabulary file {} does not exist", vocabulary_path);

    return HAILO_SUCCESS;
}

std::string LLMParams::vocabulary() const
{
    return m_vocabulary_path;
}

hailo_status LLMGeneratorParams::set_temperature(float32_t temperature)
{
    m_temperature = temperature;

    // TODO (HRT-15334): Implement when server is in C++
    LOGGER__ERROR("`set_temperature` function is not supported yet");
    return HAILO_NOT_IMPLEMENTED;
}

float32_t LLMGeneratorParams::temperature() const
{
    return m_temperature;
}

hailo_status LLMGeneratorParams::set_top_p(float32_t top_p)
{
    m_top_p = top_p;

    // TODO (HRT-15334): Implement when server is in C++
    LOGGER__ERROR("`set_top_p` function is not supported yet");
    return HAILO_NOT_IMPLEMENTED;
}

float32_t LLMGeneratorParams::top_p() const
{
    return m_top_p;
}

Expected<LLM> LLM::create(std::shared_ptr<VDeviceGenAI> vdevice, const LLMParams &llm_params)
{
    TRY(auto pimpl, Impl::create_unique(vdevice, llm_params));
    return LLM(std::move(pimpl));
}

Expected<std::unique_ptr<LLM::Impl>> LLM::Impl::create_unique(std::shared_ptr<VDeviceGenAI> vdevice, const LLMParams &llm_params)
{
    CHECK(llm_params.lora().empty(), HAILO_NOT_IMPLEMENTED, "Failed to create LLM. Setting LoRA is not Implemented.");
    CHECK(!llm_params.hef().empty(), HAILO_INVALID_OPERATION, "Failed to create LLM. HEF was not set.");
    CHECK(!llm_params.vocabulary().empty(), HAILO_INVALID_OPERATION, "Failed to create LLM. Vocabulary was not set.");

    TRY(auto session, vdevice->create_session(DEFAULT_LLM_CONNECTION_PORT));

    auto status = send_data_file(session, llm_params.hef());
    CHECK_SUCCESS(status, "Failed to load LLM hef");

    status = send_data_file(session, llm_params.vocabulary());
    CHECK_SUCCESS(status, "Failed to load LLM vocabulary");

    // Ack from server - finished hef configure
    uint8_t server_ack[SERVER_ACK_SIZE] = {};
    TRY(auto size, session->read(server_ack, SERVER_ACK_SIZE));
    std::string config_finished_ack = (0 == size)? "" : std::string(reinterpret_cast<const char*>(server_ack), size);
    LOGGER__INFO("Got ack from server: {}", config_finished_ack);

    auto llm = Impl(session, llm_params);
    auto llm_ptr = std::make_unique<Impl>(std::move(llm));
    CHECK_NOT_NULL_AS_EXPECTED(llm_ptr, HAILO_OUT_OF_HOST_MEMORY);
    return llm_ptr;
}

LLM::Impl::Impl(std::shared_ptr<GenAISession> session, const LLMParams &llm_params) :
    m_session(session), m_llm_params(llm_params)
{}

LLM::LLM(std::unique_ptr<Impl> pimpl) :
    m_pimpl(std::move(pimpl))
{}

Expected<LLMGenerator> LLM::create_generator(const LLMGeneratorParams &params)
{
    return m_pimpl->create_generator(params);
}

Expected<LLMGenerator> LLM::Impl::create_generator(const LLMGeneratorParams &params)
{
    CHECK_SUCCESS(LLMGenerator::Impl::validate_params(params));
    auto pimpl = std::make_unique<LLMGenerator::Impl>(m_session);
    CHECK_NOT_NULL_AS_EXPECTED(pimpl, HAILO_OUT_OF_HOST_MEMORY);
    return LLMGenerator(std::move(pimpl));
}

hailo_status LLMGenerator::Impl::validate_params(const LLMGeneratorParams &params)
{
    CHECK_AS_EXPECTED(0 == params.temperature(), HAILO_NOT_IMPLEMENTED,
        "Setting generator's temperature is not implemented.");
    CHECK_AS_EXPECTED(0 == params.top_p(), HAILO_NOT_IMPLEMENTED,
        "Setting generator's temperature is not implemented.");

    return HAILO_SUCCESS;
}

LLMGenerator::LLMGenerator(std::unique_ptr<Impl> pimpl) :
    m_pimpl(std::move(pimpl))
{}

LLMGenerator::Impl::Impl(std::shared_ptr<GenAISession> session) :
    m_session(session),
    m_mutex(),
    m_should_stop_write(false)
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
    // TODO (HRT-15334): - When implementing cpp server side, write the prompt directly to server
    std::unique_lock<std::mutex> lock(m_mutex);
    CHECK(!m_should_stop_write, HAILO_INVALID_OPERATION, "write() cannot be called once the generation started!");
    m_prompts.emplace_back(prompt);

    return HAILO_SUCCESS;
}

std::string concat_prompts(const std::vector<std::string> &prompts)
{
    return std::accumulate(prompts.begin(), prompts.end(), std::string());
}

Expected<LLMGeneratorCompletion> LLMGenerator::generate()
{
    return m_pimpl->generate();
}

Expected<LLMGeneratorCompletion> LLMGenerator::Impl::generate()
{
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_should_stop_write = true;
    }

    auto prompt = concat_prompts(m_prompts);
    CHECK_AS_EXPECTED(!prompt.empty(), HAILO_INVALID_OPERATION, "Generate on empty prompt is invalid");

    auto status = m_session->write(reinterpret_cast<const uint8_t*>(prompt.c_str()), prompt.size());
    CHECK_SUCCESS(status);

    auto pimpl = std::make_unique<LLMGeneratorCompletion::Impl>(m_session);
    CHECK_NOT_NULL_AS_EXPECTED(pimpl, HAILO_OUT_OF_HOST_MEMORY);
    return LLMGeneratorCompletion(std::move(pimpl));
}

LLMGeneratorCompletion::LLMGeneratorCompletion(std::unique_ptr<Impl> pimpl) :
    m_pimpl(std::move(pimpl))
{}

LLMGeneratorCompletion::Impl::Impl(std::shared_ptr<GenAISession> session) :
    m_session(session),
    m_mutex(),
    m_end_of_generation(false)
{}

Expected<size_t> LLMGeneratorCompletion::read(char *output, size_t output_size, std::chrono::milliseconds timeout)
{
    return m_pimpl->read(output, output_size, timeout);
}

Expected<size_t> LLMGeneratorCompletion::Impl::read(char *output, size_t output_size, std::chrono::milliseconds timeout)
{
    auto start_time = std::chrono::steady_clock::now();
    CHECK(!m_end_of_generation, HAILO_INVALID_OPERATION, "read() cannot be called after generation completed!");
    TRY(auto bytes_read, m_session->read(reinterpret_cast<uint8_t*>(output), output_size, timeout));

    // TODO (HRT-15334): Move logic to server
    // TODO: if IM_END_TOEKN is splitted acroos multiple reads we wont know it
    if ((bytes_read == IM_END_TOEKN.size()) && (0 == memcmp(output, IM_END_TOEKN.c_str(), IM_END_TOEKN.size()))) {
        std::vector<uint8_t> eof_token(EOF_TOEKN.size());
        auto elapsed_time =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time);
        TRY(auto res_eof_size, m_session->read(eof_token.data(), eof_token.size(), (timeout - elapsed_time)));
        CHECK((res_eof_size == EOF_TOEKN.size() && (0 == memcmp(eof_token.data(), EOF_TOEKN.c_str(), EOF_TOEKN.size()))),
            HAILO_INTERNAL_FAILURE, "EOF token {} should come after IM_END token {}", EOF_TOEKN, IM_END_TOEKN);

        m_end_of_generation = true;
        return 0;
    }

    return bytes_read;
}

Expected<std::string> LLMGeneratorCompletion::read(std::chrono::milliseconds timeout)
{
    return m_pimpl->read(timeout);
}

Expected<std::string> LLMGeneratorCompletion::Impl::read(std::chrono::milliseconds timeout)
{
    const size_t READ_CHUNK_MAX_SIZE = 1024; // High number to make sure EOF_TOEKNis not splitted across multiple reads
    char res[READ_CHUNK_MAX_SIZE] = {};

    TRY(auto size, read(res, READ_CHUNK_MAX_SIZE, timeout));
    if (0 == size) {
        return std::string("");
    }
    return std::string(res, size);
}

bool LLMGeneratorCompletion::end_of_generation() const
{
    return m_pimpl->end_of_generation();
}

bool LLMGeneratorCompletion::Impl::end_of_generation() const
{
    return m_end_of_generation;
}

hailo_status LLM::Impl::send_data_file(std::shared_ptr<GenAISession> session, const std::string &path)
{
    if ((BUILTIN == path)) {
        // Write the `BUILTIN` indicator
        auto status = session->write(reinterpret_cast<const uint8_t*>(path.c_str()), path.size());
        CHECK_SUCCESS(status);
    } else {
        // Send file bytes
        TRY(auto file_data, read_binary_file(path, BufferStorageParams::create_dma()));
        auto status = session->write(file_data.data(), file_data.size());
        CHECK_SUCCESS(status);
    }

    // TODO (HRT-15334): - adjusting all ack's once server is written in cpp
    uint8_t server_ack[SERVER_ACK_SIZE] = {}; // Ack is "HEF Config done, ack returned")
    TRY(auto size, session->read(server_ack, SERVER_ACK_SIZE));
    std::string output = (0 == size) ? "" : std::string(reinterpret_cast<const char*>(server_ack), size);
    CHECK(output != FILE_NOT_FOUND, HAILO_NOT_FOUND, "Builtin file does not exist");
    LOGGER__INFO("Sent {}, Got ack: {}", path, output);

    return HAILO_SUCCESS;
}

// https://stackoverflow.com/questions/71104545/constructor-and-destructor-in-c-when-using-the-pimpl-idiom
// All member functions shoud be implemented in the cpp module
LLM::~LLM() = default;
LLM::LLM(LLM &&) = default;

LLMGenerator::~LLMGenerator() = default;
LLMGenerator::LLMGenerator(LLMGenerator &&other) = default;

LLMGeneratorCompletion::~LLMGeneratorCompletion() = default;
LLMGeneratorCompletion::LLMGeneratorCompletion(LLMGeneratorCompletion &&) = default;

} /* namespace genai */
} /* namespace hailort */
