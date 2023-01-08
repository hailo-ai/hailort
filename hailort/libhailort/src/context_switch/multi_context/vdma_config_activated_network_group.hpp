/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_config_activated_network_group.hpp
 * @brief TODO: Represent activated network_group from HEF
 **/

#ifndef _HAILO_CONTEXT_SWITCH_VDMA_CONFIG_ACTIVATED_NETWORK_GROUP_HPP_
#define _HAILO_CONTEXT_SWITCH_VDMA_CONFIG_ACTIVATED_NETWORK_GROUP_HPP_

#include "hailo/expected.hpp"
#include "vdma_channel.hpp"
#include "pcie_stream.hpp"
#include "context_switch/active_network_group_holder.hpp"
#include "context_switch/network_group_internal.hpp"
#include "context_switch/multi_context/resource_manager.hpp"

#include <vector>
#include <map>
#include <functional>

namespace hailort
{

class VdmaConfigActivatedNetworkGroup : public ActivatedNetworkGroupBase
{
public:

    static Expected<VdmaConfigActivatedNetworkGroup> create(
        ActiveNetGroupHolder &active_net_group_holder,
        const std::string &network_group_name,
        std::shared_ptr<ResourcesManager> resources_manager,
        const hailo_activate_network_group_params_t &network_group_params,
        uint16_t dynamic_batch_size,
        std::map<std::string, std::shared_ptr<InputStream>> &input_streams,
        std::map<std::string, std::shared_ptr<OutputStream>> &output_streams,
        EventPtr network_group_activated_event,
        AccumulatorPtr deactivation_time_accumulator,
        ConfiguredNetworkGroupBase &network_group);

    virtual ~VdmaConfigActivatedNetworkGroup();

    VdmaConfigActivatedNetworkGroup(const VdmaConfigActivatedNetworkGroup &other) = delete;
    VdmaConfigActivatedNetworkGroup &operator=(const VdmaConfigActivatedNetworkGroup &other) = delete;
    VdmaConfigActivatedNetworkGroup &operator=(VdmaConfigActivatedNetworkGroup &&other) = delete;
    VdmaConfigActivatedNetworkGroup(VdmaConfigActivatedNetworkGroup &&other) noexcept;

    virtual const std::string &get_network_group_name() const override;
    virtual Expected<Buffer> get_intermediate_buffer(const IntermediateBufferKey &key) override;
    virtual hailo_status set_keep_nn_config_during_reset(const bool keep_nn_config_during_reset) override;

private:
    VdmaConfigActivatedNetworkGroup(
      const std::string &network_group_name,
      const hailo_activate_network_group_params_t &network_group_params,
      uint16_t dynamic_batch_size,
      std::map<std::string, std::shared_ptr<InputStream>> &input_streams,
      std::map<std::string, std::shared_ptr<OutputStream>> &output_streams,
      std::shared_ptr<ResourcesManager> &&resources_manager,
      ActiveNetGroupHolder &active_net_group_holder,
      EventPtr &&network_group_activated_event,
      AccumulatorPtr deactivation_time_accumulator,
      ConfiguredNetworkGroupBase &network_group, hailo_status &status);

  std::string m_network_group_name;
  bool m_should_reset_network_group;
  ActiveNetGroupHolder &m_active_net_group_holder;
  std::shared_ptr<ResourcesManager> m_resources_manager;
  AccumulatorPtr m_deactivation_time_accumulator;
  bool m_keep_nn_config_during_reset;
};

} /* namespace hailort */

#endif /* _HAILO_CONTEXT_SWITCH_VDMA_CONFIG_ACTIVATED_NETWORK_GROUP_HPP_ */
