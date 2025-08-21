/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_edge_layer.cpp
 * @brief vdma edge layer.
 **/

#include "vdma_edge_layer.hpp"
#include "control_protocol.h"
#include "sg_edge_layer.hpp"
#include "continuous_edge_layer.hpp"

namespace hailort {
namespace vdma {


Expected<std::unique_ptr<VdmaEdgeLayer>> VdmaEdgeLayer::create(HailoRTDriver &driver,
    std::shared_ptr<vdma::VdmaBuffer> backing_buffer, size_t buffer_offset, size_t size,
    uint16_t desc_page_size, uint32_t total_desc_count, bool is_circular, ChannelId channel_id)
{
    switch (backing_buffer->type()) {
    case Type::SCATTER_GATHER:
    {
        auto sg_buffer = std::static_pointer_cast<SgBuffer>(backing_buffer);
        TRY(auto sg_edge_layer, SgEdgeLayer::create(std::move(sg_buffer), size, buffer_offset, driver,
            total_desc_count, desc_page_size, is_circular, channel_id));

        auto edge_layer_ptr = make_unique_nothrow<SgEdgeLayer>(std::move(sg_edge_layer));
        CHECK_NOT_NULL(edge_layer_ptr, HAILO_OUT_OF_HOST_MEMORY);

        return std::unique_ptr<VdmaEdgeLayer>(std::move(edge_layer_ptr));
    }
    case Type::CONTINUOUS:
    {
        auto continuous_buffer = std::static_pointer_cast<ContinuousBuffer>(backing_buffer);

        CHECK(total_desc_count >= driver.get_ccb_desc_params().min_descs_count, HAILO_INTERNAL_FAILURE,
            "Total descriptor count ({}) must be greater/equal than the minimum descriptor count ({})",
            total_desc_count, driver.get_ccb_desc_params().min_descs_count);
        TRY(auto continuous_edge_layer, ContinuousEdgeLayer::create(std::move(continuous_buffer), size, buffer_offset,
            desc_page_size, total_desc_count));
        auto edge_layer_ptr = make_unique_nothrow<ContinuousEdgeLayer>(std::move(continuous_edge_layer));
        CHECK_NOT_NULL(edge_layer_ptr, HAILO_OUT_OF_HOST_MEMORY);

        return std::unique_ptr<VdmaEdgeLayer>(std::move(edge_layer_ptr));
    }
    }

    LOGGER__ERROR("Unsupported buffer type: {}", static_cast<int>(backing_buffer->type()));
    return make_unexpected(HAILO_INTERNAL_FAILURE);
}


VdmaEdgeLayer::VdmaEdgeLayer(std::shared_ptr<VdmaBuffer> &&buffer, const size_t size, const size_t offset) :
    m_buffer(std::move(buffer)),
    m_size(size),
    m_offset(offset)
{}

CONTROL_PROTOCOL__host_buffer_info_t VdmaEdgeLayer::get_host_buffer_info(uint32_t transfer_size)
{
    return get_host_buffer_info(type(), dma_address(), desc_page_size(), descs_count(), transfer_size);
}

CONTROL_PROTOCOL__host_buffer_info_t VdmaEdgeLayer::get_host_buffer_info(Type type, uint64_t dma_address,
    uint16_t desc_page_size, uint32_t desc_count, uint32_t transfer_size)
{
    CONTROL_PROTOCOL__host_buffer_info_t buffer_info{};
    buffer_info.buffer_type = static_cast<uint8_t>((type == vdma::VdmaEdgeLayer::Type::SCATTER_GATHER) ?
        CONTROL_PROTOCOL__HOST_BUFFER_TYPE_EXTERNAL_DESC :
        CONTROL_PROTOCOL__HOST_BUFFER_TYPE_CCB);
    buffer_info.dma_address = dma_address;
    buffer_info.desc_page_size = desc_page_size;
    buffer_info.total_desc_count = desc_count;
    buffer_info.bytes_in_pattern = transfer_size;

    return buffer_info;
}

hailo_status VdmaEdgeLayer::read(void *buf_dst, size_t count, size_t offset)
{
    return m_buffer->read(buf_dst, count, m_offset + offset);
}
hailo_status VdmaEdgeLayer::write(const void *buf_src, size_t count, size_t offset)
{
    return m_buffer->write(buf_src, count, m_offset + offset);
}

}
}