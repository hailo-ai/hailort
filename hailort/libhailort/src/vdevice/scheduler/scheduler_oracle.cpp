/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file scheduler_oracle.cpp
 * @brief:
 **/

#include "vdevice/scheduler/scheduler_oracle.hpp"
#include "utils/profiler/tracer_macros.hpp"


namespace hailort
{

bool CoreOpsSchedulerOracle::choose_next_model(SchedulerBase &scheduler, uint32_t device_id, bool check_threshold)
{
    auto device_info = scheduler.get_devices_info(device_id);
    auto priority_map = scheduler.get_core_op_priority_map();
    for (auto iter = priority_map.rbegin(); iter != priority_map.rend(); ++iter) {
        auto priority_group_size = iter->second.size();

        for (uint32_t i = 0; i < priority_group_size; i++) {
            uint32_t index = scheduler.get_last_choosen_core_op(iter->first) + i + 1;
            index %= static_cast<uint32_t>(priority_group_size);
            auto core_op_handle = iter->second[index];
            if (!is_core_op_active(scheduler, core_op_handle)) {
                auto ready_info = scheduler.is_core_op_ready(core_op_handle, check_threshold);
                if (ready_info.is_ready) {
                    TRACE(ChooseCoreOpTrace, "", core_op_handle, ready_info.threshold, ready_info.timeout, iter->first);
                    device_info->is_switching_core_op = true;
                    device_info->next_core_op_handle = core_op_handle;
                    scheduler.set_last_choosen_core_op(iter->first, index);

                    return true;
                }
            }
        }
    }

    return false;
}

// TODO: return device handle instead index
uint32_t CoreOpsSchedulerOracle::get_avail_device(SchedulerBase &scheduler, scheduler_core_op_handle_t core_op_handle)
{
    const bool check_threshold = false;
    auto device_count = scheduler.get_device_count();

    // Check if should be next
    /* Checking (INVALID_CORE_OP_HANDLE == m_current_core_op) for activating the first time the scheduler is running.
       In this case we don't want to check threshold. */
    for (uint32_t device_index = 0; device_index < device_count; device_index++) {
        auto active_device_info = scheduler.get_devices_info(device_index);
        if (active_device_info->is_switching_core_op && scheduler.has_core_op_drained_everything(active_device_info->current_core_op_handle, active_device_info->device_id) &&
            (((INVALID_CORE_OP_HANDLE == active_device_info->current_core_op_handle) &&
            scheduler.is_core_op_ready(core_op_handle, check_threshold).is_ready) ||
            (active_device_info->next_core_op_handle == core_op_handle))) {
            return active_device_info->device_id;
        }
    }

    // Check if device Idle
    // We dont need to check if the core op is ready, because the device is idle and if we arrive here frame is already sent and as a space in the output buffer.
    for (uint32_t device_index = 0; device_index < device_count; device_index++) {
        auto active_device_info = scheduler.get_devices_info(device_index);
        if (!active_device_info->is_switching_core_op && scheduler.has_core_op_drained_everything(active_device_info->current_core_op_handle, active_device_info->device_id)) {
            return active_device_info->device_id;
        }
    }

    return INVALID_DEVICE_ID;
}

bool CoreOpsSchedulerOracle::should_stop_streaming(SchedulerBase &scheduler, core_op_priority_t core_op_priority)
{
    auto priority_map = scheduler.get_core_op_priority_map();
    for (auto iter = priority_map.rbegin(); (iter != priority_map.rend()) && (iter->first >= core_op_priority); ++iter) {
        auto priority_group_size = iter->second.size();

        for (uint32_t i = 0; i < priority_group_size; i++) {
            uint32_t index = scheduler.get_last_choosen_core_op(iter->first) + i + 1;
            index %= static_cast<uint32_t>(priority_group_size);
            auto core_op_handle = iter->second[index];
            // We dont want to stay with the same network group if there is a other qualified network group
            if ((!is_core_op_active(scheduler, core_op_handle)) && scheduler.is_core_op_ready(core_op_handle, true).is_ready) {
                return true;
            }
        }
    }

    return false;
}

bool CoreOpsSchedulerOracle::is_core_op_active(SchedulerBase &scheduler, scheduler_core_op_handle_t core_op_handle)
{
    auto device_count = scheduler.get_device_count();
    for (uint32_t device_index = 0; device_index < device_count; device_index++) {
        auto active_device_info = scheduler.get_devices_info(device_index);
        if (core_op_handle == active_device_info->current_core_op_handle) {
            return true;
        }
    }

    return false;
}

} /* namespace hailort */
