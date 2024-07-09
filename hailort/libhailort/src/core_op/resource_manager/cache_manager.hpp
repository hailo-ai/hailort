/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
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

class CacheManager;
using CacheManagerPtr = std::shared_ptr<CacheManager>;
class CacheManager final
{
public:
    // TODO: Support getting initial_read_offset_bytes + write_offset_bytes_delta from configured_network_params
    //       s.t. the CacheManager can be created with the correct offsets, and init_caches won't be needed at the start.
    //       Currently, the CacheManager is created with the m_read_offset_bytes=0 and
    //       m_write_offset_bytes_delta=m_cache_input_size (i.e. right after where data was read from) (HRT-14288)
    static Expected<CacheManagerPtr> create_shared(HailoRTDriver &driver);

    CacheManager(HailoRTDriver &driver);
    CacheManager(CacheManager &&) = default;
    CacheManager(const CacheManager &) = delete;
    CacheManager &operator=(CacheManager &&) = delete;
    CacheManager &operator=(const CacheManager &) = delete;
    ~CacheManager() = default;

    hailo_status create_caches_from_core_op(std::shared_ptr<CoreOpMetadata> core_op_metadata);
    ExpectedRef<IntermediateBuffer> set_cache_input_channel(uint32_t cache_id, uint16_t batch_size, vdma::ChannelId channel_id);
    ExpectedRef<IntermediateBuffer> set_cache_output_channel(uint32_t cache_id, uint16_t batch_size, vdma::ChannelId channel_id);
    std::unordered_map<uint32_t, CacheBuffer> &get_cache_buffers();

    // Note: These functions are not thread-safe!
    // Programs the CacheManager instance with the given offsets, overriding the current offsets.
    hailo_status init_caches(uint32_t initial_read_offset_bytes, int32_t write_offset_bytes_delta);
    // Updates the read offset by the given delta
    hailo_status update_cache_offset(int32_t offset_delta_bytes);

    uint32_t get_cache_size() const;
    uint32_t get_read_offset_bytes() const;
    int32_t get_write_offset_bytes_delta() const;

private:
    static bool core_op_has_caches(std::shared_ptr<CoreOpMetadata> core_op_metadata);
    static bool validate_cache_edge_layers(std::shared_ptr<CoreOpMetadata> core_op_metadata,
        uint32_t cache_input_size, uint32_t cache_output_size);
    static uint32_t get_cache_input_size(std::shared_ptr<CoreOpMetadata> core_op_metadata);
    static uint32_t get_cache_output_size(std::shared_ptr<CoreOpMetadata> core_op_metadata);
    static bool validate_cache_ids(std::shared_ptr<CoreOpMetadata> core_op_metadata,
        const std::unordered_map<uint32_t, CacheBuffer> &current_cache_buffers);
    ExpectedRef<CacheBuffer> get_cache_buffer(uint32_t cache_id);
    hailo_status allocate_cache_buffers(std::shared_ptr<CoreOpMetadata> core_op_metadata);
    hailo_status program_cache_buffers();
    hailo_status try_complete_cache_initialization();

    HailoRTDriver &m_driver;
    bool m_caches_created;
    // This class is initialized (and the member is set to true) when all caches are allocated and configured with
    // input/output channels. This is done in two steps: (1) cache allocation; (2) channel configuration
    // Two steps are necessary because this class allocates the buffers, however the input/output channels are assigned
    // by the resource manager
    bool m_initialized;
    uint32_t m_cache_input_size;
    uint32_t m_cache_output_size;
    uint32_t m_cache_size;
    uint32_t m_read_offset_bytes;
    int32_t m_write_offset_bytes_delta;
    std::unordered_map<uint32_t, CacheBuffer> m_cache_buffers;
    std::unordered_set<uint32_t> m_uninitialized_caches;
};

} /* namespace hailort */

#endif /* _HAILO_CACHE_MANAGER_HPP_ */
