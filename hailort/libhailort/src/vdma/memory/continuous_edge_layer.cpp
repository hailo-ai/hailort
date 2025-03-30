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
    if (num_pages > MAX_CCB_DESCS_COUNT) {
        LOGGER__INFO("continuous memory number of pages {} must be smaller/equal to {}.", num_pages, MAX_CCB_DESCS_COUNT);
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }

    if (page_size > MAX_CCB_PAGE_SIZE) {
        LOGGER__INFO("continuous memory page size {} must be smaller/equal to {}.", page_size, MAX_CCB_PAGE_SIZE);
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }

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

Expected<uint32_t> ContinuousEdgeLayer::program_descriptors(size_t transfer_size, InterruptsDomain last_desc_interrupts_domain,
    size_t desc_offset, size_t buffer_offset, uint32_t batch_size, bool should_bind, uint32_t stride)
{
    (void)last_desc_interrupts_domain;
    (void)desc_offset;
    (void)buffer_offset;
    (void)batch_size;
    (void)should_bind;
    (void)stride;

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
