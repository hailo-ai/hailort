/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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

#include "hailo/expected.hpp"
#include "vdma/driver/hailort_driver.hpp"
#include "vdma/memory/dma_able_buffer.hpp"

#include <memory>


namespace hailort {
namespace vdma {

#define INVALID_FD (-1)

class MappedBuffer;
using MappedBufferPtr = std::shared_ptr<MappedBuffer>;

class MappedBuffer final
{
public:
    // Maps the given DmaAbleBuffer in 'data_direction'
    static Expected<MappedBufferPtr> create_shared(DmaAbleBufferPtr buffer, HailoRTDriver &driver,
        HailoRTDriver::DmaDirection data_direction);

    // A DmaAbleBuffer of 'size' bytes will be allocated and mapped to dma in 'data_direction'
    static Expected<MappedBufferPtr> create_shared_by_allocation(size_t size, HailoRTDriver &driver,
        HailoRTDriver::DmaDirection data_direction);

    // Receive an fd to a dmabuf object and map it in our driver and create MappedBuffer
    static Expected<MappedBufferPtr> create_shared_from_dmabuf(int dmabuf_fd, size_t size, HailoRTDriver &driver,
        HailoRTDriver::DmaDirection data_direction);

    MappedBuffer(HailoRTDriver &driver, DmaAbleBufferPtr buffer, HailoRTDriver::DmaDirection data_direction,
        HailoRTDriver::VdmaBufferHandle vdma_buffer_handle, size_t size, int fd = INVALID_FD);
    MappedBuffer(MappedBuffer &&other) noexcept;
    MappedBuffer(const MappedBuffer &other) = delete;
    MappedBuffer &operator=(const MappedBuffer &other) = delete;
    MappedBuffer &operator=(MappedBuffer &&other) = delete;
    ~MappedBuffer();

    size_t size() const;
    void *user_address();
    HailoRTDriver::DmaDirection direction() const;
    HailoRTDriver::VdmaBufferHandle handle();
    hailo_status synchronize(HailoRTDriver::DmaSyncDirection sync_direction);
    // TODO: validate that offset is cache aligned (HRT-9811)
    hailo_status synchronize(size_t offset, size_t count, HailoRTDriver::DmaSyncDirection sync_direction);
    Expected<int> fd();

    /**
     * Copy data from buf_src parameter to this buffer.
     */
    hailo_status write(const void *buf_src, size_t count, size_t offset, bool should_sync = true);

    /**
     * Copy data from this buffer to buf_dst.
     */
    hailo_status read(void *buf_dst, size_t count, size_t offset, bool should_sync = true);

    /**
     * Copy data from buf_src parameter to this buffer.
     *
     * Similar to 'write' but if (offset + count) is larger than the buffer size, the copy continues
     * from the start of the buffer.
     */
    hailo_status write_cyclic(const void *buf_src, size_t count, size_t offset,  bool should_sync = true);

    /**
     * Copy data from this buffer to buf_dst.
     *
     * Similar to 'read' but if (offset + count) is larger than the DmaMappedBuffer size, the copy continues
     * from the start of the buffer.
     */
    hailo_status read_cyclic(void *buf_dst, size_t count, size_t offset, bool should_sync = true);

private:
    HailoRTDriver &m_driver;
    // TODO: do we need to hold a DmaAbleBuffer here? (HRT-12389)
    DmaAbleBufferPtr m_buffer;
    HailoRTDriver::VdmaBufferHandle m_mapping_handle;
    const HailoRTDriver::DmaDirection m_data_direction;
    size_t m_size;
    int m_fd;
};

} /* namespace vdma */
} /* namespace hailort */

#endif /* _HAILO_VDMA_MAPPED_BUFFER_HPP_ */