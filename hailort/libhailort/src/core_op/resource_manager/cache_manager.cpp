/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file cache_manager.cpp
 * @brief Manges creation and configuration of cache buffers
 **/

#include "hailo/hailort.h"
#include "cache_manager.hpp"

#include "vdma/memory/sg_buffer.hpp"

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
    m_storage_manager(driver),
    m_core_op_managers(),
    m_caches_created(false),
    m_cache_size(0),
    m_cache_entry_size(0),
    m_read_offset_bytes(0)
{}

hailo_status CacheManager::create_caches_from_core_op(std::shared_ptr<CoreOpMetadata> core_op_metadata)
{
    if (!core_op_has_caches(core_op_metadata)) {
        // No cache layers found, nothing to do
        return HAILO_SUCCESS;
    }

    const auto input_size = core_op_cache_input_size(core_op_metadata);
    const auto output_size = core_op_cache_output_size(core_op_metadata);
    const auto entry_size = core_op_cache_entry_size(core_op_metadata);
    const auto cache_size = input_size + output_size;
    if (m_caches_created) {
        CHECK(m_cache_size == cache_size, HAILO_INVALID_OPERATION,
            "Cache size mismatch: expected {}, got {}", m_cache_size, cache_size);
        assert(validate_cache_ids(core_op_metadata, m_core_op_managers));
    }

    const auto core_op_name = core_op_metadata->core_op_name();
    TRY(auto core_op_manager, CoreOpManager::create(m_driver, m_storage_manager, core_op_metadata));
    m_caches_created = true;
    m_cache_size = cache_size;
    m_cache_entry_size = entry_size;

    m_core_op_managers.emplace(core_op_name, std::move(core_op_manager));

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

uint32_t CacheManager::core_op_cache_entry_size(std::shared_ptr<CoreOpMetadata> core_op_metadata)
{
    // All cache layers have the same entry size (this will be asserted in debug)
    const auto &dynamic_contexts = core_op_metadata->dynamic_contexts();
    if (dynamic_contexts.size() == 0) {
        return 0;
    }

    const auto &cache_input_layers = dynamic_contexts[0].get_cache_input_layers();
    if (cache_input_layers.size() == 0) {
        return 0;
    }

    return cache_input_layers[0].hw_shape.features;
}

uint32_t CacheManager::core_op_cache_input_size(std::shared_ptr<CoreOpMetadata> core_op_metadata)
{
    // All cache layers have the same input size (this will be asserted in debug)
    const auto &dynamic_contexts = core_op_metadata->dynamic_contexts();
    if (dynamic_contexts.size() == 0) {
        return 0;
    }

    const auto &cache_input_layers = dynamic_contexts[0].get_cache_input_layers();
    if (cache_input_layers.size() == 0) {
        return 0;
    }

    return LayerInfoUtils::get_layer_transfer_size(cache_input_layers[0]);
}

uint32_t CacheManager::core_op_cache_output_size(std::shared_ptr<CoreOpMetadata> core_op_metadata)
{
    // All cache layers have the same output size (this will be asserted in debug)
    const auto &dynamic_contexts = core_op_metadata->dynamic_contexts();
    if (dynamic_contexts.size() == 0) {
        return 0;
    }

    const auto &cache_output_layers = dynamic_contexts[0].get_cache_output_layers();
    if (cache_output_layers.size() == 0) {
        return 0;
    }

    return LayerInfoUtils::get_layer_transfer_size(cache_output_layers[0]);
}

bool CacheManager::validate_cache_ids(std::shared_ptr<CoreOpMetadata> core_op_metadata,
    const std::unordered_map<std::string, CoreOpManager> &current_core_op_managers)
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

    for (const auto &core_op_manager : current_core_op_managers) {
        const auto &cache_buffers = core_op_manager.second.get_cache_buffers();
        if (cache_ids.size() != cache_buffers.size()) {
            return false;
        }

        for (const auto &cache_id : cache_ids) {
            if (std::end(cache_buffers) == cache_buffers.find(cache_id)) {
                return false;
            }
        }
    }

    return true;
}

ExpectedRef<std::unordered_map<uint32_t, CacheBuffer>> CacheManager::get_cache_buffers(const std::string &core_op_name)
{
    const auto core_op_manager_it = m_core_op_managers.find(core_op_name);
    if (std::end(m_core_op_managers) == core_op_manager_it) {
        return make_unexpected(HAILO_NOT_FOUND);
    }

    return std::ref(core_op_manager_it->second.get_cache_buffers());
}

