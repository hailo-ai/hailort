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
    std::unique_ptr<VdmaEdgeLayer> edge_layer_ptr;

    if (BufferType::SCATTER_GATHER == backing_buffer->type()) {
        auto sg_buffer = std::static_pointer_cast<SgBuffer>(backing_buffer);
        TRY(edge_layer_ptr, SgEdgeLayer::create_unique(std::move(sg_buffer), size, buffer_offset, driver,
            total_desc_count, desc_page_size, is_circular, channel_id));
    }
    else {
        // NOTE: Both CMA and SRAM buffers are contiguous in memory
        // and therefore both are held by ContinuousEdgeLayer.
        if (BufferType::CMA == backing_buffer->type()) {
            CHECK(total_desc_count >= driver.get_continuous_desc_params().min_descs_count, HAILO_INTERNAL_FAILURE,
                "Total descriptor count ({}) must be greater/equal than the minimum descriptor count ({})",
                total_desc_count, driver.get_continuous_desc_params().min_descs_count);
        }

        auto continuous_buffer = std::static_pointer_cast<ContinuousVdmaBuffer>(backing_buffer);
        TRY(edge_layer_ptr, ContinuousEdgeLayer::create_unique(std::move(continuous_buffer), size,
            buffer_offset, desc_page_size, total_desc_count));
    }

    return edge_layer_ptr;
}

CONTROL_PROTOCOL__host_buffer_info_t VdmaEdgeLayer::get_host_buffer_info(uint32_t transfer_size)
{
    return get_host_buffer_info(type(), dma_address(), desc_page_size(), descs_count(), transfer_size);
}

CONTROL_PROTOCOL__host_buffer_info_t VdmaEdgeLayer::get_host_buffer_info(DmaType type, uint64_t dma_address,
    uint16_t desc_page_size, uint32_t desc_count, uint32_t transfer_size)
{
    CONTROL_PROTOCOL__host_buffer_info_t buffer_info{};
    buffer_info.buffer_type = static_cast<uint8_t>((type == vdma::DmaType::SCATTER_GATHER) ?
        CONTROL_PROTOCOL__HOST_BUFFER_TYPE_EXTERNAL_DESC :
        CONTROL_PROTOCOL__HOST_BUFFER_TYPE_CCB);
    buffer_info.dma_address = dma_address;
    buffer_info.desc_page_size = desc_page_size;
    buffer_info.total_desc_count = desc_count;
    buffer_info.bytes_in_pattern = transfer_size;

    return buffer_info;
}

}
}
