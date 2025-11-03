/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file serializer.cpp
 * @brief HailoRT-GenAI protocol serialization implementation
 **/

#include "hailo/genai/common.hpp"
#include "hailo/genai/llm/llm.hpp"

#include "serializer.hpp"
#include "hailo/buffer.hpp"
#include "hailo/hailort.h"
#include "hailo/hailort_common.hpp"
#include "common/utils.hpp"

// https://github.com/protocolbuffers/protobuf/tree/master/cmake#notes-on-compiler-warnings
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4244 4267 4127)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "genai_scheme.pb.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#endif

namespace hailort
{
namespace genai
{

struct GenAIRpcHeader {
    uint32_t action_id;
};

template <typename T>
Expected<Buffer> get_serialized_message(T data, uint32_t action_id, const std::string &name)
{
    // TODO: Should receive a buffer instead of creating one (HRT-16540)
    TRY(auto buffer, Buffer::create((sizeof(GenAIRpcHeader) + data.ByteSizeLong()), BufferStorageParams::create_dma()));
    uint8_t *buffer_ptr = buffer.data();
    GenAIRpcHeader *rpc_header = reinterpret_cast<GenAIRpcHeader*>(buffer_ptr);
    rpc_header->action_id = action_id;
    buffer_ptr += sizeof(GenAIRpcHeader);

    CHECK(data.SerializeToArray(buffer_ptr, static_cast<int>(data.ByteSizeLong())),
        HAILO_INTERNAL_FAILURE, "Failed to serialize {}", name);

    return buffer;
}

template <typename T>
Expected<T> get_deserialized_message(const MemoryView &buffer, uint32_t action_id, const std::string &name)
{
    const GenAIRpcHeader *rpc_header = buffer.as_pointer<GenAIRpcHeader>();
    CHECK(rpc_header != nullptr, HAILO_INTERNAL_FAILURE, "Failed to get action id");
    CHECK(rpc_header->action_id == action_id, HAILO_INTERNAL_FAILURE, "Expected id {}, received {}",
        action_id, rpc_header->action_id);
    const uint8_t *buffer_ptr = buffer.data() + sizeof(GenAIRpcHeader);
    T data;
    CHECK(data.ParseFromArray(buffer_ptr, static_cast<int>(buffer.size() - sizeof(GenAIRpcHeader))),
        HAILO_INTERNAL_FAILURE, "Failed to de-serialize {}", name);

    return data;
}

Expected<uint32_t> GenAISerializerUtils::get_action_id(const MemoryView &raw_request)
{
    // First uint32_t32_t is the action id
    const GenAIRpcHeader *rpc_header = raw_request.as_pointer<GenAIRpcHeader>();
    CHECK(rpc_header != nullptr, HAILO_INTERNAL_FAILURE, "Failed to get action id");
    CHECK(rpc_header->action_id < static_cast<uint32_t>(HailoGenAIActionID::GENAI_ACTIONS_COUNT), HAILO_INTERNAL_FAILURE,
        "Invalid action id {}", rpc_header->action_id);
    auto res = rpc_header->action_id;
    return res;
}

Expected<Buffer> LLMCreateSerializer::serialize_request(const hailo_vdevice_params_t &vdevice_params, const LLMParams &llm_params, const std::string &hef_path)
{
    LLM_Create_Request llm_create;

    llm_create.set_lora_name(llm_params.lora());
    if (!hef_path.empty()) {
        llm_create.set_hef_path(hef_path);
    }

    std::string group_id = get_group_id_as_string(vdevice_params);
    llm_create.set_group_id(group_id);

    llm_create.set_tokenizer_on_host(llm_params.optimize_memory_on_device());

    return get_serialized_message<LLM_Create_Request>(llm_create, static_cast<uint32_t>(HailoGenAIActionID::LLM__CREATE), "LLM_Create_Request");
}

Expected<Buffer> LLMCreateSerializer::serialize_request(const hailo_vdevice_params_t &vdevice_params, const LLMParams &llm_params,
    const std::string &hef_path, uint64_t file_size)
{
    LLM_Create_Request llm_create;

    llm_create.set_lora_name(llm_params.lora());
    if (!hef_path.empty()) {
        llm_create.set_hef_path(hef_path);
    }

    std::string group_id = get_group_id_as_string(vdevice_params);
    llm_create.set_group_id(group_id);

    // Set file size for chunked transfer
    llm_create.set_file_size(file_size);

    llm_create.set_tokenizer_on_host(llm_params.optimize_memory_on_device());

    return get_serialized_message<LLM_Create_Request>(llm_create, static_cast<uint32_t>(HailoGenAIActionID::LLM__CREATE), "LLM_Create_Request");
}

Expected<LLMCreateSerializer::RequestInfo> LLMCreateSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto llm_create, get_deserialized_message<LLM_Create_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__CREATE), "LLM_Create_Request"));

    return LLMCreateSerializer::RequestInfo(
        llm_create.lora_name(), llm_create.hef_path(), llm_create.group_id(), llm_create.file_size(), llm_create.tokenizer_on_host());
}

Expected<Buffer> LLMCreateSerializer::serialize_reply(hailo_status status, const std::string &prompt_template, uint32_t embedding_features)
{
    LLM_Create_Reply llm_create;
    llm_create.set_status(status);
    llm_create.set_prompt_template(prompt_template);
    llm_create.set_embedding_features(embedding_features);

    return get_serialized_message<LLM_Create_Reply>(llm_create, static_cast<uint32_t>(HailoGenAIActionID::LLM__CREATE), "LLM_Create_Reply");
}

Expected<std::pair<std::string, uint32_t>> LLMCreateSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto llm_create, get_deserialized_message<LLM_Create_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__CREATE), "LLM_Create_Reply"));

    CHECK(llm_create.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'LLM_Create_Reply'");
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(llm_create.status()), "Failed to create LLM");

    auto prompt_template = llm_create.prompt_template();
    auto embedding_features = llm_create.embedding_features();
    return std::make_pair(prompt_template, embedding_features);
}

Expected<Buffer> LLMGetGeneratorParamsSerializer::serialize_request()
{
    LLM_Get_Generator_Params_Request llm_get_generator_params;
    return get_serialized_message<LLM_Get_Generator_Params_Request>(llm_get_generator_params,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GET_GENERATOR_PARAMS), "LLM_Get_Generator_Params_Request");
}

hailo_status LLMGetGeneratorParamsSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto llm_get_generator_params, get_deserialized_message<LLM_Get_Generator_Params_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GET_GENERATOR_PARAMS), "LLM_Get_Generator_Params_Request"));

    return HAILO_SUCCESS;
}

