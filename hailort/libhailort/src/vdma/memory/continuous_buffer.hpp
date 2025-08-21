/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file continuous_edge_layer.hpp
 * @brief Continuous physical vdma edge layer.
 **/

#ifndef _HAILO_VDMA_CONTINUOUS_BUFFER_HPP_
#define _HAILO_VDMA_CONTINUOUS_BUFFER_HPP_

#include "vdma/driver/hailort_driver.hpp"
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
        m_driver(other.m_driver),
        m_buffer_info(std::exchange(other.m_buffer_info,
            ContinousBufferInfo{HailoRTDriver::INVALID_DRIVER_BUFFER_HANDLE_VALUE, 0, 0, nullptr}))
    {}

    virtual Type type() const override
    {
        return Type::CONTINUOUS;
    }

    virtual size_t size() const override;
    virtual hailo_status read(void *buf_dst, size_t count, size_t offset) override;
    virtual hailo_status write(const void *buf_src, size_t count, size_t offset) override;

    void *user_address() const;
    uint64_t dma_address() const;
private:
    ContinuousBuffer(HailoRTDriver &driver, const ContinousBufferInfo &buffer_info);

    HailoRTDriver &m_driver;

    ContinousBufferInfo m_buffer_info;
};

}; /* namespace vdma */
}; /* namespace hailort */

#endif /* _HAILO_VDMA_CONTINUOUS_BUFFER_HPP_ */
