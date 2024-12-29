/**
 * Copyright (c) 2019-2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file config_buffer.hpp
 * @brief Manages configuration vdma buffer. The configuration buffer contains nn-configurations in a specific
 *        hw format (ccw).
 */

#ifndef _HAILO_CONFIG_BUFFER_HPP_
#define _HAILO_CONFIG_BUFFER_HPP_

#include "hailo/buffer.hpp"

#include "vdma/memory/vdma_edge_layer.hpp"


namespace hailort {

#define CCW_BYTES_IN_WORD (4)
#define CCW_DATA_OFFSET (CCW_BYTES_IN_WORD * 2)
#define CCW_HEADER_SIZE (CCW_DATA_OFFSET)

class ConfigBuffer final
{
public:
    static Expected<ConfigBuffer> create(HailoRTDriver &driver, vdma::ChannelId channel_id,
        const std::vector<uint32_t> &bursts_sizes);

    // Write data to config channel
    hailo_status write(const MemoryView &data);

    // Program the descriptors for the data written so far
    Expected<uint32_t> program_descriptors();

    // On prefetch mode, we need to pad the config buffer with nops BEFORE the last write.
    hailo_status pad_with_nops();

    // Amount of bytes left to write into the buffer.
    size_t size_left() const;

    // Amount of bytes already written.
    size_t get_current_buffer_size() const;

    uint16_t desc_page_size() const;
    vdma::ChannelId channel_id() const;
    CONTROL_PROTOCOL__host_buffer_info_t get_host_buffer_info() const;

private:
    ConfigBuffer(std::unique_ptr<vdma::VdmaEdgeLayer> &&buffer, vdma::ChannelId channel_id, size_t total_buffer_size);

    hailo_status write_inner(const MemoryView &data);

    static Expected<std::unique_ptr<vdma::VdmaEdgeLayer>> create_sg_buffer(HailoRTDriver &driver,
        vdma::ChannelId channel_id, const std::vector<uint32_t> &cfg_sizes);
    static Expected<std::unique_ptr<vdma::VdmaEdgeLayer>> create_ccb_buffer(HailoRTDriver &driver,
        uint32_t buffer_size);
    static Expected<std::unique_ptr<vdma::VdmaEdgeLayer>> create_buffer(HailoRTDriver &driver, vdma::ChannelId channel_id,
        const std::vector<uint32_t> &cfg_sizes, const uint32_t buffer_size);

    static bool should_use_ccb(HailoRTDriver &driver);

    std::unique_ptr<vdma::VdmaEdgeLayer> m_buffer;
    vdma::ChannelId m_channel_id;
    const size_t m_total_buffer_size;
    size_t m_acc_buffer_offset;
    uint32_t m_acc_desc_count;
    size_t m_current_buffer_size;
};

} /* hailort */

#endif /* _HAILO_CONFIG_BUFFER_HPP_ */