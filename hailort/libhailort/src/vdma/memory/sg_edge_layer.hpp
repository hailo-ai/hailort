/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file sg_edge_layer.hpp
 * @brief Scatter-gather vdma buffer, from the user-mode point of view the buffer is continuous,
 *        but not from the physical-memory point of view.
 *        The sg buffer contains 2 parts:
 *              - MappedBuffer - the actual buffer stores the data.
 *              - Descriptors list - each descriptor points to a single "dma page" in the MappedBuffer.
 *        The hw accept the descriptors list address and parses it to get the actual data.
 **/

#ifndef _HAILO_VDMA_SG_EDGE_LAYER_HPP_
#define _HAILO_VDMA_SG_EDGE_LAYER_HPP_

#include "vdma/driver/hailort_driver.hpp"
#include "vdma/memory/vdma_edge_layer.hpp"
#include "vdma/memory/sg_buffer.hpp"
#include "vdma/memory/descriptor_list.hpp"
#include "vdma/memory/mapped_buffer.hpp"


namespace hailort {
namespace vdma {

class SgEdgeLayer final : public VdmaEdgeLayer {
public:
    static Expected<SgEdgeLayer> create(std::shared_ptr<SgBuffer> &&buffer, size_t size, size_t offset,
    HailoRTDriver &driver, uint32_t desc_count, uint16_t desc_page_size, bool is_circular, ChannelId channel_id);

    virtual ~SgEdgeLayer() = default;

    SgEdgeLayer(const SgEdgeLayer &) = delete;
    SgEdgeLayer(SgEdgeLayer &&) = default;
    SgEdgeLayer& operator=(const SgEdgeLayer &) = delete;
    SgEdgeLayer& operator=(SgEdgeLayer &&) = delete;

    virtual Type type() const override
    {
        return Type::SCATTER_GATHER;
    }

    virtual uint64_t dma_address() const override;
    virtual uint16_t desc_page_size() const override;
    virtual uint32_t descs_count() const override;

    virtual Expected<uint32_t> program_descriptors(size_t transfer_size, size_t desc_offset,
        size_t buffer_offset = 0, uint32_t batch_size = 1) override;

private:
    SgEdgeLayer(std::shared_ptr<SgBuffer> &&buffer, DescriptorList &&desc_list,
        size_t size, size_t offset, ChannelId channel_id);

    vdma::MappedBufferPtr get_mapped_buffer()
    {
        return std::static_pointer_cast<SgBuffer>(m_buffer)->get_mapped_buffer();
    }

    DescriptorList m_desc_list;
    const ChannelId m_channel_id;
};

} /* vdma */
} /* hailort */

#endif /* _HAILO_VDMA_SG_EDGE_LAYER_HPP_ */
