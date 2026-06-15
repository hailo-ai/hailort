/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file action_list_buffer_builder.cpp
 * @brief Class used to build action list and context buffers to be sent via fw-controls.
 **/

#include "action_list_buffer_builder.hpp"

namespace hailort
{

hailo_status ActionListBufferBuilder::new_context(
    std::vector<CONTROL_PROTOCOL__host_buffer_info_t> &&context_buffers,
    CONTROL_PROTOCOL__context_switch_context_type_t context_type)
{
    CHECK(context_buffers.size() <= CONTROL_PROTOCOL__MAX_VDMA_CHANNELS_PER_ENGINE,
        HAILO_INTERNAL_FAILURE, "Too many buffers in context");

    m_translation_table.handle_count = static_cast<uint8_t>(context_buffers.size());

    // We set only the handles for the translation-table. The real dma-addresses will be populated by the driver.
    for (size_t i = 0; i < context_buffers.size(); i++) {
        m_translation_table.handles[i] = context_buffers.at(i).dma_addr_handle;
    }

    m_context_type = context_type;
    m_is_first_action_in_context = true;

    return HAILO_SUCCESS;
}

hailo_status ActionListBufferBuilder::add_action(MemoryView action)
{
    const uint32_t action_size = static_cast<uint32_t>(action.size());
    const auto should_start_new_control = (m_is_first_action_in_context || !has_space_for_action(action_size));

    if (should_start_new_control) {
        start_new_control();
    }

    auto &control = m_controls.back();
    memcpy(&control.context_network_data[control.context_network_data_length], action.data(), action_size);
    control.context_network_data_length += action_size;

    m_is_first_action_in_context = false;

    return HAILO_SUCCESS;
}

size_t ActionListBufferBuilder::get_action_list_buffer_size() const
{
    size_t size = 0;

    for (const auto &control : m_controls) {
        size += control.context_network_data_length;
    }

    return size;
}

void ActionListBufferBuilder::start_new_control()
{
    if (!m_is_first_action_in_context) {
        m_controls.back().is_last_chunk_per_context = false;
    }

    // Creating a new control directly inside the vector to avoid copying the control struct.
    m_controls.emplace_back();
    auto &new_control = m_controls.back();
    new_control.translation_table = m_translation_table;
    new_control.context_network_data_length = 0;
    new_control.context_type = static_cast<uint8_t>(m_context_type);
    new_control.is_first_chunk_per_context = m_is_first_action_in_context;
    new_control.is_last_chunk_per_context = true;
}

} /* namespace hailort */
