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

namespace hailort
{
namespace genai
{

Expected<Buffer> LLMCreateSerializer::serialize_request(const hailo_vdevice_params_t &vdevice_params, const LLMParams &llm_params)
{
    TRY(auto buffer, Buffer::create(sizeof(GenAIRequest), BufferStorageParams::create_dma()));
    GenAIRequest *request = buffer.as_pointer<GenAIRequest>();

    request->type = HailoGenAIActionID::LLM__CREATE;
    auto lora = llm_params.lora();
    std::copy(lora.begin(), lora.end(), request->data.llm_create.lora_name);
    request->data.llm_create.lora_name_length = llm_params.lora().size();
    request->data.llm_create.is_builtin = (llm_params.hef() == BUILTIN);

    std::string group_id = (nullptr == vdevice_params.group_id) ? "" :
        std::string(vdevice_params.group_id);
    std::copy(group_id.begin(), group_id.end(), request->data.llm_create.group_id);
    request->data.llm_create.group_id_length = group_id.size();

    return buffer;
}

Expected<std::tuple<std::string, bool, std::string>> LLMCreateSerializer::deserialize_request(const MemoryView &serialized_request)
{
    const GenAIRequest *request = serialized_request.as_pointer<GenAIRequest>();
    CHECK(request->type == HailoGenAIActionID::LLM__CREATE, HAILO_INTERNAL_FAILURE, "Expected id {}, received {}",
        static_cast<int>(HailoGenAIActionID::LLM__CREATE), static_cast<int>(request->type));

    std::string group_id = (0 == request->data.llm_create.group_id_length) ? "" :
        std::string(request->data.llm_create.group_id, request->data.llm_create.group_id_length);
    return std::tuple<std::string, bool, std::string>(std::string(request->data.llm_create.lora_name, request->data.llm_create.lora_name_length),
        request->data.llm_create.is_builtin, group_id);
}

Expected<Buffer> LLMCreateSerializer::serialize_reply(hailo_status status)
{
    TRY(auto buffer, Buffer::create(sizeof(GenAIReply), BufferStorageParams::create_dma()));
    GenAIReply *reply = buffer.as_pointer<GenAIReply>();

    reply->type = HailoGenAIActionID::LLM__CREATE;
    reply->data.llm_create.status = status;

    return buffer;
}

hailo_status LLMCreateSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    const GenAIReply *reply = serialized_reply.as_pointer<GenAIReply>();
    CHECK(reply->type == HailoGenAIActionID::LLM__CREATE, HAILO_INTERNAL_FAILURE, "Expected id {}, received {}",
        static_cast<int>(HailoGenAIActionID::LLM__CREATE), static_cast<int>(reply->type));
    
    return reply->data.llm_create.status;
}

Expected<Buffer> LLMGetDefaultGeneratorParamsSerializer::serialize_request()
{
    TRY(auto buffer, Buffer::create(sizeof(GenAIRequest), BufferStorageParams::create_dma()));
    GenAIRequest *request = buffer.as_pointer<GenAIRequest>();

    request->type = HailoGenAIActionID::LLM__GET_DEFAULT_GENERATOR_PARAMS;

    return buffer;
}

hailo_status LLMGetDefaultGeneratorParamsSerializer::deserialize_request(const MemoryView &serialized_request)
{
    const GenAIRequest *request = serialized_request.as_pointer<GenAIRequest>();
    CHECK(request->type == HailoGenAIActionID::LLM__GET_DEFAULT_GENERATOR_PARAMS, HAILO_INTERNAL_FAILURE, "Expected id {}, received {}",
        static_cast<int>(HailoGenAIActionID::LLM__GET_DEFAULT_GENERATOR_PARAMS), static_cast<int>(request->type));
    return HAILO_SUCCESS;
}

Expected<Buffer> LLMGetDefaultGeneratorParamsSerializer::serialize_reply(const LLMGeneratorParams &default_generator_params, hailo_status status)
{
    TRY(auto buffer, Buffer::create(sizeof(GenAIReply), BufferStorageParams::create_dma()));
    GenAIReply *reply = buffer.as_pointer<GenAIReply>();

    reply->type = HailoGenAIActionID::LLM__GET_DEFAULT_GENERATOR_PARAMS;
    reply->data.llm_get_default_generator_params.status = status;
    reply->data.llm_get_default_generator_params.temperature = default_generator_params.temperature();
    reply->data.llm_get_default_generator_params.top_p = default_generator_params.top_p();
    reply->data.llm_get_default_generator_params.top_k = default_generator_params.top_k();
    reply->data.llm_get_default_generator_params.frequency_penalty = default_generator_params.frequency_penalty();
    reply->data.llm_get_default_generator_params.max_generated_tokens = default_generator_params.max_generated_tokens();
    reply->data.llm_get_default_generator_params.do_sample = default_generator_params.do_sample();
    reply->data.llm_get_default_generator_params.seed = default_generator_params.seed();

    return buffer;
}

Expected<LLMGeneratorParams> LLMGetDefaultGeneratorParamsSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    const GenAIReply *reply = serialized_reply.as_pointer<GenAIReply>();
    CHECK(reply->type == HailoGenAIActionID::LLM__GET_DEFAULT_GENERATOR_PARAMS, HAILO_INTERNAL_FAILURE, "Expected id {}, received {}",
        static_cast<int>(HailoGenAIActionID::LLM__GET_DEFAULT_GENERATOR_PARAMS), static_cast<int>(reply->type));
    CHECK_SUCCESS(reply->data.llm_get_default_generator_params.status, "Failed to get default generator params");

