/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file dma_mapped_buffer.hpp
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
#include "hailo/device.hpp"


namespace hailort {

// Forward deceleration across namespaces
namespace vdma {
    class DescriptorList;
    class MappedBufferFactory;
    class BufferedChannel;
}

// ******************************************** NOTE ******************************************** //
// Async Stream API and DmaMappedBuffer are currently not supported and are for internal use only //
// ********************************************************************************************** //
class HAILORTAPI DmaMappedBuffer final
{
public:
    static Expected<DmaMappedBuffer> create(size_t size,
        hailo_vdma_buffer_direction_flags_t data_direction_flags, Device &device);
    // TODO: doc that the addr needs to be on a new page and aligned to 64B (HRT-9559)
    //       probably best just to call mmap
    static Expected<DmaMappedBuffer> create_from_user_address(void *user_address, size_t size,
        hailo_vdma_buffer_direction_flags_t data_direction_flags, Device &device);

    DmaMappedBuffer(const DmaMappedBuffer &other) = delete;
    DmaMappedBuffer &operator=(const DmaMappedBuffer &other) = delete;
    DmaMappedBuffer(DmaMappedBuffer &&other) noexcept;
    DmaMappedBuffer &operator=(DmaMappedBuffer &&other) = delete;
    ~DmaMappedBuffer();

    void *user_address();
    size_t size() const;
    hailo_status synchronize();

private:
    static Expected<DmaMappedBuffer> create(void *user_address, size_t size,
        hailo_vdma_buffer_direction_flags_t data_direction_flags, Device &device);

    // Need access to pimpl
    friend class vdma::DescriptorList;
    friend class vdma::MappedBufferFactory;
    friend class vdma::BufferedChannel;

    class Impl;
    explicit DmaMappedBuffer(std::unique_ptr<Impl> pimpl);
    std::unique_ptr<Impl> pimpl;
};

} /* namespace hailort */

#endif /* _HAILO_DMA_MAPPED_BUFFER_HPP_ */