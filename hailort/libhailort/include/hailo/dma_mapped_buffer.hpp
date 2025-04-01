/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file dma_mapped_buffer.hpp
 * @brief Object that keeps DMA mapping to some device/vdevice alive.
 **/

#ifndef _HAILO_DMA_MAPPED_BUFFER_HPP_
#define _HAILO_DMA_MAPPED_BUFFER_HPP_

#include "hailo/hailort.h"
#include "hailo/vdevice.hpp"
#include "hailo/device.hpp"
#include "hailo/expected.hpp"

namespace hailort
{

/*!
 * \class DmaMappedBuffer
 * \brief A wrapper class for mapping and unmapping buffers using VDevice::dma_map and VDevice::dma_unmap (or their
 * variants for Device).
 *
 * The DmaMappedBuffer class provides a convenient way to keep a DMA mapping on a buffer active.
 * It encapsulates the functionality of mapping and unmapping buffers using VDevice::dma_map and
 * VDevice::dma_unmap, as well as their variants for Device.
 *
 * \note The buffer pointed to by address cannot be released until this object is destroyed.
 *
 * Example:
 * \code{.cpp}
 * // Create a DmaMappedBuffer object for a VDevice
 * void* user_address = ...;
 * size_t size = ...;
 * hailo_dma_buffer_direction_t direction = ...;
 * Expected<DmaMappedBuffer> mapped_buffer = DmaMappedBuffer::create(vdevice, user_address, size, direction);
 * if (!mapped_buffer.has_value()) {
 *     // Handle error
 * } else {
 *     // Use the mapped buffer
 * }
 * \endcode
 */
class HAILORTAPI DmaMappedBuffer final {
public:
    /**
     * Creates a DmaMappedBuffer object for a VDevice.
     *
     * @param vdevice       The VDevice object to use for mapping the buffer.
     * @param user_address  The user address of the buffer to be mapped.
     * @param size          The size of the buffer to be mapped.
     * @param direction     The direction of the DMA transfer.
     *
     * @return An Expected object containing the created DmaMappedBuffer on success, or an error on failure.
     */
    static Expected<DmaMappedBuffer> create(VDevice &vdevice, void *user_address, size_t size,
        hailo_dma_buffer_direction_t direction);

    /**
     * Creates a DmaMappedBuffer object for a Device.
     *
     * @param device        The Device object to use for mapping the buffer.
     * @param user_address  The user address of the buffer to be mapped.
     * @param size          The size of the buffer to be mapped.
     * @param direction     The direction of the DMA transfer.
     *
     * @return An Expected object containing the created DmaMappedBuffer on success, or an error on failure.
     */
    static Expected<DmaMappedBuffer> create(Device &device, void *user_address, size_t size,
        hailo_dma_buffer_direction_t direction);

    /**
     * The destructor automatically unmaps the buffer.
     */
    ~DmaMappedBuffer();

    DmaMappedBuffer(const DmaMappedBuffer &) = delete;
    DmaMappedBuffer &operator=(const DmaMappedBuffer &) = delete;


    DmaMappedBuffer(DmaMappedBuffer &&other);
    DmaMappedBuffer &operator=(DmaMappedBuffer &&other);

private:
    class Impl;
    explicit DmaMappedBuffer(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} /* namespace hailort */

#endif /* _HAILO_DMA_MAPPED_BUFFER_HPP_ */