Expected<Buffer> LLMGetGeneratorParamsSerializer::serialize_reply(hailo_status status, const LLMGeneratorParams &default_generator_params)
{
    LLM_Get_Generator_Params_Reply llm_get_generator_params;
    llm_get_generator_params.set_status(status);

    auto params_proto = llm_get_generator_params.mutable_generator_params();
    params_proto->set_temperature(default_generator_params.temperature());
    params_proto->set_top_p(default_generator_params.top_p());
    params_proto->set_top_k(default_generator_params.top_k());
    params_proto->set_frequency_penalty(default_generator_params.frequency_penalty());
    params_proto->set_max_generated_tokens(default_generator_params.max_generated_tokens());
    params_proto->set_do_sample(default_generator_params.do_sample());
    params_proto->set_seed(default_generator_params.seed());

    return get_serialized_message<LLM_Get_Generator_Params_Reply>(llm_get_generator_params,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GET_GENERATOR_PARAMS), "LLM_Get_Generator_Params_Reply");
}

Expected<LLMGeneratorParams> LLMGetGeneratorParamsSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto llm_get_generator_params, get_deserialized_message<LLM_Get_Generator_Params_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GET_GENERATOR_PARAMS), "LLM_Get_Generator_Params_Reply"));

    CHECK(llm_get_generator_params.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE,
        "Failed to de-serialize 'LLM_Get_Generator_Params_Reply'");
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(llm_get_generator_params.status()),
        "Failed to get default generator params");

    LLMGeneratorParams res(llm_get_generator_params.generator_params().temperature(), llm_get_generator_params.generator_params().top_p(),
        llm_get_generator_params.generator_params().top_k(), llm_get_generator_params.generator_params().frequency_penalty(),
        llm_get_generator_params.generator_params().max_generated_tokens(), llm_get_generator_params.generator_params().do_sample(),
        llm_get_generator_params.generator_params().seed());

    return res;
}

Expected<Buffer> LLMGeneratorCreateSerializer::serialize_request(const LLMGeneratorParams &params)
{
    LLM_Generator_Create_Request llm_generator_create;

    auto params_proto = llm_generator_create.mutable_generator_params();
    params_proto->set_temperature(params.temperature());
    params_proto->set_top_p(params.top_p());
    params_proto->set_top_k(params.top_k());
    params_proto->set_frequency_penalty(params.frequency_penalty());
    params_proto->set_max_generated_tokens(params.max_generated_tokens());
    params_proto->set_do_sample(params.do_sample());
    params_proto->set_seed(params.seed());

    return get_serialized_message<LLM_Generator_Create_Request>(llm_generator_create,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GENERATOR_CREATE), "LLM_Generator_Create_Request");
}

Expected<LLMGeneratorParams> LLMGeneratorCreateSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto llm_generator_create, get_deserialized_message<LLM_Generator_Create_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GENERATOR_CREATE), "LLM_Generator_Create_Request"));

    LLMGeneratorParams res(llm_generator_create.generator_params().temperature(), llm_generator_create.generator_params().top_p(),
        llm_generator_create.generator_params().top_k(), llm_generator_create.generator_params().frequency_penalty(),
        llm_generator_create.generator_params().max_generated_tokens(), llm_generator_create.generator_params().do_sample(),
        llm_generator_create.generator_params().seed());

    return res;
}

Expected<Buffer> LLMGeneratorCreateSerializer::serialize_reply(hailo_status status)
{
    LLM_Generator_Create_Reply llm_generator_create;
    llm_generator_create.set_status(status);

    return get_serialized_message<LLM_Generator_Create_Reply>(llm_generator_create,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GENERATOR_CREATE), "LLM_Generator_Create_Reply");
}

hailo_status LLMGeneratorCreateSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto llm_generator_create, get_deserialized_message<LLM_Generator_Create_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GENERATOR_CREATE), "LLM_Generator_Create_Reply"));

    CHECK(llm_generator_create.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'LLM_Generator_Create_Reply'");
    return static_cast<hailo_status>(llm_generator_create.status());
}

Expected<Buffer> LLMGeneratorWriteSerializer::serialize_request()
{
    LLM_Generator_Write_Request llm_generator_write;
    return get_serialized_message<LLM_Generator_Write_Request>(llm_generator_write,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GENERATOR_WRITE), "LLM_Generator_Write_Request");
}

hailo_status LLMGeneratorWriteSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto llm_generator_write, get_deserialized_message<LLM_Generator_Write_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GENERATOR_WRITE), "LLM_Generator_Write_Request"));

    return HAILO_SUCCESS;
}

Expected<Buffer> LLMGeneratorWriteSerializer::serialize_reply(hailo_status status)
{
    LLM_Generator_Write_Reply llm_generator_write;
    llm_generator_write.set_status(status);

    return get_serialized_message<LLM_Generator_Write_Reply>(llm_generator_write,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GENERATOR_WRITE), "LLM_Generator_Write_Reply");
}

hailo_status LLMGeneratorWriteSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto llm_generator_write, get_deserialized_message<LLM_Generator_Write_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GENERATOR_WRITE), "LLM_Generator_Write_Reply"));

    CHECK(llm_generator_write.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'LLM_Generator_Write_Reply'");
    return static_cast<hailo_status>(llm_generator_write.status());
}

Expected<Buffer> LLMGeneratorGenerateSerializer::serialize_request()
{
    LLM_Generator_Generate_Request llm_generator_generate;
    return get_serialized_message<LLM_Generator_Generate_Request>(llm_generator_generate,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GENERATOR_GENERATE), "LLM_Generator_Generate_Request");
}

hailo_status LLMGeneratorGenerateSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto llm_generator_generate, get_deserialized_message<LLM_Generator_Generate_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GENERATOR_GENERATE), "LLM_Generator_Generate_Request"));

    return HAILO_SUCCESS;
}

Expected<Buffer> LLMGeneratorGenerateSerializer::serialize_reply(hailo_status status, const std::vector<int> &initial_prefix_tokens)
{
    LLM_Generator_Generate_Reply llm_generator_generate;
    llm_generator_generate.set_status(status);
    for (const auto &token : initial_prefix_tokens) {
        llm_generator_generate.add_initial_prefix_tokens(token);
    }

    return get_serialized_message<LLM_Generator_Generate_Reply>(llm_generator_generate,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GENERATOR_GENERATE), "LLM_Generator_Generate_Reply");
}

Expected<std::vector<int>> LLMGeneratorGenerateSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto llm_generator_generate, get_deserialized_message<LLM_Generator_Generate_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GENERATOR_GENERATE), "LLM_Generator_Generate_Reply"));

    CHECK(llm_generator_generate.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'LLM_Generator_Generate_Reply'");
    CHECK_SUCCESS(static_cast<hailo_status>(llm_generator_generate.status()));

    std::vector<int> initial_prefix_tokens;
    for (const auto &token : llm_generator_generate.initial_prefix_tokens()) {
        initial_prefix_tokens.push_back(token);
    }
    return initial_prefix_tokens;
}

