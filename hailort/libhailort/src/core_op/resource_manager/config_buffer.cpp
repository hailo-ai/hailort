/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file config_buffer.cpp
 * @brief Manages configuration vdma buffer. The configuration buffer contains nn-configurations in a specific
 *        hw format (ccw).
 */

#include "core_op/resource_manager/config_buffer.hpp"
#include "vdma/memory/sg_buffer.hpp"
#include "vdma/memory/continuous_buffer.hpp"
#include "vdma/memory/buffer_requirements.hpp"

#include <numeric>


namespace hailort {

Expected<std::unique_ptr<vdma::VdmaBuffer>> ConfigBuffer::create_buffer(HailoRTDriver &driver, vdma::ChannelId channel_id,
    const std::vector<uint32_t> &cfg_sizes, const uint32_t buffer_size)
{
    auto buffer_ptr = should_use_ccb(driver) ?
        create_ccb_buffer(driver, buffer_size) :
        create_sg_buffer(driver, channel_id, cfg_sizes);
    if (should_use_ccb(driver) && (HAILO_OUT_OF_HOST_CMA_MEMORY == buffer_ptr.status())) {
        /* Try to use sg buffer instead */
        return create_sg_buffer(driver, channel_id, cfg_sizes);
    } else {
        return buffer_ptr;
    }
}

Expected<ConfigBuffer> ConfigBuffer::create(HailoRTDriver &driver, vdma::ChannelId channel_id,
    const std::vector<uint32_t> &cfg_sizes)
{
    const auto buffer_size = std::accumulate(cfg_sizes.begin(), cfg_sizes.end(), 0);
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT32(buffer_size), HAILO_INTERNAL_FAILURE, "config buffer size exceeded UINT32 range limit");
    auto buffer_ptr = create_buffer(driver, channel_id, cfg_sizes, static_cast<uint32_t>(buffer_size));
    CHECK_EXPECTED(buffer_ptr);

    return ConfigBuffer(buffer_ptr.release(), channel_id, buffer_size);
}

ConfigBuffer::ConfigBuffer(std::unique_ptr<vdma::VdmaBuffer> &&buffer,
    vdma::ChannelId channel_id, size_t total_buffer_size)
    : m_buffer(std::move(buffer)),
      m_channel_id(channel_id),
      m_total_buffer_size(total_buffer_size), m_acc_buffer_offset(0), m_acc_desc_count(0),
      m_current_buffer_size(0)
{}

Expected<uint32_t> ConfigBuffer::program_descriptors()
{
    // TODO HRT-9657: remove DEVICE interrupts
    auto descriptors_count =
        m_buffer->program_descriptors(m_acc_buffer_offset, vdma::InterruptsDomain::DEVICE, m_acc_desc_count);
    CHECK_EXPECTED(descriptors_count);

    m_acc_desc_count += descriptors_count.value();
    m_acc_buffer_offset = 0;

    return descriptors_count;
}

hailo_status ConfigBuffer::pad_with_nops()
{
    static constexpr uint64_t CCW_NOP = 0x0;

    auto page_size = desc_page_size();
    auto buffer_size = m_total_buffer_size;
    auto buffer_residue = buffer_size % page_size;
    if (0 != buffer_residue % CCW_HEADER_SIZE) {
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
    return m_buffer->get_host_buffer_info(m_acc_desc_count * m_buffer->desc_page_size());
}

hailo_status ConfigBuffer::write_inner(const MemoryView &data)
{
    size_t total_offset = (m_acc_desc_count * m_buffer->desc_page_size()) + m_acc_buffer_offset;
    auto status = m_buffer->write(data.data(), data.size(), total_offset);
    CHECK_SUCCESS(status);

    m_acc_buffer_offset += data.size();
    return HAILO_SUCCESS;
}

Expected<std::unique_ptr<vdma::VdmaBuffer>> ConfigBuffer::create_sg_buffer(HailoRTDriver &driver,
    vdma::ChannelId channel_id, const std::vector<uint32_t> &cfg_sizes)
{
    static const bool NOT_CIRCULAR = false;
    // For config channels (In Hailo15), the page size must be a multiplication of host default page size.
    // Therefore we use the flag force_default_page_size for those types of buffers.
    auto const FORCE_DEFAULT_PAGE_SIZE = true;
    auto const FORCE_BATCH_SIZE = true;
    auto buffer_size_requirements = vdma::BufferSizesRequirements::get_sg_buffer_requirements_multiple_transfers(
        driver.desc_max_page_size(), 1, cfg_sizes, NOT_CIRCULAR, FORCE_DEFAULT_PAGE_SIZE, FORCE_BATCH_SIZE);
    CHECK_EXPECTED(buffer_size_requirements);
    const auto page_size = buffer_size_requirements->desc_page_size();
    const auto descs_count = buffer_size_requirements->descs_count();
    const auto buffer_size = buffer_size_requirements->buffer_size();

    auto buffer = vdma::SgBuffer::create(driver, buffer_size, descs_count, page_size, NOT_CIRCULAR,
        HailoRTDriver::DmaDirection::H2D, channel_id);
    CHECK_EXPECTED(buffer);

    auto buffer_ptr = make_unique_nothrow<vdma::SgBuffer>(buffer.release());
    CHECK_NOT_NULL_AS_EXPECTED(buffer_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::unique_ptr<vdma::VdmaBuffer>(std::move(buffer_ptr));
}

Expected<std::unique_ptr<vdma::VdmaBuffer>> ConfigBuffer::create_ccb_buffer(HailoRTDriver &driver,
    uint32_t buffer_size)
{
    static const bool NOT_CIRCULAR = false;
    static const uint16_t SINGLE_TRANSFER = 1;
    auto buffer_size_requirements = vdma::BufferSizesRequirements::get_ccb_buffer_requirements_single_transfer(
        SINGLE_TRANSFER, buffer_size, NOT_CIRCULAR);
    CHECK_EXPECTED(buffer_size_requirements);

    auto buffer = vdma::ContinuousBuffer::create(buffer_size_requirements->buffer_size(), driver);
    /* Don't print error here since this might be expected error that the libhailoRT can recover from
        (out of host memory). If it's not the case, there is a print in hailort_driver.cpp file */
    if (HAILO_OUT_OF_HOST_CMA_MEMORY == buffer.status()) {
        return make_unexpected(buffer.status());
    } else {
        CHECK_EXPECTED(buffer);
    }

    auto buffer_ptr = make_unique_nothrow<vdma::ContinuousBuffer>(buffer.release());
    CHECK_NOT_NULL_AS_EXPECTED(buffer_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::unique_ptr<vdma::VdmaBuffer>(std::move(buffer_ptr));
}

bool ConfigBuffer::should_use_ccb(HailoRTDriver &driver)
{
    switch (driver.dma_type()) {
    case HailoRTDriver::DmaType::PCIE:
        return false;
    case HailoRTDriver::DmaType::DRAM:
        if (nullptr != std::getenv("HAILO_FORCE_CONF_CHANNEL_OVER_DESC")) {
            LOGGER__WARNING("Using desc instead of CCB for config channel is not optimal for performance.\n");
            return false;
        }
        else {
            return true;
        }
    }

    // Shouldn't reach here
    assert(false);
    return false;
}

} /* hailort */