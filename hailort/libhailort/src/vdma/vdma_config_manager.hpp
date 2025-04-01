/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_config_manager.hpp
 * @brief Manager of HEF parsing and vdma-core-op resources for Pcie devices (both single and multi context)
 *
 **/

#ifndef HAILO_VDMA_CONFIG_MANAGER_HPP_
#define HAILO_VDMA_CONFIG_MANAGER_HPP_

#include "hailo/hailort.h"

#include "common/utils.hpp"

#include "vdma/vdma_config_core_op.hpp"


namespace hailort
{

class VdmaConfigManager final
{
public:
    VdmaConfigManager() = delete;

    static hailo_status set_core_op(const std::string &device_id, std::shared_ptr<VdmaConfigCoreOp> current,
        std::shared_ptr<VdmaConfigCoreOp> next, uint16_t batch_size);
    static hailo_status deactivate_core_op(std::shared_ptr<VdmaConfigCoreOp> current_active_core_op);

private:
    static hailo_status set_state_machine(std::shared_ptr<VdmaConfigCoreOp> current,
        std::shared_ptr<VdmaConfigCoreOp> next, uint16_t batch_size);

    static hailo_status switch_core_op(std::shared_ptr<VdmaConfigCoreOp> current,
        std::shared_ptr<VdmaConfigCoreOp> next, uint16_t batch_size);
    static hailo_status fast_batch_switch(std::shared_ptr<VdmaConfigCoreOp> current, uint16_t batch_size);
};

} /* namespace hailort */

#endif /* HAILO_VDMA_CONFIG_MANAGER_HPP_ */
