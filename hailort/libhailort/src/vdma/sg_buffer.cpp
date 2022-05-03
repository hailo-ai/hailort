/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_sg_buffer.cpp
 * @brief Scatter-gather vdma buffer.
 **/

#include "sg_buffer.hpp"

namespace hailort {
namespace vdma {

Expected<SgBuffer> SgBuffer::create(HailoRTDriver &driver, uint32_t desc_count, uint16_t desc_page_size,
    HailoRTDriver::DmaDirection data_direction, uint8_t channel_index)
{
    auto desc_list = VdmaDescriptorList::create(desc_count, desc_page_size, driver);
    CHECK_EXPECTED(desc_list);

    assert((desc_count * desc_page_size) <= std::numeric_limits<uint32_t>::max());
    auto mapped_buffer = MappedBuffer::create(desc_count * desc_page_size, data_direction, driver);
    CHECK_EXPECTED(mapped_buffer);

    auto status = desc_list->configure_to_use_buffer(mapped_buffer.value(), channel_index);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return SgBuffer(desc_list.release(), mapped_buffer.release());
}

size_t SgBuffer::size() const
{
    return m_mapped_buffer.size();
}

uint64_t SgBuffer::dma_address() const
{
    return m_desc_list.dma_address();
}

uint16_t SgBuffer::desc_page_size() const
{
    return m_desc_list.desc_page_size();
}

uint32_t SgBuffer::descs_count() const
{
    return (uint32_t)m_desc_list.count();
}

uint8_t SgBuffer::depth() const
{
    return m_desc_list.depth();
}

uint32_t SgBuffer::descriptors_in_buffer(size_t buffer_size) const
{
    return m_desc_list.descriptors_in_buffer(buffer_size);
}

ExpectedRef<VdmaDescriptorList> SgBuffer::get_desc_list()
{
    return std::ref(m_desc_list);
}

hailo_status SgBuffer::read(void *buf_dst, size_t count, size_t offset)
{
    return m_mapped_buffer.read(buf_dst, count, offset);
}

hailo_status SgBuffer::write(const void *buf_src, size_t count, size_t offset)
{
    return m_mapped_buffer.write(buf_src, count, offset);
}

Expected<uint32_t> SgBuffer::program_descriptors(size_t transfer_size, VdmaInterruptsDomain first_desc_interrupts_domain,
    VdmaInterruptsDomain last_desc_interrupts_domain, size_t desc_offset, bool is_circular)
{
    return m_desc_list.program_descriptors(transfer_size, first_desc_interrupts_domain, last_desc_interrupts_domain,
        desc_offset, is_circular);
}

Expected<uint16_t> SgBuffer::program_descs_for_ddr_transfers(uint32_t row_size, bool should_raise_interrupt, 
    uint32_t number_of_rows_per_intrpt, uint32_t buffered_rows, uint16_t initial_descs_offset, bool is_circular)
{
    return m_desc_list.program_descs_for_ddr_transfers(row_size, should_raise_interrupt, number_of_rows_per_intrpt,
        buffered_rows, initial_descs_offset, is_circular);
}

}
}