/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_config_manager.cpp
 * @brief Vdma config manager implementation
 **/

#include "vdma_config_manager.hpp"
#include "utils/profiler/tracer_macros.hpp"

namespace hailort
{


hailo_status VdmaConfigManager::set_core_op(const std::string &device_id, std::shared_ptr<VdmaConfigCoreOp> current,
    std::shared_ptr<VdmaConfigCoreOp> next, const uint16_t batch_size)
{
    CHECK((nullptr != current) || (nullptr != next), HAILO_INVALID_ARGUMENT);

    const auto start_time = std::chrono::steady_clock::now();

    const bool is_batch_switch = (current == next) && current->get_resources_manager()->get_can_fast_batch_switch();
    if (is_batch_switch) {
        CHECK_SUCCESS(fast_batch_switch(current, batch_size), "Failed to fast batch switch");
    } else {
        CHECK_SUCCESS(switch_core_op(current, next, batch_size), "Failed to switch core-op");
    }

    const auto core_op_handle = next ? next->vdevice_core_op_handle() : INVALID_CORE_OP_HANDLE;
    const auto elapsed_time_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start_time).count();
    TRACE(SwitchCoreOpTrace, device_id, core_op_handle, elapsed_time_ms, batch_size);

    return HAILO_SUCCESS;
}


hailo_status VdmaConfigManager::deactivate_core_op(std::shared_ptr<VdmaConfigCoreOp> current_active_core_op)
{
    static const uint16_t DEACTIVATE_BATCH_SIZE = 0;
    const std::shared_ptr<VdmaConfigCoreOp> DEACTIVATE_NEXT_CORE_OP = nullptr;
    return switch_core_op(current_active_core_op, DEACTIVATE_NEXT_CORE_OP, DEACTIVATE_BATCH_SIZE);
}

// TODO: fix callback registration and deregistration in the case of switch NG (HRT-14287)
hailo_status VdmaConfigManager::set_state_machine(std::shared_ptr<VdmaConfigCoreOp> current,
    std::shared_ptr<VdmaConfigCoreOp> next, uint16_t batch_size)
{
    // TODO: HRT-13253 don't use resources manager instead call m_vdma_device directly. The device should store the 
    // current active core op.
    if (next != nullptr) {
        CHECK_SUCCESS(next->register_cache_update_callback(), "Failed to register cache update callback");
        CHECK_SUCCESS(next->get_resources_manager()->enable_state_machine(batch_size), "Failed to enable state machine");
        // In the case of switch NG, we call FW switch to next NG without marking the current NG as deactivated.
        // Added setter to mark the current NG as deactivated.
        if ((current != nullptr) && (current != next)) {
            current->get_resources_manager()->set_is_activated(false);
        }
    } else {
        assert(current != nullptr);
        CHECK_SUCCESS(current->get_resources_manager()->reset_state_machine(), "Failed to disable state machine");
    }
    return HAILO_SUCCESS;
}

hailo_status VdmaConfigManager::switch_core_op(std::shared_ptr<VdmaConfigCoreOp> current,
    std::shared_ptr<VdmaConfigCoreOp> next, const uint16_t batch_size)
{
    assert((nullptr != current) || (nullptr != next));

    if (current != nullptr) {
        CHECK_SUCCESS(current->unregister_cache_update_callback(), "Failed unregistering cache updates from previous core-op");
        CHECK_SUCCESS(current->deactivate_host_resources(), "Failed deactivating host resources for current core-op");

        // TODO: In integrated device we need to reset after deactivate. This will be fixed in MSW-762 and the "if" will be removed
        //       when we make the nn_manager responsible to reset the nn-core.
        if (Device::Type::INTEGRATED == current->get_resources_manager()->get_device().get_type()) {
            CHECK_SUCCESS(current->get_resources_manager()->reset_state_machine(), "Failed to reset state machine in switch core-op");
        }
    }

    CHECK_SUCCESS(set_state_machine(current, next, batch_size), "Failed to set state machine");

    // Activate next core op resources
    if (next != nullptr) {
        CHECK_SUCCESS(next->activate_host_resources(), "Failed activating host resources for next core-op");
    }

    if (current != nullptr) {
        CHECK_SUCCESS(current->cancel_pending_transfers(), "Failed canceling pending transfers from previous core-op");
    }

    return HAILO_SUCCESS;
}

hailo_status VdmaConfigManager::fast_batch_switch(std::shared_ptr<VdmaConfigCoreOp> current, const uint16_t batch_size)
{
    assert(nullptr != current);
    return set_state_machine(current, current, batch_size);
}

} /* namespace hailort */
