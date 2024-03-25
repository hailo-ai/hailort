/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file action_list_buffer_builder.hpp
 * @brief Pure virtual class that represents the basic functions and members for building the action list for the FW.
 * Implemented and derived by two different classes:
 * ControlActionListBufferBuilder - uses control messages to send Action list to FW
 * DDRActionListBufferBuilder (only relevant in hailo1x) - Action list is written to M4 mapped memory in DDR - and read
 * from there directly by FW
 **/
#ifndef _HAILO_ACTION_LIST_BUFFER_BUILDER_HPP_
#define _HAILO_ACTION_LIST_BUFFER_BUILDER_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/buffer.hpp"

#include <vector>

#include "control_protocol.h"

namespace hailort
{

class ActionListBufferBuilder {
public:
    enum class Type {
        CONTROL,
        DDR
    };

    virtual hailo_status write_action(MemoryView action, CONTROL_PROTOCOL__context_switch_context_type_t context_type,
        bool is_new_context, bool last_action_buffer_in_context) = 0;

    virtual uint64_t get_mapped_buffer_dma_address() const = 0;

    ActionListBufferBuilder::Type get_builder_type() const {
         return m_builder_type;
    }
protected:
    ActionListBufferBuilder(ActionListBufferBuilder::Type builder_type) :
        m_builder_type(builder_type)
    {}
    virtual ~ActionListBufferBuilder() = default;
private:
    const ActionListBufferBuilder::Type m_builder_type;
};

} /* namespace hailort */

#endif /* _HAILO_ACTION_LIST_BUFFER_BUILDER_HPP_ */