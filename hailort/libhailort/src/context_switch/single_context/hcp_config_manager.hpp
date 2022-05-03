/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hcp_config_manager.hpp
 * @brief Manager of HEF parsing for control-configured network groups (Pcie and Etherent support)
 *
 *
 **/

#ifndef HAILO_HCP_CONFIG_MANAGER_HPP_
#define HAILO_HCP_CONFIG_MANAGER_HPP_

#include "context_switch/config_manager.hpp"
#include "context_switch/network_group_wrapper.hpp"
#include "context_switch/single_context/hcp_config_network_group.hpp"
#include "hailo/hailort.h"
#include "hailo/device.hpp"
#include "hailo/expected.hpp"
#include "common/utils.hpp"

#include <vector>
#include <map>
#include <algorithm>

namespace hailort
{

class HcpConfigManager : public ConfigManager {
public:
    HcpConfigManager(Device &device) : m_device(device) {}
    virtual ~HcpConfigManager() = default;
    virtual ConfigManagerType get_manager_type();

    virtual Expected<ConfiguredNetworkGroupVector> add_hef(Hef &hef,
        const NetworkGroupsParamsMap &configure_params={});

    HcpConfigManager(const HcpConfigManager &other) = delete;
    HcpConfigManager &operator=(const HcpConfigManager &other) = delete;
    HcpConfigManager &operator=(HcpConfigManager &&other) = delete;
    HcpConfigManager(HcpConfigManager &&other) noexcept = default;

private:
    // TODO: (SDK-16665) Dont need is_active flag for dtor?
    std::vector<std::shared_ptr<HcpConfigNetworkGroup>> m_net_groups;
    std::vector<std::shared_ptr<ConfiguredNetworkGroupWrapper>> m_net_group_wrappers;
    Device &m_device;
    HcpConfigActiveAppHolder m_active_net_group_holder;
};

} /* namespace hailort */

#endif /* HAILO_HCP_CONFIG_MANAGER_HPP_ */
