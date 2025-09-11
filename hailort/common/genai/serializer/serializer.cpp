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

    return get_serialized_message<LLM_Create_Request>(llm_create, static_cast<uint32_t>(HailoGenAIActionID::LLM__CREATE), "LLM_Create_Request");
}

Expected<Buffer> LLMCreateSerializer::serialize_request(const hailo_vdevice_params_t &vdevice_params, const LLMParams &llm_params, const std::string &hef_path, uint64_t file_size)
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

    return get_serialized_message<LLM_Create_Request>(llm_create, static_cast<uint32_t>(HailoGenAIActionID::LLM__CREATE), "LLM_Create_Request");
}

Expected<std::tuple<std::string, std::string, std::string, uint64_t>> LLMCreateSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto llm_create, get_deserialized_message<LLM_Create_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__CREATE), "LLM_Create_Request"));

    return std::tuple<std::string, std::string, std::string, uint64_t>(
        llm_create.lora_name(), llm_create.hef_path(), llm_create.group_id(), llm_create.file_size());
}

Expected<Buffer> LLMCreateSerializer::serialize_reply(hailo_status status, const std::string &prompt_template)
{
    LLM_Create_Reply llm_create;
    llm_create.set_status(status);
    llm_create.set_prompt_template(prompt_template);

    return get_serialized_message<LLM_Create_Reply>(llm_create, static_cast<uint32_t>(HailoGenAIActionID::LLM__CREATE), "LLM_Create_Reply");
}

Expected<std::string> LLMCreateSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto llm_create, get_deserialized_message<LLM_Create_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__CREATE), "LLM_Create_Reply"));

    CHECK(llm_create.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'LLM_Create_Reply'");
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(llm_create.status()), "Failed to create LLM");

    auto prompt_template = llm_create.prompt_template();
    return prompt_template;
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

Expected<Buffer> LLMGeneratorGenerateSerializer::serialize_reply(hailo_status status)
{
    LLM_Generator_Generate_Reply llm_generator_generate;
    llm_generator_generate.set_status(status);

    return get_serialized_message<LLM_Generator_Generate_Reply>(llm_generator_generate,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GENERATOR_GENERATE), "LLM_Generator_Generate_Reply");
}

hailo_status LLMGeneratorGenerateSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto llm_generator_generate, get_deserialized_message<LLM_Generator_Generate_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GENERATOR_GENERATE), "LLM_Generator_Generate_Reply"));

    CHECK(llm_generator_generate.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'LLM_Generator_Generate_Reply'");
    return static_cast<hailo_status>(llm_generator_generate.status());
}

Expected<Buffer> LLMGeneratorReadSerializer::serialize_request(const std::chrono::milliseconds &timeout)
{
    LLM_Generator_Read_Request llm_generator_read;
    llm_generator_read.set_timeout_ms(static_cast<uint32_t>(timeout.count()));

    return get_serialized_message<LLM_Generator_Read_Request>(llm_generator_read,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GENERATOR_READ), "LLM_Generator_Read_Request");
}

Expected<std::chrono::milliseconds> LLMGeneratorReadSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto llm_generator_read, get_deserialized_message<LLM_Generator_Read_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GENERATOR_READ), "LLM_Generator_Read_Request"));

    return std::chrono::milliseconds(llm_generator_read.timeout_ms());
}

Expected<Buffer> LLMGeneratorReadSerializer::serialize_reply(hailo_status status, const std::string &output, LLMGeneratorCompletion::Status generation_status)
{
    LLM_Generator_Read_Reply llm_generator_read;
    llm_generator_read.set_status(status);
    llm_generator_read.set_output_token(output);
    llm_generator_read.set_generation_status(static_cast<uint32_t>(generation_status));

    return get_serialized_message<LLM_Generator_Read_Reply>(llm_generator_read,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GENERATOR_READ), "LLM_Generator_Read_Reply");
}

