/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file serializer.hpp
 * @brief HailoRT-GenAI protocol serialization
 **/

#ifndef _HAILO_GENAI_SERIALIZER_HPP_
#define _HAILO_GENAI_SERIALIZER_HPP_

#include "hailo/hailort.h"
#include "hailo/buffer.hpp"
#include "hailo/expected.hpp"
#include "common/utils.hpp"

#include "hailo/genai/llm/llm.hpp"
#include "hailo/genai/text2image/text2image.hpp"

namespace hailort
{
namespace genai
{

enum class HailoGenAIActionID {
    LLM__CREATE = 0,
    LLM__GET_GENERATOR_PARAMS,

    LLM__GENERATOR_CREATE,
    LLM__GENERATOR_WRITE,
    LLM__GENERATOR_GENERATE,
    LLM__GENERATOR_READ,

    LLM__TOKENIZE,

    LLM_CLEAR_CONTEXT,

    LLM_RELEASE,

    VLM__CREATE,
    VLM__GENERATOR_GENERATE,

    TEXT2IMAGE__CREATE,
    TEXT2IMAGE__GET_GENERATOR_PARAMS,
    TEXT2IMAGE__GENERATOR_CREATE,
    TEXT2IMAGE__GENERATOR_GENERATE,
    TEXT2IMAGE__GET_IP_ADAPTER_FRAME_INFO,
    TEXT2IMAGE__TOKENIZE,
    TEXT2IMAGE__GENERATOR_SET_INITIAL_NOISE,
    TEXT2IMAGE__RELEASE,

    GENAI_ACTIONS_COUNT,
    MAX_VALUE = HAILO_MAX_ENUM,
};

inline std::string get_group_id_as_string(const hailo_vdevice_params_t &vdevice_params)
{
    std::string group_id = (nullptr == vdevice_params.group_id) ? "" :
        std::string(vdevice_params.group_id);
    // If group-id is 'HAILO_UNIQUE_VDEVICE_GROUP_ID', we should override it and get the first available VDevice,
    // to ensure we use the same VDevice in the server
    if (HAILO_UNIQUE_VDEVICE_GROUP_ID == group_id) {
        group_id = FORCE_GET_FIRST_AVAILABLE;
    }

    return group_id;
}

class GenAISerializerUtils
{
public:
    GenAISerializerUtils() = delete;

    static Expected<uint32_t> get_action_id(const MemoryView &raw_request);
};

struct LLMCreateSerializer
{
    LLMCreateSerializer() = delete;

    static Expected<Buffer> serialize_request(const hailo_vdevice_params_t &vdevice_params, const LLMParams &llm_params);
    static Expected<std::tuple<std::string, bool, std::string>> deserialize_request(const MemoryView &serialized_request); // string - lora_name, bool - model_is_builtin

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct LLMGetGeneratorParamsSerializer
{
    LLMGetGeneratorParamsSerializer() = delete;

    static Expected<Buffer> serialize_request();
    static hailo_status deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, const LLMGeneratorParams &default_generator_params = {{}, {}, {}, {}, {}, {}, {}});
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

    static Expected<Buffer> serialize_request(const std::chrono::milliseconds &timeout);
    static Expected<std::chrono::milliseconds> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, const std::string &output = "",
        LLMGeneratorCompletion::Status generation_status = LLMGeneratorCompletion::Status::GENERATING);
    static Expected<std::pair<std::string, LLMGeneratorCompletion::Status>> deserialize_reply(const MemoryView &serialized_reply);
};

struct LLMTokenizeSerializer
{
    LLMTokenizeSerializer() = delete;

    static Expected<Buffer> serialize_request(const std::string &prompt);
    static Expected<std::string> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, const std::vector<int> &tokens = {});
    static Expected<std::vector<int>> deserialize_reply(const MemoryView &serialized_reply);

};

struct LLMClearContextSerializer
{
    LLMClearContextSerializer() = delete;

    static Expected<Buffer> serialize_request();
    static hailo_status deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct LLMReleaseSerializer
{
    LLMReleaseSerializer() = delete;

    static Expected<Buffer> serialize_request();
    static hailo_status deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};


struct VLMCreateSerializer
{
    VLMCreateSerializer() = delete;

