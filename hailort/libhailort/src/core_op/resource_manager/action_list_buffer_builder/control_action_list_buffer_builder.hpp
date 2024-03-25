/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file control_action_list_buffer_builder.cpp
 * @brief Class used to build the vector of controls containing the action list content sent to the firmware.
 **/

#ifndef _HAILO_CONTROL_ACTION_LIST_BUFFER_BUILDER_HPP_
#define _HAILO_CONTROL_ACTION_LIST_BUFFER_BUILDER_HPP_

#include "hailo/hailort.h"

#include "context_switch_defs.h"
#include "core_op/resource_manager/action_list_buffer_builder/action_list_buffer_builder.hpp"

#include "vdma/channel/channel_id.hpp"
#include "device_common/control_protocol.hpp"
#include "hef/layer_info.hpp"


namespace hailort
{

// This class manages a vector of CONTROL_PROTOCOL__context_switch_context_info_chunk_t controls to be sent
// to the firmware. Actions are written to the control buffer, until we reach the maximum control size, then we will
// start a new control. 
class ControlActionListBufferBuilder : public ActionListBufferBuilder {
public:
    ControlActionListBufferBuilder();
    static Expected<std::shared_ptr<ControlActionListBufferBuilder>> create();
    virtual ~ControlActionListBufferBuilder() = default;

    virtual hailo_status write_action(MemoryView action, CONTROL_PROTOCOL__context_switch_context_type_t context_type,
        bool is_new_context, bool last_action_buffer_in_context) override;

    virtual uint64_t get_mapped_buffer_dma_address() const override {
        return CONTEXT_SWITCH_DEFS__INVALID_DDR_CONTEXTS_BUFFER_ADDRESS;
    }

    const std::vector<CONTROL_PROTOCOL__context_switch_context_info_chunk_t> &get_controls() const {
        return m_controls;
    }
private:
    CONTROL_PROTOCOL__context_switch_context_info_chunk_t &current_control();
    bool has_space_for_action(uint32_t action_size);
    void start_new_control(CONTROL_PROTOCOL__context_switch_context_type_t context_type, bool is_new_context);

    std::vector<CONTROL_PROTOCOL__context_switch_context_info_chunk_t> m_controls;
};

} /* namespace hailort */

#endif /* _HAILO_CONTROL_ACTION_LIST_BUFFER_BUILDER_HPP_ */
