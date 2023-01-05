/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_config_manager.hpp
 * @brief Manager of HEF parsing and vdma-configured network groups resources for Pcie devices (both single and multi context)
 *
 **/

#ifndef HAILO_VDMA_CONFIG_MANAGER_HPP_
#define HAILO_VDMA_CONFIG_MANAGER_HPP_

#include "context_switch/multi_context/vdma_config_network_group.hpp"
#include "hailo/hailort.h"
#include "common/utils.hpp"


namespace hailort
{

class VdmaConfigManager final
{
public:
    VdmaConfigManager() = delete;

    static hailo_status switch_network_group(std::shared_ptr<VdmaConfigNetworkGroup> current_active_ng,
        std::shared_ptr<VdmaConfigNetworkGroup> next_ng, const uint16_t batch_size)
    {
        auto status = HAILO_UNINITIALIZED;
        // If current_active_ng is nullptr - we are activating first network group
        if (nullptr != current_active_ng) {
            status = current_active_ng->deactivate_impl();
            CHECK_SUCCESS(status, "Failed deactivating current network group");

            // TODO: MSW-762 - In mercury we need to reset after deactivate in case of mercury - this will be fixed and the
            // If will be removed when we make the nn_manager responsible to reset the nn-core
            // And if switching to nullptr (which is final deactivate - we must also reset state machine)
            if (Device::Type::CORE == current_active_ng->get_resources_manager()->get_device().get_type() ||
                (nullptr == next_ng)) {
                status = current_active_ng->get_resources_manager()->reset_state_machine(false);
                CHECK_SUCCESS(status, "Failed to reset state machine in switch network group");
            }
        }

        // If next_ng is nullptr we are deactivating last network group
        if (nullptr != next_ng) {
            status = next_ng->activate_impl(batch_size);
            CHECK_SUCCESS(status, "Failed activating network group");
        }

        return HAILO_SUCCESS;
    }
};

} /* namespace hailort */

#endif /* HAILO_VDMA_CONFIG_MANAGER_HPP_ */
