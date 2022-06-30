/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file intermediate_buffer.cpp
 * @brief Manages intermediate buffer for inter-context or ddr channels.
 */

#include "context_switch/multi_context/resource_manager.hpp"
#include "intermediate_buffer.hpp"
#include "vdma/sg_buffer.hpp"
#include "vdma/continuous_buffer.hpp"


namespace hailort
{

Expected<IntermediateBuffer> IntermediateBuffer::create(HailoRTDriver &driver,
    ChannelType channel_type, const uint32_t transfer_size, const uint16_t batch_size)
{
    auto buffer_ptr = should_use_ccb(driver, channel_type) ?
        create_ccb_buffer(driver, transfer_size, batch_size) :
        create_sg_buffer(driver, transfer_size, batch_size);
    CHECK_EXPECTED(buffer_ptr);

    return IntermediateBuffer(buffer_ptr.release(), transfer_size, batch_size);
}

hailo_status IntermediateBuffer::program_inter_context()
{
    size_t acc_offset = 0;
    for (uint16_t i = 0; i < m_max_batch_size; i++) {
        const auto first_desc_interrupts_domain = VdmaInterruptsDomain::NONE;
        const auto last_desc_interrupts_domain = ((m_max_batch_size - 1) == i) ?
            VdmaInterruptsDomain::DEVICE : VdmaInterruptsDomain::NONE;
        static const auto BUFFER_NOT_CIRCULAR = false;
        auto desc_count_local = m_buffer->program_descriptors(m_transfer_size, first_desc_interrupts_domain,
            last_desc_interrupts_domain, acc_offset, BUFFER_NOT_CIRCULAR);
        CHECK_EXPECTED_AS_STATUS(desc_count_local,
            "Failed to program descs for intermediate channels. Given batch_size is too big.");
        acc_offset += desc_count_local.value();
    }
    return HAILO_SUCCESS;
}

hailo_status IntermediateBuffer::reprogram_inter_context(uint16_t batch_size)
{
    const auto prev_batch_size = m_dynamic_batch_size;
    auto status = set_dynamic_batch_size(batch_size);
    CHECK_SUCCESS(status);

    if (prev_batch_size == m_dynamic_batch_size) {
        LOGGER__TRACE("Batch size hasn't changed ({}); nothing to be done.", batch_size);
        return HAILO_SUCCESS;
    }

    status = m_buffer->reprogram_device_interrupts_for_end_of_batch(m_transfer_size, prev_batch_size,
        VdmaInterruptsDomain::NONE);
    CHECK_SUCCESS(status, "Failed reprogramming device interrupts for the end of the previous batch (size {})",
        prev_batch_size);
    status = m_buffer->reprogram_device_interrupts_for_end_of_batch(m_transfer_size, m_dynamic_batch_size,
        VdmaInterruptsDomain::DEVICE);
    CHECK_SUCCESS(status, "Failed reprogramming device interrupts for the end of the current batch (size {})",
        m_dynamic_batch_size);

    return HAILO_SUCCESS;
}

Expected<uint16_t> IntermediateBuffer::program_ddr()
{
    const auto interrupts_domain = VdmaInterruptsDomain::NONE;
    const auto total_size = m_buffer->descs_count() * m_buffer->desc_page_size();

    auto desc_count_local = m_buffer->program_descriptors(total_size,
         interrupts_domain, interrupts_domain, 0, true);
    CHECK_EXPECTED(desc_count_local);

    return static_cast<uint16_t>(desc_count_local.release());
}

uint64_t IntermediateBuffer::dma_address() const
{
    return m_buffer->dma_address();
}

uint32_t IntermediateBuffer::descriptors_in_frame() const
{
    return m_buffer->descriptors_in_buffer(m_transfer_size);
}

uint16_t IntermediateBuffer::desc_page_size() const
{
    return m_buffer->desc_page_size();
}

uint32_t IntermediateBuffer::descs_count() const
{
    return m_buffer->descs_count();
}

uint8_t IntermediateBuffer::depth()
{
    // TODO HRT-6763: remove this function, use desc count instead
    auto count = m_buffer->descs_count();

    uint32_t depth = 0;
    while (count >>= 1) {
        ++depth;
    }
    assert(IS_FIT_IN_UINT8(depth));
    return static_cast<uint8_t>(depth);
}

Expected<Buffer> IntermediateBuffer::read()
{
    const auto size = m_transfer_size * m_dynamic_batch_size;
    assert(size <= m_buffer->size());

    auto res = Buffer::create(size);
    CHECK_EXPECTED(res);

    auto status = m_buffer->read(res->data(), size, 0);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return res.release();
}

CONTROL_PROTOCOL__host_buffer_info_t IntermediateBuffer::get_host_buffer_info() const
{
    CONTROL_PROTOCOL__host_buffer_info_t buffer_info = {};

    buffer_info.buffer_type = static_cast<uint8_t>((m_buffer->type() == vdma::VdmaBuffer::Type::SCATTER_GATHER) ?
        CONTROL_PROTOCOL__HOST_BUFFER_TYPE_EXTERNAL_DESC : 
        CONTROL_PROTOCOL__HOST_BUFFER_TYPE_CCB);
    buffer_info.dma_address = dma_address();
    buffer_info.desc_page_size = desc_page_size();
    buffer_info.total_desc_count = descs_count();
    buffer_info.bytes_in_pattern = m_transfer_size;

    return buffer_info;
}

Expected<std::unique_ptr<vdma::VdmaBuffer>> IntermediateBuffer::create_sg_buffer(HailoRTDriver &driver,
    const uint32_t transfer_size, const uint16_t batch_size)
{
    auto desc_sizes_pair = VdmaDescriptorList::get_desc_buffer_sizes_for_single_transfer(driver,
        batch_size, batch_size, transfer_size);
    CHECK_EXPECTED(desc_sizes_pair);
    auto desc_page_size = desc_sizes_pair->first;
    auto descs_count = desc_sizes_pair->second;

    // On dram-dma all descriptors should have channel index, until we implement CCB,
    // we use some fake channel index. Currently the channel_index is not in used by
    // the hw. TODO HRT-5835: remove channel_index. 
    const uint8_t channel_index = 0;
    auto buffer = vdma::SgBuffer::create(driver, descs_count, desc_page_size,
        HailoRTDriver::DmaDirection::BOTH, channel_index);
    CHECK_EXPECTED(buffer);

    auto buffer_ptr = make_unique_nothrow<vdma::SgBuffer>(buffer.release());
    CHECK_NOT_NULL_AS_EXPECTED(buffer_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::unique_ptr<vdma::VdmaBuffer>(std::move(buffer_ptr));
}

IntermediateBuffer::IntermediateBuffer(std::unique_ptr<vdma::VdmaBuffer> &&buffer, uint32_t transfer_size,
                                       uint16_t batch_size) :
    m_buffer(std::move(buffer)),
    m_transfer_size(transfer_size),
    m_max_batch_size(batch_size),
    m_dynamic_batch_size(batch_size)
{}

hailo_status IntermediateBuffer::set_dynamic_batch_size(uint16_t batch_size)
{
    if (CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE == batch_size) {
        LOGGER__TRACE("Received CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE == batch_size; "
                      "Leaving previously set value of {}", m_dynamic_batch_size);
    } else {
        CHECK(batch_size <= m_max_batch_size, HAILO_INVALID_ARGUMENT,
            "batch_size ({}) must be <= than m_max_batch_size ({})",
            batch_size, m_max_batch_size);

        LOGGER__TRACE("Setting intermediate buffer's batch_size to {}", batch_size);
        m_dynamic_batch_size = batch_size;
    }

    return HAILO_SUCCESS;
}

Expected<std::unique_ptr<vdma::VdmaBuffer>> IntermediateBuffer::create_ccb_buffer(HailoRTDriver &driver,
    const uint32_t transfer_size, const uint16_t batch_size)
{
    // The first 12 channels in D2H CCB ("regular channels") requires that the amount of descriptors will be a power
    // of 2. Altough the 4 last channels ("enhanced channels") don't have this requirements, we keep the code the same.
    auto buffer_size = vdma::ContinuousBuffer::get_buffer_size_desc_power2(transfer_size * batch_size);
    auto buffer = vdma::ContinuousBuffer::create(buffer_size, driver);
    CHECK_EXPECTED(buffer);

    auto buffer_ptr = make_unique_nothrow<vdma::ContinuousBuffer>(buffer.release());
    CHECK_NOT_NULL_AS_EXPECTED(buffer_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::unique_ptr<vdma::VdmaBuffer>(std::move(buffer_ptr));
}

bool IntermediateBuffer::should_use_ccb(HailoRTDriver &driver, ChannelType channel_type)
{
    if (ChannelType::DDR == channel_type) {
        // TODO HRT-6645: remove this if
        return false;
    }

    switch (driver.dma_type()) {
    case HailoRTDriver::DmaType::PCIE:
        return false;
    case HailoRTDriver::DmaType::DRAM:
        return true;
    default:
        assert(true);
        return false;
    }
}

} /* namespace hailort */
