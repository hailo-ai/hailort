/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_config_network_group.hpp
 * @brief Represent network_group from HEF file that can be activated 
 *
 * This network_group can be used for both single or multi context network_groups but for PCIE only
  **/

#ifndef _HAILO_CONTEXT_SWITCH_VDMA_CONFIG_NETWORK_GROUP_HPP_
#define _HAILO_CONTEXT_SWITCH_VDMA_CONFIG_NETWORK_GROUP_HPP_

#include "hailo/hailort.h"
#include "common/utils.hpp"
#include "control_protocol.h"
#include "hailort_defaults.hpp"
#include "vdma_channel.hpp"
#include "context_switch/network_group_internal.hpp"
#include "context_switch/multi_context/resource_manager.hpp"
#include "context_switch/multi_context/vdma_config_activated_network_group.hpp"
#include "context_switch/active_network_group_holder.hpp"

#include <cstdint>
#include <assert.h>
#include <map>
#include <set>

namespace hailort
{


class VdmaConfigNetworkGroup : public ConfiguredNetworkGroupBase
{
public:
    static Expected<VdmaConfigNetworkGroup> create(ActiveNetGroupHolder &active_net_group_holder,
        const ConfigureNetworkParams &config_params, 
        std::shared_ptr<ResourcesManager> resources_managers, const std::string &hef_hash,
        std::shared_ptr<NetworkGroupMetadata> network_group_metadata,
        std::vector<std::shared_ptr<NetFlowElement>> &&net_flow_ops);

    std::shared_ptr<ResourcesManager> &get_resources_manager()
    {
        return m_resources_manager;
    }

    // Functions to activate and deactivate network group for scheduler - dont create ActivatedNetworkGroup objects
    virtual hailo_status activate_impl(uint16_t dynamic_batch_size) override;
    virtual hailo_status deactivate_impl() override;

    virtual Expected<std::unique_ptr<ActivatedNetworkGroup>> create_activated_network_group(
        const hailo_activate_network_group_params_t &network_group_params, uint16_t dynamic_batch_size) override;

    virtual Expected<hailo_stream_interface_t> get_default_streams_interface() override;

    virtual Expected<std::shared_ptr<LatencyMetersMap>> get_latency_meters() override;
    virtual Expected<std::shared_ptr<VdmaChannel>> get_boundary_vdma_channel_by_stream_name(
        const std::string &stream_name) override;

    virtual hailo_status set_scheduler_timeout(const std::chrono::milliseconds &timeout, const std::string &network_name) override;
    virtual hailo_status set_scheduler_threshold(uint32_t threshold, const std::string &network_name) override;

    virtual ~VdmaConfigNetworkGroup() = default;
    VdmaConfigNetworkGroup(const VdmaConfigNetworkGroup &other) = delete;
    VdmaConfigNetworkGroup &operator=(const VdmaConfigNetworkGroup &other) = delete;
    VdmaConfigNetworkGroup &operator=(VdmaConfigNetworkGroup &&other) = delete;
    VdmaConfigNetworkGroup(VdmaConfigNetworkGroup &&other) noexcept : ConfiguredNetworkGroupBase(std::move(other)),
        m_active_net_group_holder(other.m_active_net_group_holder),
        m_resources_manager(std::move(other.m_resources_manager)),
        m_hef_hash(std::move(other.m_hef_hash))
        {}

    bool equals(const Hef &hef, const std::string &network_group_name) {
        return (network_group_name == name()) && (hef.hash() == m_hef_hash);
    }

private:
    VdmaConfigNetworkGroup(ActiveNetGroupHolder &active_net_group_holder,
        const ConfigureNetworkParams &config_params,
        std::shared_ptr<ResourcesManager> &&resources_manager, const std::string &hef_hash,
        const NetworkGroupMetadata &network_group_metadata, hailo_status &status,
        std::vector<std::shared_ptr<NetFlowElement>> &&net_flow_ops);

    ActiveNetGroupHolder &m_active_net_group_holder;
    std::shared_ptr<ResourcesManager> m_resources_manager;
    std::string m_hef_hash;

    friend class VDeviceNetworkGroupWrapper;
};

} /* namespace hailort */

#endif /* _HAILO_CONTEXT_SWITCH_VDMA_CONFIG_NETWORK_GROUP_HPP_ */