Expected<Buffer> LLMGeneratorReadSerializer::serialize_request(const std::chrono::milliseconds &timeout,
    const TextGenerationInput &request)
{
    LLM_Generator_Read_Request llm_generator_read;
    llm_generator_read.set_timeout_ms(static_cast<uint32_t>(timeout.count()));

    auto generation_input_proto = llm_generator_read.mutable_generation_input();
    generation_input_proto->set_initial_prompt(request.initial_prompt);
    for (const auto &token : request.tokens) {
        generation_input_proto->add_tokens(token);
    }

    for (const auto &embedding : request.embeddings) {
        auto *embedding_proto = generation_input_proto->add_embeddings();
        embedding_proto->assign(reinterpret_cast<const char*>(embedding->data()), embedding->size());
    }

    return get_serialized_message<LLM_Generator_Read_Request>(llm_generator_read,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GENERATOR_READ), "LLM_Generator_Read_Request");
}

Expected<std::pair<std::chrono::milliseconds, LLMGeneratorReadSerializer::TextGenerationInput>>
    LLMGeneratorReadSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto llm_generator_read, get_deserialized_message<LLM_Generator_Read_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GENERATOR_READ), "LLM_Generator_Read_Request"));

    TextGenerationInput input = {};
    input.initial_prompt = llm_generator_read.generation_input().initial_prompt();
    for (const auto &token : llm_generator_read.generation_input().tokens()) {
        input.tokens.push_back(token);
    }

    for (const auto &embedding : llm_generator_read.generation_input().embeddings()) {
        TRY(auto buffer, Buffer::create_shared(reinterpret_cast<const uint8_t*>(embedding.data()), embedding.size()));
        input.embeddings.push_back(buffer);
    }

    return std::make_pair(std::chrono::milliseconds(llm_generator_read.timeout_ms()), input);
}

Expected<Buffer> LLMGeneratorReadSerializer::serialize_reply(hailo_status status, const TextGenerationOutput &output,
    LLMGeneratorCompletion::Status generation_status, bool is_context_full)
{
    LLM_Generator_Read_Reply llm_generator_read;
    llm_generator_read.set_status(status);
    llm_generator_read.set_generation_status(static_cast<uint32_t>(generation_status));
    llm_generator_read.set_is_context_full(is_context_full);

    auto generation_output_proto = llm_generator_read.mutable_generation_output();
    generation_output_proto->set_output_token_str(output.output_token_str);
    generation_output_proto->set_output_token_id(output.output_token_id);

    return get_serialized_message<LLM_Generator_Read_Reply>(llm_generator_read,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GENERATOR_READ), "LLM_Generator_Read_Reply");
}

Expected<std::pair<LLMGeneratorReadSerializer::TextGenerationOutput, LLMGeneratorCompletion::Status>>
    LLMGeneratorReadSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto llm_generator_read, get_deserialized_message<LLM_Generator_Read_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GENERATOR_READ), "LLM_Generator_Read_Reply"));

    CHECK(llm_generator_read.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'LLM_Generator_Read_Reply'");
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(llm_generator_read.status()), "Failed to read generator");
    CHECK(llm_generator_read.generation_status() < static_cast<uint32_t>(LLMGeneratorCompletion::Status::COUNT), HAILO_INTERNAL_FAILURE,
        "Failed to de-serialize 'LLM_Generator_Read_Reply'");

    TextGenerationOutput output = {};
    output.output_token_str = llm_generator_read.generation_output().output_token_str();
    output.output_token_id = llm_generator_read.generation_output().output_token_id();
    output.is_context_full = llm_generator_read.is_context_full();

    return std::make_pair(output,
        static_cast<LLMGeneratorCompletion::Status>(llm_generator_read.generation_status()));
}

Expected<Buffer> LLMTokenizeSerializer::serialize_request(const std::string &prompt)
{
    LLM_Tokenize_Request llm_tokenize;
    llm_tokenize.set_prompt(prompt);

    return get_serialized_message<LLM_Tokenize_Request>(llm_tokenize,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__TOKENIZE), "LLM_Tokenize_Request");
}

Expected<std::string> LLMTokenizeSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto llm_tokenize, get_deserialized_message<LLM_Tokenize_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__TOKENIZE), "LLM_Tokenize_Request"));

    auto cpy = llm_tokenize.prompt();
    return cpy;
}

Expected<Buffer> LLMTokenizeSerializer::serialize_reply(hailo_status status, const std::vector<int> &tokens)
{
    LLM_Tokenize_Reply llm_tokenize;
    llm_tokenize.set_status(status);
    for (auto token : tokens) {
        llm_tokenize.add_tokens(token);
    }

    return get_serialized_message<LLM_Tokenize_Reply>(llm_tokenize,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__TOKENIZE), "LLM_Tokenize_Reply");
}

Expected<std::vector<int>> LLMTokenizeSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto llm_tokenize, get_deserialized_message<LLM_Tokenize_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__TOKENIZE), "LLM_Tokenize_Reply"));

    CHECK(llm_tokenize.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'LLM_Tokenize_Reply'");
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(llm_tokenize.status()), "Failed to tokenize");

    std::vector<int> tokens;
    for (auto token : llm_tokenize.tokens()) {
        tokens.push_back(token);
    }

    return tokens;
}

Expected<Buffer> LLMClearContextSerializer::serialize_request()
{
    LLM_Clear_Context_Request llm_clear_context;
    return get_serialized_message<LLM_Clear_Context_Request>(llm_clear_context,
        static_cast<uint32_t>(HailoGenAIActionID::LLM_CLEAR_CONTEXT), "LLM_Clear_Context_Request");
}

hailo_status LLMClearContextSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto llm_clear_context, get_deserialized_message<LLM_Clear_Context_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::LLM_CLEAR_CONTEXT), "LLM_Clear_Context_Request"));

    return HAILO_SUCCESS;
}

Expected<Buffer> LLMClearContextSerializer::serialize_reply(hailo_status status)
{
    LLM_Clear_Context_Reply llm_clear_context;
    llm_clear_context.set_status(status);

    return get_serialized_message<LLM_Clear_Context_Reply>(llm_clear_context,
        static_cast<uint32_t>(HailoGenAIActionID::LLM_CLEAR_CONTEXT), "LLM_Clear_Context_Reply");
}

hailo_status LLMClearContextSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto llm_clear_context, get_deserialized_message<LLM_Clear_Context_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::LLM_CLEAR_CONTEXT), "LLM_Clear_Context_Reply"));

    CHECK(llm_clear_context.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'LLM_Clear_Context_Reply'");
    return static_cast<hailo_status>(llm_clear_context.status());
}

Expected<Buffer> LLMGetContextSerializer::serialize_request()
{
    LLM_Get_Context_Request llm_get_context;
    return get_serialized_message<LLM_Get_Context_Request>(llm_get_context,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GET_CONTEXT), "LLM_Get_Context_Request");
}

hailo_status LLMGetContextSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto llm_get_context, get_deserialized_message<LLM_Get_Context_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GET_CONTEXT), "LLM_Get_Context_Request"));

    return HAILO_SUCCESS;
}

Expected<Buffer> LLMGetContextSerializer::serialize_reply(hailo_status status)
{
    LLM_Get_Context_Reply llm_get_context;
    llm_get_context.set_status(status);

    return get_serialized_message<LLM_Get_Context_Reply>(llm_get_context,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GET_CONTEXT), "LLM_Get_Context_Reply");
}

