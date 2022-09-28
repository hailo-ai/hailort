/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file mapped_buffer.hpp
 * @brief The mapped buffer that is continuous in virtual memory, but not on physical memory.
 *        We map the buffer to the IOMMU.
 * 
 * The buffer can be used only with the help of a descriptors list that contains pointers to a physical
 * continuous "dma pages".
 * 
 * There are 2 options to allocated the buffer:
 *      1. User mode allocation - the user mode calls `malloc` or `mmap` to allocate the buffer, then
 *         using HailoRTDriver we map the driver to the IOMMU (and pin the pages to avoid pagigs).
 *         This is the default option
 *      2. Kernel mode allocation - on some systems, the user mode doesn't allocate the memory in a "dma-able" address,
 *         so we need to allocate the pages in driver.
 **/

#ifndef _HAILO_VDMA_MAPPED_BUFFER_HPP_
#define _HAILO_VDMA_MAPPED_BUFFER_HPP_

#include "os/mmap_buffer.hpp"
#include "os/hailort_driver.hpp"
#include "hailo/expected.hpp"
#include "vdma_mapped_buffer_impl.hpp"

namespace hailort {
namespace vdma {

class MappedBuffer final
{
public:
    static Expected<MappedBuffer> create(size_t required_size, HailoRTDriver::DmaDirection data_direction,
        HailoRTDriver &driver);

    MappedBuffer(size_t required_size, HailoRTDriver::DmaDirection data_direction,
        HailoRTDriver &driver, hailo_status &status);
    ~MappedBuffer();

    MappedBuffer(const MappedBuffer &other) = delete;
    MappedBuffer &operator=(const MappedBuffer &other) = delete;
    MappedBuffer(MappedBuffer &&other) noexcept = default;
    MappedBuffer &operator=(MappedBuffer &&other) = delete;

    void *user_address() { return m_vdma_mapped_buffer->get(); }
    HailoRTDriver::VdmaBufferHandle handle() { return m_handle; }
    size_t size() const { return m_size; }

    /**
     * Copy data from buf_src parameter to this MappedBuffer.
     *
     * @note (offset + count) MUST be smaller than this MappedBuffer size
     *
     * @param[in] buf_src The buffer to copy the data from
     * @param[in] count Number of bytes to copy from buf_src
     * @param[in] offset The offset relative to this MappedBuffer to copy the data to
     */
    hailo_status write(const void *buf_src, size_t count, size_t offset);

    /**
     * Copy data from this MappedBuffer to buf_dst.
     *
     * @note (offset + count) MUST be smaller than this MappedBuffer size
     *
     * @param[out] buf_dst The buffer to copy the data to
     * @param[in] count Number of bytes to copy to buf_dst
     * @param[in] offset The offset relative to this MappedBuffer to copy the data from
     */
    hailo_status read(void *buf_dst, size_t count, size_t offset);

    /**
     * Copy data from buf_src parameter to this MappedBuffer.
     * 
     * Similar to 'write' but if (offset + count) is larger than the MappedBuffer size, the copy continues
     * from the start of the MappedBuffer.
     *
     * @note count MUST be smaller than this MappedBuffer size
     *
     * @param[in] buf_src The buffer to copy the data from
     * @param[in] count Number of bytes to copy from buf_src
     * @param[in] offset The offset relative to this MappedBuffer to copy the data to
     */
    hailo_status write_cyclic(const void *buf_src, size_t count, size_t offset);

    /**
     * Copy data from this MappedBuffer to buf_dst.
     *
     * Similar to 'read' but if (offset + count) is larger than the MappedBuffer size, the copy continues
     * from the start of the MappedBuffer.
     *
     * @note count MUST be smaller than this MappedBuffer size
     *
     * @param[out] buf_dst The buffer to copy the data to
     * @param[in] count Number of bytes to copy to buf_dst
     * @param[in] offset The offset relative to this MappedBuffer to copy the data from
     */
    hailo_status read_cyclic(void *buf_dst, size_t count, size_t offset);

private:

    std::unique_ptr<VdmaMappedBufferImpl> m_vdma_mapped_buffer;
    HailoRTDriver::VdmaBufferHandle m_handle;
    size_t m_size;
    HailoRTDriver &m_driver;
};

} /* namespace vdma */
} /* namespace hailort */

#endif /* _HAILO_VDMA_MAPPED_BUFFER_HPP_ */