Expected<std::pair<std::string, LLMGeneratorCompletion::Status>> LLMGeneratorReadSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto llm_generator_read, get_deserialized_message<LLM_Generator_Read_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GENERATOR_READ), "LLM_Generator_Read_Reply"));

    CHECK(llm_generator_read.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'LLM_Generator_Read_Reply'");
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(llm_generator_read.status()), "Failed to read generator");
    CHECK(llm_generator_read.generation_status() < static_cast<uint32_t>(LLMGeneratorCompletion::Status::COUNT), HAILO_INTERNAL_FAILURE,
        "Failed to de-serialize 'LLM_Generator_Read_Reply'");

    return std::make_pair(llm_generator_read.output_token(),
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

Expected<Buffer> LLMSetEndOfGenerationSequenceSerializer::serialize_request(const std::string &end_of_generation_sequence)
{
    LLM_Set_End_Of_Generation_Sequence_Request llm_set_end_of_generation_sequence;
    llm_set_end_of_generation_sequence.set_end_of_generation_sequence(end_of_generation_sequence);

    return get_serialized_message<LLM_Set_End_Of_Generation_Sequence_Request>(llm_set_end_of_generation_sequence,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__SET_END_OF_GENERATION_SEQUENCE), "LLM_Set_End_Of_Generation_Sequence_Request");
}

Expected<std::string> LLMSetEndOfGenerationSequenceSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto llm_set_end_of_generation_sequence, get_deserialized_message<LLM_Set_End_Of_Generation_Sequence_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__SET_END_OF_GENERATION_SEQUENCE), "LLM_Set_End_Of_Generation_Sequence_Request"));


        auto end_of_generation_sequence = llm_set_end_of_generation_sequence.end_of_generation_sequence();
    return end_of_generation_sequence;
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

Expected<Buffer> LLMGetEndOfGenerationSequenceSerializer::serialize_reply(hailo_status status, const std::string &end_of_generation_sequence)
{
    LLM_Get_End_Of_Generation_Sequence_Reply llm_get_end_of_generation_sequence;
    llm_get_end_of_generation_sequence.set_status(status);
    llm_get_end_of_generation_sequence.set_end_of_generation_sequence(end_of_generation_sequence);

    return get_serialized_message<LLM_Get_End_Of_Generation_Sequence_Reply>(llm_get_end_of_generation_sequence,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GET_END_OF_GENERATION_SEQUENCE), "LLM_Get_End_Of_Generation_Sequence_Reply");
}

Expected<std::string> LLMGetEndOfGenerationSequenceSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto llm_get_end_of_generation_sequence, get_deserialized_message<LLM_Get_End_Of_Generation_Sequence_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GET_END_OF_GENERATION_SEQUENCE), "LLM_Get_End_Of_Generation_Sequence_Reply"));

    auto end_of_generation_sequence = llm_get_end_of_generation_sequence.end_of_generation_sequence();
    return end_of_generation_sequence;
}

Expected<Buffer> LLMSetStopTokensSerializer::serialize_request(const std::vector<std::string> &stop_tokens)
{
    LLM_Set_Stop_Tokens_Request llm_set_stop_tokens;
    for (const auto &token : stop_tokens) {
        llm_set_stop_tokens.add_stop_tokens(token);
    }

    return get_serialized_message<LLM_Set_Stop_Tokens_Request>(llm_set_stop_tokens,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__SET_STOP_TOKENS), "LLM_Set_Stop_Tokens_Request");
}

Expected<std::vector<std::string>> LLMSetStopTokensSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto llm_set_stop_tokens, get_deserialized_message<LLM_Set_Stop_Tokens_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__SET_STOP_TOKENS), "LLM_Set_Stop_Tokens_Request"));

    std::vector<std::string> stop_tokens;
    for (const auto &token : llm_set_stop_tokens.stop_tokens()) {
        stop_tokens.push_back(token);
    }
    return stop_tokens;
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

Expected<Buffer> LLMGetStopTokensSerializer::serialize_reply(hailo_status status, const std::vector<std::string> &stop_tokens)
{
    LLM_Get_Stop_Tokens_Reply llm_get_stop_tokens;
    llm_get_stop_tokens.set_status(status);
    for (const auto &token : stop_tokens) {
        llm_get_stop_tokens.add_stop_tokens(token);
    }

    return get_serialized_message<LLM_Get_Stop_Tokens_Reply>(llm_get_stop_tokens,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GET_STOP_TOKENS), "LLM_Get_Stop_Tokens_Reply");
}

