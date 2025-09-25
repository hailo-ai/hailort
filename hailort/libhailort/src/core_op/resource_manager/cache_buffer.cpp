/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file cache_buffer.cpp
 * @brief Wrapper for intermediate buffers used as caches
 **/

#include "cache_buffer.hpp"
#include "hailo/hailort.h"
#include "vdma/memory/sg_buffer.hpp"
#include "core_op/resource_manager/resource_manager.hpp"
#include "vdma/memory/sg_edge_layer.hpp"
#include "vdma/memory/buffer_requirements.hpp"

namespace hailort
{

Expected<CacheBuffer> CacheBuffer::create(std::shared_ptr<vdma::VdmaBuffer> backing_buffer, uint32_t cache_size,
    uint32_t input_size, uint32_t output_size, uint32_t entry_size, uint32_t padded_entry_size)
{
    CHECK_ARG_NOT_NULL(backing_buffer);
    CHECK((cache_size > 0) && (cache_size == backing_buffer->size()), HAILO_INVALID_ARGUMENT);
    CHECK((input_size > 0) && (input_size < cache_size), HAILO_INVALID_ARGUMENT,
        "Invalid cache input size: {} (cache size: {})", input_size, cache_size);
    CHECK((output_size > 0) && (output_size < cache_size), HAILO_INVALID_ARGUMENT,
        "Invalid cache output size: {} (cache size: {})", output_size, cache_size);

    CHECK((padded_entry_size > 0) && (padded_entry_size <= std::numeric_limits<uint16_t>::max()) &&
        ((cache_size % padded_entry_size) == 0) && ((input_size % padded_entry_size) == 0) &&
        ((output_size % padded_entry_size) == 0),
        HAILO_INVALID_ARGUMENT, "Invalid cache entry size: {}", padded_entry_size);

    return CacheBuffer(cache_size, input_size, output_size, static_cast<uint16_t>(entry_size),
        static_cast<uint16_t>(padded_entry_size), backing_buffer);
}

CacheBuffer::CacheBuffer(uint32_t cache_size, uint32_t input_size, uint32_t output_size, uint16_t entry_size,
                          uint16_t padded_entry_size, std::shared_ptr<vdma::VdmaBuffer> backing_buffer) :
    m_entry_size(entry_size),
    m_padded_entry_size(padded_entry_size),
    m_cache_length(cache_size / padded_entry_size),
    m_input_length(input_size / padded_entry_size),
    m_output_length(output_size / padded_entry_size),
    m_backing_buffer(backing_buffer)
{
    // This is validated in the create function too; it's here just to be safe
    assert(cache_size % entry_size == 0);
    assert(input_size % entry_size == 0);
    assert(output_size % entry_size == 0);
}

Expected<std::shared_ptr<vdma::SgEdgeLayer>> CacheBuffer::create_sg_edge_layer_shared(HailoRTDriver &driver,
        uint32_t transfer_size, uint16_t batch_size, vdma::ChannelId channel_id,
        std::shared_ptr<vdma::VdmaBuffer> buffer, size_t buffer_offset, uint16_t max_desc_size)
{
    LOGGER__TRACE("Creating CacheBuffer: transfer_size = {}, channel_id = {}, "
        "buffer_offset = {}, max_desc_size = {}, batch_size = {}",
        transfer_size, channel_id, buffer_offset, max_desc_size, batch_size);

    const auto DONT_FORCE_DEFAULT_PAGE_SIZE = false;
    const auto FORCE_BATCH_SIZE = true;
    const auto IS_VDMA_ALIGNED_BUFFER = true;
    max_desc_size = std::min(max_desc_size, driver.desc_max_page_size());
    TRY(const auto buffer_requirements, vdma::BufferSizesRequirements::get_buffer_requirements_single_transfer(
        vdma::VdmaBuffer::Type::SCATTER_GATHER, max_desc_size, batch_size, batch_size, transfer_size,
        false , DONT_FORCE_DEFAULT_PAGE_SIZE, FORCE_BATCH_SIZE, IS_VDMA_ALIGNED_BUFFER, false));
    auto desc_page_size = buffer_requirements.desc_page_size();
    const auto descs_count = buffer_requirements.descs_count();
    const auto buffer_size = buffer_requirements.buffer_size();

    TRY(auto edge_layer, vdma::SgEdgeLayer::create(std::static_pointer_cast<vdma::SgBuffer>(buffer), buffer_size,
        buffer_offset, driver, descs_count, desc_page_size, false, channel_id));

    auto edge_layer_ptr = make_shared_nothrow<vdma::SgEdgeLayer>(std::move(edge_layer));
    CHECK_NOT_NULL_AS_EXPECTED(edge_layer_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return edge_layer_ptr;
}

ExpectedRef<CacheBuffer> CacheBuffer::set_input_channel(HailoRTDriver &driver, vdma::ChannelId channel_id)
{
    if (m_cache_input) {
        return std::ref(*this);
    }

    static const auto BUFFER_START = 0;
    // Passing the entry size as the max desc size, so that we can update the cache by entry granularity, even if the
    // entry is smaller than the default desc size. E.g. Updating the cache by one 64B entry, won't work if the desc size
    // is 512B, so the desc list should be programmed with 64B. If it is g.t.e. than 512B, the desc list will be programmed
    // as usual.
    TRY(auto cache_layer, create_sg_edge_layer_shared(driver, m_entry_size, static_cast<uint16_t>(m_input_length),
        channel_id, m_backing_buffer, BUFFER_START, m_padded_entry_size));
    m_cache_input = std::move(cache_layer);
    return std::ref(*this);
}

ExpectedRef<CacheBuffer> CacheBuffer::set_output_channel(HailoRTDriver &driver, vdma::ChannelId channel_id)
{
    if (m_cache_output) {
        return std::ref(*this);
    }

    static const auto BUFFER_START = 0;
    // Passing the entry size as the max desc size, so that we can update the cache by entry granularity, even if the
    // entry is smaller than the default desc size. E.g. Updating the cache by one 64B entry, won't work if the desc size
    // is 512B, so the desc list should be programmed with 64B. If it is g.t.e. than 512B, the desc list will be programmed
    // as usual.
    TRY(auto cache_layer, create_sg_edge_layer_shared(driver, m_entry_size, static_cast<uint16_t>(m_output_length),
        channel_id, m_backing_buffer, BUFFER_START, m_padded_entry_size));
    m_cache_output = std::move(cache_layer);
    return std::ref(*this);
}

ExpectedRef<CacheBuffer> CacheBuffer::get_input()
{
    CHECK(m_cache_input, HAILO_INTERNAL_FAILURE, "Input not set");
    return std::ref(*this);
}

ExpectedRef<CacheBuffer> CacheBuffer::get_output()
{
    CHECK(m_cache_output, HAILO_INTERNAL_FAILURE, "Output not set");
    return std::ref(*this);
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

hailo_status CacheBuffer::reprogram_descriptors_per_side(bool is_side_input, size_t buffer_offset)
{
    std::shared_ptr<vdma::SgEdgeLayer> sg_edge_layer = is_side_input ? m_cache_input : m_cache_output;
    auto batch_size = is_side_input ? static_cast<uint16_t>(m_input_length) : static_cast<uint16_t>(m_output_length);
    CHECK(buffer_offset % sg_edge_layer->desc_page_size() == 0, HAILO_INTERNAL_FAILURE,
        "Buffer offset must be aligned to descriptor page size");
    const auto total_transfer_size = static_cast<size_t>(batch_size * m_padded_entry_size);
    assert(sg_edge_layer->backing_buffer_size() >= buffer_offset);
    const auto size_to_end = sg_edge_layer->backing_buffer_size() - buffer_offset;
    const auto first_chunk_size = std::min(size_to_end, static_cast<size_t>(batch_size * m_padded_entry_size));
    CHECK(first_chunk_size % m_padded_entry_size == 0, HAILO_INTERNAL_FAILURE,
        "First chunk size must be aligned to entry size");

    // Program the first chunk of descriptors - from the buffer offset to the end of the buffer
    const bool BIND = true;
    const size_t DESC_LIST_START = 0;
    const uint32_t SINGLE_BATCH = 1;
    auto transfer_size = first_chunk_size;
    const auto stride = m_entry_size;
    TRY(const uint32_t first_chunk_desc_count, sg_edge_layer->program_descriptors(transfer_size,
        InterruptsDomain::NONE, DESC_LIST_START, buffer_offset, SINGLE_BATCH, BIND, stride));

    uint32_t second_chunk_desc_count = 0;
    if (first_chunk_size < total_transfer_size) {
        // Program the second chunk of descriptors - from the start of the buffer till the end of the remaining size
        const size_t BUFFER_START = 0;
        const auto second_chunk_size = total_transfer_size - first_chunk_size;
        CHECK(second_chunk_size % m_padded_entry_size == 0, HAILO_INTERNAL_FAILURE,
            "Second chunk size must be aligned to entry size");
        transfer_size = second_chunk_size;
        TRY(second_chunk_desc_count, sg_edge_layer->program_descriptors(transfer_size, InterruptsDomain::NONE,
            first_chunk_desc_count, BUFFER_START, SINGLE_BATCH, BIND, stride));
    }

    const auto expected_desc_count = sg_edge_layer->descs_count() - 1;
    CHECK(first_chunk_desc_count + second_chunk_desc_count == expected_desc_count, HAILO_INTERNAL_FAILURE,
        "Expected {} descriptors, got {}", expected_desc_count, first_chunk_desc_count + second_chunk_desc_count);

    return HAILO_SUCCESS;
}

hailo_status CacheBuffer::reprogram_descriptors(uint32_t new_read_offset_entries)
{
    CHECK(m_cache_input && m_cache_output, HAILO_INTERNAL_FAILURE, "IOs not set");
    bool is_side_input = true;

    const auto new_read_offset_bytes = new_read_offset_entries * padded_entry_size();
    // Input buffer
    auto status = reprogram_descriptors_per_side(is_side_input, new_read_offset_bytes);
    CHECK_SUCCESS(status, "Failed to reprogram read cache descriptors to offset 0x{:x} (0x{:x} B)",
        new_read_offset_entries, new_read_offset_bytes);

    // The write offset is right after the end of read buffer (i.e. cache_input_length entries from the read offset)
    const auto write_offset_entries_delta = input_length();
    const auto new_write_offset_entries = (new_read_offset_entries + write_offset_entries_delta) % m_cache_length;
    const auto new_write_offset_bytes = new_write_offset_entries * padded_entry_size();
    // Output buffer
    is_side_input = false;
    status = reprogram_descriptors_per_side(is_side_input, new_write_offset_bytes);
    CHECK_SUCCESS(status, "Failed to reprogram write cache descriptors to offset 0x{:x} (0x{:x} B)",
        new_write_offset_entries, new_write_offset_bytes);

    return HAILO_SUCCESS;
}

uint16_t CacheBuffer::entry_size() const
{
    return m_entry_size;
}

uint32_t CacheBuffer::cache_length() const
{
    return m_cache_length;
}

uint32_t CacheBuffer::input_length() const
{
    return m_input_length;
}

uint32_t CacheBuffer::output_length() const
{
    return m_output_length;
}

uint16_t CacheBuffer::padded_entry_size() const
{
    return m_padded_entry_size;
}

bool CacheBuffer::is_configured() const
{
    return m_cache_input && m_cache_output;
}

CacheBuffer::Snapshot::Snapshot(Buffer &&buffer, uint32_t read_offset) :
    m_buffer(std::move(buffer)),
    m_read_offset(read_offset)
{}

const Buffer &CacheBuffer::Snapshot::buffer() const
{
    return m_buffer;
}

uint32_t CacheBuffer::Snapshot::read_offset() const
{
    return m_read_offset;
}

Expected<CacheBuffer::Snapshot> CacheBuffer::create_snapshot(uint32_t read_offset)
{
    TRY(auto buffer, read_cache());
    return Snapshot(std::move(buffer), read_offset);
}

CONTROL_PROTOCOL__host_buffer_info_t CacheBuffer::get_host_input_buffer_info() const
{
    return m_cache_input->get_host_buffer_info(m_entry_size);
}

CONTROL_PROTOCOL__host_buffer_info_t CacheBuffer::get_host_output_buffer_info() const
{
    return m_cache_output->get_host_buffer_info(m_entry_size);
}

uint16_t CacheBuffer::output_batch_size() const
{
    return static_cast<uint16_t>(m_output_length);
}

uint16_t CacheBuffer::input_batch_size() const
{
    return static_cast<uint16_t>(m_input_length);
}


} /* namespace hailort */
