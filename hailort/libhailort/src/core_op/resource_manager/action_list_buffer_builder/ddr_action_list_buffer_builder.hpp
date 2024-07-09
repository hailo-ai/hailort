/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file ddr_action_list_buffer_builder.hpp
 * @brief Class used to build the action list sent to the firmware through DDR.
 **/
#ifndef _HAILO_DDR_ACTION_LIST_BUFFER_BUILDER_HPP_
#define _HAILO_DDR_ACTION_LIST_BUFFER_BUILDER_HPP_

#include "hailo/hailort.h"
#include "context_switch_defs.h"
#include "core_op/resource_manager/action_list_buffer_builder/action_list_buffer_builder.hpp"
#include "vdma/memory/continuous_buffer.hpp"
#include "vdma/vdma_device.hpp"

#define DDR_ACTION_LIST_ENV_VAR         ("HAILO_DDR_ACTION_LIST")
#define DDR_ACTION_LIST_ENV_VAR_VALUE   ("1")

namespace hailort
{

class DDRActionListBufferBuilder : public ActionListBufferBuilder {
public:
    DDRActionListBufferBuilder(void* user_address, uint64_t dma_address);
    virtual ~DDRActionListBufferBuilder() = default;
    static Expected<std::shared_ptr<DDRActionListBufferBuilder>> create(size_t num_contexts, VdmaDevice &vdma_device);

    virtual hailo_status write_action(MemoryView action, CONTROL_PROTOCOL__context_switch_context_type_t context_type,
        bool is_new_context, bool last_action_buffer_in_context) override;

    virtual uint64_t get_mapped_buffer_dma_address() const override;

    // TODO: HRT-12512 : Can remove this check when / if continuous buffer comes from designated region
    static bool verify_dma_addr(vdma::ContinuousBuffer &buffer);
private:    
    // vdma::ContinuousBuffer m_action_list_buffer;
    void* m_user_address;
    uint64_t m_dma_address;
    size_t m_write_offset;
    CONTROL_PROTOCOL__context_switch_context_info_chunk_t m_current_context_info;
};

} /* namespace hailort */

#endif /* _HAILO_DDR_ACTION_LIST_BUFFER_BUILDER_HPP_ */