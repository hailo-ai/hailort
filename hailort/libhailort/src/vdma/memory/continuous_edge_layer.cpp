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

desc_list_handle_t ContinuousEdgeLayer::handle() const
{
    return 0;
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