Expected<std::vector<std::string>> LLMGetStopTokensSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto llm_get_stop_tokens, get_deserialized_message<LLM_Get_Stop_Tokens_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::LLM__GET_STOP_TOKENS), "LLM_Get_Stop_Tokens_Reply"));

    CHECK(llm_get_stop_tokens.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'LLM_Get_Stop_Tokens_Reply'");
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(llm_get_stop_tokens.status()), "Failed to get stop tokens");

    std::vector<std::string> stop_tokens;
    for (const auto &token : llm_get_stop_tokens.stop_tokens()) {
        stop_tokens.push_back(token);
    }
    return stop_tokens;
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

Expected<Buffer> VLMCreateSerializer::serialize_request(const hailo_vdevice_params_t &vdevice_params, const std::string &hef_path)
{
    VLM_Create_Request vlm_create;

    std::string group_id = get_group_id_as_string(vdevice_params);
    vlm_create.set_group_id(group_id);
    if (!hef_path.empty()) {
        vlm_create.set_hef_path(hef_path);
    }

    return get_serialized_message<VLM_Create_Request>(vlm_create,
        static_cast<uint32_t>(HailoGenAIActionID::VLM__CREATE), "VLM_Create_Request");
}

Expected<Buffer> VLMCreateSerializer::serialize_request(const hailo_vdevice_params_t &vdevice_params, const std::string &hef_path, uint64_t file_size)
{
    VLM_Create_Request vlm_create;

    std::string group_id = get_group_id_as_string(vdevice_params);
    vlm_create.set_group_id(group_id);
    if (!hef_path.empty()) {
        vlm_create.set_hef_path(hef_path);
    }

    // Set file size for chunked transfer
    vlm_create.set_file_size(file_size);

    return get_serialized_message<VLM_Create_Request>(vlm_create,
        static_cast<uint32_t>(HailoGenAIActionID::VLM__CREATE), "VLM_Create_Request");
}

Expected<std::tuple<std::string, std::string, uint64_t>> VLMCreateSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto vlm_create, get_deserialized_message<VLM_Create_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::VLM__CREATE), "VLM_Create_Request"));

    return std::tuple<std::string, std::string, uint64_t>(
        vlm_create.group_id(), vlm_create.hef_path(), vlm_create.file_size());
}

Expected<Buffer> VLMCreateSerializer::serialize_reply(hailo_status status,
    hailo_3d_image_shape_t input_frame_shape, hailo_format_t input_frame_format, const std::string &prompt_template)
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

    return get_serialized_message<VLM_Create_Reply>(vlm_create,
        static_cast<uint32_t>(HailoGenAIActionID::VLM__CREATE), "VLM_Create_Reply");
}

Expected<std::tuple<hailo_3d_image_shape_t, hailo_format_t, std::string>> VLMCreateSerializer::deserialize_reply(const MemoryView &serialized_reply)
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

    return std::make_tuple(input_frame_shape, input_frame_format, vlm_create.prompt_template());
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

Expected<Buffer> Text2ImageCreateSerializer::serialize_request(const hailo_vdevice_params_t &vdevice_params, bool is_builtin, bool is_ip_adapter,
    HailoDiffuserSchedulerType scheduler_type)
{
    Text2Image_Create_Request text2image_create;
    text2image_create.set_is_builtin(is_builtin);
    text2image_create.set_is_ip_adapter(is_ip_adapter);
    text2image_create.set_scheduler_type(static_cast<uint32_t>(scheduler_type));

    std::string group_id = get_group_id_as_string(vdevice_params);
    text2image_create.set_group_id(group_id);

    return get_serialized_message<Text2Image_Create_Request>(text2image_create, static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__CREATE), "Text2Image_Create_Request");
}

Expected<Text2ImageCreateSerializer::RequestInfo> Text2ImageCreateSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto text2image_create, get_deserialized_message<Text2Image_Create_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__CREATE), "Text2Image_Create_Request"));

    Text2ImageCreateSerializer::RequestInfo info;
    info.is_builtin = text2image_create.is_builtin();
    info.is_ip_adapter = text2image_create.is_ip_adapter();
    info.scheduler_type = static_cast<HailoDiffuserSchedulerType>(text2image_create.scheduler_type());
    info.group_id = text2image_create.group_id();

    return info;
}

