/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file inter_context_buffer.cpp
 * @brief Manages inter-context buffer.
 */

#include "core_op/resource_manager/resource_manager.hpp"
#include "core_op/resource_manager/inter_context_buffer.hpp"
#include "vdma/memory/sg_buffer.hpp"
#include "vdma/memory/continuous_buffer.hpp"


namespace hailort
{

Expected<InterContextBuffer> InterContextBuffer::create(HailoRTDriver &driver, uint32_t transfer_size,
    uint16_t max_batch_size, vdma::ChannelId d2h_channel_id)
{
    auto buffer_exp = should_use_ccb(driver) ?
        create_ccb_buffer(driver, transfer_size, max_batch_size) :
        create_sg_buffer(driver, transfer_size, max_batch_size, d2h_channel_id);
    CHECK_EXPECTED(buffer_exp);
    auto buffer_ptr = buffer_exp.release();

    size_t acc_offset = 0;
    for (uint16_t i = 0; i < max_batch_size; i++) {
        const auto last_desc_interrupts_domain = ((max_batch_size - 1) == i) ?
            vdma::InterruptsDomain::DEVICE : vdma::InterruptsDomain::NONE;
        static const auto BUFFER_NOT_CIRCULAR = false;
        auto desc_count_local = buffer_ptr->program_descriptors(transfer_size, last_desc_interrupts_domain, acc_offset,
            BUFFER_NOT_CIRCULAR);
        CHECK_EXPECTED(desc_count_local, "Failed to program descs for inter context channels. Given max_batch_size is too big.");
        acc_offset += desc_count_local.value();
    }

    return InterContextBuffer(std::move(buffer_ptr), transfer_size, max_batch_size);
}

hailo_status InterContextBuffer::reprogram(uint16_t batch_size)
{
    const auto prev_batch_size = m_dynamic_batch_size;
    auto status = set_dynamic_batch_size(batch_size);
    CHECK_SUCCESS(status);

    if (prev_batch_size == m_dynamic_batch_size) {
        LOGGER__TRACE("Batch size hasn't changed ({}); nothing to be done.", batch_size);
        return HAILO_SUCCESS;
    }

    status = m_buffer->reprogram_device_interrupts_for_end_of_batch(m_transfer_size, prev_batch_size,
        vdma::InterruptsDomain::NONE);
    CHECK_SUCCESS(status, "Failed reprogramming device interrupts for the end of the previous batch (size {})",
        prev_batch_size);
    status = m_buffer->reprogram_device_interrupts_for_end_of_batch(m_transfer_size, m_dynamic_batch_size,
        vdma::InterruptsDomain::DEVICE);
    CHECK_SUCCESS(status, "Failed reprogramming device interrupts for the end of the current batch (size {})",
        m_dynamic_batch_size);

    return HAILO_SUCCESS;
}

Expected<Buffer> InterContextBuffer::read()
{
    const auto size = m_transfer_size * m_dynamic_batch_size;
    assert(size <= m_buffer->size());

    auto res = Buffer::create(size);
    CHECK_EXPECTED(res);

    auto status = m_buffer->read(res->data(), size, 0);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return res.release();
}

CONTROL_PROTOCOL__host_buffer_info_t InterContextBuffer::get_host_buffer_info() const
{
    return m_buffer->get_host_buffer_info(m_transfer_size);
}

InterContextBuffer::InterContextBuffer(std::unique_ptr<vdma::VdmaBuffer> &&buffer, uint32_t transfer_size,
                                       uint16_t batch_size) :
    m_buffer(std::move(buffer)),
    m_transfer_size(transfer_size),
    m_max_batch_size(batch_size),
    m_dynamic_batch_size(batch_size)
{}

hailo_status InterContextBuffer::set_dynamic_batch_size(uint16_t batch_size)
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

Expected<std::unique_ptr<vdma::VdmaBuffer>> InterContextBuffer::create_sg_buffer(HailoRTDriver &driver,
    uint32_t transfer_size, uint16_t batch_size, vdma::ChannelId d2h_channel_id)
{
    auto desc_sizes_pair = vdma::DescriptorList::get_desc_buffer_sizes_for_single_transfer(driver,
        batch_size, batch_size, transfer_size);
    CHECK_EXPECTED(desc_sizes_pair);
    const auto desc_page_size = desc_sizes_pair->first;
    const auto descs_count = desc_sizes_pair->second;

    // TODO: HRT-9914 - Instead of using aligned descriptor for each transfer, we should do it for the all frame.
    const size_t desc_per_transfer = DIV_ROUND_UP(transfer_size, desc_page_size);
    const size_t buffer_size = desc_per_transfer * desc_page_size * batch_size;
    auto buffer = vdma::SgBuffer::create(driver, buffer_size, descs_count, desc_page_size,
        HailoRTDriver::DmaDirection::BOTH, d2h_channel_id);
    CHECK_EXPECTED(buffer);

    auto buffer_ptr = make_unique_nothrow<vdma::SgBuffer>(buffer.release());
    CHECK_NOT_NULL_AS_EXPECTED(buffer_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::unique_ptr<vdma::VdmaBuffer>(std::move(buffer_ptr));
}

Expected<std::unique_ptr<vdma::VdmaBuffer>> InterContextBuffer::create_ccb_buffer(HailoRTDriver &driver,
    uint32_t transfer_size, uint16_t batch_size)
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

bool InterContextBuffer::should_use_ccb(HailoRTDriver &driver)
{
    switch (driver.dma_type()) {
    case HailoRTDriver::DmaType::PCIE:
        return false;
    case HailoRTDriver::DmaType::DRAM:
        if (nullptr == std::getenv("HAILO_FORCE_INFER_CONTEXT_CHANNEL_OVER_DESC")) {
            return false;
        }
        else {
            LOGGER__INFO("Using (non default mode) CCB for inter context channels.\n");
            return true;
        }
    }

    // Shouldn't reach here
    assert(false);
    return false;
}

} /* namespace hailort */
