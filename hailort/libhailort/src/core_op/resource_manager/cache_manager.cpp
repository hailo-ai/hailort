/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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
    m_cache_length(CACHE_LENGTH_NOT_SET),
    m_read_offset_entries(0)
{}

hailo_status CacheManager::create_caches_from_core_op(std::shared_ptr<CoreOpMetadata> core_op_metadata)
{
    if (!core_op_has_caches(core_op_metadata)) {
        // No cache layers found, nothing to do
        return HAILO_SUCCESS;
    }

    const auto core_op_name = core_op_metadata->core_op_name();
    TRY(auto core_op_manager, CoreOpManager::create(m_driver, m_storage_manager, core_op_metadata, m_cache_length));
    if (m_cache_length == CACHE_LENGTH_NOT_SET) {
        m_cache_length = core_op_manager.cache_length();
    }
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

hailo_status CacheManager::init_caches(uint32_t initial_read_offset_entries)
{
    if (!m_caches_created) {
        // No cache layers found, nothing to do
        LOGGER__WARNING("No cache layers found, but init_cache was called");
        return HAILO_SUCCESS;
    }

    CHECK(initial_read_offset_entries < m_cache_length, HAILO_INVALID_ARGUMENT);
    m_read_offset_entries = initial_read_offset_entries;

    LOGGER__INFO("Initializing caches @ read_offset={}", initial_read_offset_entries);

    static const auto INITIAL_CONFIGURATION_OFFSET = 0;
    return update_cache_offset(INITIAL_CONFIGURATION_OFFSET);
}

hailo_status CacheManager::update_cache_offset(int32_t offset_delta_entries, bool update_cache_offset,
    bool require_changes)
{
    if (!m_caches_created) {
        // No cache layers found, nothing to do
        LOGGER__WARNING("No cache layers found, but update_cache_offset was called");
        return HAILO_SUCCESS;
    }

    const auto new_read_offset_entries = (m_read_offset_entries + offset_delta_entries) % m_cache_length;

    for (auto &core : m_core_op_managers) {
        auto status = core.second.update_cache_offset(new_read_offset_entries, m_read_offset_entries,
            update_cache_offset, require_changes);
        CHECK_SUCCESS(status, "Failed to update cache offset for core_op {}", core.first);
    }

    m_read_offset_entries = new_read_offset_entries;

    return HAILO_SUCCESS;
}

uint32_t CacheManager::get_cache_length() const
{
    return m_cache_length;
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
    StorageManager &storage_manager, std::shared_ptr<CoreOpMetadata> core_op_metadata, uint32_t expected_cache_length)
{
    const auto desc_sizes_params = driver.get_sg_desc_params();
    TRY(auto cache_buffers, allocate_cache_buffers(storage_manager, core_op_metadata, expected_cache_length,
        desc_sizes_params));
    const auto &cache_buffer = cache_buffers.begin()->second;
    const auto cache_length = cache_buffer.cache_length();
    return CoreOpManager(driver, cache_length, std::move(cache_buffers));
}

Expected<CoreOpCacheIoInfos> CacheManager::CoreOpManager::get_cache_ios_infos(
    std::shared_ptr<CoreOpMetadata> core_op_metadata, bool input, const DescSizesParams &desc_sizes_params)
{
    CoreOpCacheIoInfos cache_inputs_info;
    for (const auto &context_metadata : core_op_metadata->dynamic_contexts()) {
        for (const auto &layer_info : (input ? context_metadata.get_cache_input_layers() : context_metadata.get_cache_output_layers())) {
            const auto cache_id = layer_info.cache_info.cache_id;
            CHECK(!contains(cache_inputs_info, cache_id), HAILO_INTERNAL_FAILURE,
                "Duplicate cache_id found in cache input layers (cache_id {})", cache_id);
            CacheIoInfo io_info{};
            io_info.entry_size = layer_info.hw_shape.features;

            // The page size is either default_page_size or the nearest power of 2 to the entry size (if it's smaller
            // than default_page_size)
            const auto page_size = std::min(
                get_nearest_powerof_2(io_info.entry_size, 64),
                static_cast<uint32_t>(desc_sizes_params.default_page_size));
            io_info.padded_entry_size = HailoRTCommon::align_to(io_info.entry_size, page_size);
            io_info.io_size = io_info.padded_entry_size * layer_info.hw_shape.height * layer_info.hw_shape.width;
            cache_inputs_info[cache_id] = io_info;
        }
    }

    return cache_inputs_info;
}

Expected<CoreOpCacheInfos> CacheManager::CoreOpManager::get_cache_infos(std::shared_ptr<CoreOpMetadata> core_op_metadata,
    uint32_t expected_cache_length, const DescSizesParams &desc_sizes_params)
{
    static const auto INPUT = true;
    TRY(auto cache_inputs, get_cache_ios_infos(core_op_metadata, INPUT, desc_sizes_params));

    static const auto OUTPUT = false;
    TRY(auto cache_outputs, get_cache_ios_infos(core_op_metadata, OUTPUT, desc_sizes_params));

    CHECK(get_key_set(cache_inputs) == get_key_set(cache_outputs), HAILO_INTERNAL_FAILURE,
        "Mismatch between cache input and output cache_ids");

    CoreOpCacheInfos final_cache_infos;
    for (const auto &cache_input_info : cache_inputs) {
        const auto cache_id = cache_input_info.first;
        TRY(const auto cache_info, CacheInfo::create(cache_input_info.second, cache_outputs[cache_id]));
        final_cache_infos[cache_id] = cache_info;
        if (expected_cache_length == CACHE_LENGTH_NOT_SET) {
            // If expected_cache_length is not set, set it to the first cache length
            // This happens the first time get_cache_infos is called.
            // In subsequent calls, expected_cache_length will be set to the cache length of the first cache processed
            expected_cache_length = cache_info.cache_length();
        } else {
            // Compare the cache length with the first cache length (all caches should have the same length)
            CHECK(expected_cache_length == cache_info.cache_length(), HAILO_INVALID_ARGUMENT,
                "Cache length mismatch for cache_id {} (expected {}, got {})",
                cache_id, expected_cache_length, cache_info.cache_length());
        }
    }

    return final_cache_infos;
}

Expected<std::unordered_map<uint32_t, CacheBuffer>> CacheManager::CoreOpManager::allocate_cache_buffers(
    StorageManager &storage_manager, std::shared_ptr<CoreOpMetadata> core_op_metadata, uint32_t expected_cache_length,
    const DescSizesParams &desc_sizes_params)
{
    TRY(auto cache_infos, get_cache_infos(core_op_metadata, expected_cache_length, desc_sizes_params));

    std::unordered_map<uint32_t, CacheBuffer> cache_buffers;
    for (const auto &cache_info_id_pair : cache_infos) {
        const auto cache_id = cache_info_id_pair.first;
        const auto &cache_info = cache_info_id_pair.second;

        TRY(auto backing_buffer, storage_manager.get_backing_buffer(cache_id, cache_info.size));
        TRY(auto cache_buffer, CacheBuffer::create(backing_buffer, cache_info.size, cache_info.input_size,
            cache_info.output_size, cache_info.entry_size, cache_info.padded_entry_size));
        auto emplace_res = cache_buffers.emplace(cache_id, std::move(cache_buffer));
        CHECK(emplace_res.second, HAILO_INTERNAL_FAILURE);
    }

    return cache_buffers;
}

CacheManager::CoreOpManager::CoreOpManager(HailoRTDriver &driver, uint32_t cache_length,
                                           std::unordered_map<uint32_t, CacheBuffer> &&cache_buffers) :
    m_driver(driver),
    m_initialized(false),
    m_cache_length(cache_length),
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

uint32_t CacheManager::CoreOpManager::cache_length() const
{
    return m_cache_length;
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

hailo_status CacheManager::CoreOpManager::update_cache_offset(uint32_t read_offset_entries,
    uint32_t prev_read_offset_entries, bool check_snapshots, bool require_changes)
{
    auto status = HAILO_UNINITIALIZED;
    CHECK(m_initialized, HAILO_INVALID_OPERATION, "CacheManager not initialized");

    const auto new_read_offset_entries = read_offset_entries % m_cache_length;
    for (auto &cache_buffer_id_pair : m_cache_buffers) {
        const auto cache_id = cache_buffer_id_pair.first;
        auto &cache_buffer = cache_buffer_id_pair.second;

        if (check_snapshots) {
            // Create a snapshot of the cache buffer
            // Assumes that the cache buffer is not modified while the snapshot is being created
            TRY(auto snapshot, cache_buffer.create_snapshot(prev_read_offset_entries));

            // Validate the snapshot compared to the previous one
            if (contains(m_cache_snapshots, cache_id)) {
                auto prev_snapshot = std::move(m_cache_snapshots[cache_id]);
                status = validate_cache_update(cache_buffer, cache_id, snapshot, prev_snapshot, require_changes);
                CHECK_SUCCESS(status, "Failed to validate cache update for cache_id {}", cache_id);
            }

            // Store the current snapshot (overwriting the previous one)
            m_cache_snapshots[cache_id] = std::move(snapshot);
        }

        status = cache_buffer.reprogram_descriptors(new_read_offset_entries);
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
    static const auto INITIAL_CONFIGURATION_OFFSET = 0;
    static const auto NO_CACHE_CHECK = false;
    static const auto IGNORED = false;
    return update_cache_offset(INITIAL_CONFIGURATION_OFFSET, INITIAL_CONFIGURATION_OFFSET,
        NO_CACHE_CHECK, IGNORED);
}

} /* namespace hailort */