    static Expected<Buffer> serialize_request(const hailo_vdevice_params_t &vdevice_params);
    static Expected<std::string> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status,
        hailo_3d_image_shape_t input_frame_shape = {}, hailo_format_t input_frame_format = {});
    static Expected<std::pair<hailo_3d_image_shape_t, hailo_format_t>> deserialize_reply(const MemoryView &serialized_reply);
};

struct VLMGeneratorGenerateSerializer
{
    VLMGeneratorGenerateSerializer() = delete;

    static Expected<Buffer> serialize_request(uint32_t input_frames_count);
    static Expected<size_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};
struct Text2ImageCreateSerializer
{
    Text2ImageCreateSerializer() = delete;

    struct RequestInfo {
        bool is_builtin;
        bool is_ip_adapter;
        HailoDiffuserSchedulerType scheduler_type;
        std::string group_id;
    };

    struct ReplyInfo {
        hailo_3d_image_shape_t output_frame_shape;
        hailo_format_t output_frame_format;
        hailo_3d_image_shape_t input_noise_frame_shape;
        hailo_format_t input_noise_frame_format;
    };

    static Expected<Buffer> serialize_request(const hailo_vdevice_params_t &vdevice_params, bool is_builtin, bool is_ip_adapter,
        HailoDiffuserSchedulerType scheduler_type);
    static Expected<Text2ImageCreateSerializer::RequestInfo> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, hailo_3d_image_shape_t output_frame_shape, hailo_format_t output_frame_format,
        hailo_3d_image_shape_t input_noise_frame_shape, hailo_format_t input_noise_frame_format);
    static Expected<Buffer> serialize_reply(hailo_status error_status);
    static Expected<Text2ImageCreateSerializer::ReplyInfo> deserialize_reply(const MemoryView &serialized_reply);
};

struct Text2ImageGeneratorCreateSerializer
{
    Text2ImageGeneratorCreateSerializer() = delete;

    static Expected<Buffer> serialize_request(const Text2ImageGeneratorParams &params);
    static Expected<Text2ImageGeneratorParams> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct Text2ImageGeneratorGenerateSerializer
{
    Text2ImageGeneratorGenerateSerializer() = delete;

    static Expected<Buffer> serialize_request(bool has_negative_prompt);
    static Expected<bool> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct Text2ImageGetGeneratorParamsSerializer
{
    Text2ImageGetGeneratorParamsSerializer() = delete;

    static Expected<Buffer> serialize_request();
    static hailo_status deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, const Text2ImageGeneratorParams &generator_params);
    static Expected<Buffer> serialize_reply(hailo_status error_status);
    static Expected<Text2ImageGeneratorParams> deserialize_reply(const MemoryView &serialized_reply);
};

struct Text2ImageGetIPAdapterFrameInfoSerializer
{
    Text2ImageGetIPAdapterFrameInfoSerializer() = delete;

    static Expected<Buffer> serialize_request();
    static hailo_status deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, hailo_3d_image_shape_t ip_adapter_frame_shape, hailo_format_t ip_adapter_frame_format);
    static Expected<Buffer> serialize_reply(hailo_status error_status);
    static Expected<std::pair<hailo_3d_image_shape_t, hailo_format_t>> deserialize_reply(const MemoryView &serialized_reply);
};

struct Text2ImageTokenizeSerializer
{
    Text2ImageTokenizeSerializer() = delete;

    static Expected<Buffer> serialize_request(const std::string &prompt);
    static Expected<std::string> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, const std::vector<int> &tokens = {});
    static Expected<std::vector<int>> deserialize_reply(const MemoryView &serialized_reply);
};

struct Text2ImageReleaseSerializer
{
    Text2ImageReleaseSerializer() = delete;
    
    static Expected<Buffer> serialize_request();
    static hailo_status deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct Text2ImageGeneratorSetInitialNoiseSerializer
{
    Text2ImageGeneratorSetInitialNoiseSerializer() = delete;

    static Expected<Buffer> serialize_request();
    static hailo_status deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

} /* namespace genai */

} /* namespace hailort */

#endif /* _HAILO_GENAI_SERIALIZER_HPP_ */