hailo_status LLMGetContextSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto llm_get_context, get_deserialized_message<LLM_Get_Context_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GET_CONTEXT), "LLM_Get_Context_Reply"));

    CHECK(llm_get_context.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'LLM_Get_Context_Reply'");
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(llm_get_context.status()), "Failed to get context");

    return HAILO_SUCCESS;
}

Expected<Buffer> LLMSetContextSerializer::serialize_request()
{
    LLM_Set_Context_Request llm_set_context;

    return get_serialized_message<LLM_Set_Context_Request>(llm_set_context,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__SET_CONTEXT), "LLM_Set_Context_Request");
}

hailo_status LLMSetContextSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto llm_set_context, get_deserialized_message<LLM_Set_Context_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__SET_CONTEXT), "LLM_Set_Context_Request"));

    return HAILO_SUCCESS;
}

Expected<Buffer> LLMSetContextSerializer::serialize_reply(hailo_status status)
{
    LLM_Set_Context_Reply llm_set_context;
    llm_set_context.set_status(status);

    return get_serialized_message<LLM_Set_Context_Reply>(llm_set_context,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__SET_CONTEXT), "LLM_Set_Context_Reply");
}

hailo_status LLMSetContextSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto llm_set_context, get_deserialized_message<LLM_Set_Context_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__SET_CONTEXT), "LLM_Set_Context_Reply"));

    return static_cast<hailo_status>(llm_set_context.status());
}

Expected<Buffer> LLMSetEndOfGenerationSequenceSerializer::serialize_request(const std::vector<int> &end_of_generation_sequence_tokens)
{
    LLM_Set_End_Of_Generation_Sequence_Request llm_set_end_of_generation_sequence;
    for (const auto &token : end_of_generation_sequence_tokens) {
        llm_set_end_of_generation_sequence.add_end_of_generation_sequence_tokens(token);
    }

    return get_serialized_message<LLM_Set_End_Of_Generation_Sequence_Request>(llm_set_end_of_generation_sequence,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__SET_END_OF_GENERATION_SEQUENCE), "LLM_Set_End_Of_Generation_Sequence_Request");
}

Expected<std::vector<int>> LLMSetEndOfGenerationSequenceSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto llm_set_end_of_generation_sequence, get_deserialized_message<LLM_Set_End_Of_Generation_Sequence_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__SET_END_OF_GENERATION_SEQUENCE), "LLM_Set_End_Of_Generation_Sequence_Request"));

    std::vector<int> end_of_generation_sequence_tokens;
    for (const auto &token : llm_set_end_of_generation_sequence.end_of_generation_sequence_tokens()) {
        end_of_generation_sequence_tokens.push_back(token);
    }
    return end_of_generation_sequence_tokens;
}

Expected<Buffer> LLMSetEndOfGenerationSequenceSerializer::serialize_reply(hailo_status status)
{
    LLM_Set_End_Of_Generation_Sequence_Reply llm_set_end_of_generation_sequence;
    llm_set_end_of_generation_sequence.set_status(status);

    return get_serialized_message<LLM_Set_End_Of_Generation_Sequence_Reply>(llm_set_end_of_generation_sequence,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__SET_END_OF_GENERATION_SEQUENCE), "LLM_Set_End_Of_Generation_Sequence_Reply");
}

hailo_status LLMSetEndOfGenerationSequenceSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto llm_set_end_of_generation_sequence, get_deserialized_message<LLM_Set_End_Of_Generation_Sequence_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__SET_END_OF_GENERATION_SEQUENCE), "LLM_Set_End_Of_Generation_Sequence_Reply"));

    CHECK(llm_set_end_of_generation_sequence.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'LLM_Set_End_Of_Generation_Sequence_Reply'");

    return static_cast<hailo_status>(llm_set_end_of_generation_sequence.status());
}

Expected<Buffer> LLMGetEndOfGenerationSequenceSerializer::serialize_request()
{
    LLM_Get_End_Of_Generation_Sequence_Request llm_get_end_of_generation_sequence;
    return get_serialized_message<LLM_Get_End_Of_Generation_Sequence_Request>(llm_get_end_of_generation_sequence,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GET_END_OF_GENERATION_SEQUENCE), "LLM_Get_End_Of_Generation_Sequence_Request");
}

hailo_status LLMGetEndOfGenerationSequenceSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto llm_get_end_of_generation_sequence, get_deserialized_message<LLM_Get_End_Of_Generation_Sequence_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GET_END_OF_GENERATION_SEQUENCE), "LLM_Get_End_Of_Generation_Sequence_Request"));

    return HAILO_SUCCESS;
}

Expected<Buffer> LLMGetEndOfGenerationSequenceSerializer::serialize_reply(hailo_status status, const std::string &end_of_generation_sequence,
    const std::vector<int> &end_of_generation_sequence_tokens)
{
    LLM_Get_End_Of_Generation_Sequence_Reply llm_get_end_of_generation_sequence;
    llm_get_end_of_generation_sequence.set_status(status);
    llm_get_end_of_generation_sequence.set_end_of_generation_sequence(end_of_generation_sequence);

    for (const auto &token : end_of_generation_sequence_tokens) {
        llm_get_end_of_generation_sequence.add_end_of_generation_sequence_tokens(token);
    }

    return get_serialized_message<LLM_Get_End_Of_Generation_Sequence_Reply>(llm_get_end_of_generation_sequence,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GET_END_OF_GENERATION_SEQUENCE), "LLM_Get_End_Of_Generation_Sequence_Reply");
}

Expected<std::tuple<std::string, std::vector<int>>> LLMGetEndOfGenerationSequenceSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto llm_get_end_of_generation_sequence, get_deserialized_message<LLM_Get_End_Of_Generation_Sequence_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GET_END_OF_GENERATION_SEQUENCE), "LLM_Get_End_Of_Generation_Sequence_Reply"));

    auto end_of_generation_sequence = llm_get_end_of_generation_sequence.end_of_generation_sequence();
    std::vector<int> end_of_generation_sequence_tokens;
    for (const auto &token : llm_get_end_of_generation_sequence.end_of_generation_sequence_tokens()) {
        end_of_generation_sequence_tokens.push_back(token);
    }
    return std::make_tuple(end_of_generation_sequence, end_of_generation_sequence_tokens);
}

Expected<Buffer> LLMSetStopTokensSerializer::serialize_request(const std::vector<std::vector<int>> &tokenized_stop_tokens)
{
    LLM_Set_Stop_Tokens_Request llm_set_stop_tokens;

    for (const auto &sequence : tokenized_stop_tokens) {
        auto *stop_sequence = llm_set_stop_tokens.add_tokenized_stop_tokens();
        for (const auto &token : sequence) {
            stop_sequence->add_tokens(token);
        }
    }

    return get_serialized_message<LLM_Set_Stop_Tokens_Request>(llm_set_stop_tokens,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__SET_STOP_TOKENS), "LLM_Set_Stop_Tokens_Request");
}

