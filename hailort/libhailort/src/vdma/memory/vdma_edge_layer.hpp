/**
 * Copyright (c) 2019-2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_edge_layer.hpp
 * @brief Abstract layer representing a vdma edge layer (buffer that can be read/written to the device over vdma.)
 *        The buffer can be either non-continuous with attach descriptors list (SgEdgeLayer) or continuous buffer.
 **/

#ifndef _HAILO_VDMA_VDMA_EDGE_LAYER_HPP_
#define _HAILO_VDMA_VDMA_EDGE_LAYER_HPP_

#include "vdma/driver/hailort_driver.hpp"
#include "vdma/memory/descriptor_list.hpp"
#include "control_protocol.h"
#include "vdma/memory/vdma_buffer.hpp"

namespace hailort {
namespace vdma {

class VdmaEdgeLayer {
public:

    enum class Type {
        SCATTER_GATHER,
        CONTINUOUS
    };

    virtual ~VdmaEdgeLayer() = default;

    VdmaEdgeLayer(const VdmaEdgeLayer &) = delete;
    VdmaEdgeLayer(VdmaEdgeLayer &&) = default;
    VdmaEdgeLayer& operator=(const VdmaEdgeLayer &) = delete;
    VdmaEdgeLayer& operator=(VdmaEdgeLayer &&) = delete;

    virtual Type type() const = 0;
    virtual uint64_t dma_address() const = 0;
    virtual uint16_t desc_page_size() const = 0;
    virtual uint32_t descs_count() const = 0;

    size_t size() const
    {
        return m_size;
    }

    size_t backing_buffer_size() const
    {
        return m_buffer->size();
    }

    uint32_t descriptors_in_buffer(size_t buffer_size) const
    {
        assert(buffer_size < std::numeric_limits<uint32_t>::max());
        const auto page_size = desc_page_size();
        return static_cast<uint32_t>(DIV_ROUND_UP(buffer_size, page_size));
    }

    hailo_status read(void *buf_dst, size_t count, size_t offset);
    hailo_status write(const void *buf_src, size_t count, size_t offset);

    virtual Expected<uint32_t> program_descriptors(size_t transfer_size, InterruptsDomain last_desc_interrupts_domain,
        size_t desc_offset, size_t buffer_offset = 0, bool should_bind = false) = 0;

    CONTROL_PROTOCOL__host_buffer_info_t get_host_buffer_info(uint32_t transfer_size);
    static CONTROL_PROTOCOL__host_buffer_info_t get_host_buffer_info(Type type, uint64_t dma_address,
        uint16_t desc_page_size, uint32_t total_desc_count, uint32_t transfer_size);
protected:
    VdmaEdgeLayer(std::shared_ptr<VdmaBuffer> &&buffer, const size_t size, const size_t offset);

    std::shared_ptr<VdmaBuffer> m_buffer;
    const size_t m_size;
    const size_t m_offset;
};

} /* vdma */
} /* hailort */

#endif /* _HAILO_VDMA_VDMA_EDGE_LAYER_HPP_ */
