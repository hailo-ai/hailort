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
#include "hailo/genai/llm/llm.hpp"

#include "hailo/hailort.h"
#include "hailo/hailort_common.hpp"
#include "common/filesystem.hpp"
#include "common/utils.hpp"
#include "common/file_utils.hpp"
#include "common/thread_safe_queue.hpp"
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

bool VLMParams::optimize_memory_on_device() const
{
    return m_optimize_memory_on_device;
}

void VLMParams::set_optimize_memory_on_device(bool optimize_memory_on_device)
{
    m_optimize_memory_on_device = optimize_memory_on_device;
}

Expected<VLM> VLM::create(std::shared_ptr<hailort::VDevice> vdevice, const VLMParams &vlm_params)
{
    TRY(auto pimpl, Impl::create_unique(vdevice, vlm_params));
    return VLM(std::move(pimpl));
}

Expected<std::unique_ptr<VLM::Impl>> VLM::Impl::create_unique(std::shared_ptr<hailort::VDevice> vdevice, const VLMParams &vlm_params)
{
    CHECK(!vlm_params.hef().empty(), HAILO_INVALID_OPERATION, "Failed to create VLM. HEF was not set.");

    TRY(auto hef_hash, Hef::hash(vlm_params.hef()));

    auto vdevice_params = vdevice->get_params();
    TRY(auto session_wrapper, GenAICommon::create_session_wrapper(vdevice_params, DEFAULT_VLM_CONNECTION_PORT));

    // Translate vlm_params.hef() to an absolute path if it is not already
    std::string hef_path = vlm_params.hef();
    if (!std::filesystem::path(hef_path).is_absolute()) {
        hef_path = std::filesystem::absolute(hef_path).string();
    }

    // Check if HEF exists on the server
    TRY(auto check_hef_exists_on_server_request, GenAICheckHefExistsSerializer::serialize_request(hef_path, hef_hash));
    TRY(auto check_hef_exists_on_server_reply, session_wrapper->execute(MemoryView(check_hef_exists_on_server_request)));
    TRY(auto hef_exists, GenAICheckHefExistsSerializer::deserialize_reply(MemoryView(*check_hef_exists_on_server_reply)));

    Buffer create_vlm_request;
    std::shared_ptr<Buffer> create_vlm_reply;
    std::shared_ptr<HailoTokenizer> tokenizer;
    std::shared_ptr<TokenEmbedder<uint16_t>> token_embedder;
    BufferPtr token_embedder_buffer;
    if (!hef_exists) {
        // Get file size for chunked transfer
        TRY(auto file_size, get_istream_size(hef_path));
        if (vlm_params.optimize_memory_on_device()) {
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
                } else if (name == INPUT_EMB_BINARY) {
                    // Create token_embedder after getting the embedding features from the server
                    token_embedder_buffer = resource_buffer;
                    file_size -= resource_buffer->size();
                }
            }
#endif // HAILO_CLIENT_TOKENIZER_ENABLED
        }

        // Create request with file size for chunked transfer
        TRY(create_vlm_request, VLMCreateSerializer::serialize_request(vdevice_params, "", file_size, vlm_params.optimize_memory_on_device()));
        CHECK_SUCCESS(session_wrapper->write(MemoryView(create_vlm_request)));
        CHECK_SUCCESS(session_wrapper->send_file_chunked(hef_path, file_size));
        TRY(create_vlm_reply, session_wrapper->read());
    } else {
        TRY(create_vlm_request, VLMCreateSerializer::serialize_request(vdevice_params, hef_path, vlm_params.optimize_memory_on_device()));
        TRY(create_vlm_reply, session_wrapper->execute(MemoryView(create_vlm_request)));
    }
    TRY(auto vlm_info_tuple, VLMCreateSerializer::deserialize_reply(MemoryView(*create_vlm_reply)), "Failed to create VLM");

    auto input_frame_shape = std::get<0>(vlm_info_tuple);
    auto input_frame_format = std::get<1>(vlm_info_tuple);

    auto chat_template = std::get<2>(vlm_info_tuple);
    TRY(auto prompt_template_handler, PromptTemplateHandler::create(chat_template));
    auto prompt_template_handler_ptr = make_shared_nothrow<PromptTemplateHandler>(std::move(prompt_template_handler));
    CHECK_NOT_NULL_AS_EXPECTED(prompt_template_handler_ptr, HAILO_OUT_OF_HOST_MEMORY);

    if (vlm_params.optimize_memory_on_device()) {
#ifndef HAILO_CLIENT_TOKENIZER_ENABLED
        LOGGER__ERROR("Host-tokenization is not enabled in lib compilation");
        return make_unexpected(HAILO_NOT_IMPLEMENTED);
#else
        auto embedding_features = std::get<3>(vlm_info_tuple);
        auto image_pad_token_id = std::get<4>(vlm_info_tuple);
        auto embeddings_per_frame = std::get<5>(vlm_info_tuple);

        CHECK_AS_EXPECTED(nullptr != token_embedder_buffer, HAILO_NOT_AVAILABLE, "Token embedder buffer is not available");
        TRY(token_embedder, TokenEmbedder<uint16_t>::create(token_embedder_buffer,
            token_embedder_buffer->size() / (sizeof(uint16_t) * embedding_features), embedding_features,
            image_pad_token_id, embeddings_per_frame));
#endif // HAILO_CLIENT_TOKENIZER_ENABLED
    }

    TRY(auto get_generator_default_params_request, LLMGetGeneratorParamsSerializer::serialize_request());
    TRY(auto get_generator_default_params_reply, session_wrapper->execute(MemoryView(get_generator_default_params_request)));
    TRY(auto default_generator_params, LLMGetGeneratorParamsSerializer::deserialize_reply(MemoryView(*get_generator_default_params_reply)));

    auto vlm_ptr = make_unique_nothrow<Impl>(session_wrapper, vlm_params, default_generator_params, input_frame_shape, input_frame_format, prompt_template_handler_ptr,
        tokenizer, token_embedder);
    CHECK_NOT_NULL_AS_EXPECTED(vlm_ptr, HAILO_OUT_OF_HOST_MEMORY);
    return vlm_ptr;
}

