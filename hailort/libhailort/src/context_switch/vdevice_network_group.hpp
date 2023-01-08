/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdevice_network_group.hpp
 * @brief Class declaration for VDeviceNetworkGroup, a wrapper around ConfiguredNetworkGroupBase which is used
 *        to support multiple ConfiguredNetworkGroup objects that encapsulate the same actual configured network group.
 **/

#ifndef _HAILO_VDEVICE_NETWORK_GROUP_WRAPPER_HPP_
#define _HAILO_VDEVICE_NETWORK_GROUP_WRAPPER_HPP_

#include "hailo/hailort.h"
#include "common/utils.hpp"
#include "hailo/network_group.hpp"
#include "hailo/vstream.hpp"

#include "context_switch/multi_context/vdma_config_network_group.hpp"
#include "network_group_internal.hpp"
#include "network_group_scheduler.hpp"
#include "pipeline_multiplexer.hpp"

#include <cstdint>

namespace hailort
{

class VDeviceActivatedNetworkGroup : public ActivatedNetworkGroupBase
{
public:
    static Expected<std::unique_ptr<ActivatedNetworkGroup>> create(std::vector<std::shared_ptr<VdmaConfigNetworkGroup>> configured_network_groups,
        std::map<std::string, std::shared_ptr<InputStream>> &input_streams,
        std::map<std::string, std::shared_ptr<OutputStream>> &output_streams,
        const hailo_activate_network_group_params_t &network_group_params, EventPtr network_group_activated_event,
        uint16_t dynamic_batch_size, AccumulatorPtr deactivation_time_accumulator);

    virtual ~VDeviceActivatedNetworkGroup()
    {
        if (!m_should_reset_network_group) {
            return;
        }
        const auto start_time = std::chrono::steady_clock::now();

        m_network_group_activated_event->reset();
        m_activated_network_groups.clear();

        const auto elapsed_time_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start_time).count();
        LOGGER__INFO("Deactivating took {} ms", elapsed_time_ms);
        m_deactivation_time_accumulator->add_data_point(elapsed_time_ms);
    }

    VDeviceActivatedNetworkGroup(const VDeviceActivatedNetworkGroup &other) = delete;
    VDeviceActivatedNetworkGroup &operator=(const VDeviceActivatedNetworkGroup &other) = delete;
    VDeviceActivatedNetworkGroup &operator=(VDeviceActivatedNetworkGroup &&other) = delete;
    VDeviceActivatedNetworkGroup(VDeviceActivatedNetworkGroup &&other) noexcept;

    virtual const std::string &get_network_group_name() const override;
    virtual Expected<Buffer> get_intermediate_buffer(const IntermediateBufferKey &key) override;
    virtual hailo_status set_keep_nn_config_during_reset(const bool keep_nn_config_during_reset) override;

private:
    VDeviceActivatedNetworkGroup(
        std::vector<std::unique_ptr<ActivatedNetworkGroup>> &&activated_network_groups,
        std::map<std::string, std::shared_ptr<InputStream>> &input_streams,
        std::map<std::string, std::shared_ptr<OutputStream>> &output_streams,
        const hailo_activate_network_group_params_t &network_group_params, EventPtr network_group_activated_event,
        AccumulatorPtr deactivation_time_accumulator, hailo_status &status);

    std::vector<std::unique_ptr<ActivatedNetworkGroup>> m_activated_network_groups;
    bool m_should_reset_network_group;
    AccumulatorPtr m_deactivation_time_accumulator;
};

class VDeviceNetworkGroup : public ConfiguredNetworkGroupBase
{
public:
        // TODO (HRT-8751): remove duplicate members from this class or from vdma_config_network _group
    static Expected<std::shared_ptr<VDeviceNetworkGroup>> create(std::vector<std::shared_ptr<ConfiguredNetworkGroup>> configured_network_group,
        NetworkGroupSchedulerWeakPtr network_group_scheduler);

    static Expected<std::shared_ptr<VDeviceNetworkGroup>> duplicate(std::shared_ptr<VDeviceNetworkGroup> other);

    virtual ~VDeviceNetworkGroup() = default;
    VDeviceNetworkGroup(const VDeviceNetworkGroup &other) = delete;
    VDeviceNetworkGroup &operator=(const VDeviceNetworkGroup &other) = delete;
    VDeviceNetworkGroup &operator=(VDeviceNetworkGroup &&other) = delete;
    VDeviceNetworkGroup(VDeviceNetworkGroup &&other) = default;

