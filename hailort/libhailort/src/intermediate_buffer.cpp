/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file intermediate_buffer.cpp
 * @brief Manages intermediate buffer for inter-context or ddr channels.
 */

#include "intermediate_buffer.hpp"
#include "context_switch/multi_context/resource_manager.hpp"

namespace hailort
{

Expected<IntermediateBuffer> IntermediateBuffer::create(HailoRTDriver &driver,
    const uint32_t transfer_size, const uint16_t transfers_count)
{
    auto desc_sizes_pair = VdmaDescriptorList::get_desc_buffer_sizes_for_single_transfer(driver,
        transfers_count, transfers_count, transfer_size);
    CHECK_EXPECTED(desc_sizes_pair);
    auto desc_page_size = desc_sizes_pair->first;
    auto descs_count = desc_sizes_pair->second;

    // On dram-dma all descriptors should have channel index, until we implement CCB,
    // we use some fake channel index. Currently the channel_index is not in used by
    // the hw. TODO HRT-5835: remove channel_index. 
    const uint8_t channel_index = 0;
    auto buffer = vdma::SgBuffer::create(driver, descs_count, desc_page_size,
        HailoRTDriver::DmaDirection::BOTH, channel_index);
    CHECK_EXPECTED(buffer);

    return IntermediateBuffer(buffer.release(), transfer_size, transfers_count);
}

hailo_status IntermediateBuffer::program_inter_context()
{
    size_t acc_offset = 0;
    for (uint16_t i = 0; i < m_transfers_count; i++) {
        auto first_desc_interrupts_domain = VdmaInterruptsDomain::NONE;
        auto last_desc_interrupts_domain = ((m_transfers_count - 1) == i) ? VdmaInterruptsDomain::DEVICE : 
                                                                     VdmaInterruptsDomain::NONE;
        auto desc_count_local = m_buffer.program_descriptors(m_transfer_size, first_desc_interrupts_domain,
            last_desc_interrupts_domain, acc_offset, false);
        CHECK_EXPECTED_AS_STATUS(desc_count_local,
            "Failed to program descs for intermediate channels. Given batch_size is too big.");
        acc_offset += desc_count_local.value();
    }
    return HAILO_SUCCESS;
}

Expected<uint16_t> IntermediateBuffer::program_ddr()
{
    return m_buffer.program_descs_for_ddr_transfers(m_buffer.desc_page_size(), false, 1, 
        static_cast<uint32_t>(m_buffer.descs_count()), 0, true);
}

Expected<uint16_t> IntermediateBuffer::program_host_managed_ddr(uint16_t row_size, uint32_t buffered_rows,
    uint16_t initial_desc_offset)
{
    return m_buffer.program_descs_for_ddr_transfers(row_size, true, DDR_NUMBER_OF_ROWS_PER_INTERRUPT,
        buffered_rows, initial_desc_offset, true);
}

Expected<Buffer> IntermediateBuffer::read()
{
    const auto size = m_transfer_size * m_transfers_count;
    assert(size <= m_buffer.size());

    auto res = Buffer::create(size);
    CHECK_EXPECTED(res);

    auto status = m_buffer.read(res->data(), size, 0);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return res.release();
}

} /* namespace hailort */
