/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file config_buffer.hpp
 * @brief Manages configuration vdma buffer. The configuration buffer contains nn-configurations in a specific
 *        hw format (ccw).
 * There are 2 types of config buffers:
 * 1. CopiedConfigBuffer - Used when shared_weights is off - in this flow we allocate an aligned buffer
 *      + copy the CCWs to it before transferring the data to the chip.
 * 2. ZeroCopyConfigBuffer - Used when shared_weights is on - in this flow we transfer the data directly 
 *      from the memory, without performing a memcpy (can be done since the data is already aligned).
 */

#ifndef _HAILO_CONFIG_BUFFER_HPP_
#define _HAILO_CONFIG_BUFFER_HPP_

#include "hailo/buffer.hpp"

#include "vdma/memory/vdma_edge_layer.hpp"
#include "vdma/memory/buffer_requirements.hpp"
#include "hef/core_op_metadata.hpp"


namespace hailort {

#define CCW_BYTES_IN_WORD (4)
#define CCW_DATA_OFFSET (CCW_BYTES_IN_WORD * 2)
#define CCW_HEADER_SIZE (CCW_DATA_OFFSET)
#define DEFAULT_PROGRAM_BATCH_SIZE (1)
#define NOPS_TRANSFERS_PER_ALIGNED_CCWS_TRANSFER (1)

class ConfigBuffer {
public:
    vdma::ChannelId channel_id() const { return m_channel_id; }

    virtual CONTROL_PROTOCOL__host_buffer_info_t get_host_buffer_info() const = 0;

protected:
    ConfigBuffer(vdma::ChannelId channel_id);

    static Expected<vdma::BufferSizesRequirements> get_sg_buffer_requirements(const std::vector<uint32_t> &cfg_sizes,
        const DescSizesParams &desc_sizes_params);

    vdma::ChannelId m_channel_id;
    uint32_t m_acc_desc_count;

};

class CopiedConfigBuffer final : public ConfigBuffer {
public:
    static Expected<CopiedConfigBuffer> create(HailoRTDriver &driver, vdma::ChannelId channel_id,
        const ConfigBufferInfo &config_buffer_info);

    static Expected<vdma::BufferSizesRequirements> get_buffer_requirements(const ConfigBufferInfo &config_buffer_info,
        HailoRTDriver::DmaType dma_type, const DescSizesParams &desc_sizes_params);

    // Write data to config channel
    hailo_status write(const MemoryView &data);

    // On prefetch mode, we need to pad the config buffer with nops BEFORE the last write.
    hailo_status pad_with_nops();

    // Program the descriptors for the data written so far
    Expected<uint32_t> program_descriptors();

    // Amount of bytes left to write into the buffer.
    size_t size_left() const;

    // Amount of bytes already written.
    size_t get_current_buffer_size() const;

    static bool should_use_ccb(HailoRTDriver::DmaType driver);

    uint16_t desc_page_size() const;

    CONTROL_PROTOCOL__host_buffer_info_t get_host_buffer_info() const;

private:
    CopiedConfigBuffer(std::unique_ptr<vdma::VdmaEdgeLayer> &&buffer, vdma::ChannelId channel_id, size_t total_buffer_size);

    hailo_status write_inner(const MemoryView &data);

    static Expected<std::unique_ptr<vdma::VdmaEdgeLayer>> create_sg_buffer(HailoRTDriver &driver,
        vdma::ChannelId channel_id, const std::vector<uint32_t> &cfg_sizes);
    static Expected<std::unique_ptr<vdma::VdmaEdgeLayer>> create_ccb_buffer(HailoRTDriver &driver,
        uint32_t buffer_size);

    static Expected<std::unique_ptr<vdma::VdmaEdgeLayer>> create_buffer(HailoRTDriver &driver, vdma::ChannelId channel_id,
        const std::vector<uint32_t> &cfg_sizes, const uint32_t buffer_size);

    static Expected<vdma::BufferSizesRequirements> get_ccb_buffer_requirements(uint32_t buffer_size,
        const DescSizesParams &desc_sizes_params);

    std::unique_ptr<vdma::VdmaEdgeLayer> m_buffer;
    const size_t m_total_buffer_size;
    size_t m_current_buffer_size;
    size_t m_acc_buffer_offset;

};

class ZeroCopyConfigBuffer final : public ConfigBuffer {
public:
    static Expected<ZeroCopyConfigBuffer> create(HailoRTDriver &driver, vdma::ChannelId channel_id,
        const ConfigBufferInfo &config_buffer_info, std::vector<std::shared_ptr<vdma::MappedBuffer>> mapped_buffers,
        std::shared_ptr<vdma::MappedBuffer> nops_buffer);

    // Program the descriptors for alligned ccws case
    Expected<uint32_t> program_descriptors(const std::vector<uint64_t> &ccw_bursts_offsets, const std::vector<uint32_t> &ccw_bursts_sizes);

    CONTROL_PROTOCOL__host_buffer_info_t get_host_buffer_info() const;

private:
    ZeroCopyConfigBuffer(std::unique_ptr<vdma::DescriptorList> &&desc_list, vdma::ChannelId channel_id, std::vector<std::shared_ptr<vdma::MappedBuffer>> mapped_buffers,
        std::shared_ptr<vdma::MappedBuffer> nops_buffer);

    static Expected<std::unique_ptr<vdma::DescriptorList>> build_desc_list(HailoRTDriver &driver, const std::vector<uint32_t> &ccw_bursts_sizes);

    Expected<uint32_t> program_descriptors_for_transfer(uint64_t ccw_burst_offset, uint32_t ccw_burst_size, uint32_t total_desc_count);

    std::unique_ptr<vdma::DescriptorList> m_desc_list;
    std::vector<std::shared_ptr<vdma::MappedBuffer>> m_mapped_buffers;
    std::shared_ptr<vdma::MappedBuffer> m_nops_buffer;

};

} /* hailort */

#endif /* _HAILO_CONFIG_BUFFER_HPP_ */