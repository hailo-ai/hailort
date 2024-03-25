/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
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
    std::shared_ptr<vdma::VdmaBuffer> &&buffer, size_t buffer_offset, HailoRTDriver &driver, uint32_t transfer_size,
    uint16_t max_batch_size, vdma::ChannelId d2h_channel_id, StreamingType streaming_type)
{
    const bool is_circular = (streaming_type == StreamingType::CIRCULAR_CONTINUOS);
    auto buffer_exp = (vdma::VdmaBuffer::Type::CONTINUOUS == buffer->type()) ?
        create_ccb_edge_layer(std::move(buffer), buffer_offset, driver, transfer_size, max_batch_size, is_circular) :
        create_sg_edge_layer(std::move(buffer), buffer_offset, driver, transfer_size, max_batch_size, d2h_channel_id, is_circular);

    return buffer_exp;
}

Expected<IntermediateBuffer> IntermediateBuffer::create(HailoRTDriver &driver, uint32_t transfer_size,
    uint16_t max_batch_size, vdma::ChannelId d2h_channel_id, StreamingType streaming_type,
    std::shared_ptr<vdma::VdmaBuffer> &&buffer, size_t buffer_offset)
{
    auto edge_layer_exp = create_edge_layer(std::move(buffer), buffer_offset, driver, transfer_size, max_batch_size,
        d2h_channel_id, streaming_type);
    CHECK_EXPECTED(edge_layer_exp);
    auto edge_layer_ptr = edge_layer_exp.release();

    if (streaming_type == StreamingType::BURST) {
        // We have max_batch_size transfers, so we program them one by one. The last transfer should report interrupt
        // to the device.
        size_t acc_offset = 0;
        for (uint16_t i = 0; i < max_batch_size; i++) {
            const auto last_desc_interrupts_domain = ((max_batch_size - 1) == i) ?
                InterruptsDomain::DEVICE : InterruptsDomain::NONE;
            auto desc_count_local = edge_layer_ptr->program_descriptors(transfer_size, last_desc_interrupts_domain, acc_offset);
            CHECK_EXPECTED(desc_count_local, "Failed to program descs for inter context channels. Given max_batch_size is too big.");
            acc_offset += desc_count_local.value();
        }
    } else {
        // Program all descriptors, no need for interrupt.
        const auto interrupts_domain = InterruptsDomain::NONE;
        const auto total_size = edge_layer_ptr->descs_count() * edge_layer_ptr->desc_page_size();
        auto desc_count_local = edge_layer_ptr->program_descriptors(total_size, interrupts_domain, 0);
        CHECK_EXPECTED(desc_count_local);
    }

    return IntermediateBuffer(std::move(edge_layer_ptr), transfer_size, max_batch_size);
}

Expected<Buffer> IntermediateBuffer::read()
{
    const auto size = m_transfer_size * m_dynamic_batch_size;
    assert(size <= m_edge_layer->size());

    auto res = Buffer::create(size);
    CHECK_EXPECTED(res);

    auto status = m_edge_layer->read(res->data(), size, 0);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return res.release();
}

CONTROL_PROTOCOL__host_buffer_info_t IntermediateBuffer::get_host_buffer_info() const
{
    return m_edge_layer->get_host_buffer_info(m_transfer_size);
}

IntermediateBuffer::IntermediateBuffer(std::unique_ptr<vdma::VdmaEdgeLayer> &&edge_layer, uint32_t transfer_size,
                                       uint16_t batch_size) :
    m_edge_layer(std::move(edge_layer)),
    m_transfer_size(transfer_size),
    m_dynamic_batch_size(batch_size)
{}

Expected<std::unique_ptr<vdma::VdmaEdgeLayer>> IntermediateBuffer::create_sg_edge_layer(
    std::shared_ptr<vdma::VdmaBuffer> &&buffer, size_t buffer_offset, HailoRTDriver &driver, uint32_t transfer_size,
    uint16_t batch_size, vdma::ChannelId d2h_channel_id, bool is_circular)
{
    static const auto DONT_FORCE_DEFAULT_PAGE_SIZE = false;
    static const auto FORCE_BATCH_SIZE = true;
    static const auto IS_VDMA_ALIGNED_BUFFER = true;
    auto buffer_requirements = vdma::BufferSizesRequirements::get_buffer_requirements_single_transfer(
        vdma::VdmaBuffer::Type::SCATTER_GATHER, driver.desc_max_page_size(), batch_size, batch_size, transfer_size,
        is_circular, DONT_FORCE_DEFAULT_PAGE_SIZE, FORCE_BATCH_SIZE, IS_VDMA_ALIGNED_BUFFER);
    CHECK_EXPECTED(buffer_requirements);
    const auto desc_page_size = buffer_requirements->desc_page_size();
    const auto descs_count = buffer_requirements->descs_count();
    const auto buffer_size = buffer_requirements->buffer_size();

    auto edge_layer = vdma::SgEdgeLayer::create(std::dynamic_pointer_cast<vdma::SgBuffer>(buffer), buffer_size,
        buffer_offset, driver, descs_count, desc_page_size, is_circular, d2h_channel_id);
    CHECK_EXPECTED(edge_layer);

    auto edge_layer_ptr = make_unique_nothrow<vdma::SgEdgeLayer>(edge_layer.release());
    CHECK_NOT_NULL_AS_EXPECTED(edge_layer_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::unique_ptr<vdma::VdmaEdgeLayer>(std::move(edge_layer_ptr));
}

Expected<std::unique_ptr<vdma::VdmaEdgeLayer>> IntermediateBuffer::create_ccb_edge_layer(std::shared_ptr<vdma::VdmaBuffer> &&buffer,
    size_t buffer_offset, HailoRTDriver &driver, uint32_t transfer_size, uint16_t batch_size, bool is_circular)
{
    static const auto DONT_FORCE_DEFAULT_PAGE_SIZE = false;
    static const auto FORCE_BATCH_SIZE = true;
    static const auto IS_VDMA_ALIGNED_BUFFER = true;

    auto buffer_size_requirements = vdma::BufferSizesRequirements::get_buffer_requirements_single_transfer(
        vdma::VdmaBuffer::Type::CONTINUOUS, driver.desc_max_page_size(), batch_size, batch_size, transfer_size,
        is_circular, DONT_FORCE_DEFAULT_PAGE_SIZE, FORCE_BATCH_SIZE, IS_VDMA_ALIGNED_BUFFER);
    CHECK_EXPECTED(buffer_size_requirements);

    const auto page_size = buffer_size_requirements->desc_page_size();
    const auto descs_count = buffer_size_requirements->descs_count();
    const auto buffer_size = buffer_size_requirements->buffer_size();

    auto edge_layer = vdma::ContinuousEdgeLayer::create(std::dynamic_pointer_cast<vdma::ContinuousBuffer>(buffer),
        buffer_size, buffer_offset, page_size, descs_count);
    CHECK_EXPECTED(edge_layer);

    auto edge_layer_ptr = make_unique_nothrow<vdma::ContinuousEdgeLayer>(edge_layer.release());
    CHECK_NOT_NULL_AS_EXPECTED(edge_layer_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::unique_ptr<vdma::VdmaEdgeLayer>(std::move(edge_layer_ptr));
}

} /* namespace hailort */
