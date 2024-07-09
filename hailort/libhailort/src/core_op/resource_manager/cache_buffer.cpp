
/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file cache_buffer.cpp
 * @brief Wrapper for intermediate buffers used as caches
 **/

#include "cache_buffer.hpp"
#include "hailo/hailort.h"
#include "vdma/memory/sg_buffer.hpp"

namespace hailort
{

Expected<CacheBuffer> CacheBuffer::create(HailoRTDriver &driver, uint32_t cache_size,
    uint32_t input_size, uint32_t output_size)
{
    CHECK(cache_size > 0, HAILO_INVALID_ARGUMENT);
    CHECK((input_size > 0) && (input_size < cache_size), HAILO_INVALID_ARGUMENT,
        "Invalid cache input size: {} (cache size: {})", input_size, cache_size);
    CHECK((output_size > 0) && (output_size < cache_size), HAILO_INVALID_ARGUMENT,
        "Invalid cache output size: {} (cache size: {})", output_size, cache_size);

    // Cache buffers are by sg buffers
    TRY(auto buffer, vdma::SgBuffer::create(driver, cache_size, HailoRTDriver::DmaDirection::BOTH));
    auto buffer_ptr = make_shared_nothrow<vdma::SgBuffer>(std::move(buffer));
    CHECK_NOT_NULL(buffer_ptr, HAILO_OUT_OF_HOST_MEMORY);
    return CacheBuffer(cache_size, input_size, output_size, buffer_ptr);
}

CacheBuffer::CacheBuffer(uint32_t cache_size, uint32_t input_size, uint32_t output_size,
                         std::shared_ptr<vdma::VdmaBuffer> backing_buffer) :
    m_cache_size(cache_size),
    m_input_size(input_size),
    m_output_size(output_size),
    m_backing_buffer(backing_buffer)
{}

ExpectedRef<IntermediateBuffer> CacheBuffer::set_input_channel(HailoRTDriver &driver, vdma::ChannelId channel_id)
{
    if (m_cache_input) {
        return std::ref(*m_cache_input);
    }

    static const auto SINGLE_BATCH = 1;
    static const auto BUFFER_START = 0;
    TRY(auto intermediate_buffer, IntermediateBuffer::create_shared(driver, m_input_size, SINGLE_BATCH, channel_id,
        IntermediateBuffer::StreamingType::BURST, m_backing_buffer, BUFFER_START));
    m_cache_input = intermediate_buffer;
    return std::ref(*m_cache_input);
}

ExpectedRef<IntermediateBuffer> CacheBuffer::set_output_channel(HailoRTDriver &driver, vdma::ChannelId channel_id)
{
    if (m_cache_output) {
        return std::ref(*m_cache_output);
    }

    static const auto SINGLE_BATCH = 1;
    static const auto BUFFER_START = 0;
    TRY(auto intermediate_buffer, IntermediateBuffer::create_shared(driver, m_output_size, SINGLE_BATCH, channel_id,
        IntermediateBuffer::StreamingType::BURST, m_backing_buffer, BUFFER_START));
    m_cache_output = intermediate_buffer;
    return std::ref(*m_cache_output);
}

ExpectedRef<IntermediateBuffer> CacheBuffer::get_input()
{
    CHECK(m_cache_input, HAILO_INTERNAL_FAILURE, "Input not set");
    return std::ref(*m_cache_input);
}

ExpectedRef<IntermediateBuffer> CacheBuffer::get_output()
{
    CHECK(m_cache_output, HAILO_INTERNAL_FAILURE, "Output not set");
    return std::ref(*m_cache_output);
}

Expected<Buffer> CacheBuffer::read_entire_cache()
{
    CHECK(m_cache_input && m_cache_output, HAILO_INTERNAL_FAILURE, "Input or output not set");

    return m_cache_input->read(m_cache_size);
}

uint32_t CacheBuffer::cache_size() const
{
    return m_cache_size;
}

uint32_t CacheBuffer::input_size() const
{
    return m_input_size;
}

uint32_t CacheBuffer::output_size() const
{
    return m_output_size;
}

bool CacheBuffer::is_configured() const
{
    return m_cache_input && m_cache_output;
}

} /* namespace hailort */
