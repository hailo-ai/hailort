/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file context_switch_buffer_builder.cpp
 * @brief Class used to build the context switch buffer sent to the firmware
 **/

#include "context_switch_buffer_builder.hpp"

namespace hailort
{

ContextSwitchBufferBuilder::ContextSwitchBufferBuilder(CONTROL_PROTOCOL__context_switch_context_type_t context_type) :
    m_context_type(context_type)
{
    // Initialize first control
    start_new_control();
}

void ContextSwitchBufferBuilder::write_action(MemoryView action)
{
    assert(action.size() < std::numeric_limits<uint32_t>::max());
    const uint32_t action_size = static_cast<uint32_t>(action.size());

    if (!has_space_for_action(action_size)) {
        // Size exceeded single control size, creating a new control buffer.
        start_new_control();
    }

    auto &control = current_control();
    memcpy(&control.context_network_data[control.context_network_data_length], action.data(), action_size);
    control.context_network_data_length += action_size;
    control.actions_count++;
}

const std::vector<CONTROL_PROTOCOL__context_switch_context_info_single_control_t> &ContextSwitchBufferBuilder::get_controls() const
{
    return m_controls;
}

CONTROL_PROTOCOL__context_switch_context_info_single_control_t &ContextSwitchBufferBuilder::current_control()
{
    assert(!m_controls.empty());
    return m_controls.back();
}

bool ContextSwitchBufferBuilder::has_space_for_action(uint32_t action_size)
{
    auto &control = current_control();
    return (control.context_network_data_length + action_size) <= ARRAY_ENTRIES(control.context_network_data);
}

void ContextSwitchBufferBuilder::start_new_control()
{
    if (!m_controls.empty()) {
        current_control().is_last_control_per_context = false;
    }

    // Creating a new control directly inside the vector to avoid copying the control struct.
    m_controls.emplace_back();
    auto &new_control = current_control();
    new_control.context_network_data_length = 0;
    new_control.actions_count = 0;
    new_control.context_type = static_cast<uint8_t>(m_context_type);
    new_control.is_first_control_per_context = (1 == m_controls.size());
    new_control.is_last_control_per_context = true;
}

} /* namespace hailort */
