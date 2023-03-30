/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file mapped_buffer_factory.hpp
 * @brief Static utility class for creating DmaMappedBuffers internally in hailort
 **/

#ifndef _HAILO_MAPPED_BUFFER_FACTORY_HPP_
#define _HAILO_MAPPED_BUFFER_FACTORY_HPP_

#include "hailo/hailort.h"
#include "hailo/dma_mapped_buffer.hpp"
#include "os/hailort_driver.hpp"

namespace hailort
{
namespace vdma
{

class MappedBufferFactory
{
public:
    MappedBufferFactory() = delete;
    static Expected<DmaMappedBuffer> create_mapped_buffer(size_t size,
        HailoRTDriver::DmaDirection data_direction, HailoRTDriver &driver);
};

} /* namespace vdma */
} /* namespace hailort */

#endif /* _HAILO_MAPPED_BUFFER_FACTORY_HPP_ */
