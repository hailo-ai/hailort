/**
 * Copyright (c) 2019-2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file intermediate_buffer.cpp
 * @brief Manages intermediate buffers, including inter-context and ddr buffers.
 */

#include "intermediate_buffer.hpp"

#include "core_op/resource_manager/resource_manager.hpp"
#include "vdma/memory/sg_edge_layer.hpp"
#include "vdma/memory/continuous_edge_layer.hpp"
#include "vdma/memory/buffer_requirements.hpp"


namespace hailort
{
Expected<std::unique_ptr<vdma::VdmaEdgeLayer>> IntermediateBuffer::create_edge_layer(
    std::shared_ptr<vdma::VdmaBuffer> buffer, size_t buffer_offset, HailoRTDriver &driver, uint32_t transfer_size,
    uint16_t max_batch_size, vdma::ChannelId d2h_channel_id, StreamingType streaming_type, uint16_t max_desc_size)
{
    const bool is_circular = (streaming_type == StreamingType::CIRCULAR_CONTINUOS);
    auto buffer_exp = (vdma::VdmaBuffer::Type::CONTINUOUS == buffer->type()) ?
        create_ccb_edge_layer(buffer, buffer_offset, driver, transfer_size, max_batch_size, is_circular) :
        create_sg_edge_layer(buffer, buffer_offset, driver, transfer_size, max_batch_size, d2h_channel_id, is_circular,
            max_desc_size);

    return buffer_exp;
}

Expected<IntermediateBuffer> IntermediateBuffer::create(HailoRTDriver &driver, uint32_t transfer_size,
    uint16_t max_batch_size, vdma::ChannelId d2h_channel_id, StreamingType streaming_type,
    std::shared_ptr<vdma::VdmaBuffer> buffer, size_t buffer_offset, uint16_t max_desc_size)
{
    max_desc_size = std::min(max_desc_size, driver.desc_max_page_size());

    LOGGER__TRACE("Creating IntermediateBuffer: transfer_size = {}, max_batch_size = {}, d2h_channel_id = {}, "
        "streaming_type = {}, buffer = 0x{:X}, buffer_offset = {}, max_desc_size = {}",
        transfer_size, max_batch_size, d2h_channel_id, streaming_type, (uintptr_t)buffer.get(), buffer_offset,
        max_desc_size);

    TRY(auto edge_layer_ptr, create_edge_layer(buffer, buffer_offset, driver, transfer_size, max_batch_size,
        d2h_channel_id, streaming_type, max_desc_size));

    if (streaming_type == StreamingType::BURST) {
        // We have max_batch_size transfers, so we program them one by one. The last transfer should report interrupt
        // to the device.
        size_t desc_acc_offset = 0;
        size_t buffer_acc_offset = 0;
        for (uint16_t i = 0; i < max_batch_size; i++) {
            const auto last_desc_interrupts_domain = ((max_batch_size - 1) == i) ?
                InterruptsDomain::DEVICE : InterruptsDomain::NONE;
            TRY(const auto desc_count_local, edge_layer_ptr->program_descriptors(transfer_size,
                last_desc_interrupts_domain, desc_acc_offset, buffer_acc_offset),
                "Failed to program descs for inter context channels. Given max_batch_size is too big.");
            desc_acc_offset += desc_count_local;
            buffer_acc_offset += (desc_count_local * edge_layer_ptr->desc_page_size());
        }
    } else {
        // Program all descriptors, no need for interrupt.
        const auto interrupts_domain = InterruptsDomain::NONE;
        const auto total_size = edge_layer_ptr->descs_count() * edge_layer_ptr->desc_page_size();
        TRY(const auto desc_count_local, edge_layer_ptr->program_descriptors(total_size, interrupts_domain, 0));
        (void)desc_count_local;
    }

    return IntermediateBuffer(std::move(edge_layer_ptr), transfer_size, streaming_type, max_batch_size);
}

Expected<std::shared_ptr<IntermediateBuffer>> IntermediateBuffer::create_shared(HailoRTDriver &driver,
    uint32_t transfer_size, uint16_t max_batch_size, vdma::ChannelId d2h_channel_id, StreamingType streaming_type,
    std::shared_ptr<vdma::VdmaBuffer> buffer, size_t buffer_offset, uint16_t max_desc_size)
{
    TRY(auto intermediate_buffer, create(driver, transfer_size, max_batch_size, d2h_channel_id, streaming_type,
        buffer, buffer_offset, max_desc_size));

    auto intermediate_buffer_ptr = make_shared_nothrow<IntermediateBuffer>(std::move(intermediate_buffer));
    CHECK_NOT_NULL_AS_EXPECTED(intermediate_buffer_ptr, HAILO_OUT_OF_HOST_MEMORY);
    return intermediate_buffer_ptr;
}

Expected<Buffer> IntermediateBuffer::read(size_t size)
{
    if (size == 0) {
        size = m_transfer_size * m_dynamic_batch_size;
    }
    CHECK_AS_EXPECTED(size <= m_edge_layer->backing_buffer_size(), HAILO_INTERNAL_FAILURE,
        "Requested size {} is bigger than buffer size {}", size, m_edge_layer->backing_buffer_size());

    TRY(auto res, Buffer::create(size));

    auto status = m_edge_layer->read(res.data(), size, 0);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return res;
}

CONTROL_PROTOCOL__host_buffer_info_t IntermediateBuffer::get_host_buffer_info() const
{
    return m_edge_layer->get_host_buffer_info(m_transfer_size);
}

hailo_status IntermediateBuffer::reprogram_descriptors(size_t buffer_offset)
{
    CHECK(m_streaming_type == StreamingType::BURST, HAILO_INTERNAL_FAILURE,
        "Reprogramming descriptors is only supported for burst streaming type");

    CHECK(buffer_offset % m_edge_layer->desc_page_size() == 0, HAILO_INTERNAL_FAILURE,
        "Buffer offset must be aligned to descriptor page size");

    assert(m_edge_layer->backing_buffer_size() >= buffer_offset);
    const auto size_to_end = m_edge_layer->backing_buffer_size() - buffer_offset;
    const auto first_chunk_size = std::min(size_to_end, static_cast<size_t>(m_transfer_size));

    // Program the first chunk of descriptors - from the buffer offset to the end of the buffer
    static const auto BIND = true;
    static const auto DESC_LIST_START = 0;
    TRY(const uint32_t first_chunk_desc_count, m_edge_layer->program_descriptors(first_chunk_size,
        InterruptsDomain::NONE, DESC_LIST_START, buffer_offset, BIND));

    uint32_t second_chunk_desc_count = 0;
    if (first_chunk_size < m_transfer_size) {
        // Program the second chunk of descriptors - from the start of the buffer till the end of the remaining size
        static const auto BUFFER_START = 0;
        const auto second_chunk_size = m_transfer_size - first_chunk_size;
        TRY(second_chunk_desc_count, m_edge_layer->program_descriptors(second_chunk_size, InterruptsDomain::NONE,
            first_chunk_desc_count, BUFFER_START, BIND));
    }

    const auto expected_desc_count = m_edge_layer->descs_count() - 1;
    CHECK(first_chunk_desc_count + second_chunk_desc_count == expected_desc_count, HAILO_INTERNAL_FAILURE,
        "Expected {} descriptors, got {}", expected_desc_count, first_chunk_desc_count + second_chunk_desc_count);

    return HAILO_SUCCESS;
}

uint32_t IntermediateBuffer::transfer_size() const
{
    return m_transfer_size;
}

IntermediateBuffer::IntermediateBuffer(std::unique_ptr<vdma::VdmaEdgeLayer> &&edge_layer, uint32_t transfer_size,
                                       StreamingType streaming_type, uint16_t batch_size) :
    m_edge_layer(std::move(edge_layer)),
    m_transfer_size(transfer_size),
    m_streaming_type(streaming_type),
    m_dynamic_batch_size(batch_size)
{}

Expected<std::unique_ptr<vdma::VdmaEdgeLayer>> IntermediateBuffer::create_sg_edge_layer(
    std::shared_ptr<vdma::VdmaBuffer> buffer, size_t buffer_offset, HailoRTDriver &driver, uint32_t transfer_size,
    uint16_t batch_size, vdma::ChannelId d2h_channel_id, bool is_circular, uint16_t max_desc_size)
{
    static const auto DONT_FORCE_DEFAULT_PAGE_SIZE = false;
    static const auto FORCE_BATCH_SIZE = true;
    static const auto IS_VDMA_ALIGNED_BUFFER = true;
    TRY(const auto buffer_requirements, vdma::BufferSizesRequirements::get_buffer_requirements_single_transfer(
        vdma::VdmaBuffer::Type::SCATTER_GATHER, max_desc_size, batch_size, batch_size, transfer_size,
        is_circular, DONT_FORCE_DEFAULT_PAGE_SIZE, FORCE_BATCH_SIZE, IS_VDMA_ALIGNED_BUFFER));
    const auto desc_page_size = buffer_requirements.desc_page_size();
    const auto descs_count = buffer_requirements.descs_count();
    const auto buffer_size = buffer_requirements.buffer_size();

    TRY(auto edge_layer, vdma::SgEdgeLayer::create(std::dynamic_pointer_cast<vdma::SgBuffer>(buffer), buffer_size,
        buffer_offset, driver, descs_count, desc_page_size, is_circular, d2h_channel_id));

    auto edge_layer_ptr = make_unique_nothrow<vdma::SgEdgeLayer>(std::move(edge_layer));
    CHECK_NOT_NULL_AS_EXPECTED(edge_layer_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::unique_ptr<vdma::VdmaEdgeLayer>(std::move(edge_layer_ptr));
}

Expected<std::unique_ptr<vdma::VdmaEdgeLayer>> IntermediateBuffer::create_ccb_edge_layer(std::shared_ptr<vdma::VdmaBuffer> buffer,
    size_t buffer_offset, HailoRTDriver &driver, uint32_t transfer_size, uint16_t batch_size, bool is_circular)
{
    static const auto DONT_FORCE_DEFAULT_PAGE_SIZE = false;
    static const auto FORCE_BATCH_SIZE = true;
    static const auto IS_VDMA_ALIGNED_BUFFER = true;

    TRY(const auto buffer_size_requirements, vdma::BufferSizesRequirements::get_buffer_requirements_single_transfer(
        vdma::VdmaBuffer::Type::CONTINUOUS, driver.desc_max_page_size(), batch_size, batch_size, transfer_size,
        is_circular, DONT_FORCE_DEFAULT_PAGE_SIZE, FORCE_BATCH_SIZE, IS_VDMA_ALIGNED_BUFFER));

    const auto page_size = buffer_size_requirements.desc_page_size();
    const auto descs_count = buffer_size_requirements.descs_count();
    const auto buffer_size = buffer_size_requirements.buffer_size();

    TRY(auto edge_layer, vdma::ContinuousEdgeLayer::create(std::dynamic_pointer_cast<vdma::ContinuousBuffer>(buffer),
        buffer_size, buffer_offset, page_size, descs_count));

    auto edge_layer_ptr = make_unique_nothrow<vdma::ContinuousEdgeLayer>(std::move(edge_layer));
    CHECK_NOT_NULL_AS_EXPECTED(edge_layer_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::unique_ptr<vdma::VdmaEdgeLayer>(std::move(edge_layer_ptr));
}

} /* namespace hailort */
