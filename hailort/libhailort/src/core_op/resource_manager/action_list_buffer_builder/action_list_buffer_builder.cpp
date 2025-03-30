/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file action_list_buffer_builder.cpp
 * @brief Class used to build action list and context buffers to be sent via controls or being written to ddr.
 **/

#include "action_list_buffer_builder.hpp"
#include "context_switch_defs.h"

namespace hailort
{

Expected<std::shared_ptr<ActionListBufferBuilder>> ActionListBufferBuilder::create()
{
    return make_shared_nothrow<ActionListBufferBuilder>();
}

hailo_status ActionListBufferBuilder::build_context(MemoryView action,
    CONTROL_PROTOCOL__context_switch_context_type_t context_type, bool is_new_context)
{
    assert(action.size() < std::numeric_limits<uint32_t>::max());
    const uint32_t action_size = static_cast<uint32_t>(action.size());
    const auto should_start_new_control = (is_new_context || !has_space_for_action(action_size));

    if (should_start_new_control) {
        start_new_control(context_type, is_new_context);
    }

    auto &control = current_control();
    memcpy(&control.context_network_data[control.context_network_data_length], action.data(), action_size);
    control.context_network_data_length += action_size;
    return HAILO_SUCCESS;
}

Expected<uint64_t> ActionListBufferBuilder::write_controls_to_ddr(HailoRTDriver &driver)
{
    CONTROL_PROTOCOL__context_switch_context_info_chunk_t context_info{};
    context_info.is_first_chunk_per_context = true;
    context_info.is_last_chunk_per_context = true;
    uint64_t dma_address = 0;

    for (const auto &control : m_controls) {
        if (control.is_first_chunk_per_context) {
            context_info.context_type = control.context_type;
            context_info.context_network_data_length = 0;
        }

        memcpy(&(context_info.context_network_data[context_info.context_network_data_length]),
            control.context_network_data, control.context_network_data_length);
        context_info.context_network_data_length += control.context_network_data_length;
        if (control.is_last_chunk_per_context) {
            TRY(auto dma_address_ret, driver.write_action_list(reinterpret_cast<uint8_t*>(&context_info),
                sizeof(CONTROL_PROTOCOL__context_switch_context_info_chunk_t)));
            // If this is the first write in the context, save the DMA address
            if (0 == dma_address) {
                dma_address = dma_address_ret;
            }
        }
    }

    return dma_address;
}

size_t ActionListBufferBuilder::get_action_list_buffer_size() const {
    size_t size = 0;

    for (const auto &control : m_controls) {
        size += control.context_network_data_length;
    }

    return size;
}

CONTROL_PROTOCOL__context_switch_context_info_chunk_t &ActionListBufferBuilder::current_control()
{
    assert(!m_controls.empty());
    return m_controls.back();
}

bool ActionListBufferBuilder::has_space_for_action(uint32_t action_size)
{
    auto &control = current_control();
    return (control.context_network_data_length + action_size) <= CONTROL_PROTOCOL__CONTEXT_NETWORK_DATA_SINGLE_CONTROL_MAX_SIZE;
}

void ActionListBufferBuilder::start_new_control(CONTROL_PROTOCOL__context_switch_context_type_t context_type,
    bool is_new_context)
{
    if (!is_new_context) {
        current_control().is_last_chunk_per_context = false;
    }

    // Creating a new control directly inside the vector to avoid copying the control struct.
    m_controls.emplace_back();
    auto &new_control = current_control();
    new_control.context_network_data_length = 0;
    new_control.context_type = static_cast<uint8_t>(context_type);
    new_control.is_first_chunk_per_context = is_new_context;
    new_control.is_last_chunk_per_context = true;
}

} /* namespace hailort */