    LLMGeneratorParams res(reply->data.llm_get_default_generator_params.temperature, reply->data.llm_get_default_generator_params.top_p,
        reply->data.llm_get_default_generator_params.top_k, reply->data.llm_get_default_generator_params.frequency_penalty,
        reply->data.llm_get_default_generator_params.max_generated_tokens, reply->data.llm_get_default_generator_params.do_sample,
        reply->data.llm_get_default_generator_params.seed);

    return res;
}

Expected<Buffer> LLMGeneratorCreateSerializer::serialize_request(const LLMGeneratorParams &params)
{
    TRY(auto buffer, Buffer::create(sizeof(GenAIRequest), BufferStorageParams::create_dma()));
    GenAIRequest *request = buffer.as_pointer<GenAIRequest>();

    request->type = HailoGenAIActionID::LLM__GENERATOR_CREATE;

    request->data.llm_generator_create.temperature = params.temperature();
    request->data.llm_generator_create.top_p = params.top_p();
    request->data.llm_generator_create.top_k = params.top_k();
    request->data.llm_generator_create.frequency_penalty = params.frequency_penalty();
    request->data.llm_generator_create.max_generated_tokens = params.max_generated_tokens();
    request->data.llm_generator_create.do_sample = params.do_sample();
    request->data.llm_generator_create.seed = params.seed();

    return buffer;
}

Expected<LLMGeneratorParams> LLMGeneratorCreateSerializer::deserialize_request(const MemoryView &serialized_request)
{
     const GenAIRequest *request = serialized_request.as_pointer<GenAIRequest>();
    CHECK(request->type == HailoGenAIActionID::LLM__GENERATOR_CREATE, HAILO_INTERNAL_FAILURE, "Expected id {}, received {}",
        static_cast<int>(HailoGenAIActionID::LLM__GENERATOR_CREATE), static_cast<int>(request->type));
 
    LLMGeneratorParams res(request->data.llm_generator_create.temperature, request->data.llm_generator_create.top_p,
        request->data.llm_generator_create.top_k, request->data.llm_generator_create.frequency_penalty,
        request->data.llm_generator_create.max_generated_tokens, request->data.llm_generator_create.do_sample,
        request->data.llm_generator_create.seed);

    return res;
}

Expected<Buffer> LLMGeneratorCreateSerializer::serialize_reply(hailo_status status)
{
    TRY(auto buffer, Buffer::create(sizeof(GenAIReply), BufferStorageParams::create_dma()));
    GenAIReply *reply = buffer.as_pointer<GenAIReply>();

    reply->type = HailoGenAIActionID::LLM__GENERATOR_CREATE;
    reply->data.llm_generator_create.status = status;

    return buffer;
}

hailo_status LLMGeneratorCreateSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    const GenAIReply *reply = serialized_reply.as_pointer<GenAIReply>();
    CHECK(reply->type == HailoGenAIActionID::LLM__GENERATOR_CREATE, HAILO_INTERNAL_FAILURE, "Expected id {}, received {}",
        static_cast<int>(HailoGenAIActionID::LLM__GENERATOR_CREATE), static_cast<int>(reply->type));
    return reply->data.llm_generator_create.status;
}

Expected<Buffer> LLMGeneratorWriteSerializer::serialize_request()
{
    TRY(auto buffer, Buffer::create(sizeof(GenAIRequest), BufferStorageParams::create_dma()));
    GenAIRequest *request = buffer.as_pointer<GenAIRequest>();

    request->type = HailoGenAIActionID::LLM__GENERATOR_WRITE;

    return buffer;
}

hailo_status LLMGeneratorWriteSerializer::deserialize_request(const MemoryView &serialized_request)
{
    const GenAIRequest *request = serialized_request.as_pointer<GenAIRequest>();
    CHECK(request->type == HailoGenAIActionID::LLM__GENERATOR_WRITE, HAILO_INTERNAL_FAILURE, "Expected id {}, received {}",
        static_cast<int>(HailoGenAIActionID::LLM__GENERATOR_WRITE), static_cast<int>(request->type));
    return HAILO_SUCCESS;
}

Expected<Buffer> LLMGeneratorWriteSerializer::serialize_reply(hailo_status status)
{
    TRY(auto buffer, Buffer::create(sizeof(GenAIReply), BufferStorageParams::create_dma()));
    GenAIReply *reply = buffer.as_pointer<GenAIReply>();

    reply->type = HailoGenAIActionID::LLM__GENERATOR_WRITE;
    reply->data.llm_generator_write.status = status;

    return buffer;
}

