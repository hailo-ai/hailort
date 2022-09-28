/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file network_group_internal.hpp
 * @brief Class declaration for ConfiguredNetworkGroupBase and ActivatedNetworkGroupBase that implement the basic ConfiguredNetworkGroup
 *        and ActivatedNetworkGroup interfaces. All internal classes that are relevant should inherit from the
 *        ConfiguredNetworkGroupBase and ActivatedNetworkGroupBase classes.
 *        Hence, the hierarchy is as follows:
 *        --------------------------------------------------------------------------------------------------------------
 *        |                                         ConfiguredNetworkGroup                                             |  (External "interface")
 *        |                   ________________________________|________________________________                        |
 *        |                  /                                    |                            \                       |
 *        |            ConfiguredNetworkGroupBase       ConfiguredNetworkGroupWrapper   ConfiguredNetworkGroupClient   |  (Base classes)
 *        |               /                  \                                                                         |
 *        |  VdmaConfigNetworkGroup     HcpConfigNetworkGroup                                                          |  (Actual implementations)
 *        -------------------------------------------------------------------------------------------------------------|
 *        |                         ActivatedNetworkGroup                                                              |  (External "interface")
 *        |                                   |                                                                        |
 *        |                       ActivatedNetworkGroupBase                                                            |  (Base classes)
 *        |                 __________________|__________________                                                      |
 *        |                /                                     \                                                     |
 *        |    VdmaConfigActivatedNetworkGroup         HcpConfigActivatedNetworkGroup                                  |  (Actual implementations)
 *        --------------------------------------------------------------------------------------------------------------
 **/

#ifndef _HAILO_NETWORK_GROUP_INTERNAL_HPP_
#define _HAILO_NETWORK_GROUP_INTERNAL_HPP_

#include "hailo/hailort.h"
#include "hailo/network_group.hpp"
#include "hef_internal.hpp"
#include "common/latency_meter.hpp"
#include "control_protocol.h"
#include "vdma_channel.hpp"

#ifdef HAILO_SUPPORT_MULTI_PROCESS
#include "hailort_rpc_client.hpp"
#endif // HAILO_SUPPORT_MULTI_PROCESS

namespace hailort
{

class ActivatedNetworkGroupBase : public ActivatedNetworkGroup
{
public:
    virtual ~ActivatedNetworkGroupBase() = default;
    ActivatedNetworkGroupBase(const ActivatedNetworkGroupBase &other) = delete;
    ActivatedNetworkGroupBase &operator=(const ActivatedNetworkGroupBase &other) = delete;
    ActivatedNetworkGroupBase &operator=(ActivatedNetworkGroupBase &&other) = delete;
    ActivatedNetworkGroupBase(ActivatedNetworkGroupBase &&other) = default;

    virtual uint32_t get_invalid_frames_count() override;
    // Must be called on d'tor of derived class
    void deactivate_resources();

protected:
    hailo_activate_network_group_params_t m_network_group_params;

    ActivatedNetworkGroupBase(const hailo_activate_network_group_params_t &network_group_params,
        uint16_t dynamic_batch_size,
        std::map<std::string, std::unique_ptr<InputStream>> &input_streams,
        std::map<std::string, std::unique_ptr<OutputStream>> &output_streams,         
        EventPtr &&network_group_activated_event, hailo_status &status);

private:
    hailo_status activate_low_level_streams(uint16_t dynamic_batch_size);
    hailo_status validate_network_group_params(const hailo_activate_network_group_params_t &network_group_params);

    std::map<std::string, std::unique_ptr<InputStream>> &m_input_streams;
    std::map<std::string, std::unique_ptr<OutputStream>> &m_output_streams;
    EventPtr m_network_group_activated_event;
};

class ConfiguredNetworkGroupBase : public ConfiguredNetworkGroup
{
public:
    virtual ~ConfiguredNetworkGroupBase() = default;
    ConfiguredNetworkGroupBase(const ConfiguredNetworkGroupBase &other) = delete;
    ConfiguredNetworkGroupBase &operator=(const ConfiguredNetworkGroupBase &other) = delete;
    ConfiguredNetworkGroupBase &operator=(ConfiguredNetworkGroupBase &&other) = delete;
    ConfiguredNetworkGroupBase(ConfiguredNetworkGroupBase &&other) = default;

    Expected<std::unique_ptr<ActivatedNetworkGroup>> force_activate(
        uint16_t dynamic_batch_size = CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE);
    virtual Expected<std::unique_ptr<ActivatedNetworkGroup>> activate(const hailo_activate_network_group_params_t &network_group_params) override;
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

