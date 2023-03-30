/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file continuous_buffer.hpp
 * @brief Continuous physical vdma buffer.
 **/

#include "continuous_buffer.hpp"

namespace hailort {
namespace vdma {

// Minimum size of ccb buffers in descriptors, taken from the CCB spec.
#define MIN_CCB_DESCRIPTORS_COUNT (16)

static uint32_t align(uint32_t size, uint32_t align)
{
    assert(is_powerof2(align));
    const uint32_t mask = align - 1;
    return (size + mask) & ~mask;
}

Expected<ContinuousBuffer> ContinuousBuffer::create(size_t size, HailoRTDriver &driver)
{
    auto result = driver.vdma_continuous_buffer_alloc(size);
    CHECK_EXPECTED(result, "Failed allocating continuous buffer, size {}", size);

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

uint32_t ContinuousBuffer::get_buffer_size(uint32_t buffer_size)
{
    const uint16_t page_size = DEFAULT_DESC_PAGE_SIZE;
    const auto aligned_buffer_size = align(buffer_size, page_size);

    const uint32_t min_buffer_size = page_size * MIN_CCB_DESCRIPTORS_COUNT;
    return std::max(aligned_buffer_size, min_buffer_size);
}

uint32_t ContinuousBuffer::get_buffer_size_desc_power2(uint32_t buffer_size)
{
    const uint16_t page_size = DEFAULT_DESC_PAGE_SIZE;
    const auto descriptors_in_buffer = DIV_ROUND_UP(buffer_size, page_size);
    const auto actual_descriptors_count = get_nearest_powerof_2(descriptors_in_buffer, MIN_CCB_DESCRIPTORS_COUNT);
    return actual_descriptors_count * page_size;
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

hailo_status ContinuousBuffer::read(void *buf_dst, size_t count, size_t offset, bool /* should_sync */)
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
    size_t desc_offset, bool is_circular)
{
    (void)last_desc_interrupts_domain;
    (void)desc_offset;
    (void)is_circular;

    // The descriptors in continuous mode are programmed by the hw, nothing to do here.
    return descriptors_in_buffer(transfer_size);
}

hailo_status ContinuousBuffer::reprogram_device_interrupts_for_end_of_batch(size_t transfer_size, uint16_t batch_size,
        InterruptsDomain new_interrupts_domain)
{
    (void)transfer_size;
    (void)batch_size;
    (void)new_interrupts_domain;

    // The descriptors in continuous mode are programmed by the hw, nothing to do here.
    return HAILO_SUCCESS;
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
