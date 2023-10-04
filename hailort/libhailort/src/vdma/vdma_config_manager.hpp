/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
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

    static hailo_status switch_core_op(std::shared_ptr<VdmaConfigCoreOp> current_active_core_op,
        std::shared_ptr<VdmaConfigCoreOp> next_core_op, const uint16_t batch_size, const bool is_batch_switch);

    static hailo_status deactivate_core_op(std::shared_ptr<VdmaConfigCoreOp> current_active_core_op);
};

} /* namespace hailort */

#endif /* HAILO_VDMA_CONFIG_MANAGER_HPP_ */