Expected<Buffer> Text2ImageCreateSerializer::serialize_reply(hailo_status status, hailo_3d_image_shape_t output_frame_shape, hailo_format_t output_frame_format,
    hailo_3d_image_shape_t input_noise_frame_shape, hailo_format_t input_noise_frame_format)
{
    Text2Image_Create_Reply text2image_create;
    text2image_create.set_status(status);

    auto output_frame_shape_proto = text2image_create.mutable_output_frame_shape();
    output_frame_shape_proto->set_height(output_frame_shape.height);
    output_frame_shape_proto->set_width(output_frame_shape.width);
    output_frame_shape_proto->set_features(output_frame_shape.features);

    auto output_frame_format_proto = text2image_create.mutable_output_frame_format();
    output_frame_format_proto->set_format_order(static_cast<uint32_t>(output_frame_format.order));
    output_frame_format_proto->set_format_type(static_cast<uint32_t>(output_frame_format.type));

    auto input_noise_frame_shape_proto = text2image_create.mutable_input_noise_frame_shape();
    input_noise_frame_shape_proto->set_height(input_noise_frame_shape.height);
    input_noise_frame_shape_proto->set_width(input_noise_frame_shape.width);
    input_noise_frame_shape_proto->set_features(input_noise_frame_shape.features);

    auto input_noise_frame_format_proto = text2image_create.mutable_input_noise_frame_format();
    input_noise_frame_format_proto->set_format_order(static_cast<uint32_t>(input_noise_frame_format.order));
    input_noise_frame_format_proto->set_format_type(static_cast<uint32_t>(input_noise_frame_format.type));

    return get_serialized_message<Text2Image_Create_Reply>(text2image_create, static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__CREATE), "Text2Image_Create_Reply");
}

Expected<Buffer> Text2ImageCreateSerializer::serialize_reply(hailo_status error_status)
{
    Text2Image_Create_Reply text2image_create;
    text2image_create.set_status(error_status);

    return get_serialized_message<Text2Image_Create_Reply>(text2image_create, static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__CREATE), "Text2Image_Create_Reply");
}

Expected<Text2ImageCreateSerializer::ReplyInfo> Text2ImageCreateSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto text2image_create, get_deserialized_message<Text2Image_Create_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__CREATE), "Text2Image_Create_Reply"));

    CHECK(text2image_create.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'Text2Image_Create_Reply'");
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(text2image_create.status()), "Failed to create Text2Image");

    ReplyInfo reply_info = {};
    reply_info.output_frame_shape.height = text2image_create.output_frame_shape().height();
    reply_info.output_frame_shape.width = text2image_create.output_frame_shape().width();
    reply_info.output_frame_shape.features = text2image_create.output_frame_shape().features();

    reply_info.output_frame_format.order = static_cast<hailo_format_order_t>(text2image_create.output_frame_format().format_order());
    reply_info.output_frame_format.type = static_cast<hailo_format_type_t>(text2image_create.output_frame_format().format_type());

    reply_info.input_noise_frame_shape.height = text2image_create.input_noise_frame_shape().height();
    reply_info.input_noise_frame_shape.width = text2image_create.input_noise_frame_shape().width();
    reply_info.input_noise_frame_shape.features = text2image_create.input_noise_frame_shape().features();

    reply_info.input_noise_frame_format.order = static_cast<hailo_format_order_t>(text2image_create.input_noise_frame_format().format_order());
    reply_info.input_noise_frame_format.type = static_cast<hailo_format_type_t>(text2image_create.input_noise_frame_format().format_type());

    return reply_info;
}

Expected<Buffer> Text2ImageGeneratorCreateSerializer::serialize_request(const Text2ImageGeneratorParams &params)
{
    Text2Image_Generator_Create_Request text2image_generator_create;

    auto params_proto = text2image_generator_create.mutable_generator_params();
    params_proto->set_steps_count(params.steps_count());
    params_proto->set_samples_count(params.samples_count());
    params_proto->set_guidance_scale(params.guidance_scale());
    params_proto->set_seed(params.seed());

    return get_serialized_message<Text2Image_Generator_Create_Request>(text2image_generator_create,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__GENERATOR_CREATE), "Text2Image_Generator_Create_Request");
}

