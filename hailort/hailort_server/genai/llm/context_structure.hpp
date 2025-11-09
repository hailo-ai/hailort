/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
* @file context_structure.hpp
* @brief HailoRT GenAI LLM (Large Language Models) context structure.
**/

#ifndef _HAILO_GENAI_LLM_CONTEXT_STRUCTURE_HPP_
#define _HAILO_GENAI_LLM_CONTEXT_STRUCTURE_HPP_

#include "hailo/hailort.h"

namespace hailort
{
namespace genai
{

static constexpr uint32_t LLM_CONTEXT_PROTOCOL_VERSION = 0; // Update this when the protocol is changed
static constexpr uint32_t LLM_CONTEXT_MAGIC = 0x01484546;

#pragma pack(push, 1)
struct CacheBufferEntry {
    uint32_t cache_id;
    uint32_t buffer_size;
    // Dynamic buffer data follows after this structure
};

struct LLMContextPayload {
    // Pre-process
    uint32_t timestamp_value;
    uint32_t cache_size;
    uint32_t cached_embeddings_size; // Size of cached_embeddings data
    uint32_t cached_pos_ids_size; // Size of cached_pos_ids data

    uint32_t tokens_history_length;
    uint32_t next_prompt_prefix_length;

    // Cache buffer storage
    uint32_t cache_buffer_count;

    // Dynamic data follows after this structure:
    // - cached_embeddings (cached_embeddings_size bytes)
    // - cached_pos_ids (cached_pos_ids_size bytes)
    // - tokens_history (tokens_history_length * sizeof(int32_t) bytes)
    // - next_prompt_prefix (next_prompt_prefix_length * sizeof(int32_t) bytes)
    // - cache_buffers (cache_buffer_count * CacheBufferEntry structures)
};

struct LLMContextHeader {
    uint32_t magic;
    hailo_version_t hailort_version;
    uint32_t protocol_version;

    char model_hash[HAILO_MAX_NAME_SIZE];
    uint32_t payload_size;
};

struct LLMContext {
    LLMContextHeader header;
    // Dynamic payload data follows after this structure
};
#pragma pack(pop)

class LLMContextUtils {
public:
    LLMContextUtils() = delete;

    static Expected<LLMContextHeader> create_header(const Hef &hef, uint32_t payload_size)
    {
        LLMContextHeader header {};
        header.magic = LLM_CONTEXT_MAGIC;
        CHECK_SUCCESS_AS_EXPECTED(hailo_get_library_version(&header.hailort_version));
        header.protocol_version = LLM_CONTEXT_PROTOCOL_VERSION;
        std::strncpy(header.model_hash, hef.hash().c_str(), hef.hash().size() + 1);
        header.payload_size = payload_size;

        return header;
    }

    static hailo_status validate_header(const LLMContextHeader &header, const Hef &hef)
    {
        CHECK_AS_EXPECTED(header.magic == LLM_CONTEXT_MAGIC, HAILO_INVALID_ARGUMENT, "Invalid header magic");
        CHECK_AS_EXPECTED(header.protocol_version == LLM_CONTEXT_PROTOCOL_VERSION, HAILO_INVALID_ARGUMENT, "Invalid protocol version");
        CHECK_AS_EXPECTED(std::string(header.model_hash) == hef.hash(), HAILO_INVALID_ARGUMENT, "Invalid model hash");
        return HAILO_SUCCESS;
    }

    static Expected<uint32_t> calculate_payload_size(const eigen_matrix_2d_u16_t &cached_embeddings,
        const eigen_tensor_4d_u32_t &cached_pos_ids, const std::unordered_set<int32_t> &tokens_history,
        const std::vector<int32_t> &next_prompt_prefix, const std::unordered_map<uint32_t, BufferPtr> &cache_buffers)
    {
        uint32_t cached_embeddings_size = static_cast<uint32_t>(cached_embeddings.size() * sizeof(uint16_t));
        uint32_t cached_pos_ids_size = static_cast<uint32_t>(cached_pos_ids.size() * sizeof(uint32_t));
        uint32_t tokens_history_length = static_cast<uint32_t>(tokens_history.size());
        uint32_t next_prompt_prefix_length = static_cast<uint32_t>(next_prompt_prefix.size());

        // Calculate total size: base payload + dynamic data
        uint32_t total_size = sizeof(LLMContextPayload);
        total_size += cached_embeddings_size;
        total_size += cached_pos_ids_size;
        total_size += static_cast<uint32_t>(tokens_history_length * sizeof(int32_t));
        total_size += static_cast<uint32_t>(next_prompt_prefix_length * sizeof(int32_t));

        // Add size for cache buffer entries and their data
        for (const auto &[cache_id, buffer] : cache_buffers) {
            total_size += sizeof(CacheBufferEntry);
            total_size += static_cast<uint32_t>(buffer->size());
        }

        return total_size;
    }

