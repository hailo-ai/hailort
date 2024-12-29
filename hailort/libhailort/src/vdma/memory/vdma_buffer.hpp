/**
 * Copyright (c) 2019-2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_edge_layer.hpp
 * @brief Abstract layer representing a vdma edge layer (buffer that can be read/written to the device over vdma.)
 *        The buffer can be either non-continuous with attach descriptors list (SgEdgeLayer) or continuous buffer.
 **/

#ifndef _HAILO_VDMA_VDMA_BUFFER_HPP_
#define _HAILO_VDMA_VDMA_BUFFER_HPP_

#include "vdma/driver/hailort_driver.hpp"
#include "vdma/memory/descriptor_list.hpp"
#include "control_protocol.h"


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
    virtual hailo_status read(void *buf_dst, size_t count, size_t offset) = 0;
    virtual hailo_status write(const void *buf_src, size_t count, size_t offset) = 0;
};

} /* vdma */
} /* hailort */

#endif /* _HAILO_VDMA_VDMA_BUFFER_HPP_ */