Expected<std::vector<std::vector<int>>> LLMSetStopTokensSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto llm_set_stop_tokens, get_deserialized_message<LLM_Set_Stop_Tokens_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__SET_STOP_TOKENS), "LLM_Set_Stop_Tokens_Request"));

    std::vector<std::vector<int>> tokenized_stop_tokens;
    for (const auto &sequence : llm_set_stop_tokens.tokenized_stop_tokens()) {
        std::vector<int> token_sequence;
        for (const auto &token : sequence.tokens()) {
            token_sequence.push_back(token);
        }
        tokenized_stop_tokens.push_back(token_sequence);
    }

    return tokenized_stop_tokens;
}

Expected<Buffer> LLMSetStopTokensSerializer::serialize_reply(hailo_status status)
{
    LLM_Set_Stop_Tokens_Reply llm_set_stop_tokens;
    llm_set_stop_tokens.set_status(status);

    return get_serialized_message<LLM_Set_Stop_Tokens_Reply>(llm_set_stop_tokens,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__SET_STOP_TOKENS), "LLM_Set_Stop_Tokens_Reply");
}

hailo_status LLMSetStopTokensSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto llm_set_stop_tokens, get_deserialized_message<LLM_Set_Stop_Tokens_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__SET_STOP_TOKENS), "LLM_Set_Stop_Tokens_Reply"));

    CHECK(llm_set_stop_tokens.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'LLM_Set_Stop_Tokens_Reply'");
    return static_cast<hailo_status>(llm_set_stop_tokens.status());
}

Expected<Buffer> LLMGetStopTokensSerializer::serialize_request()
{
    LLM_Get_Stop_Tokens_Request llm_get_stop_tokens;
    return get_serialized_message<LLM_Get_Stop_Tokens_Request>(llm_get_stop_tokens,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GET_STOP_TOKENS), "LLM_Get_Stop_Tokens_Request");
}

hailo_status LLMGetStopTokensSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto llm_get_stop_tokens, get_deserialized_message<LLM_Get_Stop_Tokens_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GET_STOP_TOKENS), "LLM_Get_Stop_Tokens_Request"));

    return HAILO_SUCCESS;
}

Expected<Buffer> LLMGetStopTokensSerializer::serialize_reply(hailo_status status, const std::vector<std::string> &stop_tokens, const std::vector<std::vector<int>> &tokenized_stop_tokens)
{
    LLM_Get_Stop_Tokens_Reply llm_get_stop_tokens;
    llm_get_stop_tokens.set_status(status);
    for (const auto &token : stop_tokens) {
        llm_get_stop_tokens.add_stop_tokens(token);
    }

    for (const auto &sequence : tokenized_stop_tokens) {
        auto *stop_sequence = llm_get_stop_tokens.add_tokenized_stop_tokens();
        for (const auto &token : sequence) {
            stop_sequence->add_tokens(token);
        }
    }

    return get_serialized_message<LLM_Get_Stop_Tokens_Reply>(llm_get_stop_tokens,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GET_STOP_TOKENS), "LLM_Get_Stop_Tokens_Reply");
}

Expected<std::tuple<std::vector<std::string>, std::vector<std::vector<int>>>> LLMGetStopTokensSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto llm_get_stop_tokens, get_deserialized_message<LLM_Get_Stop_Tokens_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GET_STOP_TOKENS), "LLM_Get_Stop_Tokens_Reply"));

    CHECK(llm_get_stop_tokens.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'LLM_Get_Stop_Tokens_Reply'");
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(llm_get_stop_tokens.status()), "Failed to get stop tokens");

    std::vector<std::string> stop_tokens;
    for (const auto &token : llm_get_stop_tokens.stop_tokens()) {
        stop_tokens.push_back(token);
    }

    std::vector<std::vector<int>> tokenized_stop_tokens;
    for (const auto &sequence : llm_get_stop_tokens.tokenized_stop_tokens()) {
        std::vector<int> token_sequence;
        for (const auto &token : sequence.tokens()) {
            token_sequence.push_back(token);
        }
        tokenized_stop_tokens.push_back(token_sequence);
    }

    return std::make_tuple(stop_tokens, tokenized_stop_tokens);
}

Expected<Buffer> LLMGeneratorReleaseSerializer::serialize_request()
{
    LLM_Generator_Release_Request llm_generator_release;
    return get_serialized_message<LLM_Generator_Release_Request>(llm_generator_release,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GENERATOR_RELEASE), "LLM_Generator_Release_Request");
}

hailo_status LLMGeneratorReleaseSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto llm_generator_release, get_deserialized_message<LLM_Generator_Release_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GENERATOR_RELEASE), "LLM_Generator_Release_Request"));

    return HAILO_SUCCESS;
}

Expected<Buffer> LLMGeneratorReleaseSerializer::serialize_reply(hailo_status status)
{
    LLM_Generator_Release_Reply llm_generator_release;
    llm_generator_release.set_status(status);

    return get_serialized_message<LLM_Generator_Release_Reply>(llm_generator_release,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GENERATOR_RELEASE), "LLM_Generator_Release_Reply");
}

hailo_status LLMGeneratorReleaseSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto llm_generator_release, get_deserialized_message<LLM_Generator_Release_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GENERATOR_RELEASE), "LLM_Generator_Release_Reply"));

    CHECK(llm_generator_release.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'LLM_Generator_Release_Reply'");
    return static_cast<hailo_status>(llm_generator_release.status());
}

Expected<Buffer> LLMReleaseSerializer::serialize_request()
{
    LLM_Release_Request llm_release;
    return get_serialized_message<LLM_Release_Request>(llm_release,
        static_cast<uint32_t>(HailoGenAIActionID::LLM_RELEASE), "LLM_Release_Request");
}

hailo_status LLMReleaseSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto llm_release, get_deserialized_message<LLM_Release_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::LLM_RELEASE), "LLM_Release_Request"));

    return HAILO_SUCCESS;
}

Expected<Buffer> LLMReleaseSerializer::serialize_reply(hailo_status status)
{
    LLM_Release_Reply llm_release;
    llm_release.set_status(status);

    return get_serialized_message<LLM_Release_Reply>(llm_release,
        static_cast<uint32_t>(HailoGenAIActionID::LLM_RELEASE), "LLM_Release_Reply");
}

hailo_status LLMReleaseSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto llm_release, get_deserialized_message<LLM_Release_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::LLM_RELEASE), "LLM_Release_Reply"));

    CHECK(llm_release.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'LLM_Release_Reply'");
    return static_cast<hailo_status>(llm_release.status());
}

Expected<Buffer> LLMGeneratorAbortSerializer::serialize_request()
{
    LLM_Generator_Abort_Request llm_generator_abort;
    return get_serialized_message<LLM_Generator_Abort_Request>(llm_generator_abort,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GENERATOR_ABORT), "LLM_Generator_Abort_Request");
}

