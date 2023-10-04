/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file continuous_buffer.hpp
 * @brief Continuous physical vdma buffer. 
 **/

#ifndef _HAILO_VDMA_CONTINUOUS_BUFFER_HPP_
#define _HAILO_VDMA_CONTINUOUS_BUFFER_HPP_

#include "os/hailort_driver.hpp"
#include "os/mmap_buffer.hpp"
#include "vdma/memory/vdma_buffer.hpp"


namespace hailort {
namespace vdma {

class ContinuousBuffer final : public VdmaBuffer {
public:
    static Expected<ContinuousBuffer> create(size_t size, HailoRTDriver &driver);

    ContinuousBuffer(const ContinuousBuffer &) = delete;
    ContinuousBuffer& operator=(const ContinuousBuffer &) = delete;
    ContinuousBuffer& operator=(ContinuousBuffer &&) = delete;

    virtual ~ContinuousBuffer();

    ContinuousBuffer(ContinuousBuffer &&other) noexcept :
        VdmaBuffer(std::move(other)),
        m_size(other.m_size),
        m_driver(other.m_driver),
        m_handle(std::exchange(other.m_handle, 0)),
        m_dma_address(std::exchange(other.m_dma_address, 0)),
        m_mmap(std::move(other.m_mmap))
    {}

    virtual Type type() const override
    {
        return Type::CONTINUOUS;
    }

    virtual size_t size() const override;
    virtual uint64_t dma_address() const override;
    virtual uint16_t desc_page_size() const override;
    virtual uint32_t descs_count() const override;

    virtual hailo_status read(void *buf_dst, size_t count, size_t offset) override;
    virtual hailo_status write(const void *buf_src, size_t count, size_t offset) override;

    virtual Expected<uint32_t> program_descriptors(size_t transfer_size, InterruptsDomain last_desc_interrupts_domain,
        size_t desc_offset) override;

private:
    ContinuousBuffer(size_t size, HailoRTDriver &driver, uintptr_t handle, uint64_t dma_address,
        MmapBuffer<void> &&mmap);

    const size_t m_size;
    HailoRTDriver &m_driver;
    uintptr_t m_handle;
    uint64_t m_dma_address;
    MmapBuffer<void> m_mmap;
};

}; /* namespace vdma */
}; /* namespace hailort */

#endif /* _HAILO_VDMA_CONTINUOUS_BUFFER_HPP_ */
