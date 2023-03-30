/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_config_activated_core_op.hpp
 * @brief Represent activated core-op from HEF
 **/

#ifndef _HAILO_CONTEXT_SWITCH_VDMA_CONFIG_ACTIVATED_CORE_OP_HPP_
#define _HAILO_CONTEXT_SWITCH_VDMA_CONFIG_ACTIVATED_CORE_OP_HPP_

#include "hailo/expected.hpp"

#include "vdma/channel/boundary_channel.hpp"
#include "core_op/active_core_op_holder.hpp"
#include "core_op/resource_manager/resource_manager.hpp"

#include <vector>
#include <map>
#include <functional>


namespace hailort
{

class VdmaConfigActivatedCoreOp : public ActivatedCoreOp
{
public:

    static Expected<VdmaConfigActivatedCoreOp> create(
        ActiveCoreOpHolder &active_core_op_holder,
        const std::string &core_op_name,
        std::shared_ptr<ResourcesManager> resources_manager,
        const hailo_activate_network_group_params_t &network_group_params,
        uint16_t dynamic_batch_size,
        std::map<std::string, std::shared_ptr<InputStream>> &input_streams,
        std::map<std::string, std::shared_ptr<OutputStream>> &output_streams,
        EventPtr core_op_activated_event,
        AccumulatorPtr deactivation_time_accumulator,
        bool resume_pending_stream_transfers,
        CoreOp &core_op);

    virtual ~VdmaConfigActivatedCoreOp();

    VdmaConfigActivatedCoreOp(const VdmaConfigActivatedCoreOp &other) = delete;
    VdmaConfigActivatedCoreOp &operator=(const VdmaConfigActivatedCoreOp &other) = delete;
    VdmaConfigActivatedCoreOp &operator=(VdmaConfigActivatedCoreOp &&other) = delete;
    VdmaConfigActivatedCoreOp(VdmaConfigActivatedCoreOp &&other) noexcept;

    virtual const std::string &get_network_group_name() const override;
    virtual Expected<Buffer> get_intermediate_buffer(const IntermediateBufferKey &key) override;
    virtual hailo_status set_keep_nn_config_during_reset(const bool keep_nn_config_during_reset) override;

private:
    VdmaConfigActivatedCoreOp(
      const std::string &core_op_name,
      const hailo_activate_network_group_params_t &network_group_params,
      uint16_t dynamic_batch_size,
      std::map<std::string, std::shared_ptr<InputStream>> &input_streams,
      std::map<std::string, std::shared_ptr<OutputStream>> &output_streams,
      std::shared_ptr<ResourcesManager> &&resources_manager,
      ActiveCoreOpHolder &active_core_op_holder,
      EventPtr &&core_op_activated_event,
      AccumulatorPtr deactivation_time_accumulator,
      bool resume_pending_stream_transfers,
      CoreOp &core_op,
      hailo_status &status);

  std::string m_core_op_name;
  bool m_should_reset_core_op;
  ActiveCoreOpHolder &m_active_core_op_holder;
  std::shared_ptr<ResourcesManager> m_resources_manager;
  AccumulatorPtr m_deactivation_time_accumulator;
  bool m_keep_nn_config_during_reset;
};

} /* namespace hailort */

#endif /* _HAILO_CONTEXT_SWITCH_VDMA_CONFIG_ACTIVATED_CORE_OP_HPP_ */