    // Function from vdma network group
    hailo_status create_vdevice_streams_from_config_params(std::shared_ptr<PipelineMultiplexer> multiplexer,
        scheduler_ng_handle_t scheduler_handle);
    hailo_status create_input_vdevice_stream_from_config_params(
        const hailo_stream_parameters_t &stream_params, const std::string &stream_name,
        std::shared_ptr<PipelineMultiplexer> multiplexer, scheduler_ng_handle_t scheduler_handle);
    hailo_status create_output_vdevice_stream_from_config_params(
        const hailo_stream_parameters_t &stream_params, const std::string &stream_name,
        std::shared_ptr<PipelineMultiplexer> multiplexer, scheduler_ng_handle_t scheduler_handle);

    hailo_status create_vdevice_streams_from_duplicate(std::shared_ptr<VDeviceNetworkGroup> other);

    bool equals(const Hef &hef, const std::string &network_group_name)
    {
        return m_configured_network_groups[0]->equals(hef, network_group_name);
    }

    uint32_t multiplexer_duplicates_count()
    {
        assert(m_multiplexer->instances_count() > 0);
        return static_cast<uint32_t>(m_multiplexer->instances_count() - 1);
    }

    virtual Expected<hailo_stream_interface_t> get_default_streams_interface() override;

    virtual Expected<std::shared_ptr<LatencyMetersMap>> get_latency_meters() override;
    virtual Expected<std::shared_ptr<VdmaChannel>> get_boundary_vdma_channel_by_stream_name(
        const std::string &stream_name) override;

    void set_network_group_handle(scheduler_ng_handle_t handle);
    scheduler_ng_handle_t network_group_handle() const;
    virtual hailo_status set_scheduler_timeout(const std::chrono::milliseconds &timeout, const std::string &network_name) override;
    virtual hailo_status set_scheduler_threshold(uint32_t threshold, const std::string &network_name) override;

    virtual Expected<std::vector<OutputVStream>> create_output_vstreams(const std::map<std::string, hailo_vstream_params_t> &outputs_params) override;

    virtual hailo_status wait_for_activation(const std::chrono::milliseconds &timeout) override
    {
        CHECK(!m_network_group_scheduler.lock(), HAILO_INVALID_OPERATION,
            "Waiting for network group activation is not allowed when the network group scheduler is active!");

        return m_network_group_activated_event->wait(timeout);
    }

    virtual hailo_status activate_impl(uint16_t /*dynamic_batch_size*/) override
    {
        return HAILO_INTERNAL_FAILURE;
    }

    virtual hailo_status deactivate_impl() override
    {
        return HAILO_INTERNAL_FAILURE;
    }

    virtual Expected<std::unique_ptr<ActivatedNetworkGroup>> create_activated_network_group(
        const hailo_activate_network_group_params_t &network_group_params, uint16_t dynamic_batch_size) override
    {
        auto start_time = std::chrono::steady_clock::now();

        CHECK_AS_EXPECTED(!m_network_group_scheduler.lock(), HAILO_INVALID_OPERATION,
            "Manually activating a network group is not allowed when the network group scheduler is active!");

        auto res = VDeviceActivatedNetworkGroup::create(m_configured_network_groups, m_input_streams, m_output_streams,
            network_group_params, m_network_group_activated_event, dynamic_batch_size, m_deactivation_time_accumulator);
        const auto elapsed_time_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start_time).count();
        CHECK_EXPECTED(res);

        LOGGER__INFO("Activating {} on VDevice took {} milliseconds. Note that the function is asynchronous and"
            " thus the network is not fully activated yet.", name(), elapsed_time_ms);
        m_activation_time_accumulator->add_data_point(elapsed_time_ms);

        return res;
    }

    Expected<std::shared_ptr<VdmaConfigNetworkGroup>> get_network_group_by_device_index(uint32_t device_index);

private:
    VDeviceNetworkGroup(std::vector<std::shared_ptr<VdmaConfigNetworkGroup>> configured_network_group,
        NetworkGroupSchedulerWeakPtr network_group_scheduler, std::vector<std::shared_ptr<NetFlowElement>> &&net_flow_ops,
        hailo_status &status);

    std::vector<std::shared_ptr<VdmaConfigNetworkGroup>> m_configured_network_groups;
    NetworkGroupSchedulerWeakPtr m_network_group_scheduler;
    scheduler_ng_handle_t m_scheduler_handle;
    multiplexer_ng_handle_t m_multiplexer_handle;
    std::shared_ptr<PipelineMultiplexer> m_multiplexer;
};

}

#endif /* _HAILO_VDEVICE_NETWORK_GROUP_WRAPPER_HPP_ */