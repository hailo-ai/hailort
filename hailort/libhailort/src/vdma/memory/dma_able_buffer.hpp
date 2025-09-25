/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file dma_able_buffer.hpp
 * @brief A Buffer that can be mapped to some device for dma operations.
 *        There are several options for that buffer:
 *          1. No allocation - The user gives its own buffer pointer and address. The buffer must be page aligned.
 *          2. Normal allocation - page aligned allocation. This is the default option for linux and windows.
 *          3. Driver allocation - On some platforms, default user mode memory allocation is not DMAAble. To overcome
 *             this, we allocate the buffer in a low memory using hailort driver. We check it querying
 *             HailoRTDriver::allocate_driver_buffer().
 *          4. QNX shared memory allocation - for qnx, in order to pass the driver to the resources manager, we need to
 *             create a shared memory object, and pass an handle to it in the mapping. TODO: HRT-10298 implement this.
 **/

#ifndef _HAILO_DMA_ABLE_BUFFER_HPP_
#define _HAILO_DMA_ABLE_BUFFER_HPP_

#include "hailo/expected.hpp"
#include "vdma/driver/hailort_driver.hpp"

namespace hailort {
namespace vdma {

class DmaAbleBuffer;
using DmaAbleBufferPtr = std::shared_ptr<DmaAbleBuffer>;

class DmaAbleBuffer
{
public:
    // Create a DmaAbleBuffer from the user's provided address.
    static Expected<DmaAbleBufferPtr> create_from_user_address(void *user_address, size_t size);

    // Create a DmaAbleBuffer by allocating memory.
    static Expected<DmaAbleBufferPtr> create_by_allocation(size_t size);

    // Create a DmaAbleBuffer by allocating memory, using the driver if needed (i.e.
    // if driver.allocate_driver_buffer is true)
    static Expected<DmaAbleBufferPtr> create_by_allocation(size_t size, HailoRTDriver &driver);

    DmaAbleBuffer() = default;
    DmaAbleBuffer(DmaAbleBuffer &&other) = delete;
    DmaAbleBuffer(const DmaAbleBuffer &other) = delete;
    DmaAbleBuffer &operator=(const DmaAbleBuffer &other) = delete;
    DmaAbleBuffer &operator=(DmaAbleBuffer &&other) = delete;
    virtual ~DmaAbleBuffer() = default;

    virtual void* user_address() = 0;
    virtual size_t size() const = 0;
    virtual vdma_mapped_buffer_driver_identifier buffer_identifier() = 0;
};

} /* namespace vdma */
} /* namespace hailort */

#endif /* _HAILO_DMA_ABLE_BUFFER_HPP_ */
