/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file vdma_config_manager.cpp
 * @brief Vdma config manager implementation
 **/

#include "vdma_config_manager.hpp"
#include "hailo/hailort.h"

namespace hailort
{

hailo_status VdmaConfigManager::switch_core_op(std::shared_ptr<VdmaConfigCoreOp> current_active_core_op,
    std::shared_ptr<VdmaConfigCoreOp> next_core_op, const uint16_t batch_size, const bool is_batch_switch)
{
    CHECK((nullptr != current_active_core_op) || (nullptr != next_core_op), HAILO_INVALID_ARGUMENT);

    if (nullptr == current_active_core_op) {
        // Activate first core-op
        return next_core_op->activate_impl(batch_size);
    } else if (nullptr == next_core_op) {
        // Deactivate last core-op
        return current_active_core_op->deactivate_impl();
    } else if (is_batch_switch) {
        auto status = current_active_core_op->get_resources_manager()->enable_state_machine(batch_size);
        CHECK_SUCCESS(status, "Failed to activate state-machine");
    } else {
        // We're switching from current_active_core_op to next_core_op.
        // Deactivate the current core-op on the host, meaning the fw state machine won't be reset.
        // This will be handled by activating the next core-op.
        auto status = current_active_core_op->deactivate_host_resources();
        CHECK_SUCCESS(status, "Failed deactivating current core-op");

        // TODO: In mercury we need to reset after deactivate. This will be fixed in MSW-762 and the "if" will be removed
        //       when we make the nn_manager responsible to reset the nn-core.
        if (Device::Type::INTEGRATED == current_active_core_op->get_resources_manager()->get_device().get_type()) {
            status = current_active_core_op->get_resources_manager()->reset_state_machine();
            CHECK_SUCCESS(status, "Failed to reset state machine in switch core-op");
        }

        // Switch from the current core-op to the next core-op. I.e. current core-op will be deactivated and
        // next core-op will be activated
        status = next_core_op->activate_impl(batch_size);
        CHECK_SUCCESS(status, "Failed activating next core-op");

        // Current core-op is now deactivated (we are not on batch switch), so we can cancel pending transfers.
        status = current_active_core_op->get_resources_manager()->cancel_pending_transfers();
        CHECK_SUCCESS(status, "Failed canceling pending transfers from previous core-op");
    }

    return HAILO_SUCCESS;
}

hailo_status VdmaConfigManager::deactivate_core_op(std::shared_ptr<VdmaConfigCoreOp> current_active_core_op)
{
    static const uint16_t DEACTIVATE_BATCH_SIZE = 0;
    const std::shared_ptr<VdmaConfigCoreOp> DEACTIVATE_NEXT_CORE_OP = nullptr;
    static const bool IS_NOT_BATCH_SWITCH = false;
    return switch_core_op(current_active_core_op, DEACTIVATE_NEXT_CORE_OP, DEACTIVATE_BATCH_SIZE, IS_NOT_BATCH_SWITCH);
}

} /* namespace hailort */
