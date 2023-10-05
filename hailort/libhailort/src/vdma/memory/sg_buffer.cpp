/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_sg_buffer.cpp
 * @brief Scatter-gather vdma buffer.
 **/

#include "vdma/memory/sg_buffer.hpp"
#include "vdma/channel/channel_id.hpp"


namespace hailort {
namespace vdma {

Expected<SgBuffer> SgBuffer::create(HailoRTDriver &driver, size_t size, uint32_t desc_count, uint16_t desc_page_size,
    bool is_circular, HailoRTDriver::DmaDirection data_direction, ChannelId channel_id)
{
    CHECK_AS_EXPECTED(size <= (desc_count * desc_page_size), HAILO_INTERNAL_FAILURE,
        "Requested buffer size {} must be smaller than {}", size, (desc_count * desc_page_size));
    CHECK_AS_EXPECTED((size % desc_page_size) == 0, HAILO_INTERNAL_FAILURE,
        "SgBuffer size must be a multiple of descriptors page size (size {})", size);

    auto mapped_buffer = MappedBuffer::create_shared(driver, data_direction, size);
    CHECK_EXPECTED(mapped_buffer);

    auto desc_list_exp = DescriptorList::create(desc_count, desc_page_size, is_circular, driver);
    CHECK_EXPECTED(desc_list_exp);

    auto desc_list = make_shared_nothrow<DescriptorList>(desc_list_exp.release());
    CHECK_NOT_NULL_AS_EXPECTED(desc_list, HAILO_OUT_OF_HOST_MEMORY);

    assert((desc_count * desc_page_size) <= std::numeric_limits<uint32_t>::max());

    auto status = desc_list->configure_to_use_buffer(*mapped_buffer.value(), channel_id);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return SgBuffer(mapped_buffer.release(), desc_list);
}

SgBuffer::SgBuffer(std::shared_ptr<MappedBuffer> mapped_buffer, std::shared_ptr<DescriptorList> desc_list) :
    m_mapped_buffer(mapped_buffer),
    m_desc_list(desc_list)
{}

size_t SgBuffer::size() const
{
    return m_mapped_buffer->size();
}

uint64_t SgBuffer::dma_address() const
{
    return m_desc_list->dma_address();
}

uint16_t SgBuffer::desc_page_size() const
{
    return m_desc_list->desc_page_size();
}

uint32_t SgBuffer::descs_count() const
{
    return static_cast<uint32_t>(m_desc_list->count());
}

hailo_status SgBuffer::read(void *buf_dst, size_t count, size_t offset)
{
    return m_mapped_buffer->read(buf_dst, count, offset);
}
hailo_status SgBuffer::write(const void *buf_src, size_t count, size_t offset)
{
    return m_mapped_buffer->write(buf_src, count, offset);
}

Expected<uint32_t> SgBuffer::program_descriptors(size_t transfer_size, InterruptsDomain last_desc_interrupts_domain,
    size_t desc_offset)
{
    return m_desc_list->program_last_descriptor(transfer_size, last_desc_interrupts_domain, desc_offset);
}

}
}