/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file genai_rpc.hpp
 * @brief HailoRT-GenAI protocol decleration
 **/

#ifndef _HAILO_COMMON_GENAI_RPC_HPP_
#define _HAILO_COMMON_GENAI_RPC_HPP_

namespace hailort
{
namespace genai
{

static const uint32_t MAX_STRING_SIZE = 128;

#pragma pack(push, 1)
struct LLM_Create_Request {
    char lora_name[MAX_STRING_SIZE];
    size_t lora_name_length;
    bool is_builtin; // If builtin, the next message is the HEF raw buffers

    char group_id[MAX_STRING_SIZE]; // We need 'hailo_vdevice_params_t', but only group-id is relevant
    size_t group_id_length;
};

struct LLM_Create_Reply {
    hailo_status status;
};

struct LLM_Get_Generator_Default_Params_Request {
    uint8_t placeholder;
};

struct LLM_Get_Generator_Default_Params_Reply {
    float32_t temperature;
    float32_t top_p;
    uint32_t top_k;
    float32_t frequency_penalty;
    uint32_t max_generated_tokens;
    bool do_sample;
    uint32_t seed;
    hailo_status status;
};

struct LLM_Generator_Create_Request {
    float temperature;
    float top_p;
    uint32_t top_k;
    float32_t frequency_penalty;
    uint32_t max_generated_tokens;
    bool do_sample;
    uint32_t seed;
};

struct LLM_Generator_Create_Reply {
    hailo_status status;
};

struct LLM_Generator_Write_Request {
    // Indicates that the next message to the server is the input prompt
    uint8_t placeholder;
};

struct LLM_Generator_Write_Reply {
    hailo_status status;
};

struct LLM_Generator_Generate_Request {
    // Indicates that the server should start generating text
    uint8_t placeholder;
};

struct LLM_Generator_Generate_Reply {
    hailo_status status;
};

struct LLM_Generator_Read_Request {
    // Indicates that the server should write back the next generated token
    uint8_t placeholder;
};

struct LLM_Generator_Read_Reply {
    hailo_status status;
    char output_token[MAX_STRING_SIZE];
    size_t output_token_length;
    uint32_t generation_status;
};

enum class HailoGenAIActionID {
    LLM__CREATE = 0,
    LLM__GET_DEFAULT_GENERATOR_PARAMS,
    LLM__GENERATOR_CREATE,
    LLM__GENERATOR_WRITE,
    LLM__GENERATOR_GENERATE,
    LLM__GENERATOR_READ,

    MAX_VALUE = HAILO_MAX_ENUM,
};

struct GenAIRequest {
    HailoGenAIActionID type;
    union {
        LLM_Create_Request llm_create;
        LLM_Get_Generator_Default_Params_Request llm_get_default_generator_params;
        LLM_Generator_Create_Request llm_generator_create;
        LLM_Generator_Write_Request llm_generator_write;
        LLM_Generator_Generate_Request llm_generator_generate;
        LLM_Generator_Read_Request llm_generator_read;
    } data;
};

struct GenAIReply {
    HailoGenAIActionID type;
    union {
        LLM_Create_Reply llm_create;
        LLM_Get_Generator_Default_Params_Reply llm_get_default_generator_params;
        LLM_Generator_Create_Reply llm_generator_create;
        LLM_Generator_Write_Reply llm_generator_write;
        LLM_Generator_Generate_Reply llm_generator_generate;
        LLM_Generator_Read_Reply llm_generator_read;
    } data;
};
#pragma pack(pop)

} // namespace genai
} // namespace hailort

#endif /* _HAILO_COMMON_GENAI_RPC_HPP_ */