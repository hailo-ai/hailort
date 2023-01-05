/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hcp_config_network_group.hpp
 * @brief Represent network_group from HEF file that can be activated 
 *
 * This network_group can be used for control-configured network_groups (for etherent or pcie)
  **/

#ifndef _HAILO_CONTEXT_SWITCH_HCP_CONFIG_NETWORK_GROUP_HPP_
#define _HAILO_CONTEXT_SWITCH_HCP_CONFIG_NETWORK_GROUP_HPP_

#include "hailo/device.hpp"
#include "common/utils.hpp"
#include "context_switch/network_group_internal.hpp"
#include "context_switch/active_network_group_holder.hpp"
#include "hailort_defaults.hpp"
#include "context_switch/single_context/hcp_config_activated_network_group.hpp"

#include <vector>
#include <map>

namespace hailort
{

class HcpConfigNetworkGroup : public ConfiguredNetworkGroupBase
{
public:
    HcpConfigNetworkGroup(
        Device &device, ActiveNetGroupHolder &active_net_group_holder, std::vector<WriteMemoryInfo> &&config,
        const ConfigureNetworkParams &config_params, NetworkGroupMetadata &&network_group_metadata, hailo_status &status,
        std::vector<std::shared_ptr<NetFlowElement>> &&net_flow_ops);

    virtual Expected<std::unique_ptr<ActivatedNetworkGroup>> create_activated_network_group(
        const hailo_activate_network_group_params_t &network_group_params, uint16_t dynamic_batch_size) override;
    virtual Expected<hailo_stream_interface_t> get_default_streams_interface() override;

    virtual Expected<std::shared_ptr<LatencyMetersMap>> get_latency_meters() override;
    virtual Expected<std::shared_ptr<VdmaChannel>> get_boundary_vdma_channel_by_stream_name(
        const std::string &stream_name) override;
    virtual hailo_status set_scheduler_timeout(const std::chrono::milliseconds &timeout, const std::string &network_name) override;
    virtual hailo_status set_scheduler_threshold(uint32_t threshold, const std::string &network_name) override;

    virtual hailo_status activate_impl(uint16_t dynamic_batch_size) override;
    virtual hailo_status deactivate_impl() override;

    virtual ~HcpConfigNetworkGroup() = default;
    HcpConfigNetworkGroup(const HcpConfigNetworkGroup &other) = delete;
    HcpConfigNetworkGroup &operator=(const HcpConfigNetworkGroup &other) = delete;
    HcpConfigNetworkGroup &operator=(HcpConfigNetworkGroup &&other) = delete;
    HcpConfigNetworkGroup(HcpConfigNetworkGroup &&other) noexcept : ConfiguredNetworkGroupBase(std::move(other)),
        m_config(std::move(other.m_config)), m_active_net_group_holder(other.m_active_net_group_holder),
        m_device(other.m_device) {}

private:
        std::vector<WriteMemoryInfo> m_config;
        ActiveNetGroupHolder &m_active_net_group_holder;
        Device &m_device;
};

} /* namespace hailort */

#endif /* _HAILO_CONTEXT_SWITCH_HCP_CONFIG_NETWORK_GROUP_HPP_ */
