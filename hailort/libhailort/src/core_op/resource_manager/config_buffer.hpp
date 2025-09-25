/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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
#include "vdma/memory/buffer_requirements.hpp"
#include "hef/core_op_metadata.hpp"


namespace hailort {

#define CCW_BYTES_IN_WORD (4)
#define CCW_DATA_OFFSET (CCW_BYTES_IN_WORD * 2)
#define CCW_HEADER_SIZE (CCW_DATA_OFFSET)
#define DEFAULT_PROGRAM_BATCH_SIZE (1)
#define NOPS_TRANSFERS_PER_ALIGNED_CCWS_TRANSFER (1)

class ConfigBuffer final {
public:
    // TODO HRT-16583: Split ConfigBuffer to 2 classes: one for aligned_ccws case and for the regular case
    static Expected<ConfigBuffer> create_for_aligned_ccws(HailoRTDriver &driver, vdma::ChannelId channel_id,
        const ConfigBufferInfo &config_buffer_info, std::vector<std::shared_ptr<vdma::MappedBuffer>> mapped_buffers,
        std::shared_ptr<vdma::MappedBuffer> nops_buffer);
    static Expected<ConfigBuffer> create_with_copy_descriptors(HailoRTDriver &driver, vdma::ChannelId channel_id,
        const ConfigBufferInfo &config_buffer_info);

    static Expected<vdma::BufferSizesRequirements> get_buffer_requirements(const ConfigBufferInfo &config_buffer_info,
        HailoRTDriver::DmaType dma_type, uint16_t max_desc_page_size);
    static bool should_use_ccb(HailoRTDriver::DmaType driver);

    // Write data to config channel
    hailo_status write(const MemoryView &data);

    // Program the descriptors for the data written so far
    Expected<uint32_t> program_descriptors();

    // Program the descriptors for alligned ccws case
    Expected<uint32_t> program_descriptors_for_aligned_ccws(const std::vector<std::pair<uint64_t, uint64_t>> &ccw_dma_transfers);

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

    // Constructor for the case of alligned ccws
    ConfigBuffer(std::unique_ptr<vdma::DescriptorList> &&desc_list, vdma::ChannelId channel_id, std::vector<std::shared_ptr<vdma::MappedBuffer>> mapped_buffers,
        const std::vector<std::pair<uint64_t, uint64_t>> &ccw_dma_transfers, std::shared_ptr<vdma::MappedBuffer> nops_buffer);

    hailo_status write_inner(const MemoryView &data);

    static Expected<std::unique_ptr<vdma::VdmaEdgeLayer>> create_sg_buffer(HailoRTDriver &driver,
        vdma::ChannelId channel_id, const std::vector<uint32_t> &cfg_sizes);
    static Expected<std::unique_ptr<vdma::VdmaEdgeLayer>> create_ccb_buffer(HailoRTDriver &driver,
        uint32_t buffer_size);
    static Expected<std::unique_ptr<vdma::VdmaEdgeLayer>> create_buffer(HailoRTDriver &driver, vdma::ChannelId channel_id,
        const std::vector<uint32_t> &cfg_sizes, const uint32_t buffer_size);

    // Build the descriptor list for the alligned ccws case
    static Expected<std::unique_ptr<vdma::DescriptorList>> build_desc_list(HailoRTDriver &driver,
        std::vector<std::pair<uint64_t, uint64_t>> ccw_dma_transfers);

    static Expected<vdma::BufferSizesRequirements> get_sg_buffer_requirements(const std::vector<uint32_t> &cfg_sizes,
        uint16_t max_desc_page_size);
    static Expected<vdma::BufferSizesRequirements> get_ccb_buffer_requirements(uint32_t buffer_size,
        uint16_t max_desc_page_size);

    Expected<uint32_t> program_descriptors_for_transfer(const std::pair<uint64_t, uint64_t> &ccw_dma_transfer, uint32_t total_desc_count);

    std::unique_ptr<vdma::VdmaEdgeLayer> m_buffer;
    vdma::ChannelId m_channel_id;
    const size_t m_total_buffer_size;
    size_t m_acc_buffer_offset;
    uint32_t m_acc_desc_count;
    size_t m_current_buffer_size;

    // TODO HRT-16583: Split ConfigBuffer to 2 classes: one for aligned_ccws case and for the regular case
    std::unique_ptr<vdma::DescriptorList> m_desc_list;
    bool m_aligned_ccws = false;
    std::vector<std::shared_ptr<vdma::MappedBuffer>> m_mapped_buffers;
    const std::vector<std::pair<uint64_t, uint64_t>> m_ccw_dma_transfers;
    std::shared_ptr<vdma::MappedBuffer> m_nops_buffer;
};

} /* hailort */

#endif /* _HAILO_CONFIG_BUFFER_HPP_ */