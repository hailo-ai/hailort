/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file continuous_buffer.cpp
 * @brief Continuous physical vdma buffer.
 **/

#include "continuous_buffer.hpp"

namespace hailort {
namespace vdma {

Expected<ContinuousBuffer> ContinuousBuffer::create(size_t size, HailoRTDriver &driver)
{
    if (size < MIN_CCB_PAGE_SIZE * MIN_CCB_DESCS_COUNT) {
        LOGGER__ERROR("continuous memory size ({}) must be larger/equal to {}.", size, (MIN_CCB_PAGE_SIZE * MIN_CCB_DESCS_COUNT));
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }

    auto result = driver.vdma_continuous_buffer_alloc(size);
    /* Don't print error here since this might be expected error that the libhailoRT can recover from
        (out of host memory). If it's not the case, there is a print in hailort_driver.cpp file */
    if (HAILO_OUT_OF_HOST_CMA_MEMORY == result.status()) {
        return make_unexpected(result.status());
    } else {
        CHECK_EXPECTED(result);
    }

    return ContinuousBuffer(driver, result.release());
}

ContinuousBuffer::~ContinuousBuffer()
{
    if (HailoRTDriver::INVALID_DRIVER_BUFFER_HANDLE_VALUE != m_buffer_info.handle) {
        auto status = m_driver.vdma_continuous_buffer_free(m_buffer_info);
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed free continuous buffer, {}", status);
        }
    }
}

size_t ContinuousBuffer::size() const
{
    return m_buffer_info.size;
}

uint64_t ContinuousBuffer::dma_address() const
{
    return m_buffer_info.dma_address;
}

void* ContinuousBuffer::user_address() const
{
    return m_buffer_info.user_address;
}

hailo_status ContinuousBuffer::read(void *buf_dst, size_t count, size_t offset)
{
    CHECK((count + offset) <= m_buffer_info.size, HAILO_INSUFFICIENT_BUFFER,
        "Requested size {} from offset {} is more than the buffer size {}", count, offset, m_buffer_info.size);
    // We use dma coherent mmap, so no need to sync the buffer after the memcpy.
    const auto src_address = reinterpret_cast<uint8_t*>(m_buffer_info.user_address) + offset;
    memcpy(buf_dst, src_address, count);
    return HAILO_SUCCESS;
}

hailo_status ContinuousBuffer::write(const void *buf_src, size_t count, size_t offset)
{
    CHECK((count + offset) <= m_buffer_info.size, HAILO_INSUFFICIENT_BUFFER,
        "Requested size {} from offset {} is more than the buffer size {}", count, offset, m_buffer_info.size);
    // We use dma coherent mmap, so no need to sync the buffer after the memcpy.
    const auto dst_address = reinterpret_cast<uint8_t*>(m_buffer_info.user_address) + offset;
    memcpy(dst_address, buf_src, count);
    return HAILO_SUCCESS;
}

ContinuousBuffer::ContinuousBuffer(HailoRTDriver &driver,
        const ContinousBufferInfo &buffer_info) :
    m_driver(driver),
    m_buffer_info(buffer_info)
{}

}; /* namespace vdma */
}; /* namespace hailort */
