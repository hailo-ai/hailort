/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file context_switch_buffer_builder.hpp
 * @brief Class used to build the context switch buffer sent to the firmware.
 **/

#ifndef _HAILO_CONTEXT_SWITCH_BUFFER_BUILDER_HPP_
#define _HAILO_CONTEXT_SWITCH_BUFFER_BUILDER_HPP_

#include "hailo/hailort.h"
#include "control_protocol.hpp"
#include "layer_info.hpp"
#include "vdma/channel_id.hpp"

namespace hailort
{

// This class manages a vector of CONTROL_PROTOCOL__context_switch_context_info_single_control_t controls to be sent
// to the firmware. Actions are written to the control buffer, until we reach the maximum control size, then we will
// start a new control. 
class ContextSwitchBufferBuilder final {
public:
    ContextSwitchBufferBuilder(CONTROL_PROTOCOL__context_switch_context_type_t context_type);

    void write_action(MemoryView action);
    const std::vector<CONTROL_PROTOCOL__context_switch_context_info_single_control_t> &get_controls() const;

private:
    CONTROL_PROTOCOL__context_switch_context_info_single_control_t &current_control();
    bool has_space_for_action(uint32_t action_size);
    void start_new_control();

    CONTROL_PROTOCOL__context_switch_context_type_t m_context_type;
    std::vector<CONTROL_PROTOCOL__context_switch_context_info_single_control_t> m_controls;
};

} /* namespace hailort */

#endif /* _HAILO_CONTEXT_SWITCH_BUFFER_BUILDER_HPP_ */
