/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file intermediate_buffer.hpp
 * @brief Manages intermediate buffer for inter-context or ddr channels.
 */

#ifndef _HAILO_INTERMEDIATE_BUFFER_HPP_
#define _HAILO_INTERMEDIATE_BUFFER_HPP_

#include "os/hailort_driver.hpp"
#include "vdma/vdma_buffer.hpp"
#include "hailo/expected.hpp"
#include "hailo/buffer.hpp"
#include "control_protocol.h"


namespace hailort
{

class IntermediateBuffer final {
public:
    enum class ChannelType {
        INTER_CONTEXT,
        DDR
    };

    static Expected<IntermediateBuffer> create(HailoRTDriver &driver,
        ChannelType channel_type, const uint32_t transfer_size, const uint16_t batch_size);
    // TODO: create two subclasses - one for ddr buffers and one for intercontext buffers (HRT-6784)

    hailo_status program_inter_context();
    // This is to be called after program_inter_context
    hailo_status reprogram_inter_context(uint16_t batch_size);
    // Returns the amount of programed descriptors
    Expected<uint16_t> program_ddr();
    uint64_t dma_address() const;
    uint32_t descriptors_in_frame() const;
    uint16_t desc_page_size() const;
    uint32_t descs_count() const;
    uint8_t depth();
    Expected<Buffer> read();

    CONTROL_PROTOCOL__host_buffer_info_t get_host_buffer_info() const;

private:
    IntermediateBuffer(std::unique_ptr<vdma::VdmaBuffer> &&buffer, uint32_t transfer_size, uint16_t batch_size);
    hailo_status set_dynamic_batch_size(uint16_t batch_size);

    static Expected<std::unique_ptr<vdma::VdmaBuffer>> create_sg_buffer(HailoRTDriver &driver,
        const uint32_t transfer_size, const uint16_t batch_size);
    static Expected<std::unique_ptr<vdma::VdmaBuffer>> create_ccb_buffer(HailoRTDriver &driver,
        const uint32_t transfer_size, const uint16_t batch_size);

    static bool should_use_ccb(HailoRTDriver &driver, ChannelType channel_type);

    std::unique_ptr<vdma::VdmaBuffer> m_buffer;
    const uint32_t m_transfer_size;
    const uint16_t m_max_batch_size;
    uint16_t m_dynamic_batch_size;
};

} /* namespace hailort */

#endif /* _HAILO_INTERMEDIATE_BUFFER_HPP_ */