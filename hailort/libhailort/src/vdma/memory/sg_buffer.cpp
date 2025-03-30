/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file sg_buffer.cpp
 * @brief Scatter-gather vdma buffer.
 **/

#include "vdma/memory/sg_buffer.hpp"


namespace hailort {
namespace vdma {

Expected<SgBuffer> SgBuffer::create(HailoRTDriver &driver, size_t size, HailoRTDriver::DmaDirection data_direction)
{
    auto mapped_buffer = MappedBuffer::create_shared_by_allocation(size, driver, data_direction);
    CHECK_EXPECTED(mapped_buffer);

    return SgBuffer(mapped_buffer.release());
}

SgBuffer::SgBuffer(std::shared_ptr<MappedBuffer> mapped_buffer) :
    m_mapped_buffer(mapped_buffer)
{}

size_t SgBuffer::size() const
{
    return m_mapped_buffer->size();
}

hailo_status SgBuffer::read(void *buf_dst, size_t count, size_t offset)
{
    return m_mapped_buffer->read(buf_dst, count, offset);
}
hailo_status SgBuffer::write(const void *buf_src, size_t count, size_t offset)
{
    return m_mapped_buffer->write(buf_src, count, offset);
}

std::shared_ptr<MappedBuffer> SgBuffer::get_mapped_buffer()
{
    return m_mapped_buffer;
}

}
}