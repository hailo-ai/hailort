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
        VdmaConfigActiveAppHolder &active_net_group_holder,
        const std::string &network_group_name,
        std::vector<std::shared_ptr<ResourcesManager>> resources_managers,
        const hailo_activate_network_group_params_t &network_group_params,
        uint16_t dynamic_batch_size,
        std::map<std::string, std::unique_ptr<InputStream>> &input_streams,
        std::map<std::string, std::unique_ptr<OutputStream>> &output_streams,
        EventPtr network_group_activated_event,
        AccumulatorPtr deactivation_time_accumulator);

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
      std::map<std::string, std::unique_ptr<InputStream>> &input_streams,
      std::map<std::string, std::unique_ptr<OutputStream>> &output_streams,
      std::vector<std::shared_ptr<ResourcesManager>> &&resources_managers,
      VdmaConfigActiveAppHolder &active_net_group_holder,
      EventPtr &&network_group_activated_event,
      AccumulatorPtr deactivation_time_accumulator, hailo_status &status);

    hailo_status init_ddr_resources();
    hailo_status cleanup_ddr_resources();

    static void ddr_recv_thread_main(DdrChannelsInfo ddr_info,
      std::shared_ptr<std::atomic<uint16_t>> desc_list_num_ready);
    static void ddr_send_thread_main(DdrChannelsInfo ddr_info,
      std::shared_ptr<std::atomic<uint16_t>> desc_list_num_ready);

  std::string m_network_group_name;
  bool m_should_reset_network_group;
  VdmaConfigActiveAppHolder &m_active_net_group_holder;
  // One ResourcesManager per connected physical device. Currently only one device is supported.
  std::vector<std::shared_ptr<ResourcesManager>> m_resources_managers;
  std::vector<std::thread> m_ddr_send_threads;
  std::vector<std::thread> m_ddr_recv_threads;
  AccumulatorPtr m_deactivation_time_accumulator;
  bool m_keep_nn_config_during_reset;
};

} /* namespace hailort */

#endif /* _HAILO_CONTEXT_SWITCH_VDMA_CONFIG_ACTIVATED_NETWORK_GROUP_HPP_ */
