/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file cache_manager.cpp
 * @brief Manges creation and configuration of cache buffers
 **/

#include "hailo/hailort.h"
#include "cache_manager.hpp"

#include "vdma/memory/sg_buffer.hpp"

#include <limits>

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
    m_absolute_read_offset_entries(0)
{}

hailo_status CacheManager::create_caches_from_core_op(std::shared_ptr<CoreOpMetadata> core_op_metadata)
{
    if (!core_op_has_caches(core_op_metadata)) {
        // No cache layers found, nothing to do
        return HAILO_SUCCESS;
    }

    const auto core_op_name = core_op_metadata->core_op_name();
    TRY(auto core_op_manager, CoreOpManager::create(m_driver, m_storage_manager, core_op_metadata));
    m_core_op_managers.emplace(core_op_name, std::move(core_op_manager));
    m_caches_created = true;

    assert(validate_cache_ids(core_op_metadata, m_core_op_managers));

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

bool CacheManager::validate_cache_ids(std::shared_ptr<CoreOpMetadata> core_op_metadata,
    const std::unordered_map<std::string, CoreOpManager> &current_core_op_managers)
{
    std::unordered_set<uint32_t> cache_ids;
    for (const auto &context_metadata : core_op_metadata->dynamic_contexts()) {
        for (const auto &layer_info : context_metadata.get_cache_input_layers()) {
            cache_ids.insert(layer_info.cache_info.cache_id);
        }

        for (const auto &layer_info : context_metadata.get_cache_output_layers()) {
            cache_ids.insert(layer_info.cache_info.cache_id);
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

ExpectedRef<CacheBuffer> CacheManager::set_cache_input_channel(const std::string &core_op_name,
    uint32_t cache_id, uint16_t batch_size, vdma::ChannelId channel_id)
{
    const auto core_op_manager_it = m_core_op_managers.find(core_op_name);
    if (std::end(m_core_op_managers) == core_op_manager_it) {
        return make_unexpected(HAILO_NOT_FOUND);
    }

    return core_op_manager_it->second.set_cache_input_channel(cache_id, batch_size, channel_id);
}

ExpectedRef<CacheBuffer> CacheManager::set_cache_output_channel(const std::string &core_op_name,
    uint32_t cache_id, uint16_t batch_size, vdma::ChannelId channel_id)
{
    const auto core_op_manager_it = m_core_op_managers.find(core_op_name);
    if (std::end(m_core_op_managers) == core_op_manager_it) {
        return make_unexpected(HAILO_NOT_FOUND);
    }

    return core_op_manager_it->second.set_cache_output_channel(cache_id, batch_size, channel_id);
}

hailo_status CacheManager::init_caches(uint32_t initial_absolute_read_offset_entries)
{
    if (!m_caches_created) {
        // No cache layers found, nothing to do
        LOGGER__WARNING("No cache layers found, but init_cache was called");
        return HAILO_SUCCESS;
    }

    m_absolute_read_offset_entries = initial_absolute_read_offset_entries;

    LOGGER__INFO("Initializing caches @ read_offset={}", initial_absolute_read_offset_entries);

    static constexpr int32_t NO_OFFSET_DELTA = 0;
    return update_cache_offset(NO_OFFSET_DELTA);
}

Expected<uint64_t> CacheManager::apply_offset_delta(uint64_t current_absolute_offset_entries,
    int32_t offset_delta_entries)
{
    if (0 > offset_delta_entries) {
        const auto rollback_entries = static_cast<uint64_t>(-static_cast<int64_t>(offset_delta_entries));
        CHECK(rollback_entries <= current_absolute_offset_entries, HAILO_INVALID_ARGUMENT,
            "Cannot roll back cache read offset by {} entries - only {} entries have been advanced since cache init",
            rollback_entries, current_absolute_offset_entries);
        return current_absolute_offset_entries - rollback_entries;
    }

    const auto advance_entries = static_cast<uint64_t>(offset_delta_entries);
    CHECK(advance_entries <= (std::numeric_limits<uint64_t>::max() - current_absolute_offset_entries),
        HAILO_INVALID_ARGUMENT,
        "Cannot advance cache read offset by {} entries from {} - the offset would overflow",
        advance_entries, current_absolute_offset_entries);
    return current_absolute_offset_entries + advance_entries;
}

hailo_status CacheManager::update_cache_offset(int32_t offset_delta_entries, bool check_snapshots,
    bool require_changes)
{
    if (!m_caches_created) {
        // No cache layers found, nothing to do
        LOGGER__WARNING("No cache layers found, but update_cache_offset was called");
        return HAILO_SUCCESS;
    }

    TRY(const auto new_absolute_read_offset_entries,
        apply_offset_delta(m_absolute_read_offset_entries, offset_delta_entries));

    for (auto &core : m_core_op_managers) {
        auto status = core.second.update_cache_offset(new_absolute_read_offset_entries,
            m_absolute_read_offset_entries, check_snapshots, require_changes);
        CHECK_SUCCESS(status, "Failed to update cache offset for core_op {}", core.first);
    }

    m_absolute_read_offset_entries = new_absolute_read_offset_entries;

    return HAILO_SUCCESS;
}

CacheManager::StorageManager::StorageManager(HailoRTDriver &driver) :
    m_driver(driver),
    m_backing_buffers()
{}

Expected<std::shared_ptr<vdma::VdmaBuffer>> CacheManager::StorageManager::get_backing_buffer(uint32_t cache_id,
    const CacheInfo &cache_info)
{
    CHECK(cache_info.size > 0, HAILO_INVALID_ARGUMENT);

    auto buffer_it = m_backing_buffers.find(cache_id);
    if (std::end(m_backing_buffers) != buffer_it) {
        const auto &existing_buffer = buffer_it->second;
        CHECK(existing_buffer.buffer->size() == cache_info.size, HAILO_INTERNAL_FAILURE,
            "Cache size mismatch for cache_id {}: {} and {}", cache_id, existing_buffer.buffer->size(),
            cache_info.size);
        CHECK(existing_buffer.padded_entry_size == cache_info.padded_entry_size, HAILO_INTERNAL_FAILURE,
            "Cache padded entry size mismatch for cache_id {}: {} and {}", cache_id, existing_buffer.padded_entry_size,
            cache_info.padded_entry_size);
        return Expected<std::shared_ptr<vdma::VdmaBuffer>>(existing_buffer.buffer);
    }

    // Otherwise, create a new buffer (cache buffers are by sg buffers)
    TRY(auto buffer, vdma::SgBuffer::create(m_driver, cache_info.size, HailoRTDriver::DmaDirection::BOTH));
    auto buffer_ptr = make_shared_nothrow<vdma::SgBuffer>(std::move(buffer));
    CHECK_NOT_NULL(buffer_ptr, HAILO_OUT_OF_HOST_MEMORY);

    m_backing_buffers.emplace(cache_id, BackingBuffer{buffer_ptr, cache_info.padded_entry_size});

    return std::shared_ptr<vdma::VdmaBuffer>(buffer_ptr);
}

static CacheIoInfo build_cache_io_info(const LayerInfo &layer_info, const DescSizesParams &desc_sizes_params)
{
    CacheIoInfo io_info{};
    io_info.entry_size = layer_info.hw_shape.features;

    // The page size is either default_page_size or the nearest power of 2 to the entry size (if it's smaller
    // than default_page_size)
    const auto page_size = std::min(
        get_nearest_powerof_2(io_info.entry_size, 64),
        static_cast<uint32_t>(desc_sizes_params.default_page_size));
    io_info.padded_entry_size = HailoRTCommon::align_to(io_info.entry_size, page_size);
    io_info.io_size = io_info.padded_entry_size * layer_info.hw_shape.height * layer_info.hw_shape.width;
    return io_info;
}

Expected<std::pair<CoreOpCacheIoInfos, CoreOpCacheIoInfos>> CacheManager::get_cache_ios_infos(
    const CoreOpMetadata &core_op_metadata, const DescSizesParams &desc_sizes_params)
{
    CoreOpCacheIoInfos cache_inputs_info;
    CoreOpCacheIoInfos cache_outputs_info;
    for (const auto &context_metadata : core_op_metadata.dynamic_contexts()) {
        for (const auto &layer_info : context_metadata.get_cache_input_layers()) {
            const auto cache_id = layer_info.cache_info.cache_id;
            CHECK(!contains(cache_inputs_info, cache_id), HAILO_INTERNAL_FAILURE,
                "Duplicate cache_id found in cache input layers (cache_id {})", cache_id);
            cache_inputs_info[cache_id] = build_cache_io_info(layer_info, desc_sizes_params);
        }
        for (const auto &layer_info : context_metadata.get_cache_output_layers()) {
            const auto cache_id = layer_info.cache_info.cache_id;
            CHECK(!contains(cache_outputs_info, cache_id), HAILO_INTERNAL_FAILURE,
                "Duplicate cache_id found in cache output layers (cache_id {})", cache_id);
            cache_outputs_info[cache_id] = build_cache_io_info(layer_info, desc_sizes_params);
        }
    }

    return std::make_pair(std::move(cache_inputs_info), std::move(cache_outputs_info));
}

Expected<CoreOpCacheInfos> CacheManager::get_cache_infos(const CoreOpMetadata &core_op_metadata,
    const DescSizesParams &desc_sizes_params)
{
    TRY(auto cache_ios, get_cache_ios_infos(core_op_metadata, desc_sizes_params));
    auto &cache_inputs = cache_ios.first;
    auto &cache_outputs = cache_ios.second;

    CHECK(get_key_set(cache_inputs) == get_key_set(cache_outputs), HAILO_INTERNAL_FAILURE,
        "Mismatch between cache input and output cache_ids");

    CoreOpCacheInfos final_cache_infos;
    for (const auto &cache_input_info : cache_inputs) {
        const auto cache_id = cache_input_info.first;
        TRY(const auto cache_info, CacheInfo::create(cache_input_info.second, cache_outputs[cache_id]));
        final_cache_infos[cache_id] = cache_info;
    }

    return final_cache_infos;
}

Expected<CacheManager::CoreOpManager> CacheManager::CoreOpManager::create(HailoRTDriver &driver,
    StorageManager &storage_manager, std::shared_ptr<CoreOpMetadata> core_op_metadata)
{
    const auto desc_sizes_params = driver.get_sg_desc_params();
    TRY(auto cache_buffers, allocate_cache_buffers(storage_manager, core_op_metadata, desc_sizes_params));
    CHECK(!cache_buffers.empty(), HAILO_INTERNAL_FAILURE, "Expected cache buffers to be allocated");
    return CoreOpManager(driver, std::move(cache_buffers));
}

Expected<std::unordered_map<uint32_t, CacheBuffer>> CacheManager::CoreOpManager::allocate_cache_buffers(
    StorageManager &storage_manager, std::shared_ptr<CoreOpMetadata> core_op_metadata,
    const DescSizesParams &desc_sizes_params)
{
    TRY(auto cache_infos, CacheManager::get_cache_infos(*core_op_metadata, desc_sizes_params));

    std::unordered_map<uint32_t, CacheBuffer> cache_buffers;
    for (const auto &cache_info_id_pair : cache_infos) {
        const auto cache_id = cache_info_id_pair.first;
        const auto &cache_info = cache_info_id_pair.second;

        TRY(auto backing_buffer, storage_manager.get_backing_buffer(cache_id, cache_info));
        TRY(auto cache_buffer, CacheBuffer::create(backing_buffer, cache_info.size, cache_info.input_size,
            cache_info.output_size, cache_info.entry_size, cache_info.padded_entry_size));
        auto emplace_res = cache_buffers.emplace(cache_id, std::move(cache_buffer));
        CHECK(emplace_res.second, HAILO_INTERNAL_FAILURE);
    }

    return cache_buffers;
}

CacheManager::CoreOpManager::CoreOpManager(HailoRTDriver &driver,
    std::unordered_map<uint32_t, CacheBuffer> &&cache_buffers) :
    m_driver(driver),
    m_initialized(false),
    m_cache_buffers(std::move(cache_buffers)),
    m_cache_snapshots(),
    m_uninitialized_caches(get_key_set(m_cache_buffers))
{}

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

ExpectedRef<CacheBuffer> CacheManager::CoreOpManager::set_cache_input_channel(uint32_t cache_id,
    uint16_t batch_size, vdma::ChannelId channel_id)
{
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

ExpectedRef<CacheBuffer> CacheManager::CoreOpManager::set_cache_output_channel(uint32_t cache_id,
    uint16_t batch_size, vdma::ChannelId channel_id)
{
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

hailo_status CacheManager::CoreOpManager::validate_cache_update(const CacheBuffer &cache_buffer, uint32_t cache_id,
    const CacheBuffer::Snapshot &curr_snapshot, const CacheBuffer::Snapshot &prev_snapshot, bool require_changes)
{
    const auto curr_write_offset_start = (curr_snapshot.read_offset() + cache_buffer.input_length()) % cache_buffer.cache_length();
    const auto curr_write_offset_start_bytes = curr_write_offset_start * cache_buffer.padded_entry_size();
    const auto curr_write_offset_end = (curr_write_offset_start + cache_buffer.output_length()) % cache_buffer.cache_length();
    const auto curr_write_offset_end_bytes = curr_write_offset_end * cache_buffer.padded_entry_size();

    if (curr_write_offset_end > curr_write_offset_start) {
        return validate_non_wrapping_update(cache_id, curr_snapshot, prev_snapshot,
            curr_write_offset_start_bytes, curr_write_offset_end_bytes, require_changes);
    } else {
        return validate_wrapping_update(cache_id, curr_snapshot, prev_snapshot,
            curr_write_offset_start_bytes, curr_write_offset_end_bytes, require_changes);
    }
}

hailo_status CacheManager::CoreOpManager::validate_non_wrapping_update(uint32_t cache_id,
    const CacheBuffer::Snapshot &curr_snapshot, const CacheBuffer::Snapshot &prev_snapshot,
    size_t curr_write_offset_start_bytes, size_t curr_write_offset_end_bytes, bool require_changes)
{
    // Check for equality between the snapshots at [0, curr_write_offset_start) and [curr_write_offset_end, end)
    CHECK(curr_snapshot.buffer().to(curr_write_offset_start_bytes) ==
        prev_snapshot.buffer().to(curr_write_offset_start_bytes), HAILO_INTERNAL_FAILURE);
    CHECK(curr_snapshot.buffer().from(curr_write_offset_end_bytes) ==
        prev_snapshot.buffer().from(curr_write_offset_end_bytes), HAILO_INTERNAL_FAILURE);

    // Check if the cache did not change in the write section - [curr_write_offset_start, curr_write_offset_end)
    if (curr_snapshot.buffer().slice(curr_write_offset_start_bytes, curr_write_offset_end_bytes) ==
            prev_snapshot.buffer().slice(curr_write_offset_start_bytes, curr_write_offset_end_bytes)) {
        LOGGER__WARNING("Cache buffer did not change in write section [0x{:x}, 0x{:x}) (cache_id {})",
            curr_write_offset_start_bytes, curr_write_offset_end_bytes, cache_id);

        if (require_changes) {
            return make_unexpected(HAILO_INTERNAL_FAILURE);
        }
    }

    return HAILO_SUCCESS;
}

hailo_status CacheManager::CoreOpManager::validate_wrapping_update(uint32_t cache_id,
    const CacheBuffer::Snapshot &curr_snapshot, const CacheBuffer::Snapshot &prev_snapshot,
    size_t curr_write_offset_start_bytes, size_t curr_write_offset_end_bytes, bool require_changes)
{
    // Check for equality between the snapshots at [curr_write_offset_end_bytes, curr_write_offset_start_bytes)
    CHECK(curr_snapshot.buffer().slice(curr_write_offset_end_bytes, curr_write_offset_start_bytes) ==
        prev_snapshot.buffer().slice(curr_write_offset_end_bytes, curr_write_offset_start_bytes),
        HAILO_INTERNAL_FAILURE);

    // Check if the cache did not change in the write section - [0, curr_write_offset_end) and [curr_write_offset_start, end)
    const auto first_chunk_equal = (curr_snapshot.buffer().to(curr_write_offset_end_bytes) ==
        prev_snapshot.buffer().to(curr_write_offset_end_bytes));
    const auto second_chunk_equal = (curr_snapshot.buffer().from(curr_write_offset_start_bytes) ==
        prev_snapshot.buffer().from(curr_write_offset_start_bytes));
    if (first_chunk_equal && second_chunk_equal) {
        LOGGER__WARNING("Cache buffer did not change in write section [0, 0x{:x}) or [0x{:x}, 0x{:x}) (cache_id {})",
            curr_write_offset_end_bytes, curr_write_offset_start_bytes, curr_snapshot.buffer().size(), cache_id);
        if (require_changes) {
            return make_unexpected(HAILO_INTERNAL_FAILURE);
        }
    }

    return HAILO_SUCCESS;
}

hailo_status CacheManager::CoreOpManager::update_cache_offset(uint64_t absolute_read_offset_entries,
    uint64_t prev_absolute_read_offset_entries, bool check_snapshots, bool require_changes)
{
    auto status = HAILO_UNINITIALIZED;
    CHECK(m_initialized, HAILO_INVALID_OPERATION, "CacheManager not initialized");

    for (auto &cache_buffer_id_pair : m_cache_buffers) {
        const auto cache_id = cache_buffer_id_pair.first;
        auto &cache_buffer = cache_buffer_id_pair.second;

        if (check_snapshots) {
            // Create a snapshot of the cache buffer
            // Assumes that the cache buffer is not modified while the snapshot is being created
            TRY(auto snapshot, cache_buffer.create_snapshot(prev_absolute_read_offset_entries));

            // Validate the snapshot compared to the previous one
            if (contains(m_cache_snapshots, cache_id)) {
                auto prev_snapshot = std::move(m_cache_snapshots[cache_id]);
                status = validate_cache_update(cache_buffer, cache_id, snapshot, prev_snapshot, require_changes);
                CHECK_SUCCESS(status, "Failed to validate cache update for cache_id {}", cache_id);
            }

            // Store the current snapshot (overwriting the previous one)
            m_cache_snapshots[cache_id] = std::move(snapshot);
        }

        status = cache_buffer.reprogram_descriptors(absolute_read_offset_entries);
        CHECK_SUCCESS(status, "Failed to reprogram cache descriptors for cache_id {}", cache_id);
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
    static constexpr uint64_t INITIAL_CONFIGURATION_OFFSET = 0;
    static constexpr bool NO_CACHE_CHECK = false;
    static constexpr bool IGNORED = false;
    return update_cache_offset(INITIAL_CONFIGURATION_OFFSET, INITIAL_CONFIGURATION_OFFSET, NO_CACHE_CHECK, IGNORED);
}

} /* namespace hailort */
