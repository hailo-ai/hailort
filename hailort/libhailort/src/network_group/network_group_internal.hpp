/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file network_group_internal.hpp
 * @brief TODO: HRT-9547 - Change doc after moving NGs to global context + add explanation on lagacy names
 *        Class declaration for ConfiguredNetworkGroupBase and ActivatedCoreOp that implement the basic ConfiguredNetworkGroup
 *        and ActivatedNetworkGroup interfaces. All internal classes that are relevant should inherit from the
 *        ConfiguredNetworkGroupBase and ActivatedCoreOp classes.
 *        Hence, the hierarchy is as follows:
 *        --------------------------------------------------------------------------------------------------------------
 *        |                                         ConfiguredNetworkGroup                                             |  (External "interface")
 *        |                   ________________________________|___________________________                             |
 *        |                  /                                                            \                            |
 *        |                 ConfiguredNetworkGroupBase                         ConfiguredNetworkGroupClient            |  (Base classes)
 *        |                            |                                                                               |
 *        |                            |                                                                               |
 *        |                   vector of CoreOps                                                                        |  (Actual implementations)
 *        -------------------------------------------------------------------------------------------------------------|
 *        |                         ActivatedNetworkGroup                                                              |  (External "interface")
 *        |                                   |                                                                        |
 *        |                             ActivatedCoreOp                                                                |  (Base classes)
 *        |                 __________________|_____________________________________________________                   |
 *        |                /                                         |                               \                 |
 *        |    VdmaConfigActivatedCoreOp                 VDeviceActivatedCoreOp             HcpConfigActivatedCoreOp   |  (Actual implementations)
 *        |                                                          |                                                 |
 *        |                                        vector of VdmaConfigActivatedCoreOp                                 |
 *        --------------------------------------------------------------------------------------------------------------
 **/

#ifndef _HAILO_NETWORK_GROUP_INTERNAL_HPP_
#define _HAILO_NETWORK_GROUP_INTERNAL_HPP_

#include "hailo/hailort.h"
#include "hailo/network_group.hpp"

#include "common/latency_meter.hpp"

#include "hef/hef_internal.hpp"
#include "vdma/channel/boundary_channel.hpp"
#include "core_op/active_core_op_holder.hpp"
#include "core_op/core_op.hpp"

#include "control_protocol.h"

#ifdef HAILO_SUPPORT_MULTI_PROCESS
#include "service/hailort_rpc_client.hpp"
#endif // HAILO_SUPPORT_MULTI_PROCESS


namespace hailort
{

class ConfiguredNetworkGroupBase : public ConfiguredNetworkGroup
{
public:
    static Expected<std::shared_ptr<ConfiguredNetworkGroupBase>> create(const ConfigureNetworkParams &config_params,
        std::vector<std::shared_ptr<CoreOp>> &&core_ops, std::vector<std::shared_ptr<NetFlowElement>> &&net_flow_ops)
    {
        auto net_group_ptr = std::shared_ptr<ConfiguredNetworkGroupBase>(new (std::nothrow) 
            ConfiguredNetworkGroupBase(config_params, std::move(core_ops), std::move(net_flow_ops)));
        // auto net_group_ptr = make_shared_nothrow<ConfiguredNetworkGroupBase>(config_params, std::move(core_ops), std::move(net_flow_ops));
        CHECK_NOT_NULL_AS_EXPECTED(net_group_ptr, HAILO_OUT_OF_HOST_MEMORY);

        return net_group_ptr;
    }

    virtual ~ConfiguredNetworkGroupBase() = default;
    ConfiguredNetworkGroupBase(const ConfiguredNetworkGroupBase &other) = delete;
    ConfiguredNetworkGroupBase &operator=(const ConfiguredNetworkGroupBase &other) = delete;
    ConfiguredNetworkGroupBase &operator=(ConfiguredNetworkGroupBase &&other) = delete;
    ConfiguredNetworkGroupBase(ConfiguredNetworkGroupBase &&other) = default;

    Expected<std::unique_ptr<ActivatedNetworkGroup>> activate_with_batch(
        uint16_t dynamic_batch_size = CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE,
        bool resume_pending_stream_transfers = false);
    virtual Expected<std::unique_ptr<ActivatedNetworkGroup>> activate(
        const hailo_activate_network_group_params_t &network_group_params) override;
    virtual hailo_status wait_for_activation(const std::chrono::milliseconds &timeout) override;

