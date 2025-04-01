/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file action_list_buffer_builder.hpp
 * @brief Class used to build action list and context buffers to be sent via controls or being written to ddr.
 **/
#ifndef _HAILO_ACTION_LIST_BUFFER_BUILDER_HPP_
#define _HAILO_ACTION_LIST_BUFFER_BUILDER_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/buffer.hpp"
#include "vdma/driver/hailort_driver.hpp"
#include "control_protocol.h"
#include "common/internal_env_vars.hpp"

#include <vector>


namespace hailort
{

class ActionListBufferBuilder {
public:
    static Expected<std::shared_ptr<ActionListBufferBuilder>> create();

    ActionListBufferBuilder() = default;
    ~ActionListBufferBuilder() = default;

    hailo_status build_context(MemoryView action,
        CONTROL_PROTOCOL__context_switch_context_type_t context_type, bool is_new_context);
    size_t get_action_list_buffer_size() const;
    Expected<uint64_t> write_controls_to_ddr(HailoRTDriver &driver);

    const std::vector<CONTROL_PROTOCOL__context_switch_context_info_chunk_t> &get_controls() const {
        return m_controls;
    }
private:
    void start_new_control(CONTROL_PROTOCOL__context_switch_context_type_t context_type, bool is_new_context);
    bool has_space_for_action(uint32_t action_size);
    CONTROL_PROTOCOL__context_switch_context_info_chunk_t &current_control();
    std::vector<CONTROL_PROTOCOL__context_switch_context_info_chunk_t> m_controls;
};

} /* namespace hailort */

#endif /* _HAILO_ACTION_LIST_BUFFER_BUILDER_HPP_ */