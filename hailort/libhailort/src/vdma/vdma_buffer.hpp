/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_buffer.hpp
 * @brief Abstract layer representing a vdma buffer (buffer that can be read/written to the device over vdma.)
 *        The buffer can be either non-continuous with attach descriptors list (SgBuffer) or continuous buffer.
 **/

#ifndef _HAILO_VDMA_VDMA_BUFFER_HPP_
#define _HAILO_VDMA_VDMA_BUFFER_HPP_

#include "os/hailort_driver.hpp"
#include "vdma_descriptor_list.hpp"

namespace hailort {
namespace vdma {

class VdmaBuffer {
public:

    enum class Type {
        SCATTER_GATHER,
        CONTINUOUS
    };

    virtual ~VdmaBuffer() = default;

    VdmaBuffer() = default;
    VdmaBuffer(const VdmaBuffer &) = delete;
    VdmaBuffer(VdmaBuffer &&) = default;
    VdmaBuffer& operator=(const VdmaBuffer &) = delete;
    VdmaBuffer& operator=(VdmaBuffer &&) = delete;

    virtual Type type() const = 0;
    virtual size_t size() const = 0;
    virtual uint64_t dma_address() const = 0;
    virtual uint16_t desc_page_size() const = 0;
    virtual uint32_t descs_count() const = 0;
    virtual uint32_t descriptors_in_buffer(size_t buffer_size) const = 0;

    // Should be only used for host managed ddr buffer, in the future this function may return nullptr (on CCB
    // case where there is no descriptors list)
    virtual ExpectedRef<VdmaDescriptorList> get_desc_list() = 0;

    virtual hailo_status read(void *buf_dst, size_t count, size_t offset) = 0;
    virtual hailo_status write(const void *buf_src, size_t count, size_t offset) = 0;

    virtual Expected<uint32_t> program_descriptors(size_t transfer_size, VdmaInterruptsDomain first_desc_interrupts_domain,
        VdmaInterruptsDomain last_desc_interrupts_domain, size_t desc_offset, bool is_circular) = 0;

};

} /* vdma */
} /* hailort */

#endif /* _HAILO_VDMA_VDMA_BUFFER_HPP_ */
