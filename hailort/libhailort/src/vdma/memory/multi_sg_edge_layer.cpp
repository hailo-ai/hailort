/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file multi_sg_edge_layer.cpp
 * @brief Multi-descriptor scatter-gather vdma edge layer for double buffering.
 **/

#include "vdma/memory/multi_sg_edge_layer.hpp"
#include "vdma/channel/channel_id.hpp"

namespace hailort {
namespace vdma {

Expected<MultiSgEdgeLayer> MultiSgEdgeLayer::create(std::shared_ptr<SgBuffer> &&buffer, size_t size, size_t offset,
    HailoRTDriver &driver, uint32_t desc_count, uint16_t desc_page_size, bool is_circular, 
    ChannelId channel_id, uint32_t num_descriptor_lists)
{
    CHECK(num_descriptor_lists > 0, HAILO_INVALID_ARGUMENT,
        "Number of descriptor lists must be at least 1, got {}", num_descriptor_lists);
    CHECK(size <= (desc_count * desc_page_size), HAILO_INTERNAL_FAILURE,
        "Requested buffer size {} must be smaller or equal to {}", size, (desc_count * desc_page_size));
    CHECK((size % desc_page_size) == 0, HAILO_INTERNAL_FAILURE,
        "MultiSgEdgeLayer size must be a multiple of descriptors page size (size {})", size);
    CHECK((offset % desc_page_size) == 0, HAILO_INTERNAL_FAILURE,
        "MultiSgEdgeLayer offset must be a multiple of descriptors page size (offset {}. Page size {})", offset, desc_page_size);

    CHECK(buffer->size() >= (offset + size), HAILO_INTERNAL_FAILURE,
        "Edge layer is not fully inside the connected buffer. buffer size is {} while edge layer offset {} and size {}",
        buffer->size(), offset, size);

    std::vector<DescriptorList> desc_lists;
    desc_lists.reserve(num_descriptor_lists);

    for (uint32_t i = 0; i < num_descriptor_lists; i++) {
        TRY(auto desc_list, DescriptorList::create(desc_count, desc_page_size, is_circular, driver));

        auto status = desc_list.program(*(buffer->get_mapped_buffer()), size, offset, channel_id);
        CHECK_SUCCESS(status);

        desc_lists.emplace_back(std::move(desc_list));
    }

    assert((desc_count * desc_page_size) <= std::numeric_limits<uint32_t>::max());

    return MultiSgEdgeLayer(std::move(buffer), std::move(desc_lists), size, offset, channel_id);
}

MultiSgEdgeLayer::MultiSgEdgeLayer(std::shared_ptr<SgBuffer> &&buffer, std::vector<DescriptorList> &&desc_lists,
        size_t size, size_t offset, ChannelId channel_id) :
    VdmaEdgeLayer(std::move(buffer), size, offset),
    m_desc_lists(std::move(desc_lists)),
    m_active_desc_list_index(0),
    m_channel_id(channel_id)
{
    assert(!m_desc_lists.empty());
}

uint64_t MultiSgEdgeLayer::dma_address() const
{
    assert(m_desc_lists.size() == 1);
    return m_desc_lists[0].dma_address();
}

uint16_t MultiSgEdgeLayer::desc_page_size() const
{
    return m_desc_lists[m_active_desc_list_index].desc_page_size();
}

uint32_t MultiSgEdgeLayer::descs_count() const
{
    return static_cast<uint32_t>(m_desc_lists[m_active_desc_list_index].count());
}

Expected<uint32_t> MultiSgEdgeLayer::program_descriptors(size_t transfer_size,
    size_t desc_offset, size_t buffer_offset, uint32_t batch_size)
{
    // Program the next descriptor list (for single list, next == current)
    uint32_t next_desc_list_index = get_next_descriptor_list_index();
    CHECK_SUCCESS(m_desc_lists[next_desc_list_index].program(*get_mapped_buffer(), transfer_size, buffer_offset, m_channel_id,
        static_cast<uint32_t>(desc_offset), batch_size,
        InterruptsDomain::NONE));

    m_active_desc_list_index = next_desc_list_index;

    return descriptors_in_buffer(transfer_size) * batch_size;
}

void MultiSgEdgeLayer::switch_to_next_descriptor_list()
{
    if (m_desc_lists.size() > 1) {
        m_active_desc_list_index = get_next_descriptor_list_index();
    }
}

uint32_t MultiSgEdgeLayer::get_active_descriptor_list_index() const
{
    return m_active_desc_list_index;
}

uint32_t MultiSgEdgeLayer::get_num_descriptor_lists() const
{
    return static_cast<uint32_t>(m_desc_lists.size());
}

uint64_t MultiSgEdgeLayer::get_descriptor_list_dma_address(uint32_t index) const
{
    CHECK(index < m_desc_lists.size(), HAILO_INVALID_ARGUMENT,
        "Descriptor list index {} out of range (max {})", index, m_desc_lists.size() - 1);
    return m_desc_lists[index].dma_address();
}

std::vector<uint64_t> MultiSgEdgeLayer::get_all_dma_addresses() const
{
    std::vector<uint64_t> addresses;
    addresses.reserve(m_desc_lists.size());
    
    for (const auto &desc_list : m_desc_lists) {
        addresses.push_back(desc_list.dma_address());
    }

    return addresses;
}

uint32_t MultiSgEdgeLayer::get_next_descriptor_list_index() const
{
    return (m_active_desc_list_index + 1) % static_cast<uint32_t>(m_desc_lists.size());
}

}
}