Expected<Text2ImageGeneratorParams> Text2ImageGeneratorCreateSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto text2image_generator_create, get_deserialized_message<Text2Image_Generator_Create_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__GENERATOR_CREATE), "Text2Image_Generator_Create_Request"));

    Text2ImageGeneratorParams params(text2image_generator_create.generator_params().samples_count(),
        text2image_generator_create.generator_params().steps_count(),
        text2image_generator_create.generator_params().guidance_scale(),
        text2image_generator_create.generator_params().seed());

    return params;
}

Expected<Buffer> Text2ImageGeneratorCreateSerializer::serialize_reply(hailo_status status)
{
    Text2Image_Generator_Create_Reply text2image_generator_create;
    text2image_generator_create.set_status(status);

    return get_serialized_message<Text2Image_Generator_Create_Reply>(text2image_generator_create,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__GENERATOR_CREATE), "Text2Image_Generator_Create_Reply");
}

hailo_status Text2ImageGeneratorCreateSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto text2image_generator_create, get_deserialized_message<Text2Image_Generator_Create_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__GENERATOR_CREATE), "Text2Image_Generator_Create_Reply"));

    CHECK(text2image_generator_create.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to deserialize 'Text2Image_Generator_Create_Reply'");
    return static_cast<hailo_status>(text2image_generator_create.status());
}

Expected<Buffer> Text2ImageGeneratorGenerateSerializer::serialize_request(bool has_negative_prompt)
{
    Text2Image_Generator_Generate_Request text2image_generator_generate;
    text2image_generator_generate.set_has_negative_prompt(has_negative_prompt);

    return get_serialized_message<Text2Image_Generator_Generate_Request>(text2image_generator_generate,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__GENERATOR_GENERATE), "Text2Image_Generator_Generate_Request");
}

Expected<bool> Text2ImageGeneratorGenerateSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto text2image_generator_generate, get_deserialized_message<Text2Image_Generator_Generate_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__GENERATOR_GENERATE), "Text2Image_Generator_Generate_Request"));

    return text2image_generator_generate.has_negative_prompt();
}

Expected<Buffer> Text2ImageGeneratorGenerateSerializer::serialize_reply(hailo_status status)
{
    Text2Image_Generator_Generate_Reply text2image_generator_generate;
    text2image_generator_generate.set_status(status);

    return get_serialized_message<Text2Image_Generator_Generate_Reply>(text2image_generator_generate,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__GENERATOR_GENERATE), "Text2Image_Generator_Generate_Reply");
}

hailo_status Text2ImageGeneratorGenerateSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto text2image_generator_generate, get_deserialized_message<Text2Image_Generator_Generate_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__GENERATOR_GENERATE), "Text2Image_Generator_Generate_Reply"));

    CHECK(text2image_generator_generate.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to deserialize 'Text2Image_Generator_Generate_Reply'");
    return static_cast<hailo_status>(text2image_generator_generate.status());
}

Expected<Buffer> Text2ImageGetGeneratorParamsSerializer::serialize_request()
{
    Text2Image_Get_Generator_Params_Request text2image_get_generator_params;
    return get_serialized_message<Text2Image_Get_Generator_Params_Request>(text2image_get_generator_params,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__GET_GENERATOR_PARAMS), "Text2Image_Get_Generator_Params_Request");
}


hailo_status Text2ImageGetGeneratorParamsSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto text2image_get_generator_params, get_deserialized_message<Text2Image_Get_Generator_Params_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__GET_GENERATOR_PARAMS), "Text2Image_Get_Generator_Params_Request"));

    return HAILO_SUCCESS;
}

Expected<Buffer> Text2ImageGetGeneratorParamsSerializer::serialize_reply(hailo_status status, const Text2ImageGeneratorParams &generator_params)
{
    Text2Image_Get_Generator_Params_Reply text2image_get_generator_params;
    text2image_get_generator_params.set_status(status);

    auto params_proto = text2image_get_generator_params.mutable_generator_params();
    params_proto->set_samples_count(generator_params.samples_count());
    params_proto->set_steps_count(generator_params.steps_count());
    params_proto->set_guidance_scale(generator_params.guidance_scale());
    params_proto->set_seed(generator_params.seed());

    return get_serialized_message<Text2Image_Get_Generator_Params_Reply>(text2image_get_generator_params,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__GET_GENERATOR_PARAMS), "Text2Image_Get_Generator_Params_Reply");
}

