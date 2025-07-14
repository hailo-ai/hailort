/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vlm.cpp
 * @brief VLM implementation
 **/

#include "vlm_internal.hpp"
#include "llm/llm_internal.hpp"
#include "genai/genai_common.hpp"

#include "hailo/genai/vlm/vlm.hpp"

#include "hailo/hailort.h"
#include "hailo/hailort_common.hpp"
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

hailo_status VLMParams::set_model(const std::string &hef_path)
{
    m_hef_path = hef_path;

    CHECK(Filesystem::does_file_exists(hef_path), HAILO_OPEN_FILE_FAILURE,
        "Hef file '{}' does not exist", hef_path);

    return HAILO_SUCCESS;
}

const std::string& VLMParams::hef() const
{
    return m_hef_path;
}

Expected<VLM> VLM::create(std::shared_ptr<hailort::VDevice> vdevice, const VLMParams &vlm_params)
{
    TRY(auto pimpl, Impl::create_unique(vdevice, vlm_params));
    return VLM(std::move(pimpl));
}

Expected<std::unique_ptr<VLM::Impl>> VLM::Impl::create_unique(std::shared_ptr<hailort::VDevice> vdevice, const VLMParams &vlm_params)
{
    CHECK(!vlm_params.hef().empty(), HAILO_INVALID_OPERATION, "Failed to create VLM. HEF was not set.");

    auto vdevice_params = vdevice->get_params();
    CHECK_SUCCESS(GenAICommon::validate_genai_vdevice_params(vdevice_params));

    std::string device_id = (nullptr != vdevice_params.device_ids) ? vdevice_params.device_ids[0].id : "";
    TRY(auto hailo_session, Session::connect(DEFAULT_VLM_CONNECTION_PORT, device_id));
    auto session_wrapper = make_shared_nothrow<SessionWrapper>(hailo_session);
    CHECK_NOT_NULL_AS_EXPECTED(session_wrapper, HAILO_OUT_OF_HOST_MEMORY);

    TRY(auto create_vlm_request, VLMCreateSerializer::serialize_request(vdevice_params));
    CHECK_SUCCESS(session_wrapper->write(MemoryView(create_vlm_request)), "Failed to load VLM hef");
    // Write HEF to the server
    TRY(auto file_data, read_binary_file(vlm_params.hef(), BufferStorageParams::create_dma()));
    CHECK_SUCCESS(session_wrapper->write(MemoryView(file_data), LONG_TIMEOUT));

    TRY(auto create_vlm_reply, session_wrapper->read(LONG_TIMEOUT));
    TRY(auto input_frame_info_pair, VLMCreateSerializer::deserialize_reply(MemoryView(*create_vlm_reply)), "Failed to create VLM");
    auto input_frame_shape = input_frame_info_pair.first;
    auto input_frame_format = input_frame_info_pair.second;

    TRY(auto get_generator_default_params_request, LLMGetGeneratorParamsSerializer::serialize_request());
    CHECK_SUCCESS(session_wrapper->write(MemoryView(get_generator_default_params_request)), "Failed to get default generator params");
    TRY(auto get_generator_default_params, session_wrapper->read());
    TRY(auto default_generator_params, LLMGetGeneratorParamsSerializer::deserialize_reply(MemoryView(*get_generator_default_params)));

    auto vlm_ptr = make_unique_nothrow<Impl>(session_wrapper, vlm_params, default_generator_params, input_frame_shape, input_frame_format);
    CHECK_NOT_NULL_AS_EXPECTED(vlm_ptr, HAILO_OUT_OF_HOST_MEMORY);
    return vlm_ptr;
}

VLM::Impl::Impl(std::shared_ptr<SessionWrapper> session, const VLMParams &vlm_params,
    const LLMGeneratorParams &default_generator_params, hailo_3d_image_shape_t input_frame_shape,
    hailo_format_t input_frame_format) :
        m_session(session), m_vlm_params(vlm_params), m_default_generator_params(default_generator_params),
        m_input_frame_shape(input_frame_shape), m_input_frame_format(input_frame_format)
{}

