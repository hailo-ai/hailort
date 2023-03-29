/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file inter_context_buffer.hpp
 * @brief Manages inter-context buffer.
 */

#ifndef _HAILO_INTER_CONTEXT_BUFFER_HPP_
#define _HAILO_INTER_CONTEXT_BUFFER_HPP_

#include "hailo/expected.hpp"
#include "hailo/buffer.hpp"

#include "os/hailort_driver.hpp"
#include "vdma/memory/vdma_buffer.hpp"

#include "control_protocol.h"


namespace hailort
{

class InterContextBuffer final {
public:
    static Expected<InterContextBuffer> create(HailoRTDriver &driver, uint32_t transfer_size,
        uint16_t max_batch_size, vdma::ChannelId d2h_channel_id);

    hailo_status reprogram(uint16_t batch_size);
    Expected<Buffer> read();

    CONTROL_PROTOCOL__host_buffer_info_t get_host_buffer_info() const;

private:
    InterContextBuffer(std::unique_ptr<vdma::VdmaBuffer> &&buffer, uint32_t transfer_size, uint16_t batch_size);
    hailo_status set_dynamic_batch_size(uint16_t batch_size);

    static Expected<std::unique_ptr<vdma::VdmaBuffer>> create_sg_buffer(HailoRTDriver &driver,
        uint32_t transfer_size, uint16_t batch_size, vdma::ChannelId d2h_channel_id);
    static Expected<std::unique_ptr<vdma::VdmaBuffer>> create_ccb_buffer(HailoRTDriver &driver,
        uint32_t transfer_size, uint16_t batch_size);

    static bool should_use_ccb(HailoRTDriver &driver);

    std::unique_ptr<vdma::VdmaBuffer> m_buffer;
    const uint32_t m_transfer_size;
    const uint16_t m_max_batch_size;
    uint16_t m_dynamic_batch_size;
};

} /* namespace hailort */

#endif /* _HAILO_INTER_CONTEXT_BUFFER_HPP_ */