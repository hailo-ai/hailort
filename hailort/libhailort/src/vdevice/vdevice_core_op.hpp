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

#include "vdevice/scheduler/network_group_scheduler.hpp"
#include "vdevice/pipeline_multiplexer.hpp"

#include <cstdint>


namespace hailort
{

class VDeviceActivatedCoreOp : public ActivatedCoreOp
{
public:
    static Expected<std::unique_ptr<ActivatedNetworkGroup>> create(std::vector<std::shared_ptr<CoreOp>> &core_ops,
        std::map<std::string, std::shared_ptr<InputStream>> &input_streams,
        std::map<std::string, std::shared_ptr<OutputStream>> &output_streams,
        const hailo_activate_network_group_params_t &network_group_params, EventPtr core_op_activated_event,
        uint16_t dynamic_batch_size, AccumulatorPtr deactivation_time_accumulator,
        bool resume_pending_stream_transfers);

    virtual ~VDeviceActivatedCoreOp()
    {
        if (!m_should_reset_core_op) {
            return;
        }
        const auto start_time = std::chrono::steady_clock::now();

        m_core_op_activated_event->reset();
        m_activated_network_groups.clear();

        const auto elapsed_time_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start_time).count();
        LOGGER__INFO("Deactivating took {} ms", elapsed_time_ms);
        m_deactivation_time_accumulator->add_data_point(elapsed_time_ms);
    }

    VDeviceActivatedCoreOp(const VDeviceActivatedCoreOp &other) = delete;
    VDeviceActivatedCoreOp &operator=(const VDeviceActivatedCoreOp &other) = delete;
    VDeviceActivatedCoreOp &operator=(VDeviceActivatedCoreOp &&other) = delete;
    VDeviceActivatedCoreOp(VDeviceActivatedCoreOp &&other) noexcept;

    virtual const std::string &get_network_group_name() const override;
    virtual Expected<Buffer> get_intermediate_buffer(const IntermediateBufferKey &key) override;
    virtual hailo_status set_keep_nn_config_during_reset(const bool keep_nn_config_during_reset) override;

private:
    VDeviceActivatedCoreOp(
        std::vector<std::unique_ptr<ActivatedNetworkGroup>> &&activated_network_groups,
        std::map<std::string, std::shared_ptr<InputStream>> &input_streams,
        std::map<std::string, std::shared_ptr<OutputStream>> &output_streams,
        const hailo_activate_network_group_params_t &network_group_params, EventPtr core_op_activated_event,
        AccumulatorPtr deactivation_time_accumulator, hailo_status &status);

    std::vector<std::unique_ptr<ActivatedNetworkGroup>> m_activated_network_groups;
    bool m_should_reset_core_op;
    AccumulatorPtr m_deactivation_time_accumulator;
};

class VDeviceCoreOp : public CoreOp
{
public:
    static Expected<std::shared_ptr<VDeviceCoreOp>> create(std::vector<std::shared_ptr<CoreOp>> core_ops,
        CoreOpsSchedulerWeakPtr core_ops_scheduler, const std::string &hef_hash);

    static Expected<std::shared_ptr<VDeviceCoreOp>> duplicate(std::shared_ptr<VDeviceCoreOp> other);

    virtual ~VDeviceCoreOp() = default;
    VDeviceCoreOp(const VDeviceCoreOp &other) = delete;
    VDeviceCoreOp &operator=(const VDeviceCoreOp &other) = delete;
    VDeviceCoreOp &operator=(VDeviceCoreOp &&other) = delete;
    VDeviceCoreOp(VDeviceCoreOp &&other) = default;

    hailo_status create_vdevice_streams_from_config_params(std::shared_ptr<PipelineMultiplexer> multiplexer,
        scheduler_core_op_handle_t scheduler_handle);
    hailo_status create_input_vdevice_stream_from_config_params(
        const hailo_stream_parameters_t &stream_params, const std::string &stream_name,
        std::shared_ptr<PipelineMultiplexer> multiplexer, scheduler_core_op_handle_t scheduler_handle);
    hailo_status create_output_vdevice_stream_from_config_params(
        const hailo_stream_parameters_t &stream_params, const std::string &stream_name,
        std::shared_ptr<PipelineMultiplexer> multiplexer, scheduler_core_op_handle_t scheduler_handle);

    hailo_status create_vdevice_streams_from_duplicate(std::shared_ptr<VDeviceCoreOp> other);

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

    uint32_t multiplexer_duplicates_count()
    {
        assert(m_multiplexer->instances_count() > 0);
        return static_cast<uint32_t>(m_multiplexer->instances_count() - 1);
    }

    virtual Expected<hailo_stream_interface_t> get_default_streams_interface() override;

    virtual Expected<std::shared_ptr<LatencyMetersMap>> get_latency_meters() override;
    virtual Expected<vdma::BoundaryChannelPtr> get_boundary_vdma_channel_by_stream_name(
        const std::string &stream_name) override;

    void set_core_op_handle(scheduler_core_op_handle_t handle);
    scheduler_core_op_handle_t core_op_handle() const;
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

    virtual hailo_status activate_impl(uint16_t /*dynamic_batch_size*/, bool /* resume_pending_stream_transfers */) override
    {
        return HAILO_INTERNAL_FAILURE;
    }

    virtual hailo_status deactivate_impl(bool /* keep_nn_config_during_reset */) override
    {
        return HAILO_INTERNAL_FAILURE;
    }

    virtual Expected<std::unique_ptr<ActivatedNetworkGroup>> create_activated_network_group(
        const hailo_activate_network_group_params_t &network_group_params, uint16_t dynamic_batch_size,
        bool resume_pending_stream_transfers) override;

    Expected<std::shared_ptr<VdmaConfigCoreOp>> get_core_op_by_device_index(uint32_t device_index);

private:
    VDeviceCoreOp(std::vector<std::shared_ptr<CoreOp>> core_ops, CoreOpsSchedulerWeakPtr core_ops_scheduler,
        const std::string &hef_hash, hailo_status &status);

    std::vector<std::shared_ptr<CoreOp>> m_core_ops;
    CoreOpsSchedulerWeakPtr m_core_ops_scheduler;
    scheduler_core_op_handle_t m_scheduler_handle;
    multiplexer_core_op_handle_t m_multiplexer_handle;
    std::shared_ptr<PipelineMultiplexer> m_multiplexer;
    std::string m_hef_hash;
};

}

#endif /* _HAILO_VDEVICE_CORE_OP_HPP_ */