    virtual const std::string &get_network_group_name() const override;
    virtual const std::string &name() const override;

    virtual Expected<InputStreamRefVector> get_input_streams_by_network(const std::string &network_name="") override;
    virtual Expected<OutputStreamRefVector> get_output_streams_by_network(const std::string &network_name="") override;
    virtual InputStreamRefVector get_input_streams() override;
    virtual OutputStreamRefVector get_output_streams() override;
    virtual std::vector<std::reference_wrapper<InputStream>> get_input_streams_by_interface(hailo_stream_interface_t stream_interface) override;
    virtual std::vector<std::reference_wrapper<OutputStream>> get_output_streams_by_interface(hailo_stream_interface_t stream_interface) override;
    virtual ExpectedRef<InputStream> get_input_stream_by_name(const std::string& name) override;
    virtual ExpectedRef<OutputStream> get_output_stream_by_name(const std::string& name) override;
    virtual Expected<OutputStreamWithParamsVector> get_output_streams_from_vstream_names(
        const std::map<std::string, hailo_vstream_params_t> &outputs_params) override;
    virtual Expected<LatencyMeasurementResult> get_latency_measurement(const std::string &network_name="") override;

    virtual Expected<std::map<std::string, hailo_vstream_params_t>> make_input_vstream_params(
        bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
        const std::string &network_name="") override;
    virtual Expected<std::map<std::string, hailo_vstream_params_t>> make_output_vstream_params(
        bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
        const std::string &network_name="") override;
        
    virtual Expected<std::vector<std::map<std::string, hailo_vstream_params_t>>> make_output_vstream_params_groups(
        bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size) override;

    virtual Expected<std::vector<std::vector<std::string>>> get_output_vstream_groups() override;

    virtual Expected<std::vector<hailo_network_info_t>> get_network_infos() const override;
    virtual Expected<std::vector<hailo_stream_info_t>> get_all_stream_infos(const std::string &network_name="") const override;
    virtual Expected<std::vector<hailo_vstream_info_t>> get_input_vstream_infos(const std::string &network_name="") const override;
    virtual Expected<std::vector<hailo_vstream_info_t>> get_output_vstream_infos(const std::string &network_name="") const override;
    virtual Expected<std::vector<hailo_vstream_info_t>> get_all_vstream_infos(const std::string &network_name="") const override;
    virtual AccumulatorPtr get_activation_time_accumulator() const override;
    virtual AccumulatorPtr get_deactivation_time_accumulator() const override;
    hailo_status create_streams_from_config_params(Device &device);

    virtual bool is_multi_context() const override;
    virtual const ConfigureNetworkParams get_config_params() const override;

    // TODO: HRT-9551 - Change to get_core_op_by_name() when multiple core_ops supported
    std::shared_ptr<CoreOp> get_core_op() const; 
    // TODO: HRT-9546 Remove
    const std::shared_ptr<CoreOpMetadata> get_core_op_metadata() const;

    Expected<std::vector<std::string>> get_vstream_names_from_stream_name(const std::string &stream_name);
    const SupportedFeatures &get_supported_features();
    
    Expected<uint16_t> get_stream_batch_size(const std::string &stream_name);

    virtual Expected<std::vector<InputVStream>> create_input_vstreams(const std::map<std::string, hailo_vstream_params_t> &inputs_params) override;
    virtual Expected<std::vector<OutputVStream>> create_output_vstreams(const std::map<std::string, hailo_vstream_params_t> &outputs_params) override;

    Expected<std::shared_ptr<InputStream>> get_shared_input_stream_by_name(const std::string &stream_name)
    {
        return get_core_op()->get_shared_input_stream_by_name(stream_name);
    }
    
    Expected<std::shared_ptr<OutputStream>> get_shared_output_stream_by_name(const std::string &stream_name) 
    {
        return get_core_op()->get_shared_output_stream_by_name(stream_name);
    }

    EventPtr get_core_op_activated_event()
    {
        return get_core_op()->m_core_op_activated_event;
    }
    
    hailo_status activate_impl(uint16_t dynamic_batch_size, bool resume_pending_stream_transfers = false)
    {
        return get_core_op()->activate_impl(dynamic_batch_size, resume_pending_stream_transfers);
    }