VLM::Impl::~Impl()
{
    auto release_request = LLMReleaseSerializer::serialize_request();
    if (!release_request) {
        LOGGER__CRITICAL("Failed to serialize VLM release request with status {}", release_request.status());
        return;
    }
    auto status = m_session->write(MemoryView(*release_request));
    if (HAILO_SUCCESS != status) {
        LOGGER__CRITICAL("Failed to write VLM release request with status {}", status);
        return;
    }
    auto release_reply = m_session->read();
    if (!release_reply) {
        LOGGER__CRITICAL("Failed to read VLM release reply with status {}", release_reply.status());
        return;
    }
    status = LLMReleaseSerializer::deserialize_reply(MemoryView(*release_reply));
    if (HAILO_SUCCESS != status) {
        LOGGER__CRITICAL("Failed to deserialize VLM release reply with status {}", status);
        return;
    }
}

VLM::VLM(std::unique_ptr<Impl> pimpl) :
    m_pimpl(std::move(pimpl))
{}

uint32_t VLM::input_frame_size() const
{
    return m_pimpl->input_frame_size();
}

uint32_t VLM::Impl::input_frame_size() const
{
    return HailoRTCommon::get_frame_size(m_input_frame_shape, m_input_frame_format);
}

const hailo_3d_image_shape_t& VLM::input_frame_shape() const
{
    return m_pimpl->input_frame_shape();
}

const hailo_3d_image_shape_t& VLM::Impl::input_frame_shape() const
{
    return m_input_frame_shape;
}

const hailo_format_type_t& VLM::input_frame_format_type() const
{
    return m_pimpl->input_frame_format_type();
}

const hailo_format_type_t& VLM::Impl::input_frame_format_type() const
{
    return m_input_frame_format.type;
}

const hailo_format_order_t& VLM::input_frame_format_order() const
{
    return m_pimpl->input_frame_format_order();
}

const hailo_format_order_t& VLM::Impl::input_frame_format_order() const
{
    return m_input_frame_format.order;
}

Expected<std::vector<int>> VLM::tokenize(const std::string &prompt)
{
    return m_pimpl->tokenize(prompt);
}

Expected<std::vector<int>> VLM::Impl::tokenize(const std::string &prompt)
{
    TRY(auto request, LLMTokenizeSerializer::serialize_request(prompt));
    CHECK_SUCCESS(m_session->write(MemoryView(request)), "Failed to tokenize prompt");
    TRY(auto reply, m_session->read());
    TRY(auto tokens, LLMTokenizeSerializer::deserialize_reply(MemoryView(*reply)));
    return tokens;
}

hailo_status VLM::clear_context()
{
    return m_pimpl->clear_context();
}

hailo_status VLM::Impl::clear_context()
{
    TRY(auto request, LLMClearContextSerializer::serialize_request());
    CHECK_SUCCESS(m_session->write(MemoryView(request)), "Failed to clear context");
    TRY(auto reply, m_session->read());
    CHECK_SUCCESS(LLMClearContextSerializer::deserialize_reply(MemoryView(*reply)),
        "Failed to clear context. Make sure there is no other generation in progress");
    return HAILO_SUCCESS;
}

Expected<VLMGenerator> VLM::create_generator(const LLMGeneratorParams &params)
{
    return m_pimpl->create_generator(params);
}

Expected<VLMGenerator> VLM::create_generator()
{
    TRY(auto generator_params, create_generator_params());
    return m_pimpl->create_generator(generator_params);
}

Expected<LLMGeneratorParams> VLM::create_generator_params()
{
    return m_pimpl->create_generator_params();
}

Expected<LLMGeneratorParams> VLM::Impl::create_generator_params()
{
    auto generator_params = m_default_generator_params;

    return generator_params;
}

Expected<VLMGenerator> VLM::Impl::create_generator(const LLMGeneratorParams &params)
{
    CHECK_SUCCESS(validate_generator_params(params));

    TRY(auto create_generator_request, LLMGeneratorCreateSerializer::serialize_request(params));
    CHECK_SUCCESS(m_session->write(MemoryView(create_generator_request)), "Failed to create LLM generator");
    TRY(auto create_generator_reply, m_session->read());
    CHECK_SUCCESS(LLMGeneratorCreateSerializer::deserialize_reply(MemoryView(*create_generator_reply)), "Failed to create LLM generator");

    auto pimpl = std::make_unique<VLMGenerator::Impl>(m_session);
    CHECK_NOT_NULL_AS_EXPECTED(pimpl, HAILO_OUT_OF_HOST_MEMORY);
    return VLMGenerator(std::move(pimpl));
}

