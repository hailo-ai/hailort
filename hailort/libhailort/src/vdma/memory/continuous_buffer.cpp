/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file continuous_buffer.hpp
 * @brief Continuous physical vdma buffer.
 **/

#include "continuous_buffer.hpp"

/* TODO - Support non default CCB page sizes */
#define CCB_PAGE_SIZE (512)
#define MAX_PAGES_PER_INTERRUPT (0x0003FFFF)
#define MAX_CCB_BUFFER_SIZE (CCB_PAGE_SIZE * MAX_PAGES_PER_INTERRUPT)

namespace hailort {
namespace vdma {

Expected<ContinuousBuffer> ContinuousBuffer::create(size_t size, HailoRTDriver &driver)
{
    if (size > MAX_CCB_BUFFER_SIZE) {
        LOGGER__INFO("continious memory size {} must be smaller/equal to {}.", size, MAX_CCB_BUFFER_SIZE);
        return make_unexpected(HAILO_OUT_OF_HOST_CMA_MEMORY);
    }

    auto result = driver.vdma_continuous_buffer_alloc(size);
    /* Don't print error here since this might be expected error that the libhailoRT can recover from
        (out of host memory). If it's not the case, there is a print in hailort_driver.cpp file */
    if (HAILO_OUT_OF_HOST_CMA_MEMORY == result.status()) {
        return make_unexpected(result.status());
    } else {
        CHECK_EXPECTED(result);
    }

    uintptr_t handle = 0;
    uint64_t dma_address = 0;
    std::tie(handle, dma_address) = result.release();

    auto mmap = MmapBuffer<void>::create_file_map(size, driver.fd(), handle);
    if (!mmap) {
        LOGGER__ERROR("Failed mmap continuous buffer");
        driver.vdma_continuous_buffer_free(handle);
        return make_unexpected(mmap.status());
    }

    return ContinuousBuffer(size, driver, handle, dma_address, mmap.release());
}

ContinuousBuffer::~ContinuousBuffer()
{
    if (0 != m_handle) {
        auto status = m_mmap.unmap();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed unmap mmap buffer {}", status);
        }

        status = m_driver.vdma_continuous_buffer_free(m_handle);
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed free continuous buffer, {}", status);
        }

        m_handle = 0;
    }
}

size_t ContinuousBuffer::size() const
{
    return m_size;
}

uint64_t ContinuousBuffer::dma_address() const
{
    return m_dma_address;
}

uint16_t ContinuousBuffer::desc_page_size() const
{
    // Currently we support only the default desc page size, TODO: HRT-5381 support more desc page size?
    return DEFAULT_DESC_PAGE_SIZE;
}

uint32_t ContinuousBuffer::descs_count() const
{
    return descriptors_in_buffer(m_size);
}

hailo_status ContinuousBuffer::read(void *buf_dst, size_t count, size_t offset)
{
    CHECK((count + offset) <= m_size, HAILO_INSUFFICIENT_BUFFER,
        "Requested size {} from offset {} is more than the buffer size {}", count, offset, m_size);
    // We use dma coherent mmap, so no need to sync the buffer after the memcpy.
    const auto src_address = reinterpret_cast<uint8_t*>(m_mmap.address()) + offset;
    memcpy(buf_dst, src_address, count);
    return HAILO_SUCCESS;
}

hailo_status ContinuousBuffer::write(const void *buf_src, size_t count, size_t offset)
{
    CHECK((count + offset) <= m_size, HAILO_INSUFFICIENT_BUFFER,
        "Requested size {} from offset {} is more than the buffer size {}", count, offset, m_size);
    // We use dma coherent mmap, so no need to sync the buffer after the memcpy.
    const auto dst_address = reinterpret_cast<uint8_t*>(m_mmap.address()) + offset;
    memcpy(dst_address, buf_src, count);
    return HAILO_SUCCESS;
}

Expected<uint32_t> ContinuousBuffer::program_descriptors(size_t transfer_size, InterruptsDomain last_desc_interrupts_domain,
    size_t desc_offset)
{
    (void)last_desc_interrupts_domain;
    (void)desc_offset;

    // The descriptors in continuous mode are programmed by the hw, nothing to do here.
    return descriptors_in_buffer(transfer_size);
}

ContinuousBuffer::ContinuousBuffer(size_t size, HailoRTDriver &driver, uintptr_t handle, uint64_t dma_address,
    MmapBuffer<void> &&mmap) :
    m_size(size),
    m_driver(driver),
    m_handle(handle),
    m_dma_address(dma_address),
    m_mmap(std::move(mmap))
{}

}; /* namespace vdma */
}; /* namespace hailort */
