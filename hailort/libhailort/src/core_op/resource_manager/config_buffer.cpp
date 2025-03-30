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

Expected<std::unique_ptr<vdma::DescriptorList>> ConfigBuffer::build_desc_list(HailoRTDriver &driver, 
    std::vector<std::pair<uint64_t, uint64_t>> ccw_dma_transfers)
{
    std::vector<uint32_t> burst_sizes;
    burst_sizes.push_back(NOPS_TRANSFERS_PER_ALIGNED_CCWS_TRANSFER); // For padding - adding the NOPs
    for (const auto& ccw_dma_transfer : ccw_dma_transfers) {
        burst_sizes.push_back(static_cast<uint32_t>(ccw_dma_transfer.second));
    }
    TRY(auto requirements, get_sg_buffer_requirements(burst_sizes, driver.desc_max_page_size()));
    TRY(auto desc_list, vdma::DescriptorList::create(requirements.descs_count(), requirements.desc_page_size(), false, driver));
    return make_unique_nothrow<vdma::DescriptorList>(std::move(desc_list));
}

Expected<std::unique_ptr<vdma::VdmaEdgeLayer>> ConfigBuffer::create_buffer(HailoRTDriver &driver, vdma::ChannelId channel_id,
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

Expected<ConfigBuffer> ConfigBuffer::create_for_aligned_ccws(HailoRTDriver &driver, vdma::ChannelId channel_id,
    const ConfigBufferInfo &config_buffer_info, std::vector<std::shared_ptr<vdma::MappedBuffer>> mapped_buffers,
    std::shared_ptr<vdma::MappedBuffer> nops_buffer)
{
    const auto &ccw_dma_transfers = config_buffer_info.ccw_dma_transfers;
    TRY(auto desc_list, build_desc_list(driver, ccw_dma_transfers));
    auto config_buffer = ConfigBuffer(std::move(desc_list), channel_id, mapped_buffers, ccw_dma_transfers, nops_buffer);
    auto status = config_buffer.program_descriptors_for_aligned_ccws(config_buffer_info.ccw_dma_transfers);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return config_buffer;
}

Expected<ConfigBuffer> ConfigBuffer::create_with_copy_descriptors(HailoRTDriver &driver, vdma::ChannelId channel_id,
    const ConfigBufferInfo &config_buffer_info)
{
    const auto &bursts_sizes = config_buffer_info.bursts_sizes;
    const auto buffer_size = std::accumulate(bursts_sizes.begin(), bursts_sizes.end(), uint32_t{0});
    TRY(auto buffer_ptr, create_buffer(driver, channel_id, bursts_sizes, buffer_size));
    return ConfigBuffer(std::move(buffer_ptr), channel_id, buffer_size);
}

Expected<vdma::BufferSizesRequirements> ConfigBuffer::get_buffer_requirements(const ConfigBufferInfo &config_buffer_info,
    HailoRTDriver::DmaType dma_type, uint16_t max_desc_page_size)
{
    const auto &bursts_sizes = config_buffer_info.bursts_sizes;
    const auto buffer_size = std::accumulate(bursts_sizes.begin(), bursts_sizes.end(), uint32_t{0});
    return should_use_ccb(dma_type) ?
        get_ccb_buffer_requirements(buffer_size, max_desc_page_size) :
        get_sg_buffer_requirements(bursts_sizes, max_desc_page_size);
}

ConfigBuffer::ConfigBuffer(std::unique_ptr<vdma::VdmaEdgeLayer> &&buffer,
    vdma::ChannelId channel_id, size_t total_buffer_size)
    : m_buffer(std::move(buffer)),
      m_channel_id(channel_id),
      m_total_buffer_size(total_buffer_size), m_acc_buffer_offset(0), m_acc_desc_count(0),
      m_current_buffer_size(0)
{}

ConfigBuffer::ConfigBuffer(std::unique_ptr<vdma::DescriptorList> &&desc_list, vdma::ChannelId channel_id, std::vector<std::shared_ptr<vdma::MappedBuffer>> mapped_buffers,
    const std::vector<std::pair<uint64_t, uint64_t>> &ccw_dma_transfers, std::shared_ptr<vdma::MappedBuffer> nops_buffer)
    : m_buffer(nullptr),
      m_channel_id(channel_id),
      m_total_buffer_size(0), m_acc_buffer_offset(0), m_acc_desc_count(0),
      m_current_buffer_size(0), m_desc_list(std::move(desc_list)),
      m_aligned_ccws(true), m_mapped_buffers(mapped_buffers),
      m_ccw_dma_transfers(ccw_dma_transfers), m_nops_buffer(nops_buffer)
{}

Expected<uint32_t> ConfigBuffer::program_descriptors_for_transfer(const std::pair<uint64_t, uint64_t> &ccw_dma_transfer, uint32_t total_desc_count)
{
    /*
    * This function programs the descriptors for a single ccw_dma_transfer.
    * We start by getting the first buffer index + the offset inside that buffer (first since a single ccw_dma_transfer can span over multiple buffers).
    * Starting from that index, we loop over the buffers and program the descriptors for each buffer, until we have transferred the required total number of bytes.
    */
    const uint64_t total_size_to_transfer = ccw_dma_transfer.second;
    const uint64_t start_offset = ccw_dma_transfer.first;

    uint32_t transfer_desc_count = 0;
    uint64_t bytes_transferred = 0;

    size_t current_buffer_index = start_offset / m_mapped_buffers.at(0)->size(); // all vectors in m_mapped_buffers have the same size (except from the last one)
    uint64_t offset_in_buffer = start_offset % m_mapped_buffers.at(0)->size();

    while (bytes_transferred < total_size_to_transfer) {
        const auto &curr_buffer = m_mapped_buffers.at(current_buffer_index);
        const uint64_t available_bytes = curr_buffer->size() - offset_in_buffer; // Calculate how many bytes are available in the current buffer from the current offset.
        
        // We transfer the minimum of what remains to be transferred and what is available in the current buffer.
        const uint64_t curr_bytes_to_transfer = std::min(total_size_to_transfer - bytes_transferred, available_bytes);
        CHECK_SUCCESS(m_desc_list->program(*curr_buffer, curr_bytes_to_transfer, offset_in_buffer, m_channel_id,
                                           total_desc_count + transfer_desc_count, DEFAULT_PROGRAM_BATCH_SIZE,
                                           true, InterruptsDomain::DEVICE));

        transfer_desc_count += m_desc_list->descriptors_in_buffer(curr_bytes_to_transfer);
        bytes_transferred += curr_bytes_to_transfer;
        offset_in_buffer = 0;
        current_buffer_index++;
    }
    return transfer_desc_count;
}

Expected<uint32_t> ConfigBuffer::program_descriptors_for_aligned_ccws(const std::vector<std::pair<uint64_t, uint64_t>> &ccw_dma_transfers)
{
    uint32_t total_desc_count = 0;
    const uint64_t page_size = m_desc_list->desc_page_size();

    // Transfer nops such that the number of total transferred bytes is a multiple of page_size
    const auto total_dma_transfers_size = std::accumulate(ccw_dma_transfers.begin(), ccw_dma_transfers.end(), uint64_t{0},
        [](const auto &acc, const auto &ccw_dma_transfer) { return acc + ccw_dma_transfer.second; });

    auto padding_count = page_size - (total_dma_transfers_size % page_size);
    if (padding_count > 0) {
        CHECK_SUCCESS(m_desc_list->program(*m_nops_buffer, padding_count, 0, m_channel_id,
            total_desc_count, 1, true, InterruptsDomain::DEVICE));
        total_desc_count += 1;
    }

    for (const auto &dma_tranfer : ccw_dma_transfers) {
        // Transfer the actual data (ccws)
        TRY(auto current_transfer_desc_count, program_descriptors_for_transfer(dma_tranfer, total_desc_count));
        total_desc_count += current_transfer_desc_count;
    }
    m_acc_desc_count += total_desc_count;

    return total_desc_count;
}

Expected<uint32_t> ConfigBuffer::program_descriptors()
{
    // TODO HRT-16583: Split ConfigBuffer to 2 classes: one for aligned_ccws case and for the regular case
    // After splitting - remove this check
    CHECK_AS_EXPECTED(!m_aligned_ccws, HAILO_INTERNAL_FAILURE, "Program descriptors for aligned ccws should be called");

    // TODO HRT-9657: remove DEVICE interrupts
    TRY(auto descriptors_count,
        m_buffer->program_descriptors(m_acc_buffer_offset, InterruptsDomain::DEVICE, m_acc_desc_count));

    m_acc_desc_count += descriptors_count;
    m_acc_buffer_offset = 0;

    return descriptors_count;
}

hailo_status ConfigBuffer::pad_with_nops()
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


hailo_status ConfigBuffer::write(const MemoryView &data)
{
    // TODO HRT-16583: Split ConfigBuffer to 2 classes: one for aligned_ccws case and for the regular case
    // After splitting - remove this check
    CHECK(!m_aligned_ccws, HAILO_INTERNAL_FAILURE, "Writing to ConfigBuffer when using alligned ccws is not supported");

    CHECK(data.size() <= size_left(), HAILO_INTERNAL_FAILURE, "Write too many config words");
    auto status = write_inner(data);
    CHECK_SUCCESS(status);

    m_current_buffer_size += data.size();
    return HAILO_SUCCESS;
}

size_t ConfigBuffer::size_left() const
{
    assert(m_total_buffer_size >= m_current_buffer_size);
    return m_total_buffer_size - m_current_buffer_size;
}

size_t ConfigBuffer::get_current_buffer_size() const
{
    return m_current_buffer_size;
}

uint16_t ConfigBuffer::desc_page_size() const
{
    return m_buffer->desc_page_size();
}

vdma::ChannelId ConfigBuffer::channel_id() const
{
    return m_channel_id;
}

CONTROL_PROTOCOL__host_buffer_info_t ConfigBuffer::get_host_buffer_info() const
{
    if (m_aligned_ccws) {
        return vdma::VdmaEdgeLayer::get_host_buffer_info(vdma::VdmaEdgeLayer::Type::SCATTER_GATHER, m_desc_list->dma_address(),
            m_desc_list->desc_page_size(), m_desc_list->count(), m_acc_desc_count * m_desc_list->desc_page_size());
    } else {
        return m_buffer->get_host_buffer_info(m_acc_desc_count * m_buffer->desc_page_size());
    }
}

hailo_status ConfigBuffer::write_inner(const MemoryView &data)
{
    size_t total_offset = (m_acc_desc_count * m_buffer->desc_page_size()) + m_acc_buffer_offset;
    auto status = m_buffer->write(data.data(), data.size(), total_offset);
    CHECK_SUCCESS(status);

    m_acc_buffer_offset += data.size();
    return HAILO_SUCCESS;
}

Expected<std::unique_ptr<vdma::VdmaEdgeLayer>> ConfigBuffer::create_sg_buffer(HailoRTDriver &driver,
    vdma::ChannelId channel_id, const std::vector<uint32_t> &bursts_sizes)
{
    static const auto NOT_CIRCULAR = false;

    TRY(const auto requirements,  get_sg_buffer_requirements(bursts_sizes, driver.desc_max_page_size()));

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

Expected<std::unique_ptr<vdma::VdmaEdgeLayer>> ConfigBuffer::create_ccb_buffer(HailoRTDriver &driver,
    uint32_t buffer_size)
{
    TRY(const auto requirements,  get_ccb_buffer_requirements(buffer_size, driver.desc_max_page_size()));

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


Expected<vdma::BufferSizesRequirements> ConfigBuffer::get_sg_buffer_requirements(const std::vector<uint32_t> &cfg_sizes,
    uint16_t max_desc_page_size)
{
    const auto NOT_CIRCULAR = false;
    // For config channels (In Hailo15), the page size must be a multiplication of host default page size.
    // Therefore we use the flag force_default_page_size for those types of buffers.
    const auto FORCE_DEFAULT_PAGE_SIZE = true;
    const auto FORCE_BATCH_SIZE = true;
    const bool NOT_DDR = false;

    return vdma::BufferSizesRequirements::get_buffer_requirements_multiple_transfers(
        vdma::VdmaBuffer::Type::SCATTER_GATHER, max_desc_page_size, 1, cfg_sizes, NOT_CIRCULAR,
        FORCE_DEFAULT_PAGE_SIZE, FORCE_BATCH_SIZE, NOT_DDR);
}

Expected<vdma::BufferSizesRequirements> ConfigBuffer::get_ccb_buffer_requirements(uint32_t buffer_size,
    uint16_t max_desc_page_size)
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
        vdma::VdmaBuffer::Type::CONTINUOUS, max_desc_page_size, DEFAULT_BATCH_SIZE, DEFAULT_BATCH_SIZE,
        buffer_size, NOT_CIRCULAR, FORCE_DEFAULT_PAGE_SIZE, FORCE_BATCH_SIZE, IS_VDMA_ALIGNED_BUFFER, IS_NOT_DDR);
}

bool ConfigBuffer::should_use_ccb(HailoRTDriver::DmaType dma_type)
{
    if (dma_type != HailoRTDriver::DmaType::DRAM) {
        return false; // not supported
    }

    return !is_env_variable_on(HAILO_FORCE_CONF_CHANNEL_OVER_DESC_ENV_VAR);
}

} /* hailort */