Expected<Buffer> Text2ImageGetGeneratorParamsSerializer::serialize_reply(hailo_status status)
{
    Text2Image_Get_Generator_Params_Reply text2image_get_generator_params;
    text2image_get_generator_params.set_status(status);

    return get_serialized_message<Text2Image_Get_Generator_Params_Reply>(text2image_get_generator_params,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__GET_GENERATOR_PARAMS), "Text2Image_Get_Generator_Params_Reply");
}

Expected<Text2ImageGeneratorParams> Text2ImageGetGeneratorParamsSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto text2image_get_generator_params, get_deserialized_message<Text2Image_Get_Generator_Params_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__GET_GENERATOR_PARAMS), "Text2Image_Get_Generator_Params_Reply"));

    CHECK(text2image_get_generator_params.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to deserialize 'Text2Image_Get_Generator_Params_Reply'");
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(text2image_get_generator_params.status()), "Failed to get Text2Image generator params");

    Text2ImageGeneratorParams params(text2image_get_generator_params.generator_params().samples_count(),
        text2image_get_generator_params.generator_params().steps_count(),
        text2image_get_generator_params.generator_params().guidance_scale(),
        text2image_get_generator_params.generator_params().seed());

    return params;
}

Expected<Buffer> Text2ImageGetIPAdapterFrameInfoSerializer::serialize_request()
{
    Text2Image_Get_IP_Adapter_Frame_Info_Request text2image_get_ip_adapter_frame_info;
    return get_serialized_message<Text2Image_Get_IP_Adapter_Frame_Info_Request>(text2image_get_ip_adapter_frame_info,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__GET_IP_ADAPTER_FRAME_INFO), "Text2Image_Get_IP_Adapter_Frame_Info_Request");
}

hailo_status Text2ImageGetIPAdapterFrameInfoSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto text2image_get_ip_adapter_frame_info, get_deserialized_message<Text2Image_Get_IP_Adapter_Frame_Info_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__GET_IP_ADAPTER_FRAME_INFO), "Text2Image_Get_IP_Adapter_Frame_Info_Request"));

    return HAILO_SUCCESS;
}

Expected<Buffer> Text2ImageGetIPAdapterFrameInfoSerializer::serialize_reply(hailo_status status, hailo_3d_image_shape_t ip_adapter_frame_shape, hailo_format_t ip_adapter_frame_format)
{
    Text2Image_Get_IP_Adapter_Frame_Info_Reply text2image_get_ip_adapter_frame_info;
    text2image_get_ip_adapter_frame_info.set_status(status);

    auto ip_adapter_frame_shape_proto = text2image_get_ip_adapter_frame_info.mutable_shape();
    ip_adapter_frame_shape_proto->set_height(ip_adapter_frame_shape.height);
    ip_adapter_frame_shape_proto->set_width(ip_adapter_frame_shape.width);
    ip_adapter_frame_shape_proto->set_features(ip_adapter_frame_shape.features);

    auto ip_adapter_frame_format_proto = text2image_get_ip_adapter_frame_info.mutable_format();
    ip_adapter_frame_format_proto->set_format_order(static_cast<uint32_t>(ip_adapter_frame_format.order));
    ip_adapter_frame_format_proto->set_format_type(static_cast<uint32_t>(ip_adapter_frame_format.type));

    return get_serialized_message<Text2Image_Get_IP_Adapter_Frame_Info_Reply>(text2image_get_ip_adapter_frame_info,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__GET_IP_ADAPTER_FRAME_INFO), "Text2Image_Get_IP_Adapter_Frame_Info_Reply");
}

