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
#include "hailo/genai/speech2text/speech2text.hpp"

namespace hailort
{
namespace genai
{

constexpr size_t CHUNKED_TRANSFER_CHUNK_SIZE = 64 * 1024 * 1024;

enum class HailoGenAIActionID {
    LLM__CREATE = 0,
    LLM__GET_GENERATOR_PARAMS,

    LLM__GENERATOR_CREATE,
    LLM__GENERATOR_WRITE,
    LLM__GENERATOR_GENERATE,
    LLM__GENERATOR_READ,
    LLM__GENERATOR_ABORT,
    LLM__GENERATOR_RELEASE,

    LLM__TOKENIZE,

    LLM_CLEAR_CONTEXT,
    LLM__GET_CONTEXT,
    LLM__SET_CONTEXT,

    LLM__SET_END_OF_GENERATION_SEQUENCE,
    LLM__GET_END_OF_GENERATION_SEQUENCE,

    LLM__SET_STOP_TOKENS,
    LLM__GET_STOP_TOKENS,
    LLM__GET_CONTEXT_USAGE_SIZE,
    LLM__GET_MAX_CONTEXT_CAPACITY,

    LLM_RELEASE,

    VLM__CREATE,
    VLM__GENERATOR_GENERATE,

    GENAI__CHECK_HEF_EXISTS,

    SPEECH2TEXT__CREATE,
    SPEECH2TEXT__GENERATE,
    SPEECH2TEXT__RELEASE,
    SPEECH2TEXT__TOKENIZE,

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
    struct RequestInfo {
        RequestInfo() = default;
        RequestInfo(const std::string &lora_name, const std::string &hef_path, const std::string &group_id, uint64_t file_size, bool tokenizer_on_host) :
            lora_name(lora_name), hef_path(hef_path), group_id(group_id), file_size(file_size), tokenizer_on_host(tokenizer_on_host) {}

        std::string lora_name;
        std::string hef_path;
        std::string group_id;
        uint64_t file_size;
        bool tokenizer_on_host;
    };


    LLMCreateSerializer() = delete;

    static Expected<Buffer> serialize_request(const hailo_vdevice_params_t &vdevice_params, const LLMParams &llm_params, const std::string &hef_path = "");
    static Expected<Buffer> serialize_request(const hailo_vdevice_params_t &vdevice_params, const LLMParams &llm_params, const std::string &hef_path, uint64_t file_size);
    static Expected<RequestInfo> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, const std::string &prompt_template = "", uint32_t embedding_features = 0);
    static Expected<std::pair<std::string, uint32_t>> deserialize_reply(const MemoryView &serialized_reply);
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

    static Expected<Buffer> serialize_reply(hailo_status status, const std::vector<int> &initial_prefix_tokens = {});
    static Expected<std::vector<int>> deserialize_reply(const MemoryView &serialized_reply);
};

struct LLMGeneratorReadSerializer
{
    struct TextGenerationInput {
        TextGenerationInput() = default;
        TextGenerationInput(int next_token) : tokens({ next_token }) {}

        std::string initial_prompt;
        std::vector<int> tokens;            // Client-side tokenizer: combined prefix+input tokens | Server-side tokenizer: prefix tokens (first iter) or single token (subsequent)
        std::vector<BufferPtr> embeddings; // TODO: (HRT-18669) think how to manage memory better to prevent allocations
    };

    struct TextGenerationOutput {
        TextGenerationOutput() : output_token_str(""), output_token_id(-1), is_context_full(false) {}

        std::string output_token_str;
        int output_token_id;
        bool is_context_full;
    };

    LLMGeneratorReadSerializer() = delete;

    static Expected<Buffer> serialize_request(const std::chrono::milliseconds &timeout, const TextGenerationInput &request);
    static Expected<std::pair<std::chrono::milliseconds, TextGenerationInput>> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, const TextGenerationOutput &output = {},
        LLMGeneratorCompletion::Status generation_status = LLMGeneratorCompletion::Status::GENERATING, bool is_context_full = false);
    static Expected<std::pair<TextGenerationOutput, LLMGeneratorCompletion::Status>> deserialize_reply(const MemoryView &serialized_reply);
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

struct LLMGetContextSerializer
{
    LLMGetContextSerializer() = delete;