hailo_status LLMGeneratorAbortSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto llm_generator_abort, get_deserialized_message<LLM_Generator_Abort_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GENERATOR_ABORT), "LLM_Generator_Abort_Request"));

    return HAILO_SUCCESS;
}

Expected<Buffer> LLMGeneratorAbortSerializer::serialize_reply(hailo_status status)
{
    LLM_Generator_Abort_Reply llm_generator_abort;
    llm_generator_abort.set_status(status);

    return get_serialized_message<LLM_Generator_Abort_Reply>(llm_generator_abort,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GENERATOR_ABORT), "LLM_Generator_Abort_Reply");  
}

hailo_status LLMGeneratorAbortSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto llm_generator_abort, get_deserialized_message<LLM_Generator_Abort_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GENERATOR_ABORT), "LLM_Generator_Abort_Reply")); 

    CHECK(llm_generator_abort.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'LLM_Generator_Abort_Reply'");
    return static_cast<hailo_status>(llm_generator_abort.status());
}

Expected<Buffer> LLMGetContextUsageSizeSerializer::serialize_request()
{
    LLM_Get_Context_Usage_Size_Request llm_get_context_usage_size;
    return get_serialized_message<LLM_Get_Context_Usage_Size_Request>(llm_get_context_usage_size,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GET_CONTEXT_USAGE_SIZE), "LLM_Get_Context_Usage_Size_Request");
}

hailo_status LLMGetContextUsageSizeSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto llm_get_context_usage_size, get_deserialized_message<LLM_Get_Context_Usage_Size_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GET_CONTEXT_USAGE_SIZE), "LLM_Get_Context_Usage_Size_Request"));

    return HAILO_SUCCESS;
}

Expected<Buffer> LLMGetContextUsageSizeSerializer::serialize_reply(hailo_status status, size_t context_usage)
{
    LLM_Get_Context_Usage_Size_Reply llm_get_context_usage_size;
    llm_get_context_usage_size.set_status(static_cast<uint32_t>(status));
    llm_get_context_usage_size.set_context_usage(static_cast<uint32_t>(context_usage));

    return get_serialized_message<LLM_Get_Context_Usage_Size_Reply>(llm_get_context_usage_size,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GET_CONTEXT_USAGE_SIZE), "LLM_Get_Context_Usage_Size_Reply");
}

Expected<size_t> LLMGetContextUsageSizeSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto llm_get_context_usage_size, get_deserialized_message<LLM_Get_Context_Usage_Size_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GET_CONTEXT_USAGE_SIZE), "LLM_Get_Context_Usage_Size_Reply"));

    CHECK(llm_get_context_usage_size.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'LLM_Get_Context_Usage_Size_Reply'");
    CHECK_SUCCESS(static_cast<hailo_status>(llm_get_context_usage_size.status()), "Failed to get current context usage");

    return static_cast<size_t>(llm_get_context_usage_size.context_usage());
}

Expected<Buffer> LLMGetMaxContextCapacitySerializer::serialize_request()
{
    LLM_Get_Max_Context_Capacity_Request llm_get_max_context_capacity;
    return get_serialized_message<LLM_Get_Max_Context_Capacity_Request>(llm_get_max_context_capacity,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GET_MAX_CONTEXT_CAPACITY), "LLM_Get_Max_Context_Capacity_Request");
}

hailo_status LLMGetMaxContextCapacitySerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto llm_get_max_context_capacity, get_deserialized_message<LLM_Get_Max_Context_Capacity_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GET_MAX_CONTEXT_CAPACITY), "LLM_Get_Max_Context_Capacity_Request"));

    return HAILO_SUCCESS;
}

Expected<Buffer> LLMGetMaxContextCapacitySerializer::serialize_reply(hailo_status status, size_t max_context_capacity)
{
    LLM_Get_Max_Context_Capacity_Reply llm_get_max_context_capacity;
    llm_get_max_context_capacity.set_status(static_cast<uint32_t>(status));
    llm_get_max_context_capacity.set_max_context_capacity(static_cast<uint32_t>(max_context_capacity));

    return get_serialized_message<LLM_Get_Max_Context_Capacity_Reply>(llm_get_max_context_capacity,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GET_MAX_CONTEXT_CAPACITY), "LLM_Get_Max_Context_Capacity_Reply");
}

Expected<size_t> LLMGetMaxContextCapacitySerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto llm_get_max_context_capacity, get_deserialized_message<LLM_Get_Max_Context_Capacity_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GET_MAX_CONTEXT_CAPACITY), "LLM_Get_Max_Context_Capacity_Reply"));

    CHECK(llm_get_max_context_capacity.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'LLM_Get_Max_Context_Capacity_Reply'");
    CHECK_SUCCESS(static_cast<hailo_status>(llm_get_max_context_capacity.status()), "Failed to get max context capacity");

    return static_cast<size_t>(llm_get_max_context_capacity.max_context_capacity());
}

Expected<Buffer> VLMCreateSerializer::serialize_request(const hailo_vdevice_params_t &vdevice_params, const std::string &hef_path, bool optimize_memory_on_device)
{
    VLM_Create_Request vlm_create;

    std::string group_id = get_group_id_as_string(vdevice_params);
    vlm_create.set_group_id(group_id);
    if (!hef_path.empty()) {
        vlm_create.set_hef_path(hef_path);
    }

    vlm_create.set_tokenizer_on_host(optimize_memory_on_device);

    return get_serialized_message<VLM_Create_Request>(vlm_create,
        static_cast<uint32_t>(HailoGenAIActionID::VLM__CREATE), "VLM_Create_Request");
}

Expected<Buffer> VLMCreateSerializer::serialize_request(const hailo_vdevice_params_t &vdevice_params, const std::string &hef_path, uint64_t file_size,
    bool optimize_memory_on_device)
{
    VLM_Create_Request vlm_create;

    std::string group_id = get_group_id_as_string(vdevice_params);
    vlm_create.set_group_id(group_id);
    if (!hef_path.empty()) {
        vlm_create.set_hef_path(hef_path);
    }

    // Set file size for chunked transfer
    vlm_create.set_file_size(file_size);

    vlm_create.set_tokenizer_on_host(optimize_memory_on_device);

    return get_serialized_message<VLM_Create_Request>(vlm_create,
        static_cast<uint32_t>(HailoGenAIActionID::VLM__CREATE), "VLM_Create_Request");
}

Expected<std::tuple<std::string, std::string, uint64_t, bool>> VLMCreateSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto vlm_create, get_deserialized_message<VLM_Create_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::VLM__CREATE), "VLM_Create_Request"));

    return std::tuple<std::string, std::string, uint64_t, bool>(
        vlm_create.group_id(), vlm_create.hef_path(), vlm_create.file_size(), vlm_create.tokenizer_on_host());
}

