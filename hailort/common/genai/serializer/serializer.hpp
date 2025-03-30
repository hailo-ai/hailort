/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file serializer.hpp
 * @brief HailoRT-GenAI protocol serialization
 **/

#ifndef _HAILO_SERIALIZER_HPP_
#define _HAILO_SERIALIZER_HPP_

#include "hailo/hailort.h"
#include "hailo/buffer.hpp"
#include "hailo/expected.hpp"
#include "common/utils.hpp"

#include "hailo/genai/llm/llm.hpp"

#include "genai_rpc.hpp"

namespace hailort
{
namespace genai
{

// TODO: HRT-15919 - Text2Image Serialization
#pragma pack(push, 1)
typedef struct {
    uint32_t steps_count;
    uint32_t samples_count;
    float32_t guidance_scale;
    uint32_t seed;
} text2image_generator_params_t;

typedef struct {
    bool has_negative_prompt;
    bool has_ip_adapter;
} text2image_generation_info_t;
#pragma pack(pop)

struct LLMCreateSerializer
{
    LLMCreateSerializer() = delete;

    static Expected<Buffer> serialize_request(const hailo_vdevice_params_t &vdevice_params, const LLMParams &llm_params);
    static Expected<std::tuple<std::string, bool, std::string>> deserialize_request(const MemoryView &serialized_request); // string - lora_name, bool - model_is_builtin

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct LLMGetDefaultGeneratorParamsSerializer
{
    LLMGetDefaultGeneratorParamsSerializer() = delete;

    static Expected<Buffer> serialize_request();
    static hailo_status deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(const LLMGeneratorParams &default_generator_params, hailo_status status);
    static Expected<LLMGeneratorParams> deserialize_reply(const MemoryView &serialized_reply);
};

struct LLMGeneratorCreateSerializer
{
    LLMGeneratorCreateSerializer() = delete;

    static Expected<Buffer> serialize_request(const LLMGeneratorParams &params);
    static Expected<LLMGeneratorParams> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct LLMGeneratorWriteSerializer
{
    LLMGeneratorWriteSerializer() = delete;

    static Expected<Buffer> serialize_request();
    static hailo_status deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct LLMGeneratorGenerateSerializer
{
    LLMGeneratorGenerateSerializer() = delete;

    static Expected<Buffer> serialize_request();
    static hailo_status deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct LLMGeneratorReadSerializer
{
    LLMGeneratorReadSerializer() = delete;

    static Expected<Buffer> serialize_request();
    static hailo_status deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, const std::string &output = "",
        LLMGeneratorCompletion::Status generation_status = LLMGeneratorCompletion::Status::GENERATING);
    static Expected<std::pair<std::string, LLMGeneratorCompletion::Status>> deserialize_reply(const MemoryView &serialized_reply);
};

} /* namespace genai */

} /* namespace hailort */

#endif /* _HAILO_SERIALIZER_HPP_ */
