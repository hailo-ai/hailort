/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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

#include "core_op/core_op.hpp"

#include <vector>
#include <map>


namespace hailort
{

struct WriteMemoryInfo
{
    uint32_t address;
    Buffer data;
};

class HcpConfigCoreOp : public CoreOp
{
public:
    HcpConfigCoreOp(
        Device &device, ActiveCoreOpHolder &active_core_op_holder, std::vector<WriteMemoryInfo> &&config,
        const ConfigureNetworkParams &config_params, std::shared_ptr<CoreOpMetadata> metadata, hailo_status &status);

    virtual Expected<hailo_stream_interface_t> get_default_streams_interface() override;

    virtual Expected<std::shared_ptr<LatencyMetersMap>> get_latency_meters() override;
    virtual Expected<vdma::BoundaryChannelPtr> get_boundary_vdma_channel_by_stream_name(
        const std::string &stream_name) override;
    virtual bool is_scheduled() const override;
    virtual hailo_status set_scheduler_timeout(const std::chrono::milliseconds &timeout, const std::string &network_name) override;
    virtual hailo_status set_scheduler_threshold(uint32_t threshold, const std::string &network_name) override;
    virtual hailo_status set_scheduler_priority(uint8_t priority, const std::string &network_name) override;

    virtual hailo_status activate_impl(uint16_t dynamic_batch_size) override;
    virtual hailo_status deactivate_impl() override;
    virtual hailo_status shutdown() override;
    virtual Expected<HwInferResults> run_hw_infer_estimator() override;
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

    virtual ~HcpConfigCoreOp() = default;
    HcpConfigCoreOp(const HcpConfigCoreOp &other) = delete;
    HcpConfigCoreOp &operator=(const HcpConfigCoreOp &other) = delete;
    HcpConfigCoreOp &operator=(HcpConfigCoreOp &&other) = delete;
    HcpConfigCoreOp(HcpConfigCoreOp &&other) noexcept : CoreOp(std::move(other)),
        m_config(std::move(other.m_config)),
        m_device(other.m_device) {}

private:
    std::vector<WriteMemoryInfo> m_config;
    Device &m_device;
};

} /* namespace hailort */

#endif /* _HAILO_CONTEXT_SWITCH_HCP_CONFIG_CORE_OP_HPP_ */