ExpectedRef<IntermediateBuffer> CacheManager::set_cache_input_channel(const std::string &core_op_name,
    uint32_t cache_id, uint16_t batch_size, vdma::ChannelId channel_id)
{
    const auto core_op_manager_it = m_core_op_managers.find(core_op_name);
    if (std::end(m_core_op_managers) == core_op_manager_it) {
        return make_unexpected(HAILO_NOT_FOUND);
    }

    return core_op_manager_it->second.set_cache_input_channel(cache_id, batch_size, channel_id);
}

ExpectedRef<IntermediateBuffer> CacheManager::set_cache_output_channel(const std::string &core_op_name,
    uint32_t cache_id, uint16_t batch_size, vdma::ChannelId channel_id)
{
    const auto core_op_manager_it = m_core_op_managers.find(core_op_name);
    if (std::end(m_core_op_managers) == core_op_manager_it) {
        return make_unexpected(HAILO_NOT_FOUND);
    }

    return core_op_manager_it->second.set_cache_output_channel(cache_id, batch_size, channel_id);
}

// TODO: Support write_offset_bytes_delta in CacheManager::init_caches (HRT-14397)
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

    LOGGER__INFO("Initializing caches [read_offset={}, write_offset_delta={}]",
        initial_read_offset_bytes, write_offset_bytes_delta);

    static const auto INITIAL_CONFIGURATION_OFFSET = 0;
    return update_cache_offset(INITIAL_CONFIGURATION_OFFSET);
}

hailo_status CacheManager::update_cache_offset(int32_t offset_delta_bytes)
{
    if (!m_caches_created) {
        // No cache layers found, nothing to do
        LOGGER__WARNING("No cache layers found, but update_cache_offset was called");
        return HAILO_SUCCESS;
    }

    auto new_read_offset = (m_read_offset_bytes + offset_delta_bytes) % m_cache_size;

    for (auto &core : m_core_op_managers) {
        auto status = core.second.update_cache_offset(new_read_offset);
        CHECK_SUCCESS(status, "Failed to update cache offset for core_op {}", core.first);
    }

    m_read_offset_bytes = new_read_offset;

    return HAILO_SUCCESS;
}

uint32_t CacheManager::get_cache_size() const
{
    return m_cache_size;
}

CacheManager::StorageManager::StorageManager(HailoRTDriver &driver) :
    m_driver(driver),
    m_backing_buffers()
{}

Expected<std::shared_ptr<vdma::VdmaBuffer>> CacheManager::StorageManager::get_backing_buffer(uint32_t cache_id, uint32_t cache_size)
{
    CHECK(cache_size > 0, HAILO_INVALID_ARGUMENT);

    // Check if the buffer already exists
    auto buffer_it = m_backing_buffers.find(cache_id);
    if (std::end(m_backing_buffers) != buffer_it) {
        CHECK(buffer_it->second->size() == cache_size, HAILO_INTERNAL_FAILURE,
            "Cache size mismatch for cache_id {}", cache_id);
        return Expected<std::shared_ptr<vdma::VdmaBuffer>>(buffer_it->second);
    }

    // Otherwise, create a new buffer (cache buffers are by sg buffers)
    TRY(auto buffer, vdma::SgBuffer::create(m_driver, cache_size, HailoRTDriver::DmaDirection::BOTH));
    auto buffer_ptr = make_shared_nothrow<vdma::SgBuffer>(std::move(buffer));
    CHECK_NOT_NULL(buffer_ptr, HAILO_OUT_OF_HOST_MEMORY);

    // Store the buffer in the manager
    m_backing_buffers.emplace(cache_id, buffer_ptr);

    return std::shared_ptr<vdma::VdmaBuffer>(buffer_ptr);
}

Expected<CacheManager::CoreOpManager> CacheManager::CoreOpManager::create(HailoRTDriver &driver,
    StorageManager &storage_manager, std::shared_ptr<CoreOpMetadata> core_op_metadata)
{
    hailo_status status = HAILO_SUCCESS;
    CoreOpManager result(driver, storage_manager, core_op_metadata, status);
    CHECK_SUCCESS(status);

    return result;
}

