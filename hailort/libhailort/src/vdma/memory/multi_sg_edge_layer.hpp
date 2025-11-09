/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file multi_sg_edge_layer.hpp
 * @brief Multi-descriptor scatter-gather vdma edge layer for double buffering.
 *        This layer holds multiple descriptor lists and can switch between them
 *        in a round-robin fashion for cache descriptor reprogramming without
 *        interrupting the active descriptor list.
 **/

#ifndef _HAILO_VDMA_MULTI_SG_EDGE_LAYER_HPP_
#define _HAILO_VDMA_MULTI_SG_EDGE_LAYER_HPP_

#include "vdma/driver/hailort_driver.hpp"
#include "vdma/memory/vdma_edge_layer.hpp"
#include "vdma/memory/sg_buffer.hpp"
#include "vdma/memory/descriptor_list.hpp"
#include "vdma/memory/mapped_buffer.hpp"

#include <vector>

namespace hailort {
namespace vdma {

class MultiSgEdgeLayer final : public VdmaEdgeLayer {
public:
    static Expected<MultiSgEdgeLayer> create(std::shared_ptr<SgBuffer> &&buffer, size_t size, size_t offset,
        HailoRTDriver &driver, uint32_t desc_count, uint16_t desc_page_size, bool is_circular, 
        ChannelId channel_id, uint32_t num_descriptor_lists = 1);

    virtual ~MultiSgEdgeLayer() = default;

    MultiSgEdgeLayer(const MultiSgEdgeLayer &) = delete;
    MultiSgEdgeLayer(MultiSgEdgeLayer &&) = default;
    MultiSgEdgeLayer& operator=(const MultiSgEdgeLayer &) = delete;
    MultiSgEdgeLayer& operator=(MultiSgEdgeLayer &&) = delete;

    virtual Type type() const override
    {
        return Type::SCATTER_GATHER;
    }

    // TODO : HRT-18364 : remove these methods
    virtual uint64_t dma_address() const override;
    virtual uint16_t desc_page_size() const override;
    virtual uint32_t descs_count() const override;

    virtual Expected<uint32_t> program_descriptors(size_t transfer_size,
        size_t desc_offset, size_t buffer_offset = 0, uint32_t batch_size = 1) override;

    void switch_to_next_descriptor_list();
    uint32_t get_active_descriptor_list_index() const;
    uint32_t get_num_descriptor_lists() const;
    
    uint64_t get_descriptor_list_dma_address(uint32_t index) const;
    
    std::vector<uint64_t> get_all_dma_addresses() const;

private:
    MultiSgEdgeLayer(std::shared_ptr<SgBuffer> &&buffer, std::vector<DescriptorList> &&desc_lists,
        size_t size, size_t offset, ChannelId channel_id);

    vdma::MappedBufferPtr get_mapped_buffer()
    {
        return std::static_pointer_cast<SgBuffer>(m_buffer)->get_mapped_buffer();
    }

    uint32_t get_next_descriptor_list_index() const;

    std::vector<DescriptorList> m_desc_lists;
    uint32_t m_active_desc_list_index;
    const ChannelId m_channel_id;
};

} /* vdma */
} /* hailort */

#endif /* _HAILO_VDMA_MULTI_SG_EDGE_LAYER_HPP_ */
