/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file mapped_buffer_factory.cpp
 * @brief Static utility class for creating DmaMappedBuffers internally in hailort
 **/

#include "vdma/memory/mapped_buffer_factory.hpp"
#include "vdma/memory/mapped_buffer_impl.hpp"

namespace hailort
{
namespace vdma
{

Expected<DmaMappedBuffer> MappedBufferFactory::create_mapped_buffer(size_t size,
    HailoRTDriver::DmaDirection data_direction, HailoRTDriver &driver)
{
    auto pimpl_exp = DmaMappedBuffer::Impl::create(driver, data_direction, size);
    CHECK_EXPECTED(pimpl_exp);

    auto pimpl = make_unique_nothrow<DmaMappedBuffer::Impl>(pimpl_exp.release());
    CHECK_NOT_NULL_AS_EXPECTED(pimpl, HAILO_OUT_OF_HOST_MEMORY);
    return DmaMappedBuffer(std::move(pimpl));
}

} /* namespace vdma */
} /* namespace hailort */
