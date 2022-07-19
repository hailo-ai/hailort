/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file config_buffer.cpp
 * @brief Manages configuration vdma buffer. The configuration buffer contains nn-configurations in a specific
 *        hw format (ccw).
 */

#include "config_buffer.hpp"
#include "vdma/sg_buffer.hpp"
#include "vdma/continuous_buffer.hpp"

#include <numeric>

namespace hailort {

Expected<ConfigBuffer> ConfigBuffer::create(HailoRTDriver &driver, uint8_t vdma_channel_index,
    const std::vector<uint32_t> &cfg_sizes)
{
    const auto buffer_size = std::accumulate(cfg_sizes.begin(), cfg_sizes.end(), 0);

    auto buffer_ptr = should_use_ccb(driver) ?
        create_ccb_buffer(driver, buffer_size) :
        create_sg_buffer(driver, vdma_channel_index, cfg_sizes);
    CHECK_EXPECTED(buffer_ptr);

    return ConfigBuffer(buffer_ptr.release(), vdma_channel_index, buffer_size);
}

ConfigBuffer::ConfigBuffer(std::unique_ptr<vdma::VdmaBuffer> &&buffer,
    uint8_t vdma_channel_index,
    size_t total_buffer_size)
    : m_buffer(std::move(buffer)),
      m_vdma_channel_index(vdma_channel_index),
      m_total_buffer_size(total_buffer_size), m_acc_buffer_offset(0), m_acc_desc_count(0),
      m_current_buffer_size(0)
{}

Expected<uint32_t> ConfigBuffer::program_descriptors()
{
    auto descriptors_count =
        m_buffer->program_descriptors(m_acc_buffer_offset,  VdmaInterruptsDomain::NONE, VdmaInterruptsDomain::DEVICE,
        m_acc_desc_count, false);
    CHECK_EXPECTED(descriptors_count);

    m_acc_desc_count += descriptors_count.value();
    m_acc_buffer_offset = 0;

    return descriptors_count;
}

hailo_status ConfigBuffer::write(const void *data, size_t data_size)
{
    size_t total_offset = (m_acc_desc_count * m_buffer->desc_page_size()) + m_acc_buffer_offset;
    auto status = m_buffer->write(data, data_size, total_offset);
    CHECK_SUCCESS(status);

    m_acc_buffer_offset += data_size;
    m_current_buffer_size += data_size;
    return HAILO_SUCCESS;
}

size_t ConfigBuffer::get_total_cfg_size()
{
    return m_total_buffer_size;
}

size_t ConfigBuffer::get_current_buffer_size()
{
    return m_current_buffer_size;
}

uint16_t ConfigBuffer::desc_page_size() const
{
    return m_buffer->desc_page_size();
}

CONTROL_PROTOCOL__config_channel_info_t ConfigBuffer::get_config_channel_info() const
{
    CONTROL_PROTOCOL__config_channel_info_t config_channel_info;
    config_channel_info.config_buffer_info = m_buffer->get_host_buffer_info(m_acc_desc_count * m_buffer->desc_page_size());
    config_channel_info.vdma_channel_index = m_vdma_channel_index;
    return config_channel_info;
}

Expected<std::unique_ptr<vdma::VdmaBuffer>> ConfigBuffer::create_sg_buffer(HailoRTDriver &driver,
    uint8_t vdma_channel_index, const std::vector<uint32_t> &cfg_sizes)
{
    auto desc_sizes_pair = VdmaDescriptorList::get_desc_buffer_sizes_for_multiple_transfers(driver, 1, cfg_sizes);
    CHECK_EXPECTED(desc_sizes_pair);

    auto page_size = desc_sizes_pair->first;
    auto descs_count = desc_sizes_pair->second;

    auto buffer = vdma::SgBuffer::create(driver, descs_count, page_size, HailoRTDriver::DmaDirection::H2D,
        vdma_channel_index);
    CHECK_EXPECTED(buffer);

    auto buffer_ptr = make_unique_nothrow<vdma::SgBuffer>(buffer.release());
    CHECK_NOT_NULL_AS_EXPECTED(buffer_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::unique_ptr<vdma::VdmaBuffer>(std::move(buffer_ptr));
}

Expected<std::unique_ptr<vdma::VdmaBuffer>> ConfigBuffer::create_ccb_buffer(HailoRTDriver &driver,
    uint32_t buffer_size)
{
    buffer_size = vdma::ContinuousBuffer::get_buffer_size(buffer_size);
    auto buffer = vdma::ContinuousBuffer::create(buffer_size, driver);
    CHECK_EXPECTED(buffer);

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
        return true;
    }

    // Shouldn't reach here
    assert(false);
    return false;
}

} /* hailort */