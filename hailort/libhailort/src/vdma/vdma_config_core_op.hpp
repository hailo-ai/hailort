/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_config_core_op.hpp
 * @brief Represent core-op from HEF file that can be activated 
 *
 * This core-op can be used for both single or multi context core-ops but for PCIE only
  **/

#ifndef _HAILO_CONTEXT_SWITCH_VDMA_CONFIG_CORE_OP_HPP_
#define _HAILO_CONTEXT_SWITCH_VDMA_CONFIG_CORE_OP_HPP_

#include "hailo/hailort.h"
#include "hailo/network_group.hpp"
#include "hailo/hailort_defaults.hpp"

#include "common/utils.hpp"

#include "vdma/channel/boundary_channel.hpp"
#include "core_op/resource_manager/resource_manager.hpp"
#include "vdma/vdma_config_activated_core_op.hpp"
#include "core_op/active_core_op_holder.hpp"

#include "control_protocol.h"
#include <cstdint>
#include <assert.h>
#include <map>
#include <set>


namespace hailort
{


class VdmaConfigCoreOp : public CoreOp
{
public:
    static Expected<VdmaConfigCoreOp> create(ActiveCoreOpHolder &active_core_op_holder,
        const ConfigureNetworkParams &config_params, 
        std::shared_ptr<ResourcesManager> resources_managers,
        std::shared_ptr<CoreOpMetadata> metadata);

    std::shared_ptr<ResourcesManager> &get_resources_manager()
    {
        return m_resources_manager;
    }

    // Functions to activate and deactivate core ops for scheduler - dont create ActivatedNetworkGroup objects
    // Note: Care should be taken when calling activate_impl with resume_pending_stream_transfers = true.
    //       If an output stream has outstanding transfers, and the NG is deactivated (via deactivate_impl) before they
    //       have been completed, then these pending transfers may be overwritten upon channel activation.
    //       Hence, when setting resume_pending_stream_transfers = true, the caller must validate that all pending
    //       reads have been received (i.e. an int has been raised for this transfer)
    virtual hailo_status activate_impl(uint16_t dynamic_batch_size, bool resume_pending_stream_transfers) override;
    // Will first deactivate host resources (via deactivate_host_resources) and then reset the core-op on the fw
    virtual hailo_status deactivate_impl(bool keep_nn_config_during_reset) override;
    // Deactivate all resources related to the core-op on the host, but without resetting the core-op on the fw
    hailo_status deactivate_host_resources();

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
    virtual Expected<HwInferResults> run_hw_infer_estimator() override;

    virtual ~VdmaConfigCoreOp() = default;
    VdmaConfigCoreOp(const VdmaConfigCoreOp &other) = delete;
    VdmaConfigCoreOp &operator=(const VdmaConfigCoreOp &other) = delete;
    VdmaConfigCoreOp &operator=(VdmaConfigCoreOp &&other) = delete;
    VdmaConfigCoreOp(VdmaConfigCoreOp &&other) noexcept : CoreOp(std::move(other)),
        m_active_core_op_holder(other.m_active_core_op_holder),
        m_resources_manager(std::move(other.m_resources_manager))
        {}

private:
    VdmaConfigCoreOp(ActiveCoreOpHolder &active_core_op_holder,
        const ConfigureNetworkParams &config_params,
        std::shared_ptr<ResourcesManager> &&resources_manager,
        std::shared_ptr<CoreOpMetadata> metadata, hailo_status &status);

    ActiveCoreOpHolder &m_active_core_op_holder;
    std::shared_ptr<ResourcesManager> m_resources_manager;
};

} /* namespace hailort */

#endif /* _HAILO_CONTEXT_SWITCH_VDMA_CONFIG_CORE_OP_HPP_ */