hailo_status LLMGeneratorWriteSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    const GenAIReply *reply = serialized_reply.as_pointer<GenAIReply>();
    CHECK(reply->type == HailoGenAIActionID::LLM__GENERATOR_WRITE, HAILO_INTERNAL_FAILURE, "Expected id {}, received {}",
        static_cast<int>(HailoGenAIActionID::LLM__GENERATOR_WRITE), static_cast<int>(reply->type));
    return reply->data.llm_generator_write.status;
}

Expected<Buffer> LLMGeneratorGenerateSerializer::serialize_request()
{
    TRY(auto buffer, Buffer::create(sizeof(GenAIRequest), BufferStorageParams::create_dma()));
    GenAIRequest *request = buffer.as_pointer<GenAIRequest>();

    request->type = HailoGenAIActionID::LLM__GENERATOR_GENERATE;

    return buffer;
}

hailo_status LLMGeneratorGenerateSerializer::deserialize_request(const MemoryView &serialized_request)
{
    const GenAIRequest *request = serialized_request.as_pointer<GenAIRequest>();
    CHECK(request->type == HailoGenAIActionID::LLM__GENERATOR_GENERATE, HAILO_INTERNAL_FAILURE, "Expected id {}, received {}",
        static_cast<int>(HailoGenAIActionID::LLM__GENERATOR_GENERATE), static_cast<int>(request->type));
    return HAILO_SUCCESS;
}

Expected<Buffer> LLMGeneratorGenerateSerializer::serialize_reply(hailo_status status)
{
    TRY(auto buffer, Buffer::create(sizeof(GenAIReply), BufferStorageParams::create_dma()));
    GenAIReply *reply = buffer.as_pointer<GenAIReply>();

    reply->type = HailoGenAIActionID::LLM__GENERATOR_GENERATE;
    reply->data.llm_generator_generate.status = status;

    return buffer;
}

hailo_status LLMGeneratorGenerateSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    const GenAIReply *reply = serialized_reply.as_pointer<GenAIReply>();
    CHECK(reply->type == HailoGenAIActionID::LLM__GENERATOR_GENERATE, HAILO_INTERNAL_FAILURE, "Expected id {}, received {}",
        static_cast<int>(HailoGenAIActionID::LLM__GENERATOR_GENERATE), static_cast<int>(reply->type));
    return reply->data.llm_generator_generate.status;
}

Expected<Buffer> LLMGeneratorReadSerializer::serialize_request()
{
    TRY(auto buffer, Buffer::create(sizeof(GenAIRequest), BufferStorageParams::create_dma()));
    GenAIRequest *request = buffer.as_pointer<GenAIRequest>();

    request->type = HailoGenAIActionID::LLM__GENERATOR_READ;

    return buffer;
}

hailo_status LLMGeneratorReadSerializer::deserialize_request(const MemoryView &serialized_request)
{
    const GenAIRequest *request = serialized_request.as_pointer<GenAIRequest>();
    CHECK(request->type == HailoGenAIActionID::LLM__GENERATOR_READ, HAILO_INTERNAL_FAILURE, "Expected id {}, received {}",
        static_cast<int>(HailoGenAIActionID::LLM__GENERATOR_READ), static_cast<int>(request->type));
    return HAILO_SUCCESS;
}

Expected<Buffer> LLMGeneratorReadSerializer::serialize_reply(hailo_status status, const std::string &output, LLMGeneratorCompletion::Status generation_status)
{
    TRY(auto buffer, Buffer::create(sizeof(GenAIReply), BufferStorageParams::create_dma()));
    GenAIReply *reply = buffer.as_pointer<GenAIReply>();

    reply->type = HailoGenAIActionID::LLM__GENERATOR_READ;
    reply->data.llm_generator_read.status = status;
    reply->data.llm_generator_read.output_token_length = output.size();
    std::copy(output.begin(), output.end(), reply->data.llm_generator_read.output_token);
    reply->data.llm_generator_read.generation_status = static_cast<uint32_t>(generation_status);

    return buffer;
}

Expected<std::pair<std::string, LLMGeneratorCompletion::Status>> LLMGeneratorReadSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    const GenAIReply *reply = serialized_reply.as_pointer<GenAIReply>();
    CHECK(reply->type == HailoGenAIActionID::LLM__GENERATOR_READ, HAILO_INTERNAL_FAILURE, "Expected id {}, received {}",
        static_cast<int>(HailoGenAIActionID::LLM__GENERATOR_READ), static_cast<int>(reply->type));
    CHECK_SUCCESS(reply->data.llm_generator_read.status);

    return std::make_pair(std::string(reply->data.llm_generator_read.output_token, reply->data.llm_generator_read.output_token_length),
        static_cast<LLMGeneratorCompletion::Status>(reply->data.llm_generator_read.generation_status));
}

} /* namespace genai */
} /* namespace hailort */
