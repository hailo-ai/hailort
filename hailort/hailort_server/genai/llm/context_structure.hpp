/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
* @file context_structure.hpp
* @brief HailoRT GenAI LLM (Large Language Models) context structure.
**/

#ifndef _HAILO_GENAI_LLM_CONTEXT_STRUCTURE_HPP_
#define _HAILO_GENAI_LLM_CONTEXT_STRUCTURE_HPP_

#include "hailo/hailort.h"
#include "hailo/hef.hpp"
#include "pre_process.hpp"

#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace hailort
{
namespace genai
{

static constexpr uint32_t LLM_CONTEXT_PROTOCOL_VERSION = 1; // Update this when the protocol is changed
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
    uint32_t cached_per_layer_inputs_size;
    uint32_t cached_pos_ids_size; // Size of cached_pos_ids data

    uint32_t tokens_history_length;
    uint32_t next_prompt_prefix_length;

    // Cache buffer storage
    uint32_t cache_buffer_count;

    // Dynamic data follows after this structure:
    // - cached_embeddings (cached_embeddings_size bytes)
    // - cached_per_layer_inputs (cached_per_layer_inputs_size bytes)
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
        CHECK(header.magic == LLM_CONTEXT_MAGIC, HAILO_INVALID_ARGUMENT, "Invalid header magic");
        CHECK(LLM_CONTEXT_PROTOCOL_VERSION == header.protocol_version, HAILO_INVALID_ARGUMENT,
            "Context protocol version mismatch: the context was saved with version {}, "
            "but this HailoRT expects version {}",
            header.protocol_version, LLM_CONTEXT_PROTOCOL_VERSION);
        const std::string model_hash(header.model_hash, strnlen(header.model_hash, sizeof(header.model_hash)));
        CHECK(model_hash == hef.hash(), HAILO_INVALID_ARGUMENT, "Invalid model hash");
        return HAILO_SUCCESS;
    }

    static Expected<uint32_t> calculate_payload_size(const PreProcessCache &pre_process_cache,
        const std::unordered_set<int32_t> &tokens_history, const std::vector<int32_t> &next_prompt_prefix,
        const std::unordered_map<uint32_t, BufferPtr> &cache_buffers)
    {
        const auto cached_embeddings_size = cache_section_size(pre_process_cache.embeddings);
        const auto cached_per_layer_inputs_size = cache_section_size(pre_process_cache.per_layer_inputs);
        const auto cached_pos_ids_size = cache_section_size(pre_process_cache.pos_ids);
        uint32_t tokens_history_length = static_cast<uint32_t>(tokens_history.size());
        uint32_t next_prompt_prefix_length = static_cast<uint32_t>(next_prompt_prefix.size());

        // Calculate total size: base payload + dynamic data
        uint32_t total_size = sizeof(LLMContextPayload);
        total_size += cached_embeddings_size;
        total_size += cached_per_layer_inputs_size;
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

    static hailo_status serialize_payload(MemoryView buffer, const PreProcessCache &pre_process_cache,
        const std::unordered_set<int32_t> &tokens_history, const std::vector<int32_t> &next_prompt_prefix,
        const std::unordered_map<uint32_t, BufferPtr> &cache_buffers)
    {
        uint32_t tokens_history_length = static_cast<uint32_t>(tokens_history.size());
        uint32_t next_prompt_prefix_length = static_cast<uint32_t>(next_prompt_prefix.size());
        uint32_t cache_buffer_count = static_cast<uint32_t>(cache_buffers.size());

        // Fill the base payload structure
        LLMContextPayload *payload = reinterpret_cast<LLMContextPayload*>(buffer.data());
        payload->timestamp_value = static_cast<uint32_t>(pre_process_cache.timestamp_value);
        payload->cache_size = static_cast<uint32_t>(pre_process_cache.usage_size);
        payload->cached_embeddings_size = cache_section_size(pre_process_cache.embeddings);
        payload->cached_per_layer_inputs_size = cache_section_size(pre_process_cache.per_layer_inputs);
        payload->cached_pos_ids_size = cache_section_size(pre_process_cache.pos_ids);
        payload->tokens_history_length = tokens_history_length;
        payload->next_prompt_prefix_length = next_prompt_prefix_length;
        payload->cache_buffer_count = cache_buffer_count;

        // Serialize dynamic data
        uint8_t *data_ptr = buffer.data() + sizeof(LLMContextPayload);

        write_cache_section(data_ptr, pre_process_cache.embeddings);
        write_cache_section(data_ptr, pre_process_cache.per_layer_inputs);
        write_cache_section(data_ptr, pre_process_cache.pos_ids);

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

    static hailo_status deserialize_payload(const MemoryView buffer, PreProcessCache &pre_process_cache,
        std::unordered_set<int32_t> &tokens_history, std::vector<int32_t> &next_prompt_prefix,
        std::unordered_map<uint32_t, MemoryView> &cache_buffers)
    {
        if (buffer.size() < sizeof(LLMContextPayload)) {
            return HAILO_INVALID_ARGUMENT;
        }

        const LLMContextPayload *payload = reinterpret_cast<const LLMContextPayload*>(buffer.data());
        const uint8_t *data_ptr = buffer.data() + sizeof(LLMContextPayload);
        const uint8_t *const payload_end = buffer.data() + buffer.size();

        CHECK_SUCCESS(read_cache_section(data_ptr, payload_end, payload->cached_embeddings_size,
            pre_process_cache.embeddings, "token embeddings"));
        CHECK_SUCCESS(read_cache_section(data_ptr, payload_end, payload->cached_per_layer_inputs_size,
            pre_process_cache.per_layer_inputs, "per-layer inputs"));
        CHECK_SUCCESS(read_cache_section(data_ptr, payload_end, payload->cached_pos_ids_size,
            pre_process_cache.pos_ids, "position ids"));

        TRY(const auto tokens_history_begin, consume_section(data_ptr, payload_end, payload->tokens_history_length,
            sizeof(int32_t), "tokens history"));
        TRY(const auto next_prompt_prefix_begin, consume_section(data_ptr, payload_end,
            payload->next_prompt_prefix_length, sizeof(int32_t), "next prompt prefix"));
        // TODO: HRT-20895 - check the exact cache id set and each entry's size against the model's caches
        CHECK(0 != payload->cache_buffer_count, HAILO_INVALID_ARGUMENT, "The context holds no cache buffers");
        std::unordered_map<uint32_t, MemoryView> restored_cache_buffers;
        for (uint32_t entry_index = 0; entry_index < payload->cache_buffer_count; entry_index++) {
            TRY(const auto entry_begin, consume_section(data_ptr, payload_end, 1, sizeof(CacheBufferEntry),
                "cache buffer entry"));
            CacheBufferEntry entry {};
            std::memcpy(&entry, entry_begin, sizeof(entry));
            TRY(const auto cache_buffer_begin, consume_section(data_ptr, payload_end, entry.buffer_size, 1,
                "cache buffer"));
            const auto is_new_cache_id = restored_cache_buffers.emplace(entry.cache_id,
                MemoryView::create_const(cache_buffer_begin, entry.buffer_size)).second;
            CHECK(is_new_cache_id, HAILO_INVALID_ARGUMENT, "The context holds cache id {} more than once",
                entry.cache_id);
        }
        CHECK(payload_end == data_ptr, HAILO_INVALID_ARGUMENT,
            "The context has {} bytes left over after its last section", static_cast<size_t>(payload_end - data_ptr));

        pre_process_cache.timestamp_value = static_cast<int>(payload->timestamp_value);
        pre_process_cache.usage_size = payload->cache_size;
        tokens_history.clear();
        for (uint32_t token_index = 0; token_index < payload->tokens_history_length; token_index++) {
            int32_t token = 0;
            std::memcpy(&token, tokens_history_begin + token_index * sizeof(int32_t), sizeof(token));
            tokens_history.insert(token);
        }
        next_prompt_prefix.resize(payload->next_prompt_prefix_length);
        if (!next_prompt_prefix.empty()) {
            std::memcpy(next_prompt_prefix.data(), next_prompt_prefix_begin,
                next_prompt_prefix.size() * sizeof(int32_t));
        }
        cache_buffers = std::move(restored_cache_buffers);

        return HAILO_SUCCESS;
    }

private:
    template<typename Cache>
    static uint32_t cache_section_size(const Cache &cache)
    {
        return static_cast<uint32_t>(static_cast<size_t>(cache.size()) * sizeof(typename Cache::Scalar));
    }

    template<typename Cache>
    static void write_cache_section(uint8_t *&data_ptr, const Cache &cache)
    {
        const auto section_size = cache_section_size(cache);
        if (0 != section_size) {
            std::memcpy(data_ptr, cache.data(), section_size);
        }
        data_ptr += section_size;
    }

    template<typename Cache>
    static hailo_status read_cache_section(const uint8_t *&data_ptr, const uint8_t *payload_end,
        uint32_t section_size, Cache &cache, const char *section_name)
    {
        const auto expected_section_size = cache_section_size(cache);
        CHECK(expected_section_size == section_size, HAILO_INVALID_ARGUMENT,
            "The context's {} section is {} bytes, but this model's cache is {} bytes", section_name, section_size,
            expected_section_size);
        TRY(const auto section_begin, consume_section(data_ptr, payload_end, section_size, 1, section_name));
        if (0 != section_size) {
            std::memcpy(cache.data(), section_begin, section_size);
        }
        return HAILO_SUCCESS;
    }

    static Expected<const uint8_t*> consume_section(const uint8_t *&data_ptr, const uint8_t *payload_end,
        uint32_t elements_count, size_t element_size, const char *section_name)
    {
        const auto remaining_payload_size = static_cast<size_t>(payload_end - data_ptr);
        // Dividing instead of multiplying keeps a forged count from overflowing a 32-bit size_t
        CHECK(elements_count <= (remaining_payload_size / element_size), HAILO_INVALID_ARGUMENT,
            "The context's {} section needs {} bytes, but only {} bytes of the context remain", section_name,
            static_cast<uint64_t>(elements_count) * element_size, remaining_payload_size);
        const uint8_t *section_begin = data_ptr;
        data_ptr += elements_count * element_size;
        return section_begin;
    }
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_LLM_CONTEXT_STRUCTURE_HPP_ */