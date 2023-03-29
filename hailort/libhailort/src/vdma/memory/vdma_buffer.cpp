/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_buffer.cpp
 * @brief vdma buffer.
 **/

#include "vdma_buffer.hpp"
#include "control_protocol.h"

namespace hailort {
namespace vdma {

CONTROL_PROTOCOL__host_buffer_info_t VdmaBuffer::get_host_buffer_info(uint32_t transfer_size)
{
    return get_host_buffer_info(type(), dma_address(), desc_page_size(), descs_count(), transfer_size);
}

CONTROL_PROTOCOL__host_buffer_info_t VdmaBuffer::get_host_buffer_info(Type type, uint64_t dma_address,
    uint16_t desc_page_size, uint32_t desc_count, uint32_t transfer_size)
{
    CONTROL_PROTOCOL__host_buffer_info_t buffer_info{};
    buffer_info.buffer_type = static_cast<uint8_t>((type == vdma::VdmaBuffer::Type::SCATTER_GATHER) ?
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