/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file mapping_manager.hpp
 * @brief DMA mapping registry on a given device
 **/

#ifndef _HAILO_MAPPING_MANAGER_HPP_
#define _HAILO_MAPPING_MANAGER_HPP_

#include "hailo/hailort.h"
#include "vdma/memory/mapped_buffer.hpp"
#include "os/hailort_driver.hpp"

#include <mutex>
#include <unordered_map>
#include <memory>

namespace hailort {
namespace vdma {

class MappingManager final
{
public:
    MappingManager(HailoRTDriver &driver);
    MappingManager(MappingManager &&) = delete;
    MappingManager(const MappingManager &) = delete;
    MappingManager &operator=(MappingManager &&) = delete;
    MappingManager &operator=(const MappingManager &) = delete;
    ~MappingManager() = default;

    hailo_status map_buffer(void *address, size_t size, hailo_stream_direction_t direction);
    hailo_status unmap_buffer(void *address, hailo_stream_direction_t direction);
    // Returns (MappedBufferPtr, true) if the mapping is new
    // Returns (MappedBufferPtr, false) if the mapping is pre-existing
    Expected<std::pair<MappedBufferPtr, bool>> try_dma_map(DmaAbleBufferPtr buffer, hailo_stream_direction_t direction);

private:
    inline std::unordered_map<void *, MappedBufferPtr> &get_mapping_storage(hailo_stream_direction_t direction);
    Expected<std::pair<MappedBufferPtr, bool>> try_dma_map(DmaAbleBufferPtr buffer, void *address, size_t size,
        hailo_stream_direction_t direction);

    HailoRTDriver &m_driver;
    std::mutex m_mutex;
    std::unordered_map<void *, MappedBufferPtr> m_h2d_mappings;
    std::unordered_map<void *, MappedBufferPtr> m_d2h_mappings;
};

} /* namespace vdma */
} /* namespace hailort */

#endif /* _HAILO_mapping_manager_HPP_ */
