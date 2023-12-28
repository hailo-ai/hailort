/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file mapping_manager.cpp
 * @brief DMA mapping registry on a given device
 **/

#include "mapping_manager.hpp"
#include "hailo/hailort.h"

namespace hailort {
namespace vdma {

MappingManager::MappingManager(HailoRTDriver &driver) :
    m_driver(driver),
    m_mutex(),
    m_h2d_mappings(),
    m_d2h_mappings()
{}

hailo_status MappingManager::map_buffer(void *address, size_t size, hailo_stream_direction_t direction)
{
    static const auto CREATE_DMAABLE_BUFFER = nullptr;
    auto mapping_result = try_dma_map(CREATE_DMAABLE_BUFFER, address, size, direction);
    CHECK_EXPECTED_AS_STATUS(mapping_result);

    const auto new_mapping = mapping_result->second;
    return new_mapping ? HAILO_SUCCESS : HAILO_DMA_MAPPING_ALREADY_EXISTS;
}

hailo_status MappingManager::unmap_buffer(void *address, hailo_stream_direction_t direction)
{
    auto &mappings = get_mapping_storage(direction);
    std::lock_guard<std::mutex> lock_guard(m_mutex);
    auto it = mappings.find(address);
    if (it == mappings.end()) {
        LOGGER__TRACE("Buffer {} not mapped in direction {}", address, direction);
        return HAILO_NOT_FOUND;
    }

    mappings.erase(it);
    return HAILO_SUCCESS;
}

Expected<std::pair<MappedBufferPtr, bool>> MappingManager::try_dma_map(DmaAbleBufferPtr buffer,
    hailo_stream_direction_t direction)
{
    CHECK_ARG_NOT_NULL_AS_EXPECTED(buffer);

    return try_dma_map(buffer, buffer->user_address(), buffer->size(), direction);
}

Expected<std::pair<MappedBufferPtr, bool>> MappingManager::try_dma_map(DmaAbleBufferPtr buffer,
    void *address, size_t size, hailo_stream_direction_t direction)
{
    assert((nullptr == buffer) || ((buffer->user_address() == address) && (buffer->size() == size)));
    CHECK_ARG_NOT_NULL_AS_EXPECTED(address);
    CHECK_AS_EXPECTED(0 < size, HAILO_INVALID_ARGUMENT);
    CHECK_AS_EXPECTED(HAILO_STREAM_DIRECTION_MAX_ENUM > direction, HAILO_INVALID_ARGUMENT);

    auto &mappings = get_mapping_storage(direction);
    std::lock_guard<std::mutex> lock_guard(m_mutex);
    if (mappings.end() != mappings.find(address)) {
        // Mapping exists
        return std::make_pair(mappings[address], false);
    }

    // New mapping
    if (nullptr == buffer) {
        // We only want to create a dma-able buffer if the address hasn't been mapped and we haven't gotten
        // a dma-able buffer from the user
        auto buffer_exp = DmaAbleBuffer::create_from_user_address(address, size);
        CHECK_EXPECTED(buffer_exp);
        buffer = buffer_exp.release();
    }

    const auto data_direction = (direction == HAILO_H2D_STREAM) ?
        HailoRTDriver::DmaDirection::H2D :
        HailoRTDriver::DmaDirection::D2H;
    auto mapped_buffer = MappedBuffer::create_shared(buffer, m_driver, data_direction);
    CHECK_EXPECTED(mapped_buffer);

    mappings[address] = mapped_buffer.release();

    return std::make_pair(mappings[address], true);
}

std::unordered_map<void *, MappedBufferPtr> &MappingManager::get_mapping_storage(hailo_stream_direction_t direction)
{
    // No point in failing if direction is invalid (i.e. HAILO_STREAM_DIRECTION_MAX_ENUM),
    // because the direction is checked before mappings are added (see try_dma_map). So an invalid direction
    // will result in the mapping not being found
    return (direction == HAILO_H2D_STREAM) ? m_h2d_mappings : m_d2h_mappings;
}

} /* namespace vdma */
} /* namespace hailort */
