
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

Expected<CacheBuffer> CacheBuffer::create(std::shared_ptr<vdma::VdmaBuffer> backing_buffer, uint32_t cache_size,
    uint32_t input_size, uint32_t output_size, uint32_t entry_size)
{
    CHECK_ARG_NOT_NULL(backing_buffer);
    CHECK((cache_size > 0) && (cache_size == backing_buffer->size()), HAILO_INVALID_ARGUMENT);
    CHECK((input_size > 0) && (input_size < cache_size), HAILO_INVALID_ARGUMENT,
        "Invalid cache input size: {} (cache size: {})", input_size, cache_size);
    CHECK((output_size > 0) && (output_size < cache_size), HAILO_INVALID_ARGUMENT,
        "Invalid cache output size: {} (cache size: {})", output_size, cache_size);

    CHECK((entry_size > 0) && (entry_size <= std::numeric_limits<uint16_t>::max()) &&
        ((cache_size % entry_size) == 0) && ((input_size % entry_size) == 0) && ((output_size % entry_size) == 0),
        HAILO_INVALID_ARGUMENT, "Invalid cache entry size: {}", entry_size);

    return CacheBuffer(cache_size, input_size, output_size, static_cast<uint16_t>(entry_size), backing_buffer);
}

CacheBuffer::CacheBuffer(uint32_t cache_size, uint32_t input_size, uint32_t output_size, uint16_t entry_size,
                         std::shared_ptr<vdma::VdmaBuffer> backing_buffer) :
    m_cache_size(cache_size),
    m_input_size(input_size),
    m_output_size(output_size),
    m_entry_size(entry_size),
    m_backing_buffer(backing_buffer)
{}

ExpectedRef<IntermediateBuffer> CacheBuffer::set_input_channel(HailoRTDriver &driver, vdma::ChannelId channel_id)
{
    if (m_cache_input) {
        return std::ref(*m_cache_input);
    }

    static const auto SINGLE_BATCH = 1;
    static const auto BUFFER_START = 0;
    // Passing the entry size as the max desc size, so that we can update the cache by entry granularity, even if the
    // entry is smaller than the default desc size. E.g. Updating the cache by one 64B entry, won't work if the desc size
    // is 512B, so the desc list should be programmed with 64B. If it is g.t.e. than 512B, the desc list will be programmed
    // as usual.
    TRY(auto intermediate_buffer, IntermediateBuffer::create_shared(driver, m_input_size, SINGLE_BATCH, channel_id,
        IntermediateBuffer::StreamingType::BURST, m_backing_buffer, BUFFER_START, m_entry_size));
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
    // Passing the entry size as the max desc size, so that we can update the cache by entry granularity, even if the
    // entry is smaller than the default desc size. E.g. Updating the cache by one 64B entry, won't work if the desc size
    // is 512B, so the desc list should be programmed with 64B. If it is g.t.e. than 512B, the desc list will be programmed
    // as usual.
    TRY(auto intermediate_buffer, IntermediateBuffer::create_shared(driver, m_output_size, SINGLE_BATCH, channel_id,
        IntermediateBuffer::StreamingType::BURST, m_backing_buffer, BUFFER_START, m_entry_size));
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

Expected<Buffer> CacheBuffer::read_cache()
{
    CHECK(m_backing_buffer, HAILO_INTERNAL_FAILURE, "Backing buffer not set");

    TRY(auto buffer, Buffer::create(m_backing_buffer->size()));
    CHECK_SUCCESS(m_backing_buffer->read(buffer.data(), buffer.size(), 0));
    return buffer;
}

hailo_status CacheBuffer::write_cache(MemoryView buffer)
{
    CHECK(m_backing_buffer, HAILO_INTERNAL_FAILURE, "Backing buffer not set");
    CHECK(buffer.size() == m_backing_buffer->size(), HAILO_INVALID_ARGUMENT,
        "Buffer size ({}) does not match cache size ({})", buffer.size(), m_backing_buffer->size());

    return m_backing_buffer->write(buffer.data(), buffer.size(), 0);
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