CacheManager::CoreOpManager::CoreOpManager(HailoRTDriver &driver, StorageManager &storage_manager,
                                           std::shared_ptr<CoreOpMetadata> core_op_metadata, hailo_status &status) :
    m_driver(driver),
    m_initialized(false),
    m_cache_input_size(core_op_cache_input_size(core_op_metadata)),
    m_cache_output_size(core_op_cache_output_size(core_op_metadata)),
    m_cache_size(m_cache_input_size + m_cache_output_size),
    m_cache_entry_size(core_op_cache_entry_size(core_op_metadata)),
    m_write_offset_bytes_delta(m_cache_input_size),
    m_cache_buffers(),
    m_uninitialized_caches()
{
    status = allocate_cache_buffers(storage_manager, core_op_metadata);
    if (HAILO_SUCCESS != status) {
        return;
    }

    status = HAILO_SUCCESS;
}

std::unordered_map<uint32_t, CacheBuffer> &CacheManager::CoreOpManager::get_cache_buffers()
{
    return m_cache_buffers;
}
const std::unordered_map<uint32_t, CacheBuffer> &CacheManager::CoreOpManager::get_cache_buffers() const
{
    return m_cache_buffers;
}

ExpectedRef<CacheBuffer> CacheManager::CoreOpManager::get_cache_buffer(uint32_t cache_id)
{
    const auto cache_buffer_it = m_cache_buffers.find(cache_id);
    if (std::end(m_cache_buffers) != cache_buffer_it) {
        return std::ref(cache_buffer_it->second);
    }

    return make_unexpected(HAILO_NOT_FOUND);
}

ExpectedRef<IntermediateBuffer> CacheManager::CoreOpManager::set_cache_input_channel(uint32_t cache_id,
    uint16_t batch_size, vdma::ChannelId channel_id)
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

ExpectedRef<IntermediateBuffer> CacheManager::CoreOpManager::set_cache_output_channel(uint32_t cache_id,
    uint16_t batch_size, vdma::ChannelId channel_id)
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

hailo_status CacheManager::CoreOpManager::update_cache_offset(uint32_t read_offset)
{
    CHECK(m_initialized, HAILO_INVALID_OPERATION, "CacheManager not initialized");

    auto status = HAILO_UNINITIALIZED;
    auto new_read_offset = read_offset % m_cache_size;
    auto new_write_offset = (read_offset + m_write_offset_bytes_delta) % m_cache_size;

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

    return HAILO_SUCCESS;
}

uint32_t CacheManager::CoreOpManager::get_cache_size() const
{
    return m_cache_size;
}

uint32_t CacheManager::CoreOpManager::get_input_size() const
{
    return m_cache_input_size;
}

uint32_t CacheManager::CoreOpManager::get_output_size() const
{
    return m_cache_output_size;
}

hailo_status CacheManager::CoreOpManager::allocate_cache_buffers(StorageManager &storage_manager,
    std::shared_ptr<CoreOpMetadata> core_op_metadata)
{
    // It's enough to go over cache_output_layers, as each cache has both input and output layers (that share the same buffer)
    for (const auto &context_metadata : core_op_metadata->dynamic_contexts()) {
        for (const auto &layer_info : context_metadata.get_cache_output_layers()) {
            const auto cache_id = layer_info.cache_info.id;
            TRY(auto backing_buffer, storage_manager.get_backing_buffer(cache_id, m_cache_size));
            TRY(auto cache_buffer, CacheBuffer::create(backing_buffer, m_cache_size, m_cache_input_size,
                m_cache_output_size, m_cache_entry_size));
            auto emplace_res = m_cache_buffers.emplace(cache_id, std::move(cache_buffer));
            CHECK(emplace_res.second, HAILO_INTERNAL_FAILURE);

            // The cache buffer is yet to be initialized (will be initalized when it is configured with input/output channels)
            m_uninitialized_caches.insert(cache_id);
        }
    }

    return HAILO_SUCCESS;
}

hailo_status CacheManager::CoreOpManager::try_complete_cache_initialization()
{
    // If all caches are now initialized, program their desc list and set the CacheManager as initialized
    if (m_uninitialized_caches.empty() && !m_initialized) {
        m_initialized = true;

        auto status = program_cache_buffers();
        CHECK_SUCCESS(status, "Failed to program cache buffers");
    }

    return HAILO_SUCCESS;
}

hailo_status CacheManager::CoreOpManager::program_cache_buffers()
{
    // Set the cache to the initial configuration (program the descriptors to the initial offset)
    static const auto INITIAL_CONFIGURATION_OFFSET = 0;
    return update_cache_offset(INITIAL_CONFIGURATION_OFFSET);
}

} /* namespace hailort */
