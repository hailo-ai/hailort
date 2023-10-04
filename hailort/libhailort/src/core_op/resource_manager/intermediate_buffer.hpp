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

#include "os/hailort_driver.hpp"
#include "vdma/memory/vdma_buffer.hpp"

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

    static Expected<IntermediateBuffer> create(HailoRTDriver &driver, uint32_t transfer_size,
        uint16_t max_batch_size, vdma::ChannelId d2h_channel_id, StreamingType streaming_type);

    Expected<Buffer> read();
    CONTROL_PROTOCOL__host_buffer_info_t get_host_buffer_info() const;

private:
    IntermediateBuffer(std::unique_ptr<vdma::VdmaBuffer> &&buffer, uint32_t transfer_size, uint16_t batch_size);

    static Expected<std::unique_ptr<vdma::VdmaBuffer>> create_sg_buffer(HailoRTDriver &driver,
        uint32_t transfer_size, uint16_t batch_size, vdma::ChannelId d2h_channel_id, bool is_circular);
    static Expected<std::unique_ptr<vdma::VdmaBuffer>> create_ccb_buffer(HailoRTDriver &driver,
        uint32_t transfer_size, uint16_t batch_size, bool is_circular);
    static Expected<std::unique_ptr<vdma::VdmaBuffer>> create_buffer(HailoRTDriver &driver, uint32_t transfer_size,
        uint16_t max_batch_size, vdma::ChannelId d2h_channel_id, StreamingType streaming_type);

    static bool should_use_ccb(HailoRTDriver &driver, StreamingType streaming_type);

    std::unique_ptr<vdma::VdmaBuffer> m_buffer;
    const uint32_t m_transfer_size;
    uint16_t m_dynamic_batch_size;
};

} /* namespace hailort */

#endif /* _HAILO_INTERMEDIATE_BUFFER_HPP_ */