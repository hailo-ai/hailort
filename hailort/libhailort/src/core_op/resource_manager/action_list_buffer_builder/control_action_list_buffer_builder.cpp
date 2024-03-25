/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file control_action_list_buffer_builder.cpp
 * @brief Class used to build the vector of controls containing the action list content sent to the firmware.
 **/

#include "control_action_list_buffer_builder.hpp"

namespace hailort
{

ControlActionListBufferBuilder::ControlActionListBufferBuilder() :
    ActionListBufferBuilder(ActionListBufferBuilder::Type::CONTROL)
{}

Expected<std::shared_ptr<ControlActionListBufferBuilder>> ControlActionListBufferBuilder::create()
{
    return make_shared_nothrow<ControlActionListBufferBuilder>();
}

hailo_status ControlActionListBufferBuilder::write_action(MemoryView action,
    CONTROL_PROTOCOL__context_switch_context_type_t context_type, bool is_new_context, bool last_action_buffer_in_context)
{
    (void) last_action_buffer_in_context;
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

CONTROL_PROTOCOL__context_switch_context_info_chunk_t &ControlActionListBufferBuilder::current_control()
{
    assert(!m_controls.empty());
    return m_controls.back();
}

bool ControlActionListBufferBuilder::has_space_for_action(uint32_t action_size)
{
    auto &control = current_control();
    return (control.context_network_data_length + action_size) <= CONTROL_PROTOCOL__CONTEXT_NETWORK_DATA_SINGLE_CONTROL_MAX_SIZE;
}

void ControlActionListBufferBuilder::start_new_control(CONTROL_PROTOCOL__context_switch_context_type_t context_type,
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
