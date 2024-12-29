/**
 * Copyright (c) 2019-2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file scheduler_oracle.cpp
 * @brief:
 **/

#include "vdevice/scheduler/scheduler_oracle.hpp"
#include "utils/profiler/tracer_macros.hpp"
#include "common/internal_env_vars.hpp"


namespace hailort
{

scheduler_core_op_handle_t CoreOpsSchedulerOracle::choose_next_model(SchedulerBase &scheduler, const device_id_t &device_id, bool check_threshold)
{
    auto device_info = scheduler.get_device_info(device_id);
    auto &priority_map = scheduler.get_core_op_priority_map();
    for (auto iter = priority_map.rbegin(); iter != priority_map.rend(); ++iter) {
        auto &priority_group = iter->second;

        // Iterate all core ops inside the priority group starting from priority_group next core op
        for (uint32_t i = 0; i < priority_group.size(); i++) {
            auto core_op_handle = priority_group.get(i);
            auto ready_info = scheduler.is_core_op_ready(core_op_handle, check_threshold, device_id);
            if (ready_info.is_ready) {
                // In cases device is idle the check_threshold is not needed, therefore is false.
                bool switch_because_idle = !(check_threshold);
                TRACE(OracleDecisionTrace, switch_because_idle, device_id, core_op_handle, ready_info.over_threshold, ready_info.over_timeout);
                device_info->is_switching_core_op = true;
                device_info->next_core_op_handle = core_op_handle;
                 // Set next to run as next in round-robin
                priority_group.set_next(i + 1);
                return core_op_handle;
            }
        }
    }

    return INVALID_CORE_OP_HANDLE;
}

bool CoreOpsSchedulerOracle::should_stop_streaming(SchedulerBase &scheduler, core_op_priority_t core_op_priority, const device_id_t &device_id)
{
    const auto device_info = scheduler.get_device_info(device_id);
    if (device_info->frames_left_before_stop_streaming > 0) {
        // Only when frames_left_before_stop_streaming we consider stop streaming
        return false;
    }

    // Now check if there is another qualified core op.
    const auto &priority_map = scheduler.get_core_op_priority_map();
    for (auto iter = priority_map.rbegin(); (iter != priority_map.rend()) && (iter->first >= core_op_priority); ++iter) {
        auto &priority_group = iter->second;

        // Iterate all core ops inside the priority group starting from next_core_op_index
        for (uint32_t i = 0; i < priority_group.size(); i++) {
            auto core_op_handle = priority_group.get(i);
            if (!is_core_op_active(scheduler, core_op_handle) && scheduler.is_core_op_ready(core_op_handle, true, device_id).is_ready) {
                return true;
            }
        }
    }

    return false;
}

bool CoreOpsSchedulerOracle::is_core_op_active(SchedulerBase &scheduler, scheduler_core_op_handle_t core_op_handle)
{
    auto &devices = scheduler.get_device_infos();
    for (const auto &pair : devices) {
        auto &active_device_info = pair.second;
        if (core_op_handle == active_device_info->current_core_op_handle) {
            return true;
        }
    }

    return false;
}

std::vector<RunParams> CoreOpsSchedulerOracle::get_oracle_decisions(SchedulerBase &scheduler)
{
    auto &devices = scheduler.get_device_infos();
    std::vector<RunParams> oracle_decision;

    for (const auto &pair : devices) {
        auto &active_device_info = pair.second;

        // Check if device is switching ng
        if (active_device_info->is_switching_core_op) {
            oracle_decision.push_back({active_device_info->next_core_op_handle, active_device_info->device_id});
        }

        // Check if device is idle
        if (!active_device_info->is_switching_core_op && active_device_info->is_idle()) {
            const bool CHECK_THRESHOLD = true;
            auto core_op_handle = choose_next_model(scheduler, active_device_info->device_id, CHECK_THRESHOLD);

            // If there is no suitable model when checking with threshold, and the idle optimization is disabled,
            // try again without threshold.
            if (!is_env_variable_on(HAILO_DISABLE_IDLE_OPT_ENV_VAR) && (core_op_handle == INVALID_CORE_OP_HANDLE)) {
                core_op_handle = choose_next_model(scheduler, active_device_info->device_id, !CHECK_THRESHOLD);
            }

            if (core_op_handle != INVALID_CORE_OP_HANDLE) {
                // We have a decision
                oracle_decision.push_back({core_op_handle, active_device_info->device_id});
            }
        }
    }

    return oracle_decision;
}

} /* namespace hailort */