Expected<Buffer> VLMCreateSerializer::serialize_reply(hailo_status status,
    hailo_3d_image_shape_t input_frame_shape, hailo_format_t input_frame_format, const std::string &prompt_template, uint32_t embedding_features,
    uint32_t image_pad_token_id, uint32_t embeddings_per_frame)
{
    VLM_Create_Reply vlm_create;
    vlm_create.set_status(status);

    auto input_frame_shape_proto = vlm_create.mutable_frame_shape();
    input_frame_shape_proto->set_height(input_frame_shape.height);
    input_frame_shape_proto->set_width(input_frame_shape.width);
    input_frame_shape_proto->set_features(input_frame_shape.features);

    auto input_frame_format_proto = vlm_create.mutable_frame_format();
    input_frame_format_proto->set_format_order(static_cast<uint32_t>(input_frame_format.order));
    input_frame_format_proto->set_format_type(static_cast<uint32_t>(input_frame_format.type));

    vlm_create.set_prompt_template(prompt_template);
    vlm_create.set_embedding_features(embedding_features);

    vlm_create.set_image_pad_token_id(image_pad_token_id);
    vlm_create.set_embeddings_per_frame(embeddings_per_frame);

    return get_serialized_message<VLM_Create_Reply>(vlm_create,
        static_cast<uint32_t>(HailoGenAIActionID::VLM__CREATE), "VLM_Create_Reply");
}

Expected<std::tuple<hailo_3d_image_shape_t, hailo_format_t, std::string, uint32_t, uint32_t, uint32_t>> VLMCreateSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto vlm_create, get_deserialized_message<VLM_Create_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::VLM__CREATE), "VLM_Create_Reply"));

    CHECK(vlm_create.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'VLM_Create_Reply'");
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(vlm_create.status()), "Failed to create VLM");

    hailo_3d_image_shape_t input_frame_shape = {};
    input_frame_shape.height = vlm_create.frame_shape().height();
    input_frame_shape.width = vlm_create.frame_shape().width();
    input_frame_shape.features = vlm_create.frame_shape().features();

    hailo_format_t input_frame_format = {};
    input_frame_format.order = static_cast<hailo_format_order_t>(vlm_create.frame_format().format_order());
    input_frame_format.type = static_cast<hailo_format_type_t>(vlm_create.frame_format().format_type());

    return std::make_tuple(input_frame_shape, input_frame_format, vlm_create.prompt_template(), vlm_create.embedding_features(),
        vlm_create.image_pad_token_id(), vlm_create.embeddings_per_frame());
}

Expected<Buffer> VLMGeneratorGenerateSerializer::serialize_request(uint32_t number_of_frames)
{
    VLM_Generator_Generate_Request vlm_generator_generate;
    vlm_generator_generate.set_number_of_frames(number_of_frames);

    return get_serialized_message<VLM_Generator_Generate_Request>(vlm_generator_generate,
        static_cast<uint32_t>(HailoGenAIActionID::VLM__GENERATOR_GENERATE), "VLM_Generator_Generate_Request");
}

Expected<size_t> VLMGeneratorGenerateSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto vlm_generator_generate, get_deserialized_message<VLM_Generator_Generate_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::VLM__GENERATOR_GENERATE), "VLM_Generator_Generate_Request"));

    return vlm_generator_generate.number_of_frames();
}

Expected<Buffer> VLMGeneratorGenerateSerializer::serialize_reply(hailo_status status)
{
    VLM_Generator_Generate_Reply vlm_generator_generate;
    vlm_generator_generate.set_status(status);

    return get_serialized_message<VLM_Generator_Generate_Reply>(vlm_generator_generate,
        static_cast<uint32_t>(HailoGenAIActionID::VLM__GENERATOR_GENERATE), "VLM_Generator_Generate_Reply");
}

hailo_status VLMGeneratorGenerateSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto vlm_generator_generate, get_deserialized_message<VLM_Generator_Generate_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::VLM__GENERATOR_GENERATE), "VLM_Generator_Generate_Reply"));

    CHECK(vlm_generator_generate.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'VLM_Generator_Generate_Reply'");
    return static_cast<hailo_status>(vlm_generator_generate.status());
}

Expected<Buffer> GenAICheckHefExistsSerializer::serialize_request(const std::string &hef_path, const std::string &hash)
{
    GenAI_Check_Hef_Exists_Request genai_check_hef_exists;
    genai_check_hef_exists.set_hef_path(hef_path);
    genai_check_hef_exists.set_hash(hash);

    return get_serialized_message<GenAI_Check_Hef_Exists_Request>(genai_check_hef_exists,
        static_cast<uint32_t>(HailoGenAIActionID::GENAI__CHECK_HEF_EXISTS), "GenAI_Check_Hef_Exists_Request");
}

Expected<std::pair<std::string, std::string>> GenAICheckHefExistsSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto genai_check_hef_exists, get_deserialized_message<GenAI_Check_Hef_Exists_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::GENAI__CHECK_HEF_EXISTS), "GenAI_Check_Hef_Exists_Request"));

    return std::make_pair(genai_check_hef_exists.hef_path(), genai_check_hef_exists.hash());
}

Expected<Buffer> GenAICheckHefExistsSerializer::serialize_reply(hailo_status status, bool hef_exists)
{
    GenAI_Check_Hef_Exists_Reply genai_check_hef_exists;
    genai_check_hef_exists.set_status(status);
    genai_check_hef_exists.set_hef_exists(hef_exists);

    return get_serialized_message<GenAI_Check_Hef_Exists_Reply>(genai_check_hef_exists,
        static_cast<uint32_t>(HailoGenAIActionID::GENAI__CHECK_HEF_EXISTS), "GenAI_Check_Hef_Exists_Reply");
}

Expected<bool> GenAICheckHefExistsSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto genai_check_hef_exists, get_deserialized_message<GenAI_Check_Hef_Exists_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::GENAI__CHECK_HEF_EXISTS), "GenAI_Check_Hef_Exists_Reply"));

    CHECK(genai_check_hef_exists.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'GenAI_Check_Hef_Exists_Reply'");
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(genai_check_hef_exists.status()), "Failed to check HEF existence");

    return genai_check_hef_exists.hef_exists();
}

Expected<Buffer> Speech2TextCreateSerializer::serialize_request(const hailo_vdevice_params_t &vdevice_params)
{
    Speech2Text_Create_Request speech2text_create;
    std::string group_id = get_group_id_as_string(vdevice_params);
    speech2text_create.set_group_id(group_id);

    return get_serialized_message<Speech2Text_Create_Request>(speech2text_create,
        static_cast<uint32_t>(HailoGenAIActionID::SPEECH2TEXT__CREATE), "Speech2Text_Create_Request");
}

Expected<std::string> Speech2TextCreateSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto speech2text_create, get_deserialized_message<Speech2Text_Create_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::SPEECH2TEXT__CREATE), "Speech2Text_Create_Request"));

    return std::string(speech2text_create.group_id());
}

Expected<Buffer> Speech2TextCreateSerializer::serialize_reply(hailo_status status)
{
    Speech2Text_Create_Reply speech2text_create;
    speech2text_create.set_status(status);

    return get_serialized_message<Speech2Text_Create_Reply>(speech2text_create,
        static_cast<uint32_t>(HailoGenAIActionID::SPEECH2TEXT__CREATE), "Speech2Text_Create_Reply");
}

