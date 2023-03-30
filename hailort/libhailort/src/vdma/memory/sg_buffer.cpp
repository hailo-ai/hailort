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
#include "vdma/memory/mapped_buffer_factory.hpp"


namespace hailort {
namespace vdma {

Expected<SgBuffer> SgBuffer::create(HailoRTDriver &driver, size_t size, uint32_t desc_count, uint16_t desc_page_size,
    HailoRTDriver::DmaDirection data_direction, ChannelId channel_id)
{
    CHECK_AS_EXPECTED(size <= (desc_count * desc_page_size), HAILO_INTERNAL_FAILURE,
        "Requested buffer size {} must be smaller than {}", size, (desc_count * desc_page_size));
    CHECK_AS_EXPECTED((size % desc_page_size) == 0, HAILO_INTERNAL_FAILURE,
        "SgBuffer size must be a multiple of descriptors page size (size {})", size);

    auto mapped_buffer_exp = MappedBufferFactory::create_mapped_buffer(size,
        data_direction, driver);
    CHECK_EXPECTED(mapped_buffer_exp);

    auto mapped_buffer = make_shared_nothrow<DmaMappedBuffer>(mapped_buffer_exp.release());
    CHECK_NOT_NULL_AS_EXPECTED(mapped_buffer, HAILO_OUT_OF_HOST_MEMORY);

    auto desc_list_exp = DescriptorList::create(desc_count, desc_page_size, driver);
    CHECK_EXPECTED(desc_list_exp);

    auto desc_list = make_shared_nothrow<DescriptorList>(desc_list_exp.release());
    CHECK_NOT_NULL_AS_EXPECTED(desc_list, HAILO_OUT_OF_HOST_MEMORY);

    assert((desc_count * desc_page_size) <= std::numeric_limits<uint32_t>::max());

    auto status = desc_list->configure_to_use_buffer(*mapped_buffer, channel_id);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return SgBuffer(mapped_buffer, desc_list);
}

SgBuffer::SgBuffer(std::shared_ptr<DmaMappedBuffer> mapped_buffer, std::shared_ptr<DescriptorList> desc_list) :
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

uint8_t SgBuffer::depth() const
{
    return m_desc_list->depth();
}

std::shared_ptr<DescriptorList> SgBuffer::get_desc_list()
{
    return m_desc_list;
}

// TODO: Remove after HRT-7838
void* SgBuffer::get_user_address()
{
    return m_mapped_buffer->user_address();
}

hailo_status SgBuffer::read(void *buf_dst, size_t count, size_t offset, bool should_sync)
{
    CHECK(count + offset <= m_mapped_buffer->size(), HAILO_INSUFFICIENT_BUFFER);
    if (count == 0) {
        return HAILO_SUCCESS;
    }

    if (should_sync) {
        const auto status = m_mapped_buffer->synchronize();
        CHECK_SUCCESS(status, "Failed synching SgBuffer buffer on read");
    }

    const auto src_addr = static_cast<uint8_t*>(m_mapped_buffer->user_address()) + offset;
    memcpy(buf_dst, src_addr, count);

    return HAILO_SUCCESS;
}
hailo_status SgBuffer::write(const void *buf_src, size_t count, size_t offset)
{
    CHECK(count + offset <= m_mapped_buffer->size(), HAILO_INSUFFICIENT_BUFFER);
    if (count == 0) {
        return HAILO_SUCCESS;
    }

    const auto dst_addr = static_cast<uint8_t*>(m_mapped_buffer->user_address()) + offset;
    std::memcpy(dst_addr, buf_src, count);

    const auto status = m_mapped_buffer->synchronize();
    CHECK_SUCCESS(status, "Failed synching SgBuffer buffer on write");

    return HAILO_SUCCESS;
}

Expected<uint32_t> SgBuffer::program_descriptors(size_t transfer_size, InterruptsDomain last_desc_interrupts_domain,
    size_t desc_offset, bool is_circular)
{
    return m_desc_list->program_last_descriptor(transfer_size, last_desc_interrupts_domain, desc_offset, is_circular);
}

hailo_status SgBuffer::reprogram_device_interrupts_for_end_of_batch(size_t transfer_size, uint16_t batch_size,
        InterruptsDomain new_interrupts_domain)
{
    const auto desc_per_transfer = m_desc_list->descriptors_in_buffer(transfer_size);
    const auto num_desc_in_batch = desc_per_transfer * batch_size;
    const auto last_desc_index_in_batch = num_desc_in_batch - 1;
    return m_desc_list->reprogram_descriptor_interrupts_domain(last_desc_index_in_batch, new_interrupts_domain);
}

}
}