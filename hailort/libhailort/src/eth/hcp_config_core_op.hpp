/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hcp_config_core_op.hpp
 * @brief Represent core-op from HEF file that can be activated.
 *
 * DEPRECATED: ethernet/UDP firmware control is no longer supported. The
 * HcpConfigCoreOp class is retained as a header-only stub so that legacy call
 * sites continue to compile; every method now returns HAILO_NOT_SUPPORTED
 * (or HAILO_NOT_SUPPORTED where the original behaviour was the same). It
 * will be removed in a future release.
 *
 * The WriteMemoryInfo struct below is still consumed by hef.cpp (the parser
 * builds a vector of these even when there is no ethernet device to feed it
 * into), so it is retained as-is.
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

// DEPRECATED: ethernet/UDP fw-control is no longer supported; all methods are stubs.
class HcpConfigCoreOp : public CoreOp
{
public:
    HcpConfigCoreOp(
        Device &device, ActiveCoreOpHolder &active_core_op_holder, std::vector<WriteMemoryInfo> &&config,
        const ConfigureNetworkParams &config_params, std::shared_ptr<CoreOpMetadata> metadata, hailo_status &status) :
        CoreOp(config_params, metadata, active_core_op_holder, status)
    {
        (void)device;
        (void)config;
        status = HAILO_NOT_SUPPORTED;
    }

    virtual Expected<hailo_stream_interface_t> get_default_streams_interface() override
    {
        return make_unexpected(HAILO_NOT_SUPPORTED);
    }

    virtual Expected<std::shared_ptr<LatencyMetersMap>> get_latency_meters() override
    {
        return make_unexpected(HAILO_NOT_SUPPORTED);
    }

    virtual Expected<vdma::BoundaryChannelPtr> get_boundary_vdma_channel_by_stream_name(
        const std::string &stream_name) override
    {
        (void)stream_name;
        return make_unexpected(HAILO_NOT_SUPPORTED);
    }

    virtual bool is_scheduled() const override { return false; }

    virtual hailo_status set_scheduler_timeout(const std::chrono::milliseconds &timeout, const std::string &network_name) override
    {
        (void)timeout;
        (void)network_name;
        return HAILO_NOT_SUPPORTED;
    }

    virtual hailo_status set_scheduler_threshold(uint32_t threshold, const std::string &network_name) override
    {
        (void)threshold;
        (void)network_name;
        return HAILO_NOT_SUPPORTED;
    }

    virtual hailo_status set_scheduler_priority(uint8_t priority, const std::string &network_name) override
    {
        (void)priority;
        (void)network_name;
        return HAILO_NOT_SUPPORTED;
    }

    virtual hailo_status activate_impl(uint16_t dynamic_batch_size) override
    {
        (void)dynamic_batch_size;
        return HAILO_NOT_SUPPORTED;
    }

    virtual hailo_status deactivate_impl() override { return HAILO_NOT_SUPPORTED; }
    virtual hailo_status shutdown() override { return HAILO_NOT_SUPPORTED; }

    virtual Expected<HwInferResults> run_hw_infer_estimator() override
    {
        return make_unexpected(HAILO_NOT_SUPPORTED);
    }

    virtual bool has_caches() const override { return false; }

    virtual Expected<uint32_t> get_cache_length() const override
    {
        return make_unexpected(HAILO_NOT_SUPPORTED);
    }

    virtual Expected<uint32_t> get_cache_read_length() const override
    {
        return make_unexpected(HAILO_NOT_SUPPORTED);
    }

    virtual Expected<uint32_t> get_cache_write_length() const override
    {
        return make_unexpected(HAILO_NOT_SUPPORTED);
    }

    virtual Expected<uint32_t> get_cache_entry_size(uint32_t cache_id) const override
    {
        (void)cache_id;
        return make_unexpected(HAILO_NOT_SUPPORTED);
    }

    virtual hailo_status init_cache(uint32_t read_offset) override
    {
        (void)read_offset;
        return HAILO_NOT_SUPPORTED;
    }

    virtual hailo_status update_cache_offset(int32_t offset_delta_entries) override
    {
        (void)offset_delta_entries;
        return HAILO_NOT_SUPPORTED;
    }

    virtual Expected<std::vector<uint32_t>> get_cache_ids() const override
    {
        return make_unexpected(HAILO_NOT_SUPPORTED);
    }

    virtual Expected<Buffer> read_cache_buffer(uint32_t cache_id) override
    {
        (void)cache_id;
        return make_unexpected(HAILO_NOT_SUPPORTED);
    }

    virtual hailo_status write_cache_buffer(uint32_t cache_id, MemoryView buffer) override
    {
        (void)cache_id;
        (void)buffer;
        return HAILO_NOT_SUPPORTED;
    }

    virtual ~HcpConfigCoreOp() = default;
    HcpConfigCoreOp(const HcpConfigCoreOp &other) = delete;
    HcpConfigCoreOp &operator=(const HcpConfigCoreOp &other) = delete;
    HcpConfigCoreOp &operator=(HcpConfigCoreOp &&other) = delete;
    HcpConfigCoreOp(HcpConfigCoreOp &&other) noexcept : CoreOp(std::move(other)) {}

};

} /* namespace hailort */

#endif /* _HAILO_CONTEXT_SWITCH_HCP_CONFIG_CORE_OP_HPP_ */
