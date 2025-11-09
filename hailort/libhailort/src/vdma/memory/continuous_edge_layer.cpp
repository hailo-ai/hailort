/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file continuous_edge_layer.cpp
 * @brief Continuous physical vdma edge layer.
 **/

#include "continuous_edge_layer.hpp"

namespace hailort {
namespace vdma {


Expected<ContinuousEdgeLayer> ContinuousEdgeLayer::create(std::shared_ptr<ContinuousBuffer> &&buffer, size_t size, size_t offset,
    uint16_t page_size, uint32_t num_pages)
{
    if (buffer->size() < offset + size) {
        LOGGER__ERROR("Edge layer is not fully inside the connected buffer. buffer size is {} while edge layer offset {} and size {}",
            buffer->size(), offset, size);
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }

    return ContinuousEdgeLayer(std::move(buffer), size, offset, page_size, num_pages);
}

uint64_t ContinuousEdgeLayer::dma_address() const
{
    return (std::dynamic_pointer_cast<ContinuousBuffer>(m_buffer))->dma_address() + m_offset;
}

uint16_t ContinuousEdgeLayer::desc_page_size() const
{
    return m_page_size;
}

uint32_t ContinuousEdgeLayer::descs_count() const
{
    return m_num_pages;
}

Expected<uint32_t> ContinuousEdgeLayer::program_descriptors(size_t transfer_size,
    size_t desc_offset, size_t buffer_offset, uint32_t batch_size)
{
    (void)desc_offset;
    (void)buffer_offset;
    (void)batch_size;

    // The descriptors in continuous mode are programmed by the hw, nothing to do here.
    return descriptors_in_buffer(transfer_size);
}

ContinuousEdgeLayer::ContinuousEdgeLayer(std::shared_ptr<ContinuousBuffer> &&buffer, size_t size, size_t offset,
        uint16_t page_size, uint32_t num_pages) :
    VdmaEdgeLayer(std::move(buffer), size, offset),
    m_page_size(page_size),
    m_num_pages(num_pages)
{}

}; /* namespace vdma */
}; /* namespace hailort */
