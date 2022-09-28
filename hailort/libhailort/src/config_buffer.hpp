/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file config_buffer.hpp
 * @brief Manages configuration vdma buffer. The configuration buffer contains nn-configurations in a specific
 *        hw format (ccw).
 */

#ifndef _HAILO_CONFIG_BUFFER_HPP_
#define _HAILO_CONFIG_BUFFER_HPP_

#include "vdma/vdma_buffer.hpp"

namespace hailort {


class ConfigBuffer final
{
public:
    static Expected<ConfigBuffer> create(HailoRTDriver &driver, vdma::ChannelId channel_id,
        const std::vector<uint32_t> &cfg_sizes);

    // Write data to config channel
    hailo_status write(const void *data, size_t data_size);

    // Program the descriptors for the data written so far
    Expected<uint32_t> program_descriptors();

    size_t get_current_buffer_size();

    /* Get all the config size. It's not the same as the VdmaBuffer::size() 
    since we might add NOPs to the data (Pre-fetch mode) */
    size_t get_total_cfg_size();

    uint16_t desc_page_size() const;
    CONTROL_PROTOCOL__config_channel_info_t get_config_channel_info() const;

private:
    ConfigBuffer(std::unique_ptr<vdma::VdmaBuffer> &&buffer, vdma::ChannelId channel_id, size_t total_buffer_size);

    static Expected<std::unique_ptr<vdma::VdmaBuffer>> create_sg_buffer(HailoRTDriver &driver,
        uint8_t vdma_channel_index, const std::vector<uint32_t> &cfg_sizes);
    static Expected<std::unique_ptr<vdma::VdmaBuffer>> create_ccb_buffer(HailoRTDriver &driver,
        uint32_t buffer_size);

    static bool should_use_ccb(HailoRTDriver &driver);

    std::unique_ptr<vdma::VdmaBuffer> m_buffer;
    vdma::ChannelId m_channel_id;
    const size_t m_total_buffer_size; 
    size_t m_acc_buffer_offset;
    uint32_t m_acc_desc_count;
    size_t m_current_buffer_size;
};

} /* hailort */

#endif /* _HAILO_CONFIG_BUFFER_HPP_ */