hailo_status VLM::Impl::validate_generator_params(const LLMGeneratorParams &params)
{
    CHECK_AS_EXPECTED(0 < params.temperature(), HAILO_INVALID_ARGUMENT,
        "Temperature should be higher than '0'. received: '{}'", params.temperature());
    CHECK_AS_EXPECTED((0 <= params.top_p()) && (params.top_p() <= 1), HAILO_INVALID_ARGUMENT,
        "top_p should be in range [0, 1]. received: '{}'", params.top_p());
    CHECK_AS_EXPECTED(0 < params.top_k(), HAILO_INVALID_ARGUMENT,
        "top_k should be greater than or equal to '1'. received: '{}'", params.top_k());
    CHECK_AS_EXPECTED(0 != params.frequency_penalty(), HAILO_INVALID_ARGUMENT,
        "frequency_penalty must be a nonzero value. received: '{}'", params.frequency_penalty());
    CHECK_AS_EXPECTED(2 <= params.max_generated_tokens(), HAILO_INVALID_ARGUMENT,
        "max_generated_tokens should be greater than '1'. received: '{}'", params.max_generated_tokens());
    return HAILO_SUCCESS;
}

VLMGenerator::VLMGenerator(std::unique_ptr<Impl> pimpl) :
    m_pimpl(std::move(pimpl))
{}

VLMGenerator::Impl::Impl(std::shared_ptr<SessionWrapper> session) :
    m_session(session)
{}

Expected<LLMGeneratorCompletion> VLMGenerator::generate(const char *prompt, size_t prompt_size, const std::vector<MemoryView> &input_frames)
{
    return generate(std::string(prompt, prompt_size), input_frames);
}

Expected<LLMGeneratorCompletion> VLMGenerator::generate(const std::string &prompt, const std::vector<MemoryView> &input_frames)
{
    return m_pimpl->generate(prompt, input_frames);
}

Expected<LLMGeneratorCompletion> VLMGenerator::Impl::generate(const std::string &prompt, const std::vector<MemoryView> &input_frames)
{
    CHECK(!(prompt.empty() && input_frames.empty()), HAILO_INVALID_ARGUMENT, "Prompt and input_frames are empty");

    TRY(auto generator_generate_request, VLMGeneratorGenerateSerializer::serialize_request(static_cast<uint32_t>(input_frames.size())));
    CHECK_SUCCESS(m_session->write(MemoryView(generator_generate_request)), "Failed to generate");
    CHECK_SUCCESS(m_session->write(MemoryView(prompt)), "Failed to write prompt");

    // Write input frames to the server
    for (const auto &input_frame : input_frames) {
        CHECK_SUCCESS(m_session->write(input_frame), "Failed to write input frame");
    }

    TRY(auto generator_generate_reply, m_session->read());
    CHECK_SUCCESS(VLMGeneratorGenerateSerializer::deserialize_reply(MemoryView(*generator_generate_reply)),
        "Failed to generate. Make sure the number of passed frames ('{}') matches the number of frames in the prompt," \
         " each passed frame size matches the expected input frame size (see 'VLM::input_frame_size'), and there is no other generation in progress",
            input_frames.size());

    auto pimpl = make_unique_nothrow<LLMGeneratorCompletion::Impl>(m_session);
    CHECK_NOT_NULL_AS_EXPECTED(pimpl, HAILO_OUT_OF_HOST_MEMORY);
    return LLMGeneratorCompletion(std::move(pimpl));
}

// https://stackoverflow.com/questions/71104545/constructor-and-destructor-in-c-when-using-the-pimpl-idiom
// All member functions shoud be implemented in the cpp module
VLM::~VLM() = default;
VLM::VLM(VLM &&) = default;

VLMGenerator::~VLMGenerator() = default;
VLMGenerator::VLMGenerator(VLMGenerator &&) = default;

} /* namespace genai */
} /* namespace hailort */
