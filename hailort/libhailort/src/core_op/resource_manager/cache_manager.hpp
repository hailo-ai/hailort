/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file cache_manager.hpp
 * @brief Manges creation and configuration of cache buffers
 **/

#ifndef _HAILO_CACHE_MANAGER_HPP_
#define _HAILO_CACHE_MANAGER_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "core_op/resource_manager/cache_buffer.hpp"
#include "hef/core_op_metadata.hpp"

#include <unordered_map>
#include <unordered_set>

namespace hailort
{

struct CacheIoInfo
{
    uint32_t io_size;
    uint32_t entry_size;
    uint32_t padded_entry_size;
};

struct CacheInfo
{
    static Expected<CacheInfo> create(const CacheIoInfo &input_info, const CacheIoInfo &output_info)
    {
        CHECK(input_info.entry_size == output_info.entry_size, HAILO_INVALID_ARGUMENT,
            "Input and output entry sizes must match: input={}, output={}",
            input_info.entry_size, output_info.entry_size);
        // Asserting is good enough here, as it'll be validated down the line
        assert(input_info.io_size % input_info.padded_entry_size == 0);
        assert(output_info.io_size % output_info.padded_entry_size == 0);

        return CacheInfo{input_info.io_size + output_info.io_size, input_info.entry_size, input_info.io_size,
            output_info.io_size, input_info.padded_entry_size};
    }

    uint32_t size;
    uint32_t entry_size;
    uint32_t input_size;
    uint32_t output_size;
    uint32_t padded_entry_size;

    uint32_t cache_length() const { return size / padded_entry_size; }
};

// Cache ID -> CacheIoInfo
using CoreOpCacheIoInfos = std::unordered_map<uint32_t, CacheIoInfo>;
using CoreOpCacheInfos = std::unordered_map<uint32_t, CacheInfo>;

class CacheManager;
using CacheManagerPtr = std::shared_ptr<CacheManager>;
class CacheManager final
{
public:
    static constexpr uint32_t CACHE_LENGTH_NOT_SET = 0;

    static Expected<CacheManagerPtr> create_shared(HailoRTDriver &driver);

    CacheManager(HailoRTDriver &driver);
    CacheManager(CacheManager &&) = default;
    CacheManager(const CacheManager &) = delete;
    CacheManager &operator=(CacheManager &&) = delete;
    CacheManager &operator=(const CacheManager &) = delete;
    ~CacheManager();

    void wait_for_cache_update_done();

    hailo_status create_caches_from_core_op(std::shared_ptr<CoreOpMetadata> core_op_metadata);
    ExpectedRef<CacheBuffer> set_cache_input_channel(const std::string &core_op_name, uint32_t cache_id,
        uint16_t batch_size, vdma::ChannelId channel_id);
    ExpectedRef<CacheBuffer> set_cache_output_channel(const std::string &core_op_name, uint32_t cache_id,
        uint16_t batch_size, vdma::ChannelId channel_id);
    ExpectedRef<std::unordered_map<uint32_t, CacheBuffer>> get_cache_buffers(const std::string &core_op_name);

    // Note: These functions are not thread-safe!
    // Programs the CacheManager instance with the given offset read overriding the current offset.
    hailo_status init_caches(uint32_t initial_read_offset_entries);
    // Updates the read offset by the given delta
    // * If check_snapshots is true, the function will validate that the caches have only been updated at the
    //   correct offsets
    // * If require_changes is true, the function will return an error if no changes were made relative to the
    //   previous snapshot at the current offset. (ignored if check_snapshots is false)
    hailo_status update_cache_offset(int32_t offset_delta_entries, bool check_snapshots = false, bool require_changes = false,
        std::function<void(hailo_status)> callback = nullptr);
    hailo_status finalize_caches();
    uint32_t get_cache_length() const;


private:
    class StorageManager final
    {
    public:
        StorageManager(HailoRTDriver &driver);
        StorageManager(StorageManager &&) = default;
        StorageManager(const StorageManager &) = delete;
        StorageManager &operator=(StorageManager &&) = delete;
        StorageManager &operator=(const StorageManager &) = delete;
        ~StorageManager() = default;

        // Creates a new backing buffer of the given size and stores it in the manager, or returns an existing one.
        Expected<std::shared_ptr<vdma::VdmaBuffer>> get_backing_buffer(uint32_t cache_id, uint32_t cache_size);

    private:
        HailoRTDriver &m_driver;
        std::unordered_map<uint32_t, std::shared_ptr<vdma::VdmaBuffer>> m_backing_buffers;
    };

