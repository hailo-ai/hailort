/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file cache_manager.cpp
 * @brief Manges creation and configuration of cache buffers
 **/

#include "cache_manager.hpp"
#include "hailo/hailort.h"

namespace hailort
{

Expected<CacheManagerPtr> CacheManager::create_shared(HailoRTDriver &driver)
{
    auto cache_manager = make_shared_nothrow<CacheManager>(driver);
    CHECK_NOT_NULL(cache_manager, HAILO_OUT_OF_HOST_MEMORY);
 
    return cache_manager;
}

CacheManager::CacheManager(HailoRTDriver &driver) :
    m_driver(driver),
    m_caches_created(false),
    m_initialized(false),
    m_cache_input_size(0),
    m_cache_output_size(0),
    m_cache_size(0),
    m_read_offset_bytes(0),
    m_write_offset_bytes_delta(0),
    m_cache_buffers(),
    m_uninitialized_caches()
{
}

hailo_status CacheManager::create_caches_from_core_op(std::shared_ptr<CoreOpMetadata> core_op_metadata)
{
    if (m_caches_created) {
        // Already created caches, nothing to do
        // In debug, validate that the cache sizes + ids are the same as the ones we already have
        assert(m_cache_input_size == get_cache_input_size(core_op_metadata));
        assert(m_cache_output_size == get_cache_output_size(core_op_metadata));
        assert(validate_cache_ids(core_op_metadata, m_cache_buffers));

        return HAILO_SUCCESS;
    }

    if (!core_op_has_caches(core_op_metadata)) {
        // No cache layers found, nothing to do
        return HAILO_SUCCESS;
    }

    m_cache_input_size = get_cache_input_size(core_op_metadata);
    m_cache_output_size = get_cache_output_size(core_op_metadata);
    // TODO: cache size should be a param of the hef (via sdk)
    //       that way we can also immediately know if caches are used or not
    //       it should be a param that appears once in the hef, and is used by all caches (HRT-13584)
    m_cache_size = m_cache_input_size + m_cache_output_size;
    m_write_offset_bytes_delta = m_cache_input_size;

    assert(validate_cache_edge_layers(core_op_metadata, m_cache_input_size, m_cache_output_size));
    auto status = allocate_cache_buffers(core_op_metadata);
    CHECK_SUCCESS(status, "Failed to allocate cache buffers");

    m_caches_created = true;

    return HAILO_SUCCESS;

}

bool CacheManager::core_op_has_caches(std::shared_ptr<CoreOpMetadata> core_op_metadata)
{
    for (const auto &context_metadata : core_op_metadata->dynamic_contexts()) {
        if (!context_metadata.get_cache_input_layers().empty() || !context_metadata.get_cache_output_layers().empty()) {
            return true;
        }
    }

    return false;
}

bool CacheManager::validate_cache_edge_layers(std::shared_ptr<CoreOpMetadata> core_op_metadata,
    uint32_t cache_input_size, uint32_t cache_output_size)
{
    std::unordered_map<uint32_t, uint32_t> cache_id_count;
    for (const auto &context_metadata : core_op_metadata->dynamic_contexts()) {
        for (const auto &layer_info : context_metadata.get_cache_input_layers()) {
            cache_id_count[layer_info.cache_info.id]++;
            if (cache_input_size != LayerInfoUtils::get_layer_transfer_size(layer_info)) {
                return false;
            }
        }

        for (const auto &layer_info : context_metadata.get_cache_output_layers()) {
            cache_id_count[layer_info.cache_info.id]++;
            if (cache_output_size != LayerInfoUtils::get_layer_transfer_size(layer_info)) {
                return false;
            }
        }
    }

    static const uint32_t EXPECTED_CACHE_ID_COUNT = 2; // Each cache has 2 layers (input and output)
    for (const auto &cache_id : cache_id_count) {
        if (cache_id.second != EXPECTED_CACHE_ID_COUNT) {
            return false;
        }
    }

    return true;
}

uint32_t CacheManager::get_cache_input_size(std::shared_ptr<CoreOpMetadata> core_op_metadata)
{
    // All cache layers have the same input size (this will be asserted in debug)
    for (const auto &context_metadata : core_op_metadata->dynamic_contexts()) {
        for (const auto &layer_info : context_metadata.get_cache_input_layers()) {
            return LayerInfoUtils::get_layer_transfer_size(layer_info);
        }
    }

    // No cache layers found
    return 0;
}

uint32_t CacheManager::get_cache_output_size(std::shared_ptr<CoreOpMetadata> core_op_metadata)
{
    // All cache layers have the same output size (this will be asserted in debug)
    for (const auto &context_metadata : core_op_metadata->dynamic_contexts()) {
        for (const auto &layer_info : context_metadata.get_cache_output_layers()) {
            return LayerInfoUtils::get_layer_transfer_size(layer_info);
        }
    }

    // No cache layers found
    return 0;
}

bool CacheManager::validate_cache_ids(std::shared_ptr<CoreOpMetadata> core_op_metadata,
    const std::unordered_map<uint32_t, CacheBuffer> &current_cache_buffers)
{
    std::unordered_set<uint32_t> cache_ids;
    for (const auto &context_metadata : core_op_metadata->dynamic_contexts()) {
        for (const auto &layer_info : context_metadata.get_cache_input_layers()) {
            cache_ids.insert(layer_info.cache_info.id);
        }

        for (const auto &layer_info : context_metadata.get_cache_output_layers()) {
            cache_ids.insert(layer_info.cache_info.id);
        }
    }

    if (cache_ids.size() != current_cache_buffers.size()) {
        return false;
    }

    for (const auto &cache_id : cache_ids) {
        if (std::end(current_cache_buffers) == current_cache_buffers.find(cache_id)) {
            return false;
        }
    }

    return true;
}

ExpectedRef<CacheBuffer> CacheManager::get_cache_buffer(uint32_t cache_id)
{
    const auto cache_buffer_it = m_cache_buffers.find(cache_id);
    if (std::end(m_cache_buffers) != cache_buffer_it) {
        return std::ref(cache_buffer_it->second);
    }

    return make_unexpected(HAILO_NOT_FOUND);
}

hailo_status CacheManager::allocate_cache_buffers(std::shared_ptr<CoreOpMetadata> core_op_metadata)
{
    // It's enough to go over cache_output_layers, as each cache has both input and output layers (that share the same buffer)
    for (const auto &context_metadata : core_op_metadata->dynamic_contexts()) {
        for (const auto &layer_info : context_metadata.get_cache_output_layers()) {
            const auto cache_id = layer_info.cache_info.id;
            TRY(auto cache_buffer, CacheBuffer::create(m_driver, m_cache_size, m_cache_input_size, m_cache_output_size));
            auto emplace_res = m_cache_buffers.emplace(cache_id, std::move(cache_buffer));
            CHECK(emplace_res.second, HAILO_INTERNAL_FAILURE);

            // The cache buffer is yet to be initialized (will be initalized when it is configured with input/output channels)
            m_uninitialized_caches.insert(cache_id);
        }
    }

    return HAILO_SUCCESS;
}

hailo_status CacheManager::program_cache_buffers()
{
    // Set the cache to the initial configuration (program the descriptors to the initial offset)
    static const auto INITIAL_CONFIGURATION_OFFSET = 0;
    return update_cache_offset(INITIAL_CONFIGURATION_OFFSET);
}

hailo_status CacheManager::try_complete_cache_initialization()
{
    // If all caches are now initialized, program their desc list and set the CacheManager as initialized
    if (m_uninitialized_caches.empty() && !m_initialized) {
        m_initialized = true;

        auto status = program_cache_buffers();
        CHECK_SUCCESS(status, "Failed to program cache buffers");
    }

    return HAILO_SUCCESS;
}

ExpectedRef<IntermediateBuffer> CacheManager::set_cache_input_channel(uint32_t cache_id, uint16_t batch_size,
    vdma::ChannelId channel_id)
{

    // TODO: Support non-1 batches? (HRT-13628)
    CHECK(1 == batch_size, HAILO_INVALID_ARGUMENT, "Cache input batch size must be 1");
    TRY(auto cache_buffer, get_cache_buffer(cache_id));
    if (m_initialized) {
        // Cache is already initialized, return the input channel
        return cache_buffer.get().get_input();
    }
    TRY(auto result, cache_buffer.get().set_input_channel(m_driver, channel_id));

    // If the cache is now fully configured, remove it from the uninitialized set
    if (cache_buffer.get().is_configured()) {
        m_uninitialized_caches.erase(cache_id);
        auto status = try_complete_cache_initialization();
        CHECK_SUCCESS(status);
    }

    return result;
}

ExpectedRef<IntermediateBuffer> CacheManager::set_cache_output_channel(uint32_t cache_id, uint16_t batch_size,
    vdma::ChannelId channel_id)
{
    // TODO: Support non-1 batches? (HRT-13628)
    CHECK(1 == batch_size, HAILO_INVALID_ARGUMENT, "Cache output batch size must be 1");
    TRY(auto cache_buffer, get_cache_buffer(cache_id));
    if (m_initialized) {
        // Cache is already initialized, return the output channel
        return cache_buffer.get().get_output();
    }
    TRY(auto result, cache_buffer.get().set_output_channel(m_driver, channel_id));

    // If the cache is now fully configured, remove it from the uninitialized set
    if (cache_buffer.get().is_configured()) {
        m_uninitialized_caches.erase(cache_id);
        auto status = try_complete_cache_initialization();
        CHECK_SUCCESS(status);
    }

    return result;
}

std::unordered_map<uint32_t, CacheBuffer> &CacheManager::get_cache_buffers()
{
    return m_cache_buffers;
}

hailo_status CacheManager::init_caches(uint32_t initial_read_offset_bytes, int32_t write_offset_bytes_delta)
{
    if (!m_caches_created) {
        // No cache layers found, nothing to do
        LOGGER__WARNING("No cache layers found, but init_cache was called");
        return HAILO_SUCCESS;
    }

    CHECK(initial_read_offset_bytes < m_cache_size, HAILO_INVALID_ARGUMENT);
    CHECK(write_offset_bytes_delta != 0, HAILO_INVALID_ARGUMENT);

    m_read_offset_bytes = initial_read_offset_bytes;
    m_write_offset_bytes_delta = write_offset_bytes_delta;

    LOGGER__WARNING("Initializing caches [read_offset={}, write_offset_delta={}]",
        m_read_offset_bytes, m_write_offset_bytes_delta);

    return program_cache_buffers();
}

hailo_status CacheManager::update_cache_offset(int32_t offset_delta_bytes)
{
    if (!m_caches_created) {
        // No cache layers found, nothing to do
        LOGGER__WARNING("No cache layers found, but update_cache_offset was called");
        return HAILO_SUCCESS;
    }

    CHECK(m_initialized, HAILO_INVALID_OPERATION, "CacheManager not initialized");

    auto status = HAILO_UNINITIALIZED;
    auto new_read_offset = (m_read_offset_bytes + offset_delta_bytes) % m_cache_size;
    auto new_write_offset = (m_read_offset_bytes + offset_delta_bytes + m_write_offset_bytes_delta) % m_cache_size;

    for (auto &cache_buffer : m_cache_buffers) {
        TRY(auto cache_input, cache_buffer.second.get_input());
        status = cache_input.get().reprogram_descriptors(new_read_offset);
        CHECK_SUCCESS(status, "Failed to reprogram read cache descriptors to offset 0x{:x} (cache_id {})",
            new_read_offset, cache_buffer.first);

        TRY(auto cache_output, cache_buffer.second.get_output());
        status = cache_output.get().reprogram_descriptors(new_write_offset);
        CHECK_SUCCESS(status, "Failed to reprogram write cache descriptors to offset 0x{:x} (cache_id {})",
            new_write_offset, cache_buffer.first);
    }

    m_read_offset_bytes = new_read_offset;

    return HAILO_SUCCESS;
}

uint32_t CacheManager::get_cache_size() const
{
    return m_cache_size;
}

uint32_t CacheManager::get_read_offset_bytes() const
{
    return m_read_offset_bytes;
}

int32_t CacheManager::get_write_offset_bytes_delta() const
{
    return m_write_offset_bytes_delta;
}

} /* namespace hailort */
