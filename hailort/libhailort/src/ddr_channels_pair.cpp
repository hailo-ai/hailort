/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file ddr_channels_pair.cpp
 **/

#include "ddr_channels_pair.hpp"
#include "vdma/continuous_buffer.hpp"
#include "vdma/sg_buffer.hpp"
#include "common/utils.hpp"

namespace hailort
{


Expected<DdrChannelsPair> DdrChannelsPair::create(HailoRTDriver &driver, const DdrChannelsInfo &ddr_channels_info)
{
    auto buffer_exp = should_use_ccb(driver) ?
        create_ccb_buffer(driver, ddr_channels_info.row_size, ddr_channels_info.min_buffered_rows) :
        create_sg_buffer(driver, ddr_channels_info.row_size, ddr_channels_info.min_buffered_rows);
    CHECK_EXPECTED(buffer_exp);
    auto buffer_ptr = buffer_exp.release();

    CHECK_AS_EXPECTED(0 == (ddr_channels_info.row_size % buffer_ptr->desc_page_size()), HAILO_INTERNAL_FAILURE,
        "DDR channel buffer row size must be a multiple of descriptor page size");

    const auto interrupts_domain = VdmaInterruptsDomain::NONE;
    const auto total_size = buffer_ptr->descs_count() * buffer_ptr->desc_page_size();
    auto desc_count_local = buffer_ptr->program_descriptors(total_size, interrupts_domain, interrupts_domain, 0, true);
    CHECK_EXPECTED(desc_count_local);

    return DdrChannelsPair(std::move(buffer_ptr), ddr_channels_info);
}

uint16_t DdrChannelsPair::descs_count() const
{
    assert(IS_FIT_IN_UINT16(m_buffer->descs_count()));
    return static_cast<uint16_t>(m_buffer->descs_count());
}

uint32_t DdrChannelsPair::descriptors_per_frame() const
{
    return (m_info.row_size / m_buffer->desc_page_size()) * m_info.total_buffers_per_frame;
}

Expected<Buffer> DdrChannelsPair::read() const
{
    const auto size = m_buffer->size();
    auto res = Buffer::create(size);
    CHECK_EXPECTED(res);

    auto status = m_buffer->read(res->data(), size, 0);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return res.release();
}

const DdrChannelsInfo& DdrChannelsPair::info() const
{
    return m_info;
}


bool DdrChannelsPair::need_manual_credit_management() const
{
    // On scatter gather manual credit management is needed
    return m_buffer->type() == vdma::VdmaBuffer::Type::SCATTER_GATHER;
}

CONTROL_PROTOCOL__host_buffer_info_t DdrChannelsPair::get_host_buffer_info() const
{
    return m_buffer->get_host_buffer_info(m_info.row_size);
}

Expected<std::unique_ptr<vdma::VdmaBuffer>> DdrChannelsPair::create_sg_buffer(HailoRTDriver &driver,
    uint32_t row_size, uint16_t buffered_rows)
{
    auto desc_sizes_pair = VdmaDescriptorList::get_desc_buffer_sizes_for_single_transfer(driver,
        buffered_rows, buffered_rows, row_size);
    CHECK_EXPECTED(desc_sizes_pair);
    auto desc_page_size = desc_sizes_pair->first;
    auto descs_count = desc_sizes_pair->second;

    auto buffer = vdma::SgBuffer::create(driver, descs_count, desc_page_size,
        HailoRTDriver::DmaDirection::BOTH);
    CHECK_EXPECTED(buffer);

    auto buffer_ptr = make_unique_nothrow<vdma::SgBuffer>(buffer.release());
    CHECK_NOT_NULL_AS_EXPECTED(buffer_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::unique_ptr<vdma::VdmaBuffer>(std::move(buffer_ptr));
}

DdrChannelsPair::DdrChannelsPair(std::unique_ptr<vdma::VdmaBuffer> &&buffer, const DdrChannelsInfo &ddr_channels_info) :
    m_buffer(std::move(buffer)),
    m_info(ddr_channels_info)
{}

Expected<std::unique_ptr<vdma::VdmaBuffer>> DdrChannelsPair::create_ccb_buffer(HailoRTDriver &driver,
    uint32_t row_size, uint16_t buffered_rows)
{
    // The first 12 channels in D2H CCB ("regular channels") requires that the amount of descriptors will be a power
    // of 2. Altough the 4 last channels ("enhanced channels") don't have this requirements, we keep the code the same.
    auto buffer_size = vdma::ContinuousBuffer::get_buffer_size_desc_power2(row_size * buffered_rows);
    auto buffer = vdma::ContinuousBuffer::create(buffer_size, driver);
    CHECK_EXPECTED(buffer);

    auto buffer_ptr = make_unique_nothrow<vdma::ContinuousBuffer>(buffer.release());
    CHECK_NOT_NULL_AS_EXPECTED(buffer_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::unique_ptr<vdma::VdmaBuffer>(std::move(buffer_ptr));
}

bool DdrChannelsPair::should_use_ccb(HailoRTDriver &driver)
{
    switch (driver.dma_type()) {
    case HailoRTDriver::DmaType::PCIE:
        return false;
    case HailoRTDriver::DmaType::DRAM:
        return true;
    }


    // Shouldn't reach here
    assert(false);
    return false;
}

} /* namespace hailort */
