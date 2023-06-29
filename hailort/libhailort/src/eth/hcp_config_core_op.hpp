/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hcp_config_core_op.hpp
 * @brief Represent core-op from HEF file that can be activated 
 *
 * This core-op can be used for control-core-op (for etherent or pcie)
  **/

#ifndef _HAILO_CONTEXT_SWITCH_HCP_CONFIG_CORE_OP_HPP_
#define _HAILO_CONTEXT_SWITCH_HCP_CONFIG_CORE_OP_HPP_

#include "hailo/device.hpp"
#include "hailo/hailort_defaults.hpp"

#include "common/utils.hpp"

#include "eth/hcp_config_activated_core_op.hpp"
#include "core_op/active_core_op_holder.hpp"
#include "core_op/core_op.hpp"

#include <vector>
#include <map>


namespace hailort
{

class HcpConfigCoreOp : public CoreOp
{
public:
    HcpConfigCoreOp(
        Device &device, ActiveCoreOpHolder &active_core_op_holder, std::vector<WriteMemoryInfo> &&config,
        const ConfigureNetworkParams &config_params, std::shared_ptr<CoreOpMetadata> metadata, hailo_status &status);

    virtual Expected<std::unique_ptr<ActivatedNetworkGroup>> create_activated_network_group(
        const hailo_activate_network_group_params_t &network_group_params, uint16_t dynamic_batch_size,
        bool resume_pending_stream_transfers) override;
    virtual Expected<hailo_stream_interface_t> get_default_streams_interface() override;

    virtual Expected<std::shared_ptr<LatencyMetersMap>> get_latency_meters() override;
    virtual Expected<vdma::BoundaryChannelPtr> get_boundary_vdma_channel_by_stream_name(
        const std::string &stream_name) override;
    virtual bool is_scheduled() const override;
    virtual hailo_status set_scheduler_timeout(const std::chrono::milliseconds &timeout, const std::string &network_name) override;
    virtual hailo_status set_scheduler_threshold(uint32_t threshold, const std::string &network_name) override;
    virtual hailo_status set_scheduler_priority(uint8_t priority, const std::string &network_name) override;

    virtual hailo_status activate_impl(uint16_t dynamic_batch_size, bool resume_pending_stream_transfers) override;
    virtual hailo_status deactivate_impl(bool keep_nn_config_during_reset) override;
    virtual Expected<HwInferResults> run_hw_infer_estimator() override;

    virtual ~HcpConfigCoreOp() = default;
    HcpConfigCoreOp(const HcpConfigCoreOp &other) = delete;
    HcpConfigCoreOp &operator=(const HcpConfigCoreOp &other) = delete;
    HcpConfigCoreOp &operator=(HcpConfigCoreOp &&other) = delete;
    HcpConfigCoreOp(HcpConfigCoreOp &&other) noexcept : CoreOp(std::move(other)),
        m_config(std::move(other.m_config)), m_active_core_op_holder(other.m_active_core_op_holder),
        m_device(other.m_device) {}

private:
        std::vector<WriteMemoryInfo> m_config;
        ActiveCoreOpHolder &m_active_core_op_holder;
        Device &m_device;
};

} /* namespace hailort */

#endif /* _HAILO_CONTEXT_SWITCH_HCP_CONFIG_CORE_OP_HPP_ */