    hailo_status deactivate_impl(bool keep_nn_config_during_reset)
    {
        return get_core_op()->deactivate_impl(keep_nn_config_during_reset);
    }

    Expected<std::unique_ptr<ActivatedNetworkGroup>> create_activated_network_group(
        const hailo_activate_network_group_params_t &network_group_params, uint16_t dynamic_batch_size, bool resume_pending_stream_transfers)
    {
        return get_core_op()->create_activated_network_group(network_group_params, dynamic_batch_size, resume_pending_stream_transfers);
    }

    Expected<std::shared_ptr<LatencyMetersMap>> get_latency_meters()
    {
        return get_core_op()->get_latency_meters();
    }

    Expected<vdma::BoundaryChannelPtr> get_boundary_vdma_channel_by_stream_name(const std::string &stream_name)
    {
        return get_core_op()->get_boundary_vdma_channel_by_stream_name(stream_name);
    }

    virtual bool is_scheduled() const override
    {
        return get_core_op()->is_scheduled();
    }

    virtual hailo_status set_scheduler_timeout(const std::chrono::milliseconds &timeout, const std::string &network_name) override
    {
        return get_core_op()->set_scheduler_timeout(timeout, network_name);
    }

    virtual hailo_status set_scheduler_threshold(uint32_t threshold, const std::string &network_name) override
    {
        return get_core_op()->set_scheduler_threshold(threshold, network_name);
    }

    virtual Expected<hailo_stream_interface_t> get_default_streams_interface() override
    {
        return get_core_op()->get_default_streams_interface();
    }

    virtual hailo_status set_scheduler_priority(uint8_t priority, const std::string &network_name) override
    {
        return get_core_op()->set_scheduler_priority(priority, network_name);
    }

    std::vector<std::shared_ptr<CoreOp>> &get_core_ops()
    {
        return m_core_ops;
    }

private:
    ConfiguredNetworkGroupBase(const ConfigureNetworkParams &config_params,
        std::vector<std::shared_ptr<CoreOp>> &&core_ops, std::vector<std::shared_ptr<NetFlowElement>> &&net_flow_ops);

    static uint16_t get_smallest_configured_batch_size(const ConfigureNetworkParams &config_params);
    hailo_status create_vdma_input_stream(Device &device, const std::string &stream_name,
        const LayerInfo &layer_info, const hailo_stream_parameters_t &stream_params);
    hailo_status create_vdma_output_stream(Device &device, const std::string &stream_name,
        const LayerInfo &layer_info, const hailo_stream_parameters_t &stream_params);
    hailo_status create_output_stream_from_config_params(Device &device,
        const hailo_stream_parameters_t &stream_params, const std::string &stream_name);
    hailo_status create_input_stream_from_config_params(Device &device,
        const hailo_stream_parameters_t &stream_params, const std::string &stream_name);
    hailo_status add_mux_streams_by_edges_names(OutputStreamWithParamsVector &result,
        const std::unordered_map<std::string, hailo_vstream_params_t> &outputs_edges_params);
    Expected<OutputStreamPtrVector> get_output_streams_by_vstream_name(const std::string &name);
    Expected<LayerInfo> get_layer_info(const std::string &stream_name);

    hailo_status activate_low_level_streams(uint16_t dynamic_batch_size, bool resume_pending_stream_transfers);
    hailo_status deactivate_low_level_streams();

    const ConfigureNetworkParams m_config_params;
    std::vector<std::shared_ptr<CoreOp>> m_core_ops;
    std::vector<std::shared_ptr<NetFlowElement>> m_net_flow_ops;

    friend class VDeviceCoreOp;
    friend class VDeviceActivatedCoreOp;
};

// Move client ng to different header
#ifdef HAILO_SUPPORT_MULTI_PROCESS
class ConfiguredNetworkGroupClient : public ConfiguredNetworkGroup
{
public:
    ConfiguredNetworkGroupClient(std::unique_ptr<HailoRtRpcClient> client, uint32_t handle);

    virtual ~ConfiguredNetworkGroupClient();
    ConfiguredNetworkGroupClient(const ConfiguredNetworkGroupClient &other) = delete;
    ConfiguredNetworkGroupClient &operator=(const ConfiguredNetworkGroupClient &other) = delete;
    ConfiguredNetworkGroupClient &operator=(ConfiguredNetworkGroupClient &&other) = delete;
    ConfiguredNetworkGroupClient(ConfiguredNetworkGroupClient &&other) = default;

