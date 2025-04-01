/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdevice_core_op.hpp
 * @brief Class declaration for VDeviceCoreOp, which is used to support multiple CoreOps objects,
 *        that encapsulate the same actual CoreOp.
 **/

#ifndef _HAILO_VDEVICE_CORE_OP_HPP_
#define _HAILO_VDEVICE_CORE_OP_HPP_

#include "hailo/hailort.h"
#include "common/utils.hpp"
#include "hailo/network_group.hpp"
#include "hailo/vstream.hpp"
#include "hailo/vdevice.hpp"

#include "vdevice/scheduler/scheduler.hpp"
#include "vdevice/scheduler/infer_request_accumulator.hpp"
#include "utils/profiler/tracer_macros.hpp"

#include <cstdint>


namespace hailort
{


class VDeviceCoreOp : public CoreOp
{
public:
    static Expected<std::shared_ptr<VDeviceCoreOp>> create(
        VDevice &vdevice,
        ActiveCoreOpHolder &active_core_op_holder,
        const ConfigureNetworkParams &configure_params,
        const std::map<device_id_t, std::shared_ptr<CoreOp>> &core_ops,
        CoreOpsSchedulerWeakPtr core_ops_scheduler, vdevice_core_op_handle_t core_op_handle,
        const std::string &hef_hash);

    static Expected<std::shared_ptr<VDeviceCoreOp>> duplicate(std::shared_ptr<VDeviceCoreOp> other,
        const ConfigureNetworkParams &configure_params);

    virtual ~VDeviceCoreOp();
    VDeviceCoreOp(const VDeviceCoreOp &other) = delete;
    VDeviceCoreOp &operator=(const VDeviceCoreOp &other) = delete;
    VDeviceCoreOp &operator=(VDeviceCoreOp &&other) = delete;

    bool equals(const Hef &hef, const std::pair<const std::string, ConfigureNetworkParams> &params_pair)
    {
        if ((params_pair.first == name()) && (hef.hash() == m_hef_hash)) {
            if ((params_pair.second.batch_size == m_config_params.batch_size) &&
                (params_pair.second.power_mode == m_config_params.power_mode) &&
                (equal_batch(params_pair.second.network_params_by_name, m_config_params.network_params_by_name))) {
                    return true;
            }
            LOGGER__INFO("The network group: {} was already configured to the device with different params."
                " To use the Stream Multiplexer configure the network with the same params.", name());
        }

        return false;
    }

    virtual Expected<hailo_stream_interface_t> get_default_streams_interface() override;

    virtual Expected<std::shared_ptr<LatencyMetersMap>> get_latency_meters() override;
    virtual Expected<vdma::BoundaryChannelPtr> get_boundary_vdma_channel_by_stream_name(
        const std::string &stream_name) override;

    vdevice_core_op_handle_t core_op_handle() const;
    virtual bool is_scheduled() const override;
    virtual hailo_status set_scheduler_timeout(const std::chrono::milliseconds &timeout, const std::string &network_name) override;
    virtual hailo_status set_scheduler_threshold(uint32_t threshold, const std::string &network_name) override;
    virtual hailo_status set_scheduler_priority(uint8_t priority, const std::string &network_name) override;

    virtual hailo_status wait_for_activation(const std::chrono::milliseconds &timeout) override
    {
        CHECK(!m_core_ops_scheduler.lock(), HAILO_INVALID_OPERATION,
            "Waiting for core-op activation is not allowed when the core-ops scheduler is active!");

        return m_core_op_activated_event->wait(timeout);
    }

    virtual hailo_status activate_impl(uint16_t dynamic_batch_size) override;
    virtual hailo_status deactivate_impl() override;
    virtual hailo_status shutdown() override;

    size_t devices_count() const { return m_core_ops.size(); }
    Expected<std::shared_ptr<VdmaConfigCoreOp>> get_core_op_by_device_id(const device_id_t &device_bdf_id);

    Expected<size_t> get_infer_queue_size_per_device() const;

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

    VDeviceCoreOp(VDevice &vdevice,
        ActiveCoreOpHolder &active_core_op_holder,
        const ConfigureNetworkParams &configure_params,
        const std::map<device_id_t, std::shared_ptr<CoreOp>> &core_ops,
        CoreOpsSchedulerWeakPtr core_ops_scheduler, scheduler_core_op_handle_t core_op_handle,
        const std::string &hef_hash,
        size_t max_queue_size,
        hailo_status &status);

private:
    hailo_status shutdown_impl();
    hailo_status create_vdevice_streams_from_config_params();
    hailo_status create_input_vdevice_stream_from_config_params(
        const hailo_stream_parameters_t &stream_params, const std::string &stream_name);
    hailo_status create_output_vdevice_stream_from_config_params(
        const hailo_stream_parameters_t &stream_params, const std::string &stream_name);

    hailo_status create_vdevice_streams_from_duplicate(std::shared_ptr<VDeviceCoreOp> other);

    hailo_status add_to_trace();

    bool equal_batch(const std::map<std::string, hailo_network_parameters_t> &lhs, const std::map<std::string, hailo_network_parameters_t> &rhs);

    VDevice &m_vdevice;
    std::map<device_id_t, std::shared_ptr<CoreOp>> m_core_ops;
    CoreOpsSchedulerWeakPtr m_core_ops_scheduler;
    const vdevice_core_op_handle_t m_core_op_handle;
    std::string m_hef_hash;
    std::atomic_bool m_is_shutdown{false};

    std::shared_ptr<InferRequestAccumulator> m_infer_requests_accumulator;
};

}

#endif /* _HAILO_VDEVICE_CORE_OP_HPP_ */