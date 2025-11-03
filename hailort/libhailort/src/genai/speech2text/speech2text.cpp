/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file speech2text.cpp
 * @brief HailoRT GenAI Speech2Text implementation of client side.
 **/

#include "speech2text_internal.hpp"
#include "hailo/hailort.h"
#include "common/filesystem.hpp"
#include "common/genai/connection_ports.hpp"
#include "common/genai/serializer/serializer.hpp"
#include "genai/genai_common.hpp"
#include <filesystem>

namespace hailort
{
namespace genai
{

Speech2TextParams::Speech2TextParams(std::string_view hef_path) :
    m_hef_path(hef_path)
{}

hailo_status Speech2TextParams::set_model(std::string_view hef_path)
{
    m_hef_path = hef_path;

    CHECK(std::filesystem::exists(hef_path), HAILO_OPEN_FILE_FAILURE,"Hef file '{}' does not exist", hef_path);

    return HAILO_SUCCESS;
}

std::string_view Speech2TextParams::hef() const
{
    return m_hef_path;
}

Speech2TextGeneratorParams::Speech2TextGeneratorParams() :
    m_task(Speech2TextTask::TRANSCRIBE), m_language(SPEECH2TEXT_DEFAULT_LANGUAGE)
{}

Speech2TextGeneratorParams::Speech2TextGeneratorParams(Speech2TextTask task, std::string_view language) :
    m_task(task), m_language(language)
{}

hailo_status Speech2TextGeneratorParams::set_task(Speech2TextTask task)
{
    m_task = task;
    return HAILO_SUCCESS;
}

hailo_status Speech2TextGeneratorParams::set_language(std::string_view language)
{
    // TODO: HRT-18727 - Add language validation
    m_language = language;
    return HAILO_SUCCESS;
}

Speech2TextTask Speech2TextGeneratorParams::task() const
{
    return m_task;
}

std::string_view Speech2TextGeneratorParams::language() const
{
    return m_language;
}

Speech2Text::Speech2Text(std::unique_ptr<Impl> pimpl) :
    m_pimpl(std::move(pimpl))
{}

Expected<Speech2Text> Speech2Text::create(std::shared_ptr<hailort::VDevice> vdevice, const Speech2TextParams &speech2text_params)
{
    TRY(auto pimpl, Impl::create_unique(vdevice, speech2text_params));
    return Speech2Text(std::move(pimpl));
}

Expected<Speech2TextGeneratorParams> Speech2Text::create_generator_params()
{
    return m_pimpl->create_generator_params();
}

Expected<std::string> Speech2Text::generate_all_text(MemoryView audio_buffer, const Speech2TextGeneratorParams &generator_params, std::chrono::milliseconds timeout)
{
    return m_pimpl->generate_all_text(audio_buffer, generator_params, timeout);
}

Expected<std::string> Speech2Text::generate_all_text(MemoryView audio_buffer, std::chrono::milliseconds timeout)
{
    TRY(auto generator_params, create_generator_params());
    return m_pimpl->generate_all_text(audio_buffer, generator_params, timeout);
}

Expected<std::vector<Speech2Text::SegmentInfo>> Speech2Text::generate_all_segments(MemoryView audio_buffer, const Speech2TextGeneratorParams &generator_params, std::chrono::milliseconds timeout)
{
    return m_pimpl->generate_all_segments(audio_buffer, generator_params, timeout);
}

Expected<std::vector<Speech2Text::SegmentInfo>> Speech2Text::generate_all_segments(MemoryView audio_buffer, std::chrono::milliseconds timeout)
{
    TRY(auto generator_params, create_generator_params());
    return m_pimpl->generate_all_segments(audio_buffer, generator_params, timeout);
}

Expected<std::vector<int>> Speech2Text::tokenize(const std::string &text)
{
    return m_pimpl->tokenize(text);
}

Speech2Text::Impl::Impl(std::shared_ptr<SessionWrapper> session, const Speech2TextParams &speech2text_params) :
    m_session(session), m_speech2text_params(speech2text_params)
{}

Speech2Text::Impl::~Impl()
{
    auto release_request = Speech2TextReleaseSerializer::serialize_request();
    if (!release_request) {
        LOGGER__CRITICAL("Failed to serialize Speech2Text release request with status {}", release_request.status());
        return;
    }
    auto release_reply = m_session->execute(MemoryView(release_request.value()));
    if (!release_reply) {
        LOGGER__CRITICAL("Failed to execute Speech2Text release request with status {}", release_reply.status());
        return;
    }
    auto status = Speech2TextReleaseSerializer::deserialize_reply(MemoryView(*release_reply));
    if (HAILO_SUCCESS != status) {
        LOGGER__CRITICAL("Failed to deserialize Speech2Text release reply with status {}", status);
        return;
    }
}

Expected<std::unique_ptr<Speech2Text::Impl>> Speech2Text::Impl::create_unique(std::shared_ptr<hailort::VDevice> vdevice, const Speech2TextParams &speech2text_params)
{
    CHECK(!speech2text_params.hef().empty(), HAILO_INVALID_OPERATION, "Failed to create Speech2Text. HEF was not set.");

    auto vdevice_params = vdevice->get_params();
    TRY(auto session_wrapper, GenAICommon::create_session_wrapper(vdevice_params, DEFAULT_SPEECH2TEXT_CONNECTION_PORT));

    TRY(auto create_speech2text_request, Speech2TextCreateSerializer::serialize_request(vdevice_params));
    std::vector<MemoryView> write_buffers = { MemoryView(create_speech2text_request) };
    TRY(auto hef_data, read_binary_file(std::string(speech2text_params.hef()), BufferStorageParams::create_dma()));
    write_buffers.push_back(MemoryView(hef_data));
    TRY(auto create_speech2text_reply, session_wrapper->execute(write_buffers));
    CHECK_SUCCESS_AS_EXPECTED(Speech2TextCreateSerializer::deserialize_reply(MemoryView(*create_speech2text_reply)), "Failed to create Speech2Text");

    auto speech2text_ptr = make_unique_nothrow<Impl>(session_wrapper, speech2text_params);
    CHECK_NOT_NULL_AS_EXPECTED(speech2text_ptr, HAILO_OUT_OF_HOST_MEMORY);
    return speech2text_ptr;
}

Expected<Speech2TextGeneratorParams> Speech2Text::Impl::create_generator_params()
{
    Speech2TextGeneratorParams generator_params;
    return generator_params;
}

Expected<std::string> Speech2Text::Impl::generate_all_text(MemoryView audio_buffer, const Speech2TextGeneratorParams &generator_params, std::chrono::milliseconds timeout)
{
    TRY(auto segments, generate_all_segments(audio_buffer, generator_params, timeout));

    size_t total_text_size = 0;
    for (const auto &segment : segments) {
        total_text_size += segment.text.size();
    }

    std::string all_text;
    all_text.reserve(total_text_size);
    for (const auto &segment : segments) {
        all_text += segment.text;
    }
    return all_text;
}

Expected<std::vector<Speech2Text::SegmentInfo>> Speech2Text::Impl::generate_all_segments(MemoryView audio_buffer, const Speech2TextGeneratorParams &generator_params, std::chrono::milliseconds timeout)
{
    return generate_impl(audio_buffer, generator_params, timeout);
}

Expected<std::vector<Speech2Text::SegmentInfo>> Speech2Text::Impl::generate_impl(MemoryView audio_buffer,
    const Speech2TextGeneratorParams &generator_params, std::chrono::milliseconds timeout)
{
    (void)timeout; // TODO: HRT-18570 - Support timeout

    TRY(auto generate_request, Speech2TextGenerateSerializer::serialize_request(generator_params));
    std::vector<MemoryView> write_buffers = { MemoryView(generate_request) };
    write_buffers.push_back(audio_buffer);
    TRY(auto generate_reply, m_session->execute(write_buffers));
    TRY(auto segments, Speech2TextGenerateSerializer::deserialize_reply(MemoryView(*generate_reply)));

    return segments;
}

Expected<std::vector<int>> Speech2Text::Impl::tokenize(const std::string &text)
{
    TRY(auto request, Speech2TextTokenizeSerializer::serialize_request(text));
    TRY(auto reply, m_session->execute(MemoryView(request)));
    TRY(auto tokens, Speech2TextTokenizeSerializer::deserialize_reply(MemoryView(*reply)));
    return tokens;
}


// https://stackoverflow.com/questions/71104545/constructor-and-destructor-in-c-when-using-the-pimpl-idiom
// All member functions shoud be implemented in the cpp module
Speech2Text::~Speech2Text() = default;
Speech2Text::Speech2Text(Speech2Text &&) = default;

} /* namespace genai */
} /* namespace hailort */
