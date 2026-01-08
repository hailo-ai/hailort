/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file cma_buffer.hpp
 * @brief Contiguous physical VDMA-buffer allocated via CMA.
 **/

#ifndef _HAILO_VDMA_CMA_BUFFER_HPP_
#define _HAILO_VDMA_CMA_BUFFER_HPP_

#include "vdma/driver/hailort_driver.hpp"
#include "vdma/memory/vdma_buffer.hpp"


namespace hailort {
namespace vdma {

class CmaBuffer final : public ContinuousVdmaBuffer {
public:
    static Expected<CmaBuffer> create(size_t size, HailoRTDriver &driver);

    CmaBuffer(const CmaBuffer &) = delete;
    CmaBuffer& operator=(const CmaBuffer &) = delete;
    CmaBuffer& operator=(CmaBuffer &&) = delete;

    virtual ~CmaBuffer();

    CmaBuffer(CmaBuffer &&other) noexcept :
        ContinuousVdmaBuffer(std::move(other)),
        m_driver(other.m_driver),
        m_buffer_info(std::exchange(other.m_buffer_info,
            CmaBufferInfo{HailoRTDriver::INVALID_DRIVER_BUFFER_HANDLE_VALUE, 0, 0, nullptr}))
    {}

    virtual BufferType type() const override
    {
        return BufferType::CMA;
    }

    virtual size_t size() const override
    {
        return m_buffer_info.size;
    }

    virtual uint64_t dma_address() const override
    {
        return m_buffer_info.dma_address;
    }

    virtual hailo_status read(void *buf_dst, size_t count, size_t offset) override;
    virtual hailo_status write(const void *buf_src, size_t count, size_t offset) override;

    void* user_address() const
    {
        return m_buffer_info.user_address;
    }

private:
    CmaBuffer(HailoRTDriver &driver, const CmaBufferInfo &buffer_info) :
        m_driver(driver), m_buffer_info(buffer_info)
    {}

    HailoRTDriver &m_driver;
    CmaBufferInfo m_buffer_info;
};

}; /* namespace vdma */
}; /* namespace hailort */

#endif /* _HAILO_VDMA_CMA_BUFFER_HPP_ */