hailo_status Speech2TextCreateSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto speech2text_create, get_deserialized_message<Speech2Text_Create_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::SPEECH2TEXT__CREATE), "Speech2Text_Create_Reply"));

    CHECK(speech2text_create.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'Speech2Text_Create_Reply'");
    return static_cast<hailo_status>(speech2text_create.status());
}

Expected<Buffer> Speech2TextGenerateSerializer::serialize_request(const Speech2TextGeneratorParams &generator_params)
{
    Speech2Text_Generate_Request speech2text_generate;
    speech2text_generate.set_task_type(static_cast<uint32_t>(generator_params.task()));
    speech2text_generate.set_language(std::string(generator_params.language()));

    return get_serialized_message<Speech2Text_Generate_Request>(speech2text_generate,
        static_cast<uint32_t>(HailoGenAIActionID::SPEECH2TEXT__GENERATE), "Speech2Text_Generate_Request");
}

Expected<Speech2TextGeneratorParams> Speech2TextGenerateSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto speech2text_generate, get_deserialized_message<Speech2Text_Generate_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::SPEECH2TEXT__GENERATE), "Speech2Text_Generate_Request"));

    Speech2TextGeneratorParams generator_params(static_cast<Speech2TextTask>(speech2text_generate.task_type()), std::string(speech2text_generate.language()));
    return generator_params;
}

Expected<Buffer> Speech2TextGenerateSerializer::serialize_reply(hailo_status error_status)
{
    Speech2Text_Generate_Reply speech2text_generate;
    speech2text_generate.set_status(error_status);

    return get_serialized_message<Speech2Text_Generate_Reply>(speech2text_generate,
        static_cast<uint32_t>(HailoGenAIActionID::SPEECH2TEXT__GENERATE), "Speech2Text_Generate_Reply");
}

Expected<Buffer> Speech2TextGenerateSerializer::serialize_reply(hailo_status status, const std::vector<Speech2Text::SegmentInfo> &segments_infos)
{
    Speech2Text_Generate_Reply speech2text_generate;
    speech2text_generate.set_status(status);
    for (const auto &segment_info : segments_infos) {
        auto segment_info_proto = speech2text_generate.mutable_segments_infos()->Add();
        segment_info_proto->set_start_sec(segment_info.start_sec);
        segment_info_proto->set_end_sec(segment_info.end_sec);
        segment_info_proto->set_text(segment_info.text);
    }

    return get_serialized_message<Speech2Text_Generate_Reply>(speech2text_generate,
        static_cast<uint32_t>(HailoGenAIActionID::SPEECH2TEXT__GENERATE), "Speech2Text_Generate_Reply");
}

Expected<std::vector<Speech2Text::SegmentInfo>> Speech2TextGenerateSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto speech2text_generate, get_deserialized_message<Speech2Text_Generate_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::SPEECH2TEXT__GENERATE), "Speech2Text_Generate_Reply"));

    CHECK(speech2text_generate.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'Speech2Text_Generate_Reply'");
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(speech2text_generate.status()), "Failed to generate Speech2Text");

    std::vector<Speech2Text::SegmentInfo> segments_infos;
    for (const auto &segment_info_proto : speech2text_generate.segments_infos()) {
        Speech2Text::SegmentInfo segment_info;
        segment_info.start_sec = segment_info_proto.start_sec();
        segment_info.end_sec = segment_info_proto.end_sec();
        segment_info.text = segment_info_proto.text();
        segments_infos.push_back(std::move(segment_info));
    }

    return segments_infos;
}

Expected<Buffer> Speech2TextReleaseSerializer::serialize_request()
{
    Speech2Text_Release_Request speech2text_release;
    return get_serialized_message<Speech2Text_Release_Request>(speech2text_release,
        static_cast<uint32_t>(HailoGenAIActionID::SPEECH2TEXT__RELEASE), "Speech2Text_Release_Request");
}

hailo_status Speech2TextReleaseSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto speech2text_release, get_deserialized_message<Speech2Text_Release_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::SPEECH2TEXT__RELEASE), "Speech2Text_Release_Request"));

    return HAILO_SUCCESS;
}

Expected<Buffer> Speech2TextReleaseSerializer::serialize_reply(hailo_status status)
{
    Speech2Text_Release_Reply speech2text_release;
    speech2text_release.set_status(status);

    return get_serialized_message<Speech2Text_Release_Reply>(speech2text_release,
        static_cast<uint32_t>(HailoGenAIActionID::SPEECH2TEXT__RELEASE), "Speech2Text_Release_Reply");
}

hailo_status Speech2TextReleaseSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto speech2text_release, get_deserialized_message<Speech2Text_Release_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::SPEECH2TEXT__RELEASE), "Speech2Text_Release_Reply"));

    CHECK(speech2text_release.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'Speech2Text_Release_Reply'");
    return static_cast<hailo_status>(speech2text_release.status());
}

Expected<Buffer> Speech2TextTokenizeSerializer::serialize_request(const std::string &text)
{
    Speech2Text_Tokenize_Request speech2text_tokenize;
    speech2text_tokenize.set_text(text);
    return get_serialized_message<Speech2Text_Tokenize_Request>(speech2text_tokenize,
        static_cast<uint32_t>(HailoGenAIActionID::SPEECH2TEXT__TOKENIZE), "Speech2Text_Tokenize_Request");
}

Expected<std::string> Speech2TextTokenizeSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto speech2text_tokenize, get_deserialized_message<Speech2Text_Tokenize_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::SPEECH2TEXT__TOKENIZE), "Speech2Text_Tokenize_Request"));
    return std::string(speech2text_tokenize.text());
}

Expected<Buffer> Speech2TextTokenizeSerializer::serialize_reply(hailo_status status, const std::vector<int> &tokens)
{
    Speech2Text_Tokenize_Reply speech2text_tokenize;
    speech2text_tokenize.set_status(status);
    for (const auto &token : tokens) {
        speech2text_tokenize.add_tokens(token);
    }
    return get_serialized_message<Speech2Text_Tokenize_Reply>(speech2text_tokenize,
        static_cast<uint32_t>(HailoGenAIActionID::SPEECH2TEXT__TOKENIZE), "Speech2Text_Tokenize_Reply");
}

Expected<std::vector<int>> Speech2TextTokenizeSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto speech2text_tokenize, get_deserialized_message<Speech2Text_Tokenize_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::SPEECH2TEXT__TOKENIZE), "Speech2Text_Tokenize_Reply"));

    CHECK(speech2text_tokenize.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'Speech2Text_Tokenize_Reply'");
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(speech2text_tokenize.status()), "Failed to tokenize");

    std::vector<int> tokens;
    for (const auto &token : speech2text_tokenize.tokens()) {
        tokens.push_back(token);
    }
    return tokens;
}

} /* namespace genai */
} /* namespace hailort */
