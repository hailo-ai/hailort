/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file intermediate_buffer.hpp
 * @brief Manages intermediate buffers, including inter-context and ddr buffers.
 */

#ifndef _HAILO_INTERMEDIATE_BUFFER_HPP_
#define _HAILO_INTERMEDIATE_BUFFER_HPP_

#include "hailo/expected.hpp"
#include "hailo/buffer.hpp"

#include "vdma/driver/hailort_driver.hpp"
#include "vdma/memory/vdma_edge_layer.hpp"
#include "vdma/memory/descriptor_list.hpp"
#include "control_protocol.h"


namespace hailort
{

class IntermediateBuffer final {
public:

    enum class StreamingType {
        // Used for inter-context buffer. The buffer is not circular and the data is fetched in bursts.
        BURST,

        // Used for ddr-channel buffers. The buffer is circular and fetched continuously.
        CIRCULAR_CONTINUOS,
    };

    // The default value of max_desc_size (= vdma::MAX_SG_PAGE_SIZE) corresponds to the maximum descriptor size
    // supported by the sg dma.
    static Expected<IntermediateBuffer> create(HailoRTDriver &driver, uint32_t transfer_size,
        uint16_t max_batch_size, vdma::ChannelId d2h_channel_id, StreamingType streaming_type,
        std::shared_ptr<vdma::VdmaBuffer> buffer, size_t buffer_offset,
        uint16_t max_desc_size = vdma::MAX_SG_PAGE_SIZE);
    static Expected<std::shared_ptr<IntermediateBuffer>> create_shared(HailoRTDriver &driver, uint32_t transfer_size,
        uint16_t max_batch_size, vdma::ChannelId d2h_channel_id, StreamingType streaming_type,
        std::shared_ptr<vdma::VdmaBuffer> buffer, size_t buffer_offset,
        uint16_t max_desc_size = vdma::MAX_SG_PAGE_SIZE);

    // If size is 0, the entire buffer is read (based on the transfer size passed in the create function)
    Expected<Buffer> read(size_t size=0);
    CONTROL_PROTOCOL__host_buffer_info_t get_host_buffer_info() const;
    hailo_status reprogram_descriptors(size_t buffer_offset);
    uint32_t transfer_size() const;

private:
    IntermediateBuffer(std::unique_ptr<vdma::VdmaEdgeLayer> &&buffer, uint32_t transfer_size,
        StreamingType streaming_type, uint16_t batch_size);

    static Expected<std::unique_ptr<vdma::VdmaEdgeLayer>> create_sg_edge_layer(std::shared_ptr<vdma::VdmaBuffer> buffer,
        size_t buffer_offset, HailoRTDriver &driver, uint32_t transfer_size, uint16_t batch_size,
        vdma::ChannelId d2h_channel_id, bool is_circular, uint16_t max_desc_size);
    static Expected<std::unique_ptr<vdma::VdmaEdgeLayer>> create_ccb_edge_layer(std::shared_ptr<vdma::VdmaBuffer> buffer,
        size_t buffer_offset, HailoRTDriver &driver, uint32_t transfer_size, uint16_t batch_size, bool is_circular);
    static Expected<std::unique_ptr<vdma::VdmaEdgeLayer>> create_edge_layer(std::shared_ptr<vdma::VdmaBuffer> buffer,
        size_t buffer_offset, HailoRTDriver &driver, uint32_t transfer_size, uint16_t max_batch_size,
        vdma::ChannelId d2h_channel_id, StreamingType streaming_type, uint16_t max_desc_size);

    std::unique_ptr<vdma::VdmaEdgeLayer> m_edge_layer;
    const uint32_t m_transfer_size;
    const StreamingType m_streaming_type;
    uint16_t m_dynamic_batch_size;
};

} /* namespace hailort */

#endif /* _HAILO_INTERMEDIATE_BUFFER_HPP_ */