/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_buffer.hpp
 * @brief Provides a buffer that can be used for VDMA
 *
 **/

#ifndef _HAILO_VDMA_BUFFER_HPP_
#define _HAILO_VDMA_BUFFER_HPP_

#include "os/mmap_buffer.hpp"
#include "os/hailort_driver.hpp"
#include "hailo/expected.hpp"

namespace hailort
{

class VdmaBuffer final
{
public:
    static Expected<VdmaBuffer> create(size_t required_size, HailoRTDriver::DmaDirection data_direction,
        HailoRTDriver &driver);

    VdmaBuffer(size_t required_size, HailoRTDriver::DmaDirection data_direction,
        HailoRTDriver &driver, hailo_status &status);
    ~VdmaBuffer();

    VdmaBuffer(const VdmaBuffer &other) = delete;
    VdmaBuffer &operator=(const VdmaBuffer &other) = delete;
    VdmaBuffer(VdmaBuffer &&other) noexcept = default;
    VdmaBuffer &operator=(VdmaBuffer &&other) = delete;

    void *user_address() { return m_user_address.get(); }
    size_t handle() { return m_handle; }
    size_t size() const { return m_size; }

    /**
     * Copy data from buf_src parameter to this VdmaBuffer.
     *
     * @note (offset + count) MUST be smaller than this VdmaBuffer size
     *
     * @param[in] buf_src The buffer to copy the data from
     * @param[in] count Number of bytes to copy from buf_src
     * @param[in] offset The offset relative to this VdmaBuffer to copy the data to
     */
    hailo_status write(const void *buf_src, size_t count, size_t offset);

    /**
     * Copy data from this VdmaBuffer to buf_dst.
     *
     * @note (offset + count) MUST be smaller than this VdmaBuffer size
     *
     * @param[out] buf_dst The buffer to copy the data to
     * @param[in] count Number of bytes to copy to buf_dst
     * @param[in] offset The offset relative to this VdmaBuffer to copy the data from
     */
    hailo_status read(void *buf_dst, size_t count, size_t offset);

    /**
     * Copy data from buf_src parameter to this VdmaBuffer.
     * 
     * Similar to 'write' but if (offset + count) is larger than the VdmaBuffer size, the copy continues
     * from the start of the VdmaBuffer.
     *
     * @note count MUST be smaller than this VdmaBuffer size
     *
     * @param[in] buf_src The buffer to copy the data from
     * @param[in] count Number of bytes to copy from buf_src
     * @param[in] offset The offset relative to this VdmaBuffer to copy the data to
     */
    hailo_status write_cyclic(const void *buf_src, size_t count, size_t offset);

    /**
     * Copy data from this VdmaBuffer to buf_dst.
     *
     * Similar to 'read' but if (offset + count) is larger than the VdmaBuffer size, the copy continues
     * from the start of the VdmaBuffer.
     *
     * @note count MUST be smaller than this VdmaBuffer size
     *
     * @param[out] buf_dst The buffer to copy the data to
     * @param[in] count Number of bytes to copy to buf_dst
     * @param[in] offset The offset relative to this VdmaBuffer to copy the data from
     */
    hailo_status read_cyclic(void *buf_dst, size_t count, size_t offset);

private:

    static Expected<MmapBuffer<void>> allocate_vdma_buffer(HailoRTDriver &driver, size_t required_size,
        uintptr_t &driver_buff_handle);

    MmapBuffer<void> m_user_address;
    HailoRTDriver::VdmaBufferHandle m_handle;
    size_t m_size;
    HailoRTDriver &m_driver;
    uintptr_t m_driver_buff_handle;
};

} /* namespace hailort */

#endif /* _HAILO_VDMA_BUFFER_HPP_ */