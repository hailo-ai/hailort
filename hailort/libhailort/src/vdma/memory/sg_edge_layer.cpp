/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file sg_edge_layer.cpp
 * @brief Scatter-gather vdma edge layer.
 **/

#include "vdma/memory/sg_edge_layer.hpp"
#include "vdma/channel/channel_id.hpp"


namespace hailort {
namespace vdma {

Expected<SgEdgeLayer> SgEdgeLayer::create(std::shared_ptr<SgBuffer> &&buffer, size_t size, size_t offset,
    HailoRTDriver &driver, uint32_t desc_count, uint16_t desc_page_size, bool is_circular, ChannelId channel_id)
{
    CHECK(size <= (desc_count * desc_page_size), HAILO_INTERNAL_FAILURE,
        "Requested buffer size {} must be smaller or equal to {}", size, (desc_count * desc_page_size));
    CHECK((size % desc_page_size) == 0, HAILO_INTERNAL_FAILURE,
        "SgEdgeLayer size must be a multiple of descriptors page size (size {})", size);
    CHECK((offset % desc_page_size) == 0, HAILO_INTERNAL_FAILURE,
        "SgEdgeLayer offset must be a multiple of descriptors page size (offset {}. Page size {})", offset, desc_page_size);

    CHECK(buffer->size() >= (offset + size), HAILO_INTERNAL_FAILURE,
        "Edge layer is not fully inside the connected buffer. buffer size is {} while edge layer offset {} and size {}",
        buffer->size(), offset, size);

    TRY(auto desc_list, DescriptorList::create(desc_count, desc_page_size, is_circular, driver));
    assert((desc_count * desc_page_size) <= std::numeric_limits<uint32_t>::max());
    return SgEdgeLayer(std::move(buffer), std::move(desc_list), size, offset, channel_id);
}

SgEdgeLayer::SgEdgeLayer(std::shared_ptr<SgBuffer> &&buffer, DescriptorList &&desc_list,
        size_t size, size_t offset, ChannelId channel_id) :
    VdmaEdgeLayer(std::move(buffer), size, offset),
    m_desc_list(std::move(desc_list)),
    m_channel_id(channel_id)
{}

uint64_t SgEdgeLayer::dma_address() const
{
    return m_desc_list.dma_address();
}

uint16_t SgEdgeLayer::desc_page_size() const
{
    return m_desc_list.desc_page_size();
}

uint32_t SgEdgeLayer::descs_count() const
{
    return static_cast<uint32_t>(m_desc_list.count());
}

Expected<uint32_t> SgEdgeLayer::program_descriptors(size_t transfer_size,
    size_t desc_offset, size_t buffer_offset, uint32_t batch_size)
{
    CHECK_SUCCESS(m_desc_list.program(*get_mapped_buffer(), transfer_size, buffer_offset+m_offset, m_channel_id,
        static_cast<uint32_t>(desc_offset), batch_size, InterruptsDomain::NONE));
    return descriptors_in_buffer(transfer_size) * batch_size;
}

}
}