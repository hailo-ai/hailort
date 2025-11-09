/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file config_buffer.cpp
 * @brief Manages configuration vdma buffer. The configuration buffer contains nn-configurations in a specific
 *        hw format (ccw).
 */

#include "core_op/resource_manager/config_buffer.hpp"
#include "vdma/memory/sg_edge_layer.hpp"
#include "vdma/memory/continuous_edge_layer.hpp"
#include "vdma/memory/buffer_requirements.hpp"
#include "common/internal_env_vars.hpp"

#include <numeric>


namespace hailort {

ConfigBuffer::ConfigBuffer(vdma::ChannelId channel_id)
    : m_channel_id(channel_id), m_acc_desc_count(0)
{}

Expected<vdma::BufferSizesRequirements> ConfigBuffer::get_sg_buffer_requirements(const std::vector<uint32_t> &cfg_sizes,
    const DescSizesParams &desc_sizes_params)
{
    const bool NOT_CIRCULAR = false;
    // For config channels (In Hailo15), the page size must be a multiplication of host default page size.
    // Therefore we use the flag force_default_page_size for those types of buffers.
    const bool FORCE_DEFAULT_PAGE_SIZE = true;
    const bool FORCE_BATCH_SIZE = true;
    const bool NOT_DDR = false;

    return vdma::BufferSizesRequirements::get_buffer_requirements_multiple_transfers(
        vdma::VdmaBuffer::Type::SCATTER_GATHER, desc_sizes_params, desc_sizes_params.max_page_size, 1, cfg_sizes, NOT_CIRCULAR,
        FORCE_DEFAULT_PAGE_SIZE, FORCE_BATCH_SIZE, NOT_DDR);
}

CopiedConfigBuffer::CopiedConfigBuffer(std::unique_ptr<vdma::VdmaEdgeLayer> &&buffer,
    vdma::ChannelId channel_id, size_t total_buffer_size)
    : ConfigBuffer(channel_id), m_buffer(std::move(buffer)),
      m_total_buffer_size(total_buffer_size),
      m_current_buffer_size(0), m_acc_buffer_offset(0)
{}

Expected<CopiedConfigBuffer> CopiedConfigBuffer::create(HailoRTDriver &driver, vdma::ChannelId channel_id,
    const ConfigBufferInfo &config_buffer_info)
{
    const auto &bursts_sizes = config_buffer_info.ccw_dma_transfers;
    const auto buffer_size = std::accumulate(bursts_sizes.begin(), bursts_sizes.end(), uint32_t{0});
    TRY(auto buffer_ptr, create_buffer(driver, channel_id, bursts_sizes, buffer_size));
    return CopiedConfigBuffer(std::move(buffer_ptr), channel_id, buffer_size);
}

Expected<std::unique_ptr<vdma::VdmaEdgeLayer>> CopiedConfigBuffer::create_buffer(HailoRTDriver &driver, vdma::ChannelId channel_id,
    const std::vector<uint32_t> &bursts_sizes, const uint32_t buffer_size)
{
    auto buffer_ptr = should_use_ccb(driver.dma_type()) ?
        create_ccb_buffer(driver, buffer_size) :
        create_sg_buffer(driver, channel_id, bursts_sizes);
    if (should_use_ccb(driver.dma_type()) && (HAILO_OUT_OF_HOST_CMA_MEMORY == buffer_ptr.status())) {
        /* Try to use sg buffer instead */
        return create_sg_buffer(driver, channel_id, bursts_sizes);
    } else {
        return buffer_ptr;
    }
}

Expected<vdma::BufferSizesRequirements> CopiedConfigBuffer::get_buffer_requirements(const ConfigBufferInfo &config_buffer_info,
    HailoRTDriver::DmaType dma_type, const DescSizesParams &desc_sizes_params)
{
    const auto &bursts_sizes = config_buffer_info.ccw_dma_transfers;
    const auto buffer_size = std::accumulate(bursts_sizes.begin(), bursts_sizes.end(), uint32_t{0});

    return should_use_ccb(dma_type) ?
        get_ccb_buffer_requirements(buffer_size, desc_sizes_params) :
        get_sg_buffer_requirements(bursts_sizes, desc_sizes_params);
}

Expected<uint32_t> CopiedConfigBuffer::program_descriptors()
{
    const size_t offset = m_acc_desc_count * desc_page_size();
    TRY(auto descriptors_count, m_buffer->program_descriptors(m_acc_buffer_offset, m_acc_desc_count, offset));

    m_acc_desc_count += descriptors_count;
    m_acc_buffer_offset = 0;

    return descriptors_count;
}

hailo_status CopiedConfigBuffer::pad_with_nops()
{
    static constexpr uint64_t CCW_NOP = 0x0;

    auto page_size = desc_page_size();
    auto buffer_size = m_total_buffer_size;
    auto buffer_residue = buffer_size % page_size;
    if (0 != (page_size - buffer_residue) % CCW_HEADER_SIZE) {
        LOGGER__ERROR("CFG channel buffer size must be a multiple of CCW header size ({})", CCW_HEADER_SIZE);
        return HAILO_INTERNAL_FAILURE;
    }
    /* If buffer does not fit info descriptor, the host must pad the buffer with CCW NOPs. */
    auto nop_count = (buffer_residue == 0) ? 0 : ((page_size - buffer_residue) / CCW_HEADER_SIZE);
    for (uint8_t nop_index = 0; nop_index < nop_count; nop_index++) {
        /* Generate nop transaction.
           CCW of all zeros (64'h0) should be treated as NOP - ignore CCW and expect CCW in next 64b word.
           When CSM recognize it is a NOP it pops it from the channel FIFO without forward any address/data/command,
           does not contribute to CRC calculations but return credits to the peripheral as usual. */
        auto status = write_inner(MemoryView::create_const(reinterpret_cast<const void *>(&CCW_NOP), sizeof(CCW_NOP)));
        CHECK_SUCCESS(status);
    }
    return HAILO_SUCCESS;
}


hailo_status CopiedConfigBuffer::write(const MemoryView &data)
{
    CHECK(data.size() <= size_left(), HAILO_INTERNAL_FAILURE, "Write too many config words");
    auto status = write_inner(data);
    CHECK_SUCCESS(status);

    m_current_buffer_size += data.size();
    return HAILO_SUCCESS;
}

size_t CopiedConfigBuffer::size_left() const
{
    assert(m_total_buffer_size >= m_current_buffer_size);
    return m_total_buffer_size - m_current_buffer_size;
}

size_t CopiedConfigBuffer::get_current_buffer_size() const
{
    return m_current_buffer_size;
}

uint16_t CopiedConfigBuffer::desc_page_size() const
{
    return m_buffer->desc_page_size();
}

CONTROL_PROTOCOL__host_buffer_info_t CopiedConfigBuffer::get_host_buffer_info() const
{
    return m_buffer->get_host_buffer_info(m_acc_desc_count * m_buffer->desc_page_size());
}

hailo_status CopiedConfigBuffer::write_inner(const MemoryView &data)
{
    size_t total_offset = (m_acc_desc_count * m_buffer->desc_page_size()) + m_acc_buffer_offset;
    auto status = m_buffer->write(data.data(), data.size(), total_offset);
    CHECK_SUCCESS(status);

    m_acc_buffer_offset += data.size();
    return HAILO_SUCCESS;
}

Expected<std::unique_ptr<vdma::VdmaEdgeLayer>> CopiedConfigBuffer::create_sg_buffer(HailoRTDriver &driver,
    vdma::ChannelId channel_id, const std::vector<uint32_t> &bursts_sizes)
{
    static const auto NOT_CIRCULAR = false;

    TRY(const auto requirements, get_sg_buffer_requirements(bursts_sizes, driver.get_sg_desc_params()));

    TRY(auto buffer, vdma::SgBuffer::create(driver, requirements.buffer_size(), HailoRTDriver::DmaDirection::H2D));

    auto buffer_ptr = make_shared_nothrow<vdma::SgBuffer>(std::move(buffer));
    CHECK_NOT_NULL_AS_EXPECTED(buffer_ptr, HAILO_OUT_OF_HOST_MEMORY);

    static const auto DEFAULT_OFFSET = 0;
    TRY(auto edge_layer, vdma::SgEdgeLayer::create(std::move(buffer_ptr), requirements.buffer_size(), DEFAULT_OFFSET,
        driver, requirements.descs_count(), requirements.desc_page_size(), NOT_CIRCULAR, channel_id));

    auto edge_layer_ptr = make_unique_nothrow<vdma::SgEdgeLayer>(std::move(edge_layer));
    CHECK_NOT_NULL_AS_EXPECTED(edge_layer_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::unique_ptr<vdma::VdmaEdgeLayer>(std::move(edge_layer_ptr));
}

Expected<std::unique_ptr<vdma::VdmaEdgeLayer>> CopiedConfigBuffer::create_ccb_buffer(HailoRTDriver &driver,
    uint32_t buffer_size)
{
    TRY(const auto requirements, get_ccb_buffer_requirements(buffer_size, driver.get_ccb_desc_params()));

    TRY_WITH_ACCEPTABLE_STATUS(HAILO_OUT_OF_HOST_CMA_MEMORY, auto buffer,
        vdma::ContinuousBuffer::create(requirements.buffer_size(), driver));

    auto buffer_ptr = make_shared_nothrow<vdma::ContinuousBuffer>(std::move(buffer));
    CHECK_NOT_NULL_AS_EXPECTED(buffer_ptr, HAILO_OUT_OF_HOST_MEMORY);

    static const auto DEFAULT_OFFSET = 0;
    TRY(auto edge_layer, vdma::ContinuousEdgeLayer::create(std::move(buffer_ptr), requirements.buffer_size(),
        DEFAULT_OFFSET, requirements.desc_page_size(), requirements.descs_count()));

    auto edge_layer_ptr = make_unique_nothrow<vdma::ContinuousEdgeLayer>(std::move(edge_layer));
    CHECK_NOT_NULL_AS_EXPECTED(edge_layer_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::unique_ptr<vdma::VdmaEdgeLayer>(std::move(edge_layer_ptr));
}

Expected<vdma::BufferSizesRequirements> CopiedConfigBuffer::get_ccb_buffer_requirements(uint32_t buffer_size,
    const DescSizesParams &desc_sizes_params)
{
    const auto NOT_CIRCULAR = false;
    // For config channels (In Hailo15), the page size must be a multiplication of host default page size.
    // Therefore we use the flag force_default_page_size for those types of buffers.
    static const auto FORCE_DEFAULT_PAGE_SIZE = true;
    static const auto FORCE_BATCH_SIZE = true;
    static const uint16_t DEFAULT_BATCH_SIZE = 1;
    static const auto IS_VDMA_ALIGNED_BUFFER = true;
    static const bool IS_NOT_DDR = false;

    return vdma::BufferSizesRequirements::get_buffer_requirements_single_transfer(
        vdma::VdmaBuffer::Type::CONTINUOUS, desc_sizes_params, desc_sizes_params.max_page_size, DEFAULT_BATCH_SIZE,
        DEFAULT_BATCH_SIZE, buffer_size, NOT_CIRCULAR, FORCE_DEFAULT_PAGE_SIZE, FORCE_BATCH_SIZE, IS_VDMA_ALIGNED_BUFFER,
        IS_NOT_DDR);
}

bool CopiedConfigBuffer::should_use_ccb(HailoRTDriver::DmaType dma_type)
{
    if (dma_type != HailoRTDriver::DmaType::DRAM) {
        return false; // not supported
    }

    return !is_env_variable_on(HAILO_FORCE_CONF_CHANNEL_OVER_DESC_ENV_VAR);
}

ZeroCopyConfigBuffer::ZeroCopyConfigBuffer(std::unique_ptr<vdma::DescriptorList> &&desc_list, vdma::ChannelId channel_id, std::vector<std::shared_ptr<vdma::MappedBuffer>> mapped_buffers,
    std::shared_ptr<vdma::MappedBuffer> nops_buffer)
    : ConfigBuffer(channel_id), m_desc_list(std::move(desc_list)), m_mapped_buffers(mapped_buffers), m_nops_buffer(nops_buffer)
{}

Expected<ZeroCopyConfigBuffer> ZeroCopyConfigBuffer::create(HailoRTDriver &driver, vdma::ChannelId channel_id,
    const ConfigBufferInfo &config_buffer_info, std::vector<std::shared_ptr<vdma::MappedBuffer>> mapped_buffers,
    std::shared_ptr<vdma::MappedBuffer> nops_buffer)
{
    const auto &ccw_bursts_offsets = config_buffer_info.ccw_bursts_offsets;
    const auto &ccw_bursts_sizes = config_buffer_info.ccw_bursts_sizes;
    TRY(auto desc_list, build_desc_list(driver, ccw_bursts_sizes));
    auto config_buffer = ZeroCopyConfigBuffer(std::move(desc_list), channel_id, mapped_buffers, nops_buffer);
    auto status = config_buffer.program_descriptors(ccw_bursts_offsets, ccw_bursts_sizes);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return config_buffer;
}

Expected<std::unique_ptr<vdma::DescriptorList>> ZeroCopyConfigBuffer::build_desc_list(HailoRTDriver &driver, const std::vector<uint32_t> &ccw_bursts_sizes)
{
    std::vector<uint32_t> burst_sizes;

    // Reserve space for NOPs + bursts
    burst_sizes.reserve(1 + ccw_bursts_sizes.size());

    // For padding - adding the NOPs
    burst_sizes.push_back(NOPS_TRANSFERS_PER_ALIGNED_CCWS_TRANSFER);

    for (const auto& ccw_bursts_size : ccw_bursts_sizes) {
        burst_sizes.push_back(static_cast<uint32_t>(ccw_bursts_size));
    }
    TRY(auto requirements, get_sg_buffer_requirements(burst_sizes, driver.get_sg_desc_params()));
    TRY(auto desc_list, vdma::DescriptorList::create(requirements.descs_count(), requirements.desc_page_size(), false, driver));
    return make_unique_nothrow<vdma::DescriptorList>(std::move(desc_list));
}

Expected<uint32_t> ZeroCopyConfigBuffer::program_descriptors(const std::vector<uint64_t> &ccw_bursts_offsets, const std::vector<uint32_t> &ccw_bursts_sizes)
{
    uint32_t current_desc_index = 0;
    const uint16_t page_size = m_desc_list->desc_page_size();

    // Transfer nops such that the number of total transferred bytes is a multiple of page_size
    const auto total_dma_transfers_size = std::accumulate(ccw_bursts_sizes.begin(), ccw_bursts_sizes.end(), uint64_t{0});

    const size_t padding_count = page_size - (total_dma_transfers_size % page_size);
    if (padding_count > 0) {
        auto status = m_desc_list->program(*m_nops_buffer, padding_count, 0, m_channel_id, current_desc_index, 1);
        CHECK_SUCCESS(status, "Failed to program nops buffer");
        current_desc_index += 1;
    }

    for (size_t i = 0; i < ccw_bursts_offsets.size(); ++i) {
        TRY(auto current_transfer_desc_count, program_descriptors_for_transfer(ccw_bursts_offsets[i], ccw_bursts_sizes[i], current_desc_index));
        current_desc_index += current_transfer_desc_count;
    }
    m_acc_desc_count += current_desc_index;

    return current_desc_index;
}

/*
* Programs the descriptors for a single CCW DMA transfer.
*
* A CCW transfer may span across multiple mapped buffers. This function calculates the starting buffer index
* and the offset within that buffer based on the provided burst offset.
*
* It then iterates over the relevant buffers, programming descriptors for each segment of the transfer, until the entire burst size has been processed.
*
* - On the first iteration, the transfer starts from a non-zero offset within the first buffer (based on the CCW offset).
* - On all subsequent iterations, the transfer continues from the start of each subsequent buffer.
*/
Expected<uint32_t> ZeroCopyConfigBuffer::program_descriptors_for_transfer(uint64_t ccw_burst_offset, uint32_t ccw_burst_size, uint32_t current_desc_index)
{
    // all vectors in m_mapped_buffers have the same size (except from the last one)
    size_t current_buffer_index = ccw_burst_offset / m_mapped_buffers.at(0)->size();

    uint32_t transfer_desc_count = 0;
    uint64_t bytes_transferred = 0;
 
    // We transfer data until we reach the total burst size.
    while (bytes_transferred < ccw_burst_size) {
        const auto &curr_buffer = m_mapped_buffers.at(current_buffer_index++);

        // In first iteration we use offset from current buffer - every other one we use start
        const uint64_t offset_in_buffer = (0 == bytes_transferred) ? ccw_burst_offset % m_mapped_buffers.at(0)->size() : 0;

        // Calculate how many bytes are available in the current buffer from the current offset.
        const uint64_t available_bytes = curr_buffer->size() - offset_in_buffer;

        // We transfer the minimum of what remains to be transferred and what is available in the current buffer.
        const uint64_t curr_bytes_to_transfer = std::min(ccw_burst_size - bytes_transferred, available_bytes);
        CHECK_SUCCESS(m_desc_list->program(*curr_buffer, curr_bytes_to_transfer, offset_in_buffer, m_channel_id,
                                           current_desc_index + transfer_desc_count));

        transfer_desc_count += m_desc_list->descriptors_in_buffer(curr_bytes_to_transfer);
        bytes_transferred += curr_bytes_to_transfer;
    }
    return transfer_desc_count;
}

CONTROL_PROTOCOL__host_buffer_info_t ZeroCopyConfigBuffer::get_host_buffer_info() const
{
    return vdma::VdmaEdgeLayer::get_host_buffer_info(vdma::VdmaEdgeLayer::Type::SCATTER_GATHER, m_desc_list->dma_address(),
        m_desc_list->desc_page_size(), m_desc_list->count(), m_acc_desc_count * m_desc_list->desc_page_size());
}

} /* hailort */