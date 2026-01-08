/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file sg_edge_layer.cpp
 * @brief Scatter-gather vdma edge layer.
 **/

#include "vdma/memory/sg_edge_layer.hpp"


namespace hailort {
namespace vdma {

Expected<std::unique_ptr<VdmaEdgeLayer>> SgEdgeLayer::create_unique(std::shared_ptr<SgBuffer> &&buffer, size_t size, size_t offset,
    HailoRTDriver &driver, uint32_t desc_count, uint16_t desc_page_size, bool is_circular, ChannelId channel_id)
{
    CHECK(size <= (desc_count * desc_page_size), HAILO_INTERNAL_FAILURE,
        "Requested buffer size {} must be smaller or equal to {}", size, (desc_count * desc_page_size));

    CHECK((size % desc_page_size) == 0, HAILO_INTERNAL_FAILURE,
        "SgEdgeLayer size aligned to desc_page_size (size: {}, page-size: {})", size, desc_page_size);

    CHECK((offset % desc_page_size) == 0, HAILO_INTERNAL_FAILURE,
        "SgEdgeLayer offset not aligned to desc_page_size (offset: {}, page-size: {})", offset, desc_page_size);

    CHECK(buffer->size() >= (offset + size), HAILO_INTERNAL_FAILURE,
        "Edge-layer (size: {}, offset: {}) does not fit in buffer (size: {})", size, offset, buffer->size());

    CHECK((desc_count * desc_page_size) <= std::numeric_limits<uint32_t>::max(), HAILO_INTERNAL_FAILURE,
        "Edge layer size too large: descs_count: {}, desc_page_size: {}", desc_count, desc_page_size);

    TRY(auto desc_list, DescriptorList::create(desc_count, desc_page_size, is_circular, driver));
    auto edge_layer = SgEdgeLayer(std::move(buffer), std::move(desc_list), size, offset, channel_id);
    auto edge_layer_ptr = make_unique_nothrow<SgEdgeLayer>(std::move(edge_layer));
    CHECK_NOT_NULL(edge_layer_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::unique_ptr<VdmaEdgeLayer>(std::move(edge_layer_ptr));
}

Expected<uint32_t> SgEdgeLayer::program_descriptors(size_t transfer_size,
    size_t desc_offset, size_t buffer_offset, uint32_t batch_size)
{
    CHECK_SUCCESS(m_desc_list.program(get_mapped_buffer(), transfer_size, buffer_offset+m_offset, m_channel_id,
        static_cast<uint32_t>(desc_offset), batch_size, InterruptsDomain::NONE));
    return descriptors_in_buffer(transfer_size) * batch_size;
}

}
}
