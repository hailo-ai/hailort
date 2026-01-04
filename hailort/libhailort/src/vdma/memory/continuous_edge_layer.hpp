/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file continuous_edge_layer.hpp
 * @brief Continuous physical vdma edge layer.
 **/

#ifndef _HAILO_VDMA_CONTINUOUS_EDGE_LAYER_HPP_
#define _HAILO_VDMA_CONTINUOUS_EDGE_LAYER_HPP_

#include "vdma/memory/vdma_edge_layer.hpp"


namespace hailort {
namespace vdma {

class ContinuousEdgeLayer final : public VdmaEdgeLayer {
public:
    static Expected<std::unique_ptr<VdmaEdgeLayer>> create_unique(std::shared_ptr<ContinuousVdmaBuffer> &&buffer,
        size_t size, size_t offset, uint16_t page_size, uint32_t num_pages);

    virtual ~ContinuousEdgeLayer() = default;

    ContinuousEdgeLayer(ContinuousEdgeLayer &&) = default;

    virtual DmaType type() const override
    {
        return DmaType::CONTINUOUS;
    }

    uint64_t dma_address() const override
    {
        return (std::dynamic_pointer_cast<ContinuousVdmaBuffer>(m_buffer))->dma_address() + m_offset;
    }

    uint16_t desc_page_size() const override
    {
        return m_page_size;
    }

    uint32_t descs_count() const override
    {
        return m_num_pages;
    }

    Expected<uint32_t> program_descriptors(size_t transfer_size,
        size_t /* desc_offset */, size_t /* buffer_offset */, uint32_t /* batch_size */) override
    {
        // No need to program descriptors in continuous mode.
        return descriptors_in_buffer(transfer_size);
    }

private:
    ContinuousEdgeLayer(std::shared_ptr<ContinuousVdmaBuffer> &&buffer, size_t size, size_t offset, uint16_t page_size,
        uint32_t num_pages) :
        VdmaEdgeLayer(std::move(buffer), size, offset), m_page_size(page_size), m_num_pages(num_pages)
    {}

    const uint16_t m_page_size;
    const uint32_t m_num_pages;
};

}; /* namespace vdma */
}; /* namespace hailort */

#endif /* _HAILO_VDMA_CONTINUOUS_EDGE_LAYER_HPP_ */