    virtual const std::string &get_network_group_name() const override;
    virtual const std::string &name() const override;
    virtual Expected<hailo_stream_interface_t> get_default_streams_interface() override;
    virtual std::vector<std::reference_wrapper<InputStream>> get_input_streams_by_interface(hailo_stream_interface_t stream_interface) override;
    virtual std::vector<std::reference_wrapper<OutputStream>> get_output_streams_by_interface(hailo_stream_interface_t stream_interface) override;
    virtual ExpectedRef<InputStream> get_input_stream_by_name(const std::string &name) override;
    virtual ExpectedRef<OutputStream> get_output_stream_by_name(const std::string &name) override;
    virtual Expected<InputStreamRefVector> get_input_streams_by_network(const std::string &network_name="") override;
    virtual Expected<OutputStreamRefVector> get_output_streams_by_network(const std::string &network_name="") override;
    virtual InputStreamRefVector get_input_streams() override;
    virtual OutputStreamRefVector get_output_streams() override;
    virtual Expected<OutputStreamWithParamsVector> get_output_streams_from_vstream_names(
        const std::map<std::string, hailo_vstream_params_t> &outputs_params) override;

    virtual Expected<LatencyMeasurementResult> get_latency_measurement(const std::string &network_name="") override;
    virtual Expected<std::unique_ptr<ActivatedNetworkGroup>> activate(const hailo_activate_network_group_params_t &network_group_params) override;
    virtual hailo_status wait_for_activation(const std::chrono::milliseconds &timeout) override;

    virtual Expected<std::map<std::string, hailo_vstream_params_t>> make_input_vstream_params(
        bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
        const std::string &network_name="") override;
    virtual Expected<std::map<std::string, hailo_vstream_params_t>> make_output_vstream_params(
        bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
        const std::string &network_name="") override;
    virtual Expected<std::vector<std::map<std::string, hailo_vstream_params_t>>> make_output_vstream_params_groups(
        bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size) override;
    virtual Expected<std::vector<std::vector<std::string>>> get_output_vstream_groups() override;

    virtual Expected<std::vector<hailo_stream_info_t>> get_all_stream_infos(const std::string &network_name="") const override;
    virtual Expected<std::vector<hailo_network_info_t>> get_network_infos() const override;
    virtual Expected<std::vector<hailo_vstream_info_t>> get_input_vstream_infos(const std::string &network_name="") const override;
    virtual Expected<std::vector<hailo_vstream_info_t>> get_output_vstream_infos(const std::string &network_name="") const override;
    virtual Expected<std::vector<hailo_vstream_info_t>> get_all_vstream_infos(const std::string &network_name="") const override;

    virtual bool is_scheduled() const override;
    virtual hailo_status set_scheduler_timeout(const std::chrono::milliseconds &timeout, const std::string &network_name) override;
    virtual hailo_status set_scheduler_threshold(uint32_t threshold, const std::string &network_name) override;
    virtual hailo_status set_scheduler_priority(uint8_t priority, const std::string &network_name) override;

    virtual AccumulatorPtr get_activation_time_accumulator() const override;
    virtual AccumulatorPtr get_deactivation_time_accumulator() const override;

    virtual bool is_multi_context() const override;
    virtual const ConfigureNetworkParams get_config_params() const override;

    virtual Expected<std::vector<InputVStream>> create_input_vstreams(const std::map<std::string, hailo_vstream_params_t> &inputs_params);
    virtual Expected<std::vector<OutputVStream>> create_output_vstreams(const std::map<std::string, hailo_vstream_params_t> &outputs_params);

    virtual hailo_status before_fork() override;
    virtual hailo_status after_fork_in_parent() override;
    virtual hailo_status after_fork_in_child() override;

private:
    hailo_status create_client();

    std::unique_ptr<HailoRtRpcClient> m_client;
    uint32_t m_handle;
    std::string m_network_group_name;
};
#endif // HAILO_SUPPORT_MULTI_PROCESS

} /* namespace hailort */

#endif /* _HAILO_NETWORK_GROUP_INTERNAL_HPP_ */