VLM::Impl::Impl(std::shared_ptr<SessionWrapper> session, const VLMParams &vlm_params,
    const LLMGeneratorParams &default_generator_params, hailo_3d_image_shape_t input_frame_shape,
    hailo_format_t input_frame_format, std::shared_ptr<PromptTemplateHandler> prompt_template_handler,
    std::shared_ptr<HailoTokenizer> tokenizer, std::shared_ptr<TokenEmbedder<uint16_t>> token_embedder) :
        m_session(session), m_vlm_params(vlm_params), m_default_generator_params(default_generator_params),
        m_input_frame_shape(input_frame_shape), m_input_frame_format(input_frame_format),
        m_prompt_template_handler(prompt_template_handler),
        m_tokenizer(tokenizer),
        m_token_embedder(token_embedder)
{}

VLM::Impl::~Impl()
{
    auto release_request = LLMReleaseSerializer::serialize_request();
    if (!release_request) {
        LOGGER__CRITICAL("Failed to serialize VLM release request with status {}", release_request.status());
        return;
    }
    auto release_reply = m_session->execute(MemoryView(release_request.value()));
    if (!release_reply) {
        LOGGER__CRITICAL("Failed to execute VLM release request with status {}", release_reply.status());
        return;
    }

    auto status = LLMReleaseSerializer::deserialize_reply(MemoryView(release_reply.value()));
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

Expected<size_t> VLM::get_context_usage_size()
{
    return m_pimpl->get_context_usage_size();
}

Expected<size_t> VLM::Impl::get_context_usage_size()
{
    TRY(auto request, LLMGetContextUsageSizeSerializer::serialize_request());
    TRY(auto reply, m_session->execute(MemoryView(request)));
    TRY(auto context_usage, LLMGetContextUsageSizeSerializer::deserialize_reply(MemoryView(*reply)));
    return context_usage;
}

Expected<size_t> VLM::max_context_capacity()
{
    return m_pimpl->max_context_capacity();
}

Expected<size_t> VLM::Impl::max_context_capacity()
{
    TRY(auto request, LLMGetMaxContextCapacitySerializer::serialize_request());
    TRY(auto reply, m_session->execute(MemoryView(request)));
    TRY(auto max_context_capacity, LLMGetMaxContextCapacitySerializer::deserialize_reply(MemoryView(*reply)));
    return max_context_capacity;
}

hailo_status VLM::clear_context()
{
    return m_pimpl->clear_context();
}

hailo_status VLM::Impl::clear_context()
{
    TRY(auto request, LLMClearContextSerializer::serialize_request());
    TRY(auto reply, m_session->execute(MemoryView(request)));
    CHECK_SUCCESS(LLMClearContextSerializer::deserialize_reply(MemoryView(*reply)),
        "Failed to clear context. Make sure there is no other generation in progress");
    m_prompt_template_handler->reset_state();
    return HAILO_SUCCESS;
}

Expected<BufferPtr> VLM::save_context()
{
    return m_pimpl->save_context();
}

Expected<BufferPtr> VLM::Impl::save_context()
{
    TRY(auto request, LLMGetContextSerializer::serialize_request());
    CHECK_SUCCESS(m_session->write(MemoryView(request)), "Failed to write request");
    TRY(auto context_shared, m_session->read());
    TRY(auto reply, m_session->read());
    CHECK_SUCCESS(LLMGetContextSerializer::deserialize_reply(MemoryView(*reply)), "Failed to get context");
    return context_shared;
}

hailo_status VLM::load_context(const MemoryView &context)
{
    return m_pimpl->load_context(context);
}

hailo_status VLM::Impl::load_context(const MemoryView &context)
{
    TRY(auto request, LLMSetContextSerializer::serialize_request());
    CHECK_SUCCESS(m_session->write(MemoryView(request)), "Failed to write request");
    CHECK_SUCCESS(m_session->write(context), "Failed to write context");
    TRY(auto reply, m_session->read());
    CHECK_SUCCESS(LLMSetContextSerializer::deserialize_reply(MemoryView(*reply)), "Failed to set context");
    return HAILO_SUCCESS;
}

Expected<std::string> VLM::prompt_template()
{
    return m_pimpl->prompt_template();
}

Expected<std::string> VLM::Impl::prompt_template()
{
    TRY(auto prompt_template, m_prompt_template_handler->prompt_template());
    CHECK(!prompt_template.empty(), HAILO_NOT_AVAILABLE,
        "Prompt template is not set for this VLM");
    return prompt_template;
}

hailo_status VLM::set_generation_recovery_sequence(const std::string &abort_sequence)
{
    return m_pimpl->set_generation_recovery_sequence(abort_sequence);
}

hailo_status VLM::Impl::set_generation_recovery_sequence(const std::string &abort_sequence)
{
    TRY(auto tokens, tokenize(abort_sequence));
    TRY(auto set_end_of_generation_sequence_request, LLMSetEndOfGenerationSequenceSerializer::serialize_request(tokens));
    TRY(auto set_end_of_generation_sequence_reply, m_session->execute(MemoryView(set_end_of_generation_sequence_request)));
    CHECK_SUCCESS(LLMSetEndOfGenerationSequenceSerializer::deserialize_reply(MemoryView(*set_end_of_generation_sequence_reply)), "Failed to set generation recovery sequence");
    return HAILO_SUCCESS;
}

Expected<std::string> VLM::get_generation_recovery_sequence()
{
    return m_pimpl->get_generation_recovery_sequence();
}

Expected<std::string> VLM::Impl::get_generation_recovery_sequence()
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

hailo_status VLM::set_stop_tokens(const std::vector<std::string> &stop_tokens)
{
    return m_pimpl->set_stop_tokens(stop_tokens);
}

hailo_status VLM::Impl::set_stop_tokens(const std::vector<std::string> &stop_tokens)
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

Expected<std::vector<std::string>> VLM::get_stop_tokens()
{
    return m_pimpl->get_stop_tokens();
}

Expected<std::vector<std::string>> VLM::Impl::get_stop_tokens()
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

Expected<LLMGeneratorCompletion> VLM::generate(const LLMGeneratorParams &params,
    const std::vector<std::string> &messages_json_strings, const std::vector<MemoryView> &input_frames)
{
    return m_pimpl->generate(params, messages_json_strings, input_frames);
}

Expected<LLMGeneratorCompletion> VLM::generate(const std::vector<std::string> &messages_json_strings, const std::vector<MemoryView> &input_frames)
{
    TRY(auto generator_params, create_generator_params());
    return m_pimpl->generate(generator_params, messages_json_strings, input_frames);
}

Expected<LLMGeneratorCompletion> VLM::Impl::generate(const LLMGeneratorParams &params,
    const std::vector<std::string> &messages_json_strings, const std::vector<MemoryView> &input_frames)
{
    TRY(auto generator, create_generator(params));
    TRY(auto completion, generator.generate(messages_json_strings, input_frames));
    // Generator is kept alive via shared_from_this() in VLMGenerator::Impl::generate()
    return completion;
}

Expected<VLMGenerator> VLM::Impl::create_generator(const LLMGeneratorParams &params)
{
    CHECK_SUCCESS(validate_generator_params(params));

    TRY(auto create_generator_request, LLMGeneratorCreateSerializer::serialize_request(params));
    TRY(auto create_generator_reply, m_session->execute(MemoryView(create_generator_request)));
    CHECK_SUCCESS(LLMGeneratorCreateSerializer::deserialize_reply(MemoryView(*create_generator_reply)), "Failed to create LLM generator");

    auto pimpl = make_unique_nothrow<VLMGenerator::Impl>(m_session, m_prompt_template_handler,
        m_tokenizer, m_token_embedder);
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
    CHECK_AS_EXPECTED(0 < params.max_generated_tokens(), HAILO_INVALID_ARGUMENT,
        "max_generated_tokens should be greater than or equal to '1'. received: '{}'", params.max_generated_tokens());
    return HAILO_SUCCESS;
}

VLMGenerator::VLMGenerator(std::shared_ptr<Impl> pimpl) :
    m_pimpl(pimpl)
{}

VLMGenerator::Impl::Impl(std::shared_ptr<SessionWrapper> session, std::shared_ptr<PromptTemplateHandler> prompt_template_handler,
    std::shared_ptr<HailoTokenizer> tokenizer, std::shared_ptr<TokenEmbedder<uint16_t>> token_embedder) :
        m_session(session), m_prompt_template_handler(prompt_template_handler),
        m_tokenizer(tokenizer),
        m_token_embedder(token_embedder)
{}

Expected<LLMGeneratorCompletion> VLMGenerator::generate(const std::string &prompt, const std::vector<MemoryView> &input_frames)
{
    return m_pimpl->generate(prompt, input_frames);
}

Expected<LLMGeneratorCompletion> VLMGenerator::generate(const std::vector<std::string> &messages_json_strings, const std::vector<MemoryView> &input_frames)
{
    return m_pimpl->generate(messages_json_strings, input_frames);
}

Expected<LLMGeneratorCompletion> VLMGenerator::Impl::generate(const std::string &prompt, const std::vector<MemoryView> &input_frames)
{
    CHECK_AS_EXPECTED(!prompt.empty(), HAILO_INVALID_ARGUMENT, "Prompt cannot be empty");

    TRY(auto generator_generate_request, VLMGeneratorGenerateSerializer::serialize_request(static_cast<uint32_t>(input_frames.size())));
    std::vector<MemoryView> write_buffers;
    write_buffers.reserve(input_frames.size() + 2);
    write_buffers.push_back(MemoryView(generator_generate_request));
    write_buffers.insert(write_buffers.end(), input_frames.begin(), input_frames.end());

    TRY(auto generator_generate_reply, m_session->execute(write_buffers));
    CHECK_SUCCESS(VLMGeneratorGenerateSerializer::deserialize_reply(MemoryView(*generator_generate_reply)),
        "Failed to generate. Make sure the number of passed frames ('{}') matches the number of frames in the prompt," \
         " each passed frame size matches the expected input frame size (see 'VLM::input_frame_size'), and there is no other generation in progress",
            input_frames.size());

    TRY(auto shutdown_event, Event::create_shared(Event::State::not_signalled));
    const auto TOKENS_BACKLOG = 1024;
    TRY(auto client_token_queue, SpscQueue<LLMTokenPair>::create(
        TOKENS_BACKLOG, shutdown_event, HAILO_INFINITE_TIMEOUT));

    auto pimpl = make_unique_nothrow<LLMGeneratorCompletion::Impl>(m_session, shared_from_this(),
        std::move(client_token_queue), shutdown_event, m_tokenizer, m_token_embedder, prompt);
    CHECK_NOT_NULL_AS_EXPECTED(pimpl, HAILO_OUT_OF_HOST_MEMORY);
    return LLMGeneratorCompletion(std::move(pimpl));
}

Expected<LLMGeneratorCompletion> VLMGenerator::Impl::generate(const std::vector<std::string> &messages_json_strings, const std::vector<MemoryView> &input_frames)
{
    CHECK_AS_EXPECTED(!messages_json_strings.empty(), HAILO_INVALID_ARGUMENT, "Messages cannot be empty");

    TRY(auto processed_prompt, apply_vlm_template_from_json(messages_json_strings));

    return generate(processed_prompt, input_frames);
}

Expected<std::string> VLMGenerator::Impl::apply_vlm_template_from_json(const std::vector<std::string> &messages_json_strings)
{
    return m_prompt_template_handler->render(messages_json_strings);
}

// https://stackoverflow.com/questions/71104545/constructor-and-destructor-in-c-when-using-the-pimpl-idiom
// All member functions shoud be implemented in the cpp module
VLM::~VLM() = default;
VLM::VLM(VLM &&) = default;

VLMGenerator::~VLMGenerator() = default;
VLMGenerator::VLMGenerator(VLMGenerator &&) = default;

} /* namespace genai */
} /* namespace hailort */
