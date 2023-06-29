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

#ifndef _HAILO_DMA_MAPPED_BUFFER_HPP_
#define _HAILO_DMA_MAPPED_BUFFER_HPP_

#include "hailo/expected.hpp"
#include "os/hailort_driver.hpp"
#include "vdma/memory/dma_able_buffer.hpp"

#include <memory>


namespace hailort {
namespace vdma {


class MappedBuffer;
using MappedBufferPtr = std::shared_ptr<MappedBuffer>;

class MappedBuffer final
{
public:
    // Maps the given DmaAbleBuffer in the right direction.
    static Expected<MappedBuffer> create(HailoRTDriver &driver, std::shared_ptr<DmaAbleBuffer> buffer,
        HailoRTDriver::DmaDirection data_direction);
    static Expected<MappedBufferPtr> create_shared(HailoRTDriver &driver, std::shared_ptr<DmaAbleBuffer> buffer,
        HailoRTDriver::DmaDirection data_direction);

    // If user_address is nullptr, a buffer of size 'size' will be allocated and mapped to dma in 'data_direction'
    // Otherwise, the buffer pointed to by user_address will be mapped to dma in 'data_direction'
    static Expected<MappedBuffer> create(HailoRTDriver &driver, HailoRTDriver::DmaDirection data_direction,
        size_t size, void *user_address = nullptr);
    static Expected<MappedBufferPtr> create_shared(HailoRTDriver &driver, HailoRTDriver::DmaDirection data_direction,
        size_t size, void *user_address = nullptr);


    MappedBuffer(MappedBuffer &&other) noexcept;
    MappedBuffer(const MappedBuffer &other) = delete;
    MappedBuffer &operator=(const MappedBuffer &other) = delete;
    MappedBuffer &operator=(MappedBuffer &&other) = delete;
    ~MappedBuffer();

    size_t size() const;
    void *user_address();
    HailoRTDriver::VdmaBufferHandle handle();
    hailo_status synchronize(HailoRTDriver::DmaSyncDirection sync_direction);
    // TODO: validate that offset is cache aligned (HRT-9811)
    hailo_status synchronize(size_t offset, size_t count, HailoRTDriver::DmaSyncDirection sync_direction);

private:
    MappedBuffer(HailoRTDriver &driver, std::shared_ptr<DmaAbleBuffer> buffer, HailoRTDriver::DmaDirection data_direction,
        hailo_status &status);

    HailoRTDriver &m_driver;
    std::shared_ptr<DmaAbleBuffer> m_buffer;
    HailoRTDriver::VdmaBufferHandle m_mapping_handle;
    const HailoRTDriver::DmaDirection m_data_direction;
};

} /* namespace vdma */
} /* namespace hailort */

#endif /* _HAILO_DMA_MAPPED_BUFFER_HPP_ */