Expected<std::pair<hailo_3d_image_shape_t, hailo_format_t>> Text2ImageGetIPAdapterFrameInfoSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto text2image_get_ip_adapter_frame_info, get_deserialized_message<Text2Image_Get_IP_Adapter_Frame_Info_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__GET_IP_ADAPTER_FRAME_INFO), "Text2Image_Get_IP_Adapter_Frame_Info_Reply"));

    CHECK(text2image_get_ip_adapter_frame_info.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'Text2Image_Get_IP_Adapter_Frame_Info_Reply'");
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(text2image_get_ip_adapter_frame_info.status()), "Failed to get IP adapter frame info");

    hailo_3d_image_shape_t ip_adapter_frame_shape = {};
    ip_adapter_frame_shape.height = text2image_get_ip_adapter_frame_info.shape().height();
    ip_adapter_frame_shape.width = text2image_get_ip_adapter_frame_info.shape().width();
    ip_adapter_frame_shape.features = text2image_get_ip_adapter_frame_info.shape().features();

    hailo_format_t ip_adapter_frame_format = {};
    ip_adapter_frame_format.order = static_cast<hailo_format_order_t>(text2image_get_ip_adapter_frame_info.format().format_order());
    ip_adapter_frame_format.type = static_cast<hailo_format_type_t>(text2image_get_ip_adapter_frame_info.format().format_type());

    return std::make_pair(ip_adapter_frame_shape, ip_adapter_frame_format);
}

Expected<Buffer> Text2ImageGetIPAdapterFrameInfoSerializer::serialize_reply(hailo_status error_status)
{
    Text2Image_Get_IP_Adapter_Frame_Info_Reply text2image_get_ip_adapter_frame_info;
    text2image_get_ip_adapter_frame_info.set_status(error_status);

    return get_serialized_message<Text2Image_Get_IP_Adapter_Frame_Info_Reply>(text2image_get_ip_adapter_frame_info,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__GET_IP_ADAPTER_FRAME_INFO), "Text2Image_Get_IP_Adapter_Frame_Info_Reply");
}

Expected<Buffer> Text2ImageTokenizeSerializer::serialize_request(const std::string &prompt)
{
    Text2Image_Tokenize_Request text2image_tokenize;
    text2image_tokenize.set_prompt(prompt);

    return get_serialized_message<Text2Image_Tokenize_Request>(text2image_tokenize,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__TOKENIZE), "Text2Image_Tokenize_Request");
}

Expected<std::string> Text2ImageTokenizeSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto text2image_tokenize, get_deserialized_message<Text2Image_Tokenize_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__TOKENIZE), "Text2Image_Tokenize_Request"));

    auto cpy = text2image_tokenize.prompt();
    return cpy;
}

Expected<Buffer> Text2ImageTokenizeSerializer::serialize_reply(hailo_status status, const std::vector<int> &tokens)
{
    Text2Image_Tokenize_Reply text2image_tokenize;
    text2image_tokenize.set_status(status);
    for (auto token : tokens) {
        text2image_tokenize.add_tokens(token);
    }

    return get_serialized_message<Text2Image_Tokenize_Reply>(text2image_tokenize,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__TOKENIZE), "Text2Image_Tokenize_Reply");
}

Expected<std::vector<int>> Text2ImageTokenizeSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto text2image_tokenize, get_deserialized_message<Text2Image_Tokenize_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__TOKENIZE), "Text2Image_Tokenize_Reply"));

    CHECK(text2image_tokenize.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'Text2Image_Tokenize_Reply'");
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(text2image_tokenize.status()), "Failed to tokenize");

    std::vector<int> tokens;
    for (auto token : text2image_tokenize.tokens()) {
        tokens.push_back(token);
    }

    return tokens;
}

Expected<Buffer> Text2ImageGeneratorAbortSerializer::serialize_request()
{
    Text2Image_Generator_Abort_Request text2image_generator_abort;
    return get_serialized_message<Text2Image_Generator_Abort_Request>(text2image_generator_abort,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__GENERATOR_ABORT), "Text2Image_Generator_Abort_Request");
}

hailo_status Text2ImageGeneratorAbortSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto text2image_generator_abort, get_deserialized_message<Text2Image_Generator_Abort_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__GENERATOR_ABORT), "Text2Image_Generator_Abort_Request"));

    return HAILO_SUCCESS;
}