    class CoreOpManager final
    {
    public:
        // If expected_cache_length is CACHE_LENGTH_NOT_SET, the cache length will be set to the first cache length
        // All subsequent caches must have the same length
        // If it is set, all caches must have a length equal to expected_cache_length
        static Expected<CoreOpManager> create(HailoRTDriver &driver, StorageManager &storage_manager,
            std::shared_ptr<CoreOpMetadata> core_op_metadata, uint32_t expected_cache_length);
        CoreOpManager(CoreOpManager &&) = default;
        CoreOpManager(const CoreOpManager &) = delete;
        CoreOpManager &operator=(CoreOpManager &&) = delete;
        CoreOpManager &operator=(const CoreOpManager &) = delete;
        ~CoreOpManager() = default;

        std::unordered_map<uint32_t, CacheBuffer> &get_cache_buffers();
        const std::unordered_map<uint32_t, CacheBuffer> &get_cache_buffers() const;
        ExpectedRef<CacheBuffer> get_cache_buffer(uint32_t cache_id);
        ExpectedRef<CacheBuffer> set_cache_input_channel(uint32_t cache_id, uint16_t batch_size,
            vdma::ChannelId channel_id);
        ExpectedRef<CacheBuffer> set_cache_output_channel(uint32_t cache_id, uint16_t batch_size,
            vdma::ChannelId channel_id);
        uint32_t cache_length() const;
        // Note: read_offset is absolute, not relative to the current read offset
        //       See further documentation under CacheManager::update_cache_offset
        hailo_status update_cache_offset(uint32_t read_offset_entries, uint32_t prev_read_offset_entries,
            bool check_snapshots, bool require_changes);

    private:
        static Expected<CoreOpCacheIoInfos> get_cache_ios_infos(std::shared_ptr<CoreOpMetadata> core_op_metadata,
            bool input, const DescSizesParams &desc_sizes_params);
        static Expected<CoreOpCacheInfos> get_cache_infos(std::shared_ptr<CoreOpMetadata> core_op_metadata,
            uint32_t expected_cache_length, const DescSizesParams &desc_sizes_params);
        static Expected<std::unordered_map<uint32_t, CacheBuffer>> allocate_cache_buffers(
            StorageManager &storage_manager, std::shared_ptr<CoreOpMetadata> core_op_metadata,
            uint32_t expected_cache_length, const DescSizesParams &desc_sizes_params);
        static hailo_status validate_cache_update(const CacheBuffer &cache_buffer, uint32_t cache_id,
            const CacheBuffer::Snapshot &curr_snapshot, const CacheBuffer::Snapshot &prev_snapshot,
            bool require_changes);
        static hailo_status validate_non_wrapping_update(uint32_t cache_id, const CacheBuffer::Snapshot &curr_snapshot,
            const CacheBuffer::Snapshot &prev_snapshot, size_t curr_write_offset_start_bytes,
            size_t curr_write_offset_end_bytes, bool require_changes);
        static hailo_status validate_wrapping_update(uint32_t cache_id, const CacheBuffer::Snapshot &curr_snapshot,
            const CacheBuffer::Snapshot &prev_snapshot, size_t curr_write_offset_start_bytes,
            size_t curr_write_offset_end_bytes, bool require_changes);

        CoreOpManager(HailoRTDriver &driver, uint32_t cache_length,
            std::unordered_map<uint32_t, CacheBuffer> &&cache_buffers);
        hailo_status try_complete_cache_initialization();
        hailo_status program_cache_buffers();

        HailoRTDriver &m_driver;
        // This class is initialized (and the member is set to true) when all caches are allocated and configured with
        // input/output channels. This is done in two steps: (1) cache allocation; (2) channel configuration
        // Two steps are necessary because this class allocates the buffers, however the input/output channels are assigned
        // by the resource manager
        bool m_initialized;
        uint32_t m_cache_length;
        std::unordered_map<uint32_t, CacheBuffer> m_cache_buffers;
        std::unordered_map<uint32_t, CacheBuffer::Snapshot> m_cache_snapshots;
        std::unordered_set<uint32_t> m_uninitialized_caches;
    };

    static bool core_op_has_caches(std::shared_ptr<CoreOpMetadata> core_op_metadata);
    static bool validate_cache_edge_layers(std::shared_ptr<CoreOpMetadata> core_op_metadata,
        uint32_t cache_input_size, uint32_t cache_output_size);
    static bool validate_cache_ids(std::shared_ptr<CoreOpMetadata> core_op_metadata,
        const std::unordered_map<std::string, CoreOpManager> &current_core_op_managers);
    hailo_status program_cache_buffers();
    hailo_status update_cache_offset_impl(int32_t offset_delta_entries, bool check_snapshots = false, bool require_changes = false);

    HailoRTDriver &m_driver;
    StorageManager m_storage_manager;
    std::unordered_map<std::string, CoreOpManager> m_core_op_managers;
    bool m_caches_created;
    uint32_t m_cache_length;
    uint32_t m_read_offset_entries;
    std::thread m_cache_update_thread;
};

} /* namespace hailort */

#endif /* _HAILO_CACHE_MANAGER_HPP_ */
