/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hcp_config_activated_core_op.hpp
 * @brief Represent activated core-op from HEF. 
 *
 * This core-op can be used for control-core-op only (for etherent or pcie)
  **/

#ifndef _HAILO_CONTEXT_SWITCH_HCP_CONFIG_ACTIVATED_CORE_OP_HPP_
#define _HAILO_CONTEXT_SWITCH_HCP_CONFIG_ACTIVATED_CORE_OP_HPP_

#include "hailo/device.hpp"

#include "common/utils.hpp"

#include "core_op/active_core_op_holder.hpp"

#include <vector>
#include <map>


namespace hailort
{

struct WriteMemoryInfo
{
    uint32_t address;
    Buffer data;
};

class HcpConfigActivatedCoreOp : public ActivatedCoreOp
{
  public:
    static Expected<HcpConfigActivatedCoreOp> create(Device &device, std::vector<WriteMemoryInfo> &config,
        const std::string &core_op_name,
        const hailo_activate_network_group_params_t &network_group_params,
        std::map<std::string, std::shared_ptr<InputStream>> &input_streams,
        std::map<std::string, std::shared_ptr<OutputStream>> &output_streams,
        ActiveCoreOpHolder &active_core_op_holder,
        hailo_power_mode_t power_mode, EventPtr core_op_activated_event,
        CoreOp &core_op);

    virtual ~HcpConfigActivatedCoreOp();
    HcpConfigActivatedCoreOp(const HcpConfigActivatedCoreOp &) = delete;
    HcpConfigActivatedCoreOp &operator=(const HcpConfigActivatedCoreOp &) = delete;
    HcpConfigActivatedCoreOp &operator=(HcpConfigActivatedCoreOp &&) = delete;
    HcpConfigActivatedCoreOp(HcpConfigActivatedCoreOp &&other) noexcept :
      ActivatedCoreOp(std::move(other)), m_active_core_op_holder(other.m_active_core_op_holder),
      m_is_active(std::exchange(other.m_is_active, false)), m_power_mode(other.m_power_mode),
      m_device(other.m_device), m_core_op_name(std::move(other.m_core_op_name)) {};

    virtual const std::string &get_network_group_name() const override;

    virtual Expected<Buffer> get_intermediate_buffer(const IntermediateBufferKey &/*key*/) override
    {
        LOGGER__ERROR("get_intermediate_buffer() is not supported on single_context core_ops");
        return make_unexpected(HAILO_INVALID_OPERATION);
    }

    virtual hailo_status set_keep_nn_config_during_reset(const bool /* keep_nn_config_during_reset */) override
    {
        LOGGER__ERROR("set_keep_nn_config_during_reset() is not supported on single_context core_ops");
        return HAILO_INVALID_OPERATION;
    }

  private:
      HcpConfigActivatedCoreOp(Device &device, ActiveCoreOpHolder &active_core_op_holder,
        const std::string &core_op_name,
        const hailo_activate_network_group_params_t &network_group_params,
        std::map<std::string, std::shared_ptr<InputStream>> &input_streams,
        std::map<std::string, std::shared_ptr<OutputStream>> &output_streams,        
        hailo_power_mode_t power_mode, EventPtr &&core_op_activated_event,
        CoreOp &core_op, hailo_status &status);

    ActiveCoreOpHolder &m_active_core_op_holder;
    bool m_is_active;
    hailo_power_mode_t m_power_mode;
    Device &m_device;
    std::string m_core_op_name;
};

} /* namespace hailort */

#endif /* _HAILO_CONTEXT_SWITCH_HCP_CONFIG_ACTIVATED_CORE_OP_HPP_ */
