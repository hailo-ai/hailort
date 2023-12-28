/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file intermediate_buffer.cpp
 * @brief Manages intermediate buffers, including inter-context and ddr buffers.
 */

#include "intermediate_buffer.hpp"

#include "core_op/resource_manager/resource_manager.hpp"
#include "vdma/memory/sg_buffer.hpp"
#include "vdma/memory/continuous_buffer.hpp"
#include "vdma/memory/buffer_requirements.hpp"


namespace hailort
{
Expected<std::unique_ptr<vdma::VdmaBuffer>> IntermediateBuffer::create_buffer(HailoRTDriver &driver, uint32_t transfer_size,
    uint16_t max_batch_size, vdma::ChannelId d2h_channel_id, StreamingType streaming_type)
{
    const bool is_circular = (streaming_type == StreamingType::CIRCULAR_CONTINUOS);
    auto buffer_exp = should_use_ccb(driver, streaming_type) ?
        create_ccb_buffer(driver, transfer_size, max_batch_size, is_circular) :
        create_sg_buffer(driver, transfer_size, max_batch_size, d2h_channel_id, is_circular);

    if (should_use_ccb(driver, streaming_type) && (HAILO_OUT_OF_HOST_CMA_MEMORY == buffer_exp.status())) {
        /* Try to use sg buffer instead */
        return create_sg_buffer(driver, transfer_size, max_batch_size, d2h_channel_id, is_circular);
    } else {
        return buffer_exp;
    }
}

Expected<IntermediateBuffer> IntermediateBuffer::create(HailoRTDriver &driver, uint32_t transfer_size,
    uint16_t max_batch_size, vdma::ChannelId d2h_channel_id, StreamingType streaming_type)
{
    auto buffer_exp = create_buffer(driver, transfer_size, max_batch_size, d2h_channel_id, streaming_type);
    CHECK_EXPECTED(buffer_exp);
    auto buffer_ptr = buffer_exp.release();

    if (streaming_type == StreamingType::BURST) {
        // We have max_batch_size transfers, so we program them one by one. The last transfer should report interrupt
        // to the device.
        size_t acc_offset = 0;
        for (uint16_t i = 0; i < max_batch_size; i++) {
            const auto last_desc_interrupts_domain = ((max_batch_size - 1) == i) ?
                vdma::InterruptsDomain::DEVICE : vdma::InterruptsDomain::NONE;
            auto desc_count_local = buffer_ptr->program_descriptors(transfer_size, last_desc_interrupts_domain, acc_offset);
            CHECK_EXPECTED(desc_count_local, "Failed to program descs for inter context channels. Given max_batch_size is too big.");
            acc_offset += desc_count_local.value();
        }
    } else {
        // Program all descriptors, no need for interrupt.
        const auto interrupts_domain = vdma::InterruptsDomain::NONE;
        const auto total_size = buffer_ptr->descs_count() * buffer_ptr->desc_page_size();
        auto desc_count_local = buffer_ptr->program_descriptors(total_size, interrupts_domain, 0);
        CHECK_EXPECTED(desc_count_local);
    }

    return IntermediateBuffer(std::move(buffer_ptr), transfer_size, max_batch_size);
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
    return m_buffer->get_host_buffer_info(m_transfer_size);
}

IntermediateBuffer::IntermediateBuffer(std::unique_ptr<vdma::VdmaBuffer> &&buffer, uint32_t transfer_size,
                                       uint16_t batch_size) :
    m_buffer(std::move(buffer)),
    m_transfer_size(transfer_size),
    m_dynamic_batch_size(batch_size)
{}

Expected<std::unique_ptr<vdma::VdmaBuffer>> IntermediateBuffer::create_sg_buffer(HailoRTDriver &driver,
    uint32_t transfer_size, uint16_t batch_size, vdma::ChannelId d2h_channel_id, bool is_circular)
{
    auto const DONT_FORCE_DEFAULT_PAGE_SIZE = false;
    auto const FORCE_BATCH_SIZE = true;
    auto const IS_VDMA_ALIGNED_BUFFER = true;
    auto buffer_requirements = vdma::BufferSizesRequirements::get_sg_buffer_requirements_single_transfer(
        driver.desc_max_page_size(), batch_size, batch_size, transfer_size, is_circular, DONT_FORCE_DEFAULT_PAGE_SIZE,
        FORCE_BATCH_SIZE, IS_VDMA_ALIGNED_BUFFER);
    CHECK_EXPECTED(buffer_requirements);
    const auto desc_page_size = buffer_requirements->desc_page_size();
    const auto descs_count = buffer_requirements->descs_count();
    const auto buffer_size = buffer_requirements->buffer_size();

    auto buffer = vdma::SgBuffer::create(driver, buffer_size, descs_count, desc_page_size, is_circular,
        HailoRTDriver::DmaDirection::BOTH, d2h_channel_id);
    CHECK_EXPECTED(buffer);

    auto buffer_ptr = make_unique_nothrow<vdma::SgBuffer>(buffer.release());
    CHECK_NOT_NULL_AS_EXPECTED(buffer_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::unique_ptr<vdma::VdmaBuffer>(std::move(buffer_ptr));
}

Expected<std::unique_ptr<vdma::VdmaBuffer>> IntermediateBuffer::create_ccb_buffer(HailoRTDriver &driver,
    uint32_t transfer_size, uint16_t batch_size, bool is_circular)
{
    auto buffer_size_requirements = vdma::BufferSizesRequirements::get_ccb_buffer_requirements_single_transfer(
        batch_size, transfer_size, is_circular);
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

bool IntermediateBuffer::should_use_ccb(HailoRTDriver &driver, StreamingType streaming_type)
{
    if (driver.dma_type() == HailoRTDriver::DmaType::PCIE) {
        // CCB not supported on PCIe
        return false;
    }

    switch (streaming_type) {
    case StreamingType::BURST:
        // On burst (aka inter-context), because the buffers are big (And depends on the max_batch_size), we currently
        // don't want to use CCB by default.
        if (nullptr != std::getenv("HAILO_FORCE_INFER_CONTEXT_CHANNEL_OVER_DESC")) {
            LOGGER__WARNING("Using desc instead of CCB for inter context channels is not optimal for performance.\n");
            return false;
        } else {
            return true;
        }
    case StreamingType::CIRCULAR_CONTINUOS:
        // On circular_continuous (aka ddr), the buffers are relatively small and we want to verify the C2C mechanism,
        // therefore the CCB is the default behaviour.
        // Due to request from the DFC group (Memory issues) - DDR buffers would run over DESC and not CCB buffers.
        if (nullptr != std::getenv("HAILO_FORCE_DDR_CHANNEL_OVER_CCB")) {
            LOGGER__INFO("Using Non default buffer type (CCB instead of DESC) for ddr channel. \n");
            return true;
        } else {
            return false;
        }
    }

    // Shouldn't reach here
    assert(false);
    return false;
}

} /* namespace hailort */
