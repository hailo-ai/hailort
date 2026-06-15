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

#include <vector>


namespace hailort
{

class ActionListBufferBuilder {
public:
    static Expected<std::shared_ptr<ActionListBufferBuilder>> create()
    {
        return make_shared_nothrow<ActionListBufferBuilder>();
    }

    ActionListBufferBuilder() = default;
    ~ActionListBufferBuilder() = default;

    hailo_status new_context(
        std::vector<CONTROL_PROTOCOL__host_buffer_info_t> &&context_buffers,
        CONTROL_PROTOCOL__context_switch_context_type_t context_type);

    hailo_status add_action(MemoryView action);
    size_t get_action_list_buffer_size() const;

    const std::vector<CONTROL_PROTOCOL__context_switch_context_info_chunk_t> &get_list_in_chunks() const
    {
        return m_controls;
    }

private:
    bool has_space_for_action(uint32_t action_size)
    {
        auto new_size = m_controls.back().context_network_data_length + action_size;
        return new_size <= CONTROL_PROTOCOL__CONTEXT_NETWORK_DATA_SINGLE_CONTROL_MAX_SIZE;
    }

    void start_new_control();

    std::vector<CONTROL_PROTOCOL__context_switch_context_info_chunk_t> m_controls;
    CONTROL_PROTOCOL__context_switch_dma_addr_translation_table_t m_translation_table;
    CONTROL_PROTOCOL__context_switch_context_type_t m_context_type;
    bool m_is_first_action_in_context = false;
};

} /* namespace hailort */

#endif /* _HAILO_ACTION_LIST_BUFFER_BUILDER_HPP_ */