    static hailo_status serialize_payload(MemoryView buffer,
        size_t cache_size, const eigen_matrix_2d_u16_t &cached_embeddings,
        const eigen_tensor_4d_u32_t &cached_pos_ids,
        uint32_t timestamp_value, const std::unordered_set<int32_t> &tokens_history,
        const std::vector<int32_t> &next_prompt_prefix,
        const std::unordered_map<uint32_t, BufferPtr> &cache_buffers)
    {
        uint32_t cached_embeddings_size = static_cast<uint32_t>(cached_embeddings.size() * sizeof(uint16_t));
        uint32_t cached_pos_ids_size = static_cast<uint32_t>(cached_pos_ids.size() * sizeof(uint32_t));
        uint32_t tokens_history_length = static_cast<uint32_t>(tokens_history.size());
        uint32_t next_prompt_prefix_length = static_cast<uint32_t>(next_prompt_prefix.size());
        uint32_t cache_buffer_count = static_cast<uint32_t>(cache_buffers.size());

        // Fill the base payload structure
        LLMContextPayload *payload = reinterpret_cast<LLMContextPayload*>(buffer.data());
        payload->timestamp_value = timestamp_value;
        payload->cache_size = static_cast<uint32_t>(cache_size);
        payload->cached_embeddings_size = cached_embeddings_size;
        payload->cached_pos_ids_size = cached_pos_ids_size;
        payload->tokens_history_length = tokens_history_length;
        payload->next_prompt_prefix_length = next_prompt_prefix_length;
        payload->cache_buffer_count = cache_buffer_count;

        // Serialize dynamic data
        uint8_t *data_ptr = buffer.data() + sizeof(LLMContextPayload);

        // Copy cached embeddings
        std::memcpy(data_ptr, cached_embeddings.data(), cached_embeddings_size);
        data_ptr += cached_embeddings_size;

        // Copy cached pos ids
        std::memcpy(data_ptr, cached_pos_ids.data(), cached_pos_ids_size);
        data_ptr += cached_pos_ids_size;

        // Copy tokens history
        uint32_t i = 0;
        for (auto token : tokens_history) {
            *reinterpret_cast<int32_t*>(data_ptr + i * sizeof(int32_t)) = token;
            i++;
        }
        data_ptr += tokens_history_length * sizeof(int32_t);

        // Copy next prompt prefix
        std::memcpy(data_ptr, next_prompt_prefix.data(), next_prompt_prefix.size() * sizeof(int32_t));
        data_ptr += next_prompt_prefix_length * sizeof(int32_t);

        // Copy cache buffers
        for (const auto &[cache_id, cache_buffer] : cache_buffers) {
            CacheBufferEntry *entry = reinterpret_cast<CacheBufferEntry*>(data_ptr);
            entry->cache_id = cache_id;
            entry->buffer_size = static_cast<uint32_t>(cache_buffer->size());
            data_ptr += sizeof(CacheBufferEntry);

            std::memcpy(data_ptr, cache_buffer->data(), cache_buffer->size());
            data_ptr += cache_buffer->size();
        }

        return HAILO_SUCCESS;
    }

    static hailo_status deserialize_payload(const MemoryView buffer, size_t &cache_size,
        eigen_matrix_2d_u16_t &cached_embeddings, eigen_tensor_4d_u32_t &cached_pos_ids,
        uint32_t &timestamp_value, std::unordered_set<int32_t> &tokens_history,
        std::vector<int32_t> &next_prompt_prefix,
        std::unordered_map<uint32_t, MemoryView> &cache_buffers)
    {
        if (buffer.size() < sizeof(LLMContextPayload)) {
            return HAILO_INVALID_ARGUMENT;
        }

        const LLMContextPayload *payload = reinterpret_cast<const LLMContextPayload*>(buffer.data());
        timestamp_value = payload->timestamp_value;
        cache_size = payload->cache_size;

        const uint8_t *data_ptr = buffer.data() + sizeof(LLMContextPayload);

        // Read cached embeddings
        std::memcpy(cached_embeddings.data(), data_ptr, payload->cached_embeddings_size);
        data_ptr += payload->cached_embeddings_size;

        // Read cached pos ids
        std::memcpy(cached_pos_ids.data(), data_ptr, payload->cached_pos_ids_size);
        data_ptr += payload->cached_pos_ids_size;

        // Read tokens history
        tokens_history.clear();
        for (uint32_t i = 0; i < payload->tokens_history_length; i++) {
            tokens_history.insert(*reinterpret_cast<const int32_t*>(data_ptr + i * sizeof(int32_t)));
        }
        data_ptr += payload->tokens_history_length * sizeof(int32_t);

        // Read next prompt prefix
        next_prompt_prefix.resize(payload->next_prompt_prefix_length);
        std::memcpy(next_prompt_prefix.data(), data_ptr, payload->next_prompt_prefix_length * sizeof(int32_t));
        data_ptr += payload->next_prompt_prefix_length * sizeof(int32_t);

        // Read cache buffers
        cache_buffers.clear();
        for (uint32_t i = 0; i < payload->cache_buffer_count; i++) {
            const CacheBufferEntry *entry = reinterpret_cast<const CacheBufferEntry*>(data_ptr);
            data_ptr += sizeof(CacheBufferEntry);

            cache_buffers[entry->cache_id] = MemoryView::create_const(data_ptr, entry->buffer_size);
            data_ptr += entry->buffer_size;
        }

        return HAILO_SUCCESS;
    }
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_LLM_CONTEXT_STRUCTURE_HPP_ */