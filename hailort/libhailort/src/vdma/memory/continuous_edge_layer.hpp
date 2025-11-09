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

#include "vdma/driver/hailort_driver.hpp"
#include "vdma/memory/vdma_edge_layer.hpp"
#include "vdma/memory/continuous_buffer.hpp"


namespace hailort {
namespace vdma {

class ContinuousEdgeLayer final : public VdmaEdgeLayer {
public:
    static Expected<ContinuousEdgeLayer> create(std::shared_ptr<ContinuousBuffer> &&buffer, size_t size, size_t offset,
        uint16_t page_size, uint32_t num_pages);

    virtual ~ContinuousEdgeLayer() = default;

    ContinuousEdgeLayer(const ContinuousEdgeLayer &) = delete;
    ContinuousEdgeLayer(ContinuousEdgeLayer &&) = default;
    ContinuousEdgeLayer& operator=(const ContinuousEdgeLayer &) = delete;
    ContinuousEdgeLayer& operator=(ContinuousEdgeLayer &&) = delete;

    virtual Type type() const override
    {
        return Type::CONTINUOUS;
    }

    virtual uint64_t dma_address() const override;
    virtual uint16_t desc_page_size() const override;
    virtual uint32_t descs_count() const override;

    virtual Expected<uint32_t> program_descriptors(size_t transfer_size, size_t desc_offset,
        size_t buffer_offset = 0, uint32_t batch_size = 1) override;

private:
    ContinuousEdgeLayer(std::shared_ptr<ContinuousBuffer> &&buffer, size_t size, size_t offset,
        uint16_t page_size, uint32_t num_pages);

    const uint16_t m_page_size;
    const uint32_t m_num_pages;
};

}; /* namespace vdma */
}; /* namespace hailort */

#endif /* _HAILO_VDMA_CONTINUOUS_EDGE_LAYER_HPP_ */
