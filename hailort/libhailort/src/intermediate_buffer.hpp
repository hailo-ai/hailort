/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file intermediate_buffer.hpp
 * @brief Manages intermediate buffer for inter-context or ddr channels.
 */

#ifndef _HAILO_INTERMEDIATE_BUFFER_HPP_
#define _HAILO_INTERMEDIATE_BUFFER_HPP_

#include "os/hailort_driver.hpp"
#include "vdma/sg_buffer.hpp"
#include "hailo/expected.hpp"
#include "hailo/buffer.hpp"


namespace hailort
{

class IntermediateBuffer final {
public:
    static Expected<IntermediateBuffer> create(HailoRTDriver &driver, const uint32_t transfer_size,
        const uint16_t batch_size);

    hailo_status program_inter_context();

    // Returns the amount of programed descriptors
    Expected<uint16_t> program_ddr();
    Expected<uint16_t> program_host_managed_ddr(uint16_t row_size, uint32_t buffered_rows,
        uint16_t initial_desc_offset);

    uint64_t dma_address() const
    {
        return m_buffer.dma_address();
    }

    uint32_t descriptors_in_frame() const
    {
        return m_buffer.descriptors_in_buffer(m_transfer_size);
    }

    uint16_t desc_page_size() const
    {
        return m_buffer.desc_page_size();
    }

    uint32_t descs_count() const
    {
        return m_buffer.descs_count();
    }

    uint8_t depth()
    {
        return m_buffer.depth();
    }

    // Should be only used for host managed ddr buffer, not suported on continuous buffers
    ExpectedRef<VdmaDescriptorList> get_desc_list()
    {
        return m_buffer.get_desc_list();
    }

    Expected<Buffer> read();

private:
    IntermediateBuffer(vdma::SgBuffer &&buffer,
        const uint32_t transfer_size, const uint32_t transfers_count) :
           m_buffer(std::move(buffer)), m_transfer_size(transfer_size), m_transfers_count(transfers_count) {}

    vdma::SgBuffer m_buffer;
    const uint32_t m_transfer_size;
    const uint32_t m_transfers_count;
};

} /* namespace hailort */

#endif /* _HAILO_INTERMEDIATE_BUFFER_HPP_ */