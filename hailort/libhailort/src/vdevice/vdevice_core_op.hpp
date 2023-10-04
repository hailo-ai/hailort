/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
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

#include "vdevice/scheduler/scheduler.hpp"
#include "vdevice/pipeline_multiplexer.hpp"
#include "utils/profiler/tracer_macros.hpp"

#include <cstdint>


namespace hailort
{


class VDeviceCoreOp : public CoreOp
{
public:
    static Expected<std::shared_ptr<VDeviceCoreOp>> create(
        ActiveCoreOpHolder &active_core_op_holder,
        const ConfigureNetworkParams &configure_params,
        const std::map<device_id_t, std::shared_ptr<CoreOp>> &core_ops,
        CoreOpsSchedulerWeakPtr core_ops_scheduler, vdevice_core_op_handle_t core_op_handle,
        std::shared_ptr<PipelineMultiplexer> multiplexer, const std::string &hef_hash);

    static Expected<std::shared_ptr<VDeviceCoreOp>> duplicate(std::shared_ptr<VDeviceCoreOp> other,
        const ConfigureNetworkParams &configure_params);

    virtual ~VDeviceCoreOp() = default;
    VDeviceCoreOp(const VDeviceCoreOp &other) = delete;
    VDeviceCoreOp &operator=(const VDeviceCoreOp &other) = delete;
    VDeviceCoreOp &operator=(VDeviceCoreOp &&other) = delete;
    VDeviceCoreOp(VDeviceCoreOp &&other) = default;

    bool equals(const Hef &hef, const std::pair<const std::string, ConfigureNetworkParams> &params_pair)
    {
        if ((params_pair.first == name()) && (hef.hash() == m_hef_hash)) {
            if ((params_pair.second.batch_size == m_config_params.batch_size) &&
                (params_pair.second.power_mode == m_config_params.power_mode)) {
                    return true;
            }
            LOGGER__INFO("The network group: {} was already configured to the device with different params."
                " To use the Stream Multiplexer configure the network with the same params.", name());
        }

        return false;
    }

    uint32_t multiplexer_duplicates_count() const
    {
        if (m_multiplexer) {
            assert(m_multiplexer->instances_count() > 0);
            return static_cast<uint32_t>(m_multiplexer->instances_count() - 1);
        } else {
            return 0;
        }
    }

    bool multiplexer_supported() const
    {
        return nullptr != m_multiplexer;
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

    void set_vstreams_multiplexer_callbacks(std::vector<OutputVStream> &output_vstreams) override;

    virtual hailo_status wait_for_activation(const std::chrono::milliseconds &timeout) override
    {
        CHECK(!m_core_ops_scheduler.lock(), HAILO_INVALID_OPERATION,
            "Waiting for core-op activation is not allowed when the core-ops scheduler is active!");

        return m_core_op_activated_event->wait(timeout);
    }

    virtual hailo_status activate_impl(uint16_t dynamic_batch_size) override;
    virtual hailo_status deactivate_impl() override;

    Expected<std::shared_ptr<VdmaConfigCoreOp>> get_core_op_by_device_id(const device_id_t &device_bdf_id);

    virtual Expected<HwInferResults> run_hw_infer_estimator() override;
    virtual Expected<Buffer> get_intermediate_buffer(const IntermediateBufferKey &) override;

private:
    VDeviceCoreOp(ActiveCoreOpHolder &active_core_op_holder,
        const ConfigureNetworkParams &configure_params,
        const std::map<device_id_t, std::shared_ptr<CoreOp>> &core_ops,
        CoreOpsSchedulerWeakPtr core_ops_scheduler, scheduler_core_op_handle_t core_op_handle,
        std::shared_ptr<PipelineMultiplexer> multiplexer, // TODO: multiplexer handle
        const std::string &hef_hash, hailo_status &status);

    hailo_status create_vdevice_streams_from_config_params();
    hailo_status create_input_vdevice_stream_from_config_params(
        const hailo_stream_parameters_t &stream_params, const std::string &stream_name);
    hailo_status create_output_vdevice_stream_from_config_params(
        const hailo_stream_parameters_t &stream_params, const std::string &stream_name);

    hailo_status create_vdevice_streams_from_duplicate(std::shared_ptr<VDeviceCoreOp> other);

    std::map<device_id_t, std::shared_ptr<CoreOp>> m_core_ops;
    CoreOpsSchedulerWeakPtr m_core_ops_scheduler;
    const vdevice_core_op_handle_t m_core_op_handle;
    std::shared_ptr<PipelineMultiplexer> m_multiplexer;
    multiplexer_core_op_handle_t m_multiplexer_handle;
    std::string m_hef_hash;
};

}

#endif /* _HAILO_VDEVICE_CORE_OP_HPP_ */