    static Expected<Buffer> serialize_request();
    static hailo_status deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct LLMSetContextSerializer
{
    LLMSetContextSerializer() = delete;

    static Expected<Buffer> serialize_request();
    static hailo_status deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct LLMSetEndOfGenerationSequenceSerializer
{
    LLMSetEndOfGenerationSequenceSerializer() = delete;

    static Expected<Buffer> serialize_request(const std::vector<int> &end_of_generation_sequence_tokens);
    static Expected<std::vector<int>> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct LLMGetEndOfGenerationSequenceSerializer
{
    LLMGetEndOfGenerationSequenceSerializer() = delete;

    static Expected<Buffer> serialize_request();
    static hailo_status deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, const std::string &end_of_generation_sequence = "",
        const std::vector<int> &end_of_generation_sequence_tokens = {});
    static Expected<std::tuple<std::string, std::vector<int>>> deserialize_reply(const MemoryView &serialized_reply);
};

struct LLMSetStopTokensSerializer
{
    LLMSetStopTokensSerializer() = delete;

    static Expected<Buffer> serialize_request(const std::vector<std::vector<int>> &stop_sequences);
    static Expected<std::vector<std::vector<int>>> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct LLMGetStopTokensSerializer
{
    LLMGetStopTokensSerializer() = delete;

    static Expected<Buffer> serialize_request();
    static hailo_status deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, const std::vector<std::string> &stop_tokens = {}, const std::vector<std::vector<int>> &stop_sequences = {});
    static Expected<std::tuple<std::vector<std::string>, std::vector<std::vector<int>>>> deserialize_reply(const MemoryView &serialized_reply);
};

struct LLMGetContextUsageSizeSerializer
{
    LLMGetContextUsageSizeSerializer() = delete;

    static Expected<Buffer> serialize_request();
    static hailo_status deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, size_t context_usage = 0);
    static Expected<size_t> deserialize_reply(const MemoryView &serialized_reply);
};

struct LLMGetMaxContextCapacitySerializer
{
    LLMGetMaxContextCapacitySerializer() = delete;

    static Expected<Buffer> serialize_request();
    static hailo_status deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, size_t max_context_capacity = 0);
    static Expected<size_t> deserialize_reply(const MemoryView &serialized_reply);
};

struct LLMGeneratorReleaseSerializer
{
    LLMGeneratorReleaseSerializer() = delete;

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

struct LLMGeneratorAbortSerializer
{
    LLMGeneratorAbortSerializer() = delete;

    static Expected<Buffer> serialize_request();
    static hailo_status deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct VLMCreateSerializer
{
    VLMCreateSerializer() = delete;

    static Expected<Buffer> serialize_request(const hailo_vdevice_params_t &vdevice_params, const std::string &hef_path = "", bool optimize_memory_on_device = false);
    static Expected<Buffer> serialize_request(const hailo_vdevice_params_t &vdevice_params, const std::string &hef_path, uint64_t file_size, bool optimize_memory_on_device = false);
    // group_id, hef_path, file_size, tokenizer_on_host
    static Expected<std::tuple<std::string, std::string, uint64_t, bool>> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status,
        hailo_3d_image_shape_t input_frame_shape = {}, hailo_format_t input_frame_format = {}, const std::string &prompt_template = "", uint32_t embedding_features = 0,
        uint32_t image_pad_token_id = 0, uint32_t embeddings_per_frame = 0);
    static Expected<std::tuple<hailo_3d_image_shape_t, hailo_format_t, std::string, uint32_t, uint32_t, uint32_t>> deserialize_reply(const MemoryView &serialized_reply);
};

struct VLMGeneratorGenerateSerializer
{
    VLMGeneratorGenerateSerializer() = delete;

    static Expected<Buffer> serialize_request(uint32_t input_frames_count);
    static Expected<size_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct GenAICheckHefExistsSerializer
{
    GenAICheckHefExistsSerializer() = delete;

    static Expected<Buffer> serialize_request(const std::string &hef_path, const std::string &hash);
    static Expected<std::pair<std::string, std::string>> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, bool hef_exists = false);
    static Expected<bool> deserialize_reply(const MemoryView &serialized_reply);
};

struct Speech2TextCreateSerializer
{
    Speech2TextCreateSerializer() = delete;

    static Expected<Buffer> serialize_request(const hailo_vdevice_params_t &vdevice_params);
    static Expected<std::string> deserialize_request(const MemoryView &serialized_request);
    
    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct Speech2TextGenerateSerializer
{
    Speech2TextGenerateSerializer() = delete;

    static Expected<Buffer> serialize_request(const Speech2TextGeneratorParams &generator_params);
    static Expected<Speech2TextGeneratorParams> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, const std::vector<Speech2Text::SegmentInfo> &segments_infos);
    static Expected<Buffer> serialize_reply(hailo_status error_status);
    static Expected<std::vector<Speech2Text::SegmentInfo>> deserialize_reply(const MemoryView &serialized_reply);
};

struct Speech2TextReleaseSerializer
{
    Speech2TextReleaseSerializer() = delete;

    static Expected<Buffer> serialize_request();
    static hailo_status deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct Speech2TextTokenizeSerializer
{
    Speech2TextTokenizeSerializer() = delete;

    static Expected<Buffer> serialize_request(const std::string &text);
    static Expected<std::string> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, const std::vector<int> &tokens = {});
    static Expected<std::vector<int>> deserialize_reply(const MemoryView &serialized_reply);
};

} /* namespace genai */

} /* namespace hailort */

#endif /* _HAILO_GENAI_SERIALIZER_HPP_ */
