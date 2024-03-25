/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file ddr_action_list_buffer_builder.cpp
 * @brief Class used to build the action list sent to the firmware through DDR.
 **/

#include "ddr_action_list_buffer_builder.hpp"

namespace hailort
{

// TODO: HRT-12512 : Can remove these variables when / if continuous buffer comes from designated region
// In hailo15 - the DDR memory range of 0x80000000 - 0x90000000 is mapped to the M4 using a LUT (look up table) to addresses
// 0x50000000 - 0x60000000, Currently this is the range the CMA allocation should come from seeing as this is one of the first CMA allocations
// and the linux cma memory pool according to the hailo15 dtsi is - "alloc-ranges = <0 0x80000000 0 0x40000000>"
// (meaning starts from 0x80000000 and goes for 992 MB) - so anything allocated from 0x90000000 and on ward will be outside the mapped area
// The solution to this issue is to create a specific range for this allocation inide the mapped area - seeing as this affects other components
// Like the dsp etc...need to check with them before doing so. For now - this should almost always retirn in the mapped area and we will verify
// to double check

DDRActionListBufferBuilder::DDRActionListBufferBuilder(vdma::ContinuousBuffer &&buffer) :
    ActionListBufferBuilder(ActionListBufferBuilder::Type::DDR),
    m_action_list_buffer(std::move(buffer)),
    m_write_offset(0),
    m_current_context_info{}
{}

bool DDRActionListBufferBuilder::verify_dma_addr(vdma::ContinuousBuffer &buffer)
{
    // verify that buffer starts and ends inside mapped range
    if (buffer.dma_address() < CONTEXT_SWITCH_DEFS__START_M4_MAPPED_DDR_ADDRESS ||
        (buffer.dma_address() + buffer.size() >= CONTEXT_SWITCH_DEFS__END_M4_MAPPED_DDR_ADDRESS)) {
        return false;
    }
    return true;
}

Expected<std::shared_ptr<DDRActionListBufferBuilder>> DDRActionListBufferBuilder::create(size_t num_contexts,
    HailoRTDriver &driver)
{
    // Try to allocate continous buffer for action list in DDR
    auto continous_alloc = vdma::ContinuousBuffer::create(num_contexts * 
        sizeof(CONTROL_PROTOCOL__context_switch_context_info_chunk_t), driver);

    // TODO HRT-12512 - Add fallback to Control if continous buffer allocation fails
    CHECK_EXPECTED(continous_alloc);
    // Verify that continous buffer is in allocated region
    CHECK_AS_EXPECTED(verify_dma_addr(continous_alloc.value()), HAILO_INTERNAL_FAILURE,
        "Failed to allocate continous buffer in M4 mapped memory region");
    return make_shared_nothrow<DDRActionListBufferBuilder>(continous_alloc.release());
}

hailo_status DDRActionListBufferBuilder::write_action(MemoryView action,
    CONTROL_PROTOCOL__context_switch_context_type_t context_type, bool is_new_context, bool is_last_action_in_context)
{
    assert(action.size() < std::numeric_limits<uint32_t>::max());
    const uint32_t action_size = static_cast<uint32_t>(action.size());

    if (is_new_context) {
        m_current_context_info.is_first_chunk_per_context = true;
        m_current_context_info.is_last_chunk_per_context = true;
        m_current_context_info.context_type = static_cast<uint8_t>(context_type);
        m_current_context_info.context_network_data_length = 0;
    }

    CHECK(m_current_context_info.context_network_data_length + action_size <=
        ARRAY_ENTRIES(m_current_context_info.context_network_data), HAILO_INVALID_ARGUMENT,
        "Context exceeds maximum context size {}", ARRAY_ENTRIES(m_current_context_info.context_network_data));

    // TODO HRT-12788 - make more efficient by writing directly to DDR without using the local context_info_single_control_t
    memcpy(&(m_current_context_info.context_network_data[m_current_context_info.context_network_data_length]),
        action.data(), action_size);
    m_current_context_info.context_network_data_length += action_size;

    if (is_last_action_in_context) {
        const auto write_size = sizeof(CONTROL_PROTOCOL__context_switch_context_info_chunk_t);
        auto status = m_action_list_buffer.write(&m_current_context_info, write_size, m_write_offset);
        CHECK_SUCCESS(status);
        m_write_offset += write_size;
    }

    return HAILO_SUCCESS;
}

uint64_t DDRActionListBufferBuilder::get_mapped_buffer_dma_address() const
{
    return m_action_list_buffer.dma_address();
}

} /* namespace hailort */