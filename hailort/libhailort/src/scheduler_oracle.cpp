/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file scheduler_oracle.cpp
 * @brief:
 **/

#include "scheduler_oracle.hpp"
#include "tracer_macros.hpp"

namespace hailort
{

bool NetworkGroupSchedulerOracle::choose_next_model(NetworkGroupScheduler &scheduler, uint32_t device_id)
{
    auto cngs_size = scheduler.m_cngs.size();
    auto& device_info = scheduler.m_devices[device_id];
    for (uint32_t i = 0; i < cngs_size; i++) {
        uint32_t index = scheduler.m_last_choosen_network_group + i + 1;
        index %= static_cast<uint32_t>(cngs_size);
        auto ready_info = scheduler.is_network_group_ready(index, true, device_id);
        if (ready_info.is_ready) {
            TRACE(ChooseNetworkGroupTrace, "", index, ready_info.threshold, ready_info.timeout);
            device_info->is_switching_network_group = true;
            device_info->next_network_group_handle = index;
            scheduler.m_last_choosen_network_group = index;
            return true;
        }
    }
    return false;
}

// TODO: return device handle instead index
uint32_t NetworkGroupSchedulerOracle::get_avail_device(NetworkGroupScheduler &scheduler, scheduler_ng_handle_t network_group_handle)
{
    const bool check_threshold = false;

    // Check if should be next
    /* Checking (INVALID_NETWORK_GROUP_HANDLE == m_current_network_group) for activating the first time the scheduler is running.
       In this case we don't want to check threshold. */
    for (auto active_device_info : scheduler.m_devices) {
        if (active_device_info->is_switching_network_group && scheduler.has_ng_drained_everything(active_device_info->current_network_group_handle, active_device_info->device_id) &&
            (((INVALID_NETWORK_GROUP_HANDLE == active_device_info->current_network_group_handle) &&
            scheduler.is_network_group_ready(network_group_handle, check_threshold, active_device_info->device_id).is_ready) ||
            (active_device_info->next_network_group_handle == network_group_handle))) {
            return active_device_info->device_id;
        }
    }

    // Check if device Idle with this network active
    for (auto active_device_info : scheduler.m_devices) {
        if ((active_device_info->current_network_group_handle == network_group_handle) && !active_device_info->is_switching_network_group &&
            scheduler.has_ng_drained_everything(active_device_info->current_network_group_handle, active_device_info->device_id) &&
            scheduler.is_network_group_ready(network_group_handle, check_threshold, active_device_info->device_id).is_ready) {
            scheduler.m_last_choosen_network_group = network_group_handle;
            return active_device_info->device_id;
        }
    }

    // Check if device Idle
    for (auto active_device_info : scheduler.m_devices) {
        if (!active_device_info->is_switching_network_group && scheduler.has_ng_drained_everything(active_device_info->current_network_group_handle, active_device_info->device_id) &&
            scheduler.is_network_group_ready(network_group_handle, check_threshold, active_device_info->device_id).is_ready) {
            scheduler.m_last_choosen_network_group = network_group_handle;
            return active_device_info->device_id;
        }
    }

    return INVALID_DEVICE_ID;
}

} /* namespace hailort */
