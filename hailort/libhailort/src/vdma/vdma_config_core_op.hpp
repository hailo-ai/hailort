/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_config_core_op.hpp
 * @brief Represent core-op configured over vDMA for single physical device
  **/

#ifndef _HAILO_CONTEXT_SWITCH_VDMA_CONFIG_CORE_OP_HPP_
#define _HAILO_CONTEXT_SWITCH_VDMA_CONFIG_CORE_OP_HPP_

#include "hailo/hailort.h"
#include "hailo/network_group.hpp"
#include "hailo/hailort_defaults.hpp"

#include "common/utils.hpp"

#include "vdma/channel/boundary_channel.hpp"
#include "core_op/resource_manager/resource_manager.hpp"
#include "core_op/resource_manager/cache_manager.hpp"
#include "core_op/active_core_op_holder.hpp"

#include "control_protocol.h"
#include <cstdint>
#include <assert.h>
#include <map>
#include <set>


namespace hailort
{


class VdmaConfigCoreOp : public CoreOp {
public:
    static Expected<std::shared_ptr<VdmaConfigCoreOp>> create_shared(ActiveCoreOpHolder &active_core_op_holder,
        const ConfigureNetworkParams &config_params,
        std::shared_ptr<ResourcesManager> resources_manager,
        std::shared_ptr<CacheManager> cache_manager,
        std::shared_ptr<CoreOpMetadata> metadata);

    std::shared_ptr<ResourcesManager> &get_resources_manager()
    {
        return m_resources_manager;
    }

    // Functions to activate and deactivate core ops for scheduler - dont create ActivatedNetworkGroup objects
    virtual hailo_status activate_impl(uint16_t dynamic_batch_size) override;
    // Will first deactivate host resources (via deactivate_host_resources) and then reset the core-op on the fw
    virtual hailo_status deactivate_impl() override;
    virtual hailo_status shutdown() override;

    // Activate all resources related to the core-op on the host.
    hailo_status activate_host_resources();

    // Deactivate all resources related to the core-op on the host, but without resetting the core-op on the fw
    hailo_status deactivate_host_resources();

    hailo_status cancel_pending_transfers();

    hailo_status prepare_transfers(std::unordered_map<std::string, TransferRequest> &transfers);
    hailo_status cancel_prepared_transfers();

    virtual Expected<hailo_stream_interface_t> get_default_streams_interface() override;

    virtual Expected<std::shared_ptr<LatencyMetersMap>> get_latency_meters() override;
    virtual Expected<vdma::BoundaryChannelPtr> get_boundary_vdma_channel_by_stream_name(
        const std::string &stream_name) override;

    virtual bool is_scheduled() const override;
    virtual hailo_status set_scheduler_timeout(const std::chrono::milliseconds &timeout, const std::string &network_name) override;
    virtual hailo_status set_scheduler_threshold(uint32_t threshold, const std::string &network_name) override;
    virtual hailo_status set_scheduler_priority(uint8_t priority, const std::string &network_name) override;
    virtual Expected<HwInferResults> run_hw_infer_estimator() override;
    virtual Expected<Buffer> get_intermediate_buffer(const IntermediateBufferKey &) override;
    virtual bool has_caches() const override;
    virtual Expected<uint32_t> get_cache_length() const override;
    virtual Expected<uint32_t> get_cache_read_length() const override;
    virtual Expected<uint32_t> get_cache_write_length() const override;
    virtual Expected<uint32_t> get_cache_entry_size(uint32_t cache_id) const override;
    virtual hailo_status init_cache(uint32_t read_offset) override;
    virtual hailo_status update_cache_offset(int32_t offset_delta_entries) override;
    virtual Expected<std::vector<uint32_t>> get_cache_ids() const override;
    virtual Expected<Buffer> read_cache_buffer(uint32_t cache_id) override;
    virtual hailo_status write_cache_buffer(uint32_t cache_id, MemoryView buffer) override;

    virtual ~VdmaConfigCoreOp();
    VdmaConfigCoreOp(const VdmaConfigCoreOp &other) = delete;
    VdmaConfigCoreOp &operator=(const VdmaConfigCoreOp &other) = delete;
    VdmaConfigCoreOp &operator=(VdmaConfigCoreOp &&other) = delete;
    VdmaConfigCoreOp(VdmaConfigCoreOp &&other) noexcept = delete;
    VdmaConfigCoreOp(ActiveCoreOpHolder &active_core_op_holder,
        const ConfigureNetworkParams &config_params,
        std::shared_ptr<ResourcesManager> &&resources_manager,
        std::shared_ptr<CacheManager> cache_manager,
        std::shared_ptr<CoreOpMetadata> metadata, hailo_status &status);

private:
    Expected<uint32_t> get_cache_length_impl(std::function<size_t(const CacheBuffer&)> length_getter,
        const std::string &length_type) const;
    hailo_status shutdown_impl();

    std::shared_ptr<ResourcesManager> m_resources_manager;
    std::shared_ptr<CacheManager> m_cache_manager;
    std::atomic_bool m_is_shutdown{false};
};

} /* namespace hailort */

#endif /* _HAILO_CONTEXT_SWITCH_VDMA_CONFIG_CORE_OP_HPP_ */