    static Expected<LatencyMeterPtr> create_hw_latency_meter(Device &device,
        const std::vector<LayerInfo> &layers);

    Expected<std::vector<std::string>> get_vstream_names_from_stream_name(const std::string &stream_name)
    {
        return m_network_group_metadata.get_vstream_names_from_stream_name(stream_name);
    }

    const NetworkGroupSupportedFeatures &get_supported_features()
    {
        return m_network_group_metadata.supported_features();
    }
    
    Expected<uint16_t> get_stream_batch_size(const std::string &stream_name);

    virtual Expected<std::vector<InputVStream>> create_input_vstreams(const std::map<std::string, hailo_vstream_params_t> &inputs_params);
    virtual Expected<std::vector<OutputVStream>> create_output_vstreams(const std::map<std::string, hailo_vstream_params_t> &outputs_params);

protected:
    ConfiguredNetworkGroupBase(const ConfigureNetworkParams &config_params, const uint8_t m_net_group_index, 
        const NetworkGroupMetadata &network_group_metadata, hailo_status &status);
    ConfiguredNetworkGroupBase(const ConfigureNetworkParams &config_params, const uint8_t m_net_group_index, 
        const NetworkGroupMetadata &network_group_metadata, bool is_scheduling, hailo_status &status);

    virtual Expected<std::unique_ptr<ActivatedNetworkGroup>> activate_internal(
        const hailo_activate_network_group_params_t &network_group_params, uint16_t dynamic_batch_size) override;
    virtual Expected<std::unique_ptr<ActivatedNetworkGroup>> activate_impl(
        const hailo_activate_network_group_params_t &network_group_params, uint16_t dynamic_batch_size) = 0;

    hailo_status create_output_stream_from_config_params(Device &device,
        const hailo_stream_parameters_t &stream_params, const std::string &stream_name);
    hailo_status create_input_stream_from_config_params(Device &device,
        const hailo_stream_parameters_t &stream_params, const std::string &stream_name);
    hailo_status add_mux_streams_by_edges_names(OutputStreamWithParamsVector &result,
        const std::unordered_map<std::string, hailo_vstream_params_t> &outputs_edges_params);
    Expected<OutputStreamRefVector> get_output_streams_by_vstream_name(const std::string &name);

    Expected<LayerInfo> get_layer_info(const std::string &stream_name);

    virtual Expected<std::shared_ptr<LatencyMetersMap>> get_latency_meters() = 0;
    virtual Expected<std::shared_ptr<VdmaChannel>> get_boundary_vdma_channel_by_stream_name(const std::string &stream_name) = 0;

    const ConfigureNetworkParams m_config_params;
    const uint16_t m_min_configured_batch_size; // TODO: remove after HRT-6535
    uint8_t m_net_group_index;
    std::map<std::string, std::unique_ptr<InputStream>> m_input_streams;
    std::map<std::string, std::unique_ptr<OutputStream>> m_output_streams;
    EventPtr m_network_group_activated_event;
    const NetworkGroupMetadata m_network_group_metadata;
    AccumulatorPtr m_activation_time_accumulator;
    AccumulatorPtr m_deactivation_time_accumulator;

private:
    friend class ConfiguredNetworkGroupWrapper;

    static uint16_t get_smallest_configured_batch_size(const ConfigureNetworkParams &config_params);

    bool m_is_scheduling;
};

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

    virtual hailo_status set_scheduler_timeout(const std::chrono::milliseconds &timeout, const std::string &network_name) override;
    virtual hailo_status set_scheduler_threshold(uint32_t threshold, const std::string &network_name) override;

    virtual AccumulatorPtr get_activation_time_accumulator() const override;
    virtual AccumulatorPtr get_deactivation_time_accumulator() const override;

    virtual bool is_multi_context() const override;
    virtual const ConfigureNetworkParams get_config_params() const override;

    virtual Expected<std::vector<InputVStream>> create_input_vstreams(const std::map<std::string, hailo_vstream_params_t> &inputs_params);
    virtual Expected<std::vector<OutputVStream>> create_output_vstreams(const std::map<std::string, hailo_vstream_params_t> &outputs_params);

protected:
    virtual Expected<std::unique_ptr<ActivatedNetworkGroup>> activate_internal(
        const hailo_activate_network_group_params_t &network_group_params, uint16_t dynamic_batch_size) override;

private:
    std::unique_ptr<HailoRtRpcClient> m_client;
    uint32_t m_handle;
    std::string m_network_group_name;
};
#endif // HAILO_SUPPORT_MULTI_PROCESS

} /* namespace hailort */

#endif /* _HAILO_NETWORK_GROUP_INTERNAL_HPP_ */
