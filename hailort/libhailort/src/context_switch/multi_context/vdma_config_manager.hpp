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

#include "context_switch/config_manager.hpp"
#include "context_switch/network_group_wrapper.hpp"
#include "context_switch/multi_context/vdma_config_network_group.hpp"
#include "hailo/hailort.h"
#include "hailo/device.hpp"
#include "hailo/vdevice.hpp"
#include "hailo/expected.hpp"
#include "common/utils.hpp"
#include "hlpcie.hpp"
#include "vdma_channel.hpp"
#include "network_group_scheduler.hpp"

#include <vector>
#include <map>
#include <algorithm>
#include <bitset>

namespace hailort
{

class VdmaConfigManager : public ConfigManager
{
public:
    static Expected<VdmaConfigManager> create(VdmaDevice &device);
    static Expected<VdmaConfigManager> create(VDevice &vdevice);
    virtual ConfigManagerType get_manager_type();
    virtual Expected<ConfiguredNetworkGroupVector> add_hef(Hef &hef,
        const NetworkGroupsParamsMap &configure_params, bool is_scheduler_used=false);

    static hailo_status update_network_batch_size(ConfigureNetworkParams &configure_params);

    virtual ~VdmaConfigManager() = default;
    VdmaConfigManager(const VdmaConfigManager &other) noexcept = delete;
    VdmaConfigManager &operator=(const VdmaConfigManager &other) = delete;
    VdmaConfigManager &operator=(VdmaConfigManager &&other) = delete;
    VdmaConfigManager(VdmaConfigManager &&other) noexcept = default;

  private:
    VdmaConfigManager(std::vector<std::reference_wrapper<VdmaDevice>> &&devices, bool is_vdevice, NetworkGroupSchedulerWeakPtr network_group_scheduler);

    // TODO: (SDK-16665) Dont need is_active flag for dtor?
    std::vector<std::reference_wrapper<VdmaDevice>> m_devices;
    std::vector<std::shared_ptr<VdmaConfigNetworkGroup>> m_net_groups;
    std::vector<std::shared_ptr<ConfiguredNetworkGroupWrapper>> m_net_group_wrappers;
    VdmaConfigActiveAppHolder m_active_net_group_holder;
    bool m_is_vdevice;
    NetworkGroupSchedulerWeakPtr m_network_group_scheduler;
};

} /* namespace hailort */

#endif /* HAILO_VDMA_CONFIG_MANAGER_HPP_ */