Expected<Buffer> Text2ImageGeneratorAbortSerializer::serialize_reply(hailo_status status)
{
    Text2Image_Generator_Abort_Reply text2image_generator_abort;
    text2image_generator_abort.set_status(status);

    return get_serialized_message<Text2Image_Generator_Abort_Reply>(text2image_generator_abort,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__GENERATOR_ABORT), "Text2Image_Generator_Abort_Reply");
}

hailo_status Text2ImageGeneratorAbortSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto text2image_generator_abort, get_deserialized_message<Text2Image_Generator_Abort_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__GENERATOR_ABORT), "Text2Image_Generator_Abort_Reply"));

    CHECK(text2image_generator_abort.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'Text2Image_Generator_Abort_Reply'");
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(text2image_generator_abort.status()), "Failed to abort Text2Image generation");

    return HAILO_SUCCESS;
}

Expected<Buffer> Text2ImageReleaseSerializer::serialize_request()
{
    Text2Image_Release_Request text2image_release;
    return get_serialized_message<Text2Image_Release_Request>(text2image_release,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__RELEASE), "Text2Image_Release_Request");
}

hailo_status Text2ImageReleaseSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto text2image_release, get_deserialized_message<Text2Image_Release_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__RELEASE), "Text2Image_Release_Request"));

    return HAILO_SUCCESS;
}

Expected<Buffer> Text2ImageReleaseSerializer::serialize_reply(hailo_status status)
{
    Text2Image_Release_Reply text2image_release;
    text2image_release.set_status(status);

    return get_serialized_message<Text2Image_Release_Reply>(text2image_release,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__RELEASE), "Text2Image_Release_Reply");
}

hailo_status Text2ImageReleaseSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto text2image_release, get_deserialized_message<Text2Image_Release_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__RELEASE), "Text2Image_Release_Reply"));

    CHECK(text2image_release.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'Text2Image_Release_Reply'");
    return static_cast<hailo_status>(text2image_release.status());
}

Expected<Buffer> Text2ImageGeneratorSetInitialNoiseSerializer::serialize_request()
{
    Text2Image_Generator_Set_Initial_Noise_Request text2image_generator_set_initial_noise;
    return get_serialized_message<Text2Image_Generator_Set_Initial_Noise_Request>(text2image_generator_set_initial_noise,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__GENERATOR_SET_INITIAL_NOISE), "Text2Image_Generator_Set_Initial_Noise_Request");
}

hailo_status Text2ImageGeneratorSetInitialNoiseSerializer::deserialize_request(const MemoryView &serialized_request)
{
    TRY(auto text2image_generator_set_initial_noise, get_deserialized_message<Text2Image_Generator_Set_Initial_Noise_Request>(serialized_request,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__GENERATOR_SET_INITIAL_NOISE), "Text2Image_Generator_Set_Initial_Noise_Request"));

    return HAILO_SUCCESS;
}

Expected<Buffer> Text2ImageGeneratorSetInitialNoiseSerializer::serialize_reply(hailo_status status)
{
    Text2Image_Generator_Set_Initial_Noise_Reply text2image_generator_set_initial_noise;
    text2image_generator_set_initial_noise.set_status(status);

    return get_serialized_message<Text2Image_Generator_Set_Initial_Noise_Reply>(text2image_generator_set_initial_noise,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__GENERATOR_SET_INITIAL_NOISE), "Text2Image_Generator_Set_Initial_Noise_Reply");
}

hailo_status Text2ImageGeneratorSetInitialNoiseSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    TRY(auto text2image_generator_set_initial_noise, get_deserialized_message<Text2Image_Generator_Set_Initial_Noise_Reply>(serialized_reply,
        static_cast<uint32_t>(HailoGenAIActionID::TEXT2IMAGE__GENERATOR_SET_INITIAL_NOISE), "Text2Image_Generator_Set_Initial_Noise_Reply"));

    CHECK(text2image_generator_set_initial_noise.status() < HAILO_STATUS_COUNT, HAILO_INTERNAL_FAILURE, "Failed to de-serialize 'Text2Image_Generator_Set_Initial_Noise_Reply'");
    return static_cast<hailo_status>(text2image_generator_set_initial_noise.status());
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

} /* namespace genai */
} /* namespace hailort */
