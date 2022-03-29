/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file network_group_internal.hpp
 * @brief Class declaration for ConfiguredNetworkGroupBase and ActivatedNetworkGroupBase that implement the basic ConfiguredNetworkGroup
 *        and ActivatedNetworkGroup interfaces. All internal classes that are relavant should inherit from the
 *        ConfiguredNetworkGroupBase and ActivatedNetworkGroupBase classes.
 *        Hence, the hiearchy is as follows:
 *        -----------------------------------------------------------------------------
 *        |                        ConfiguredNetworkGroup                             |  (External "interface")
 *        |                                  |                                        |
 *        |                      ConfiguredNetworkGroupBase                           |  (Base classes)
 *        |                          /                \                               |
 *        |           VdmaConfigNetworkGroup       HcpConfigNetworkGroup              | (Actual implementations)
 *        -----------------------------------------------------------------------------
 *        |                         ActivatedNetworkGroup                             |  (External "interface")
 *        |                                   |                                       |
 *        |                       ActivatedNetworkGroupBase                           |  (Base classes)
 *        |                 __________________|__________________                     |
 *        |                /                                     \                    |
 *        |    VdmaConfigActivatedNetworkGroup         HcpConfigActivatedNetworkGroup |  (Actual implementations)
 *        -----------------------------------------------------------------------------
 **/

#ifndef _HAILO_NETWORK_GROUP_INTERNAL_HPP_
#define _HAILO_NETWORK_GROUP_INTERNAL_HPP_

#include "hailo/hailort.h"
#include "hailo/network_group.hpp"
#include "hef_internal.hpp"
#include "common/latency_meter.hpp"

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
        std::map<std::string, std::unique_ptr<InputStream>> &input_streams,
        std::map<std::string, std::unique_ptr<OutputStream>> &output_streams,         
        EventPtr &&network_group_activated_event, hailo_status &status);

private:
    hailo_status activate_low_level_streams();
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

    virtual hailo_status wait_for_activation(const std::chrono::milliseconds &timeout) override;

    virtual const std::string &get_network_group_name() const override;

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

protected:
    ConfiguredNetworkGroupBase(const ConfigureNetworkParams &config_params, const uint8_t m_net_group_index, 
        const NetworkGroupMetadata &network_group_metadata, hailo_status &status);

    hailo_status create_output_stream_from_config_params(Device &device,
        const hailo_stream_parameters_t &stream_params, const std::string &stream_name);
    hailo_status create_input_stream_from_config_params(Device &device,
        const hailo_stream_parameters_t &stream_params, const std::string &stream_name);
    hailo_status add_mux_streams_by_edges_names(OutputStreamWithParamsVector &result,
        const std::unordered_map<std::string, hailo_vstream_params_t> &outputs_edges_params);
    Expected<OutputStreamRefVector> get_output_streams_by_vstream_name(const std::string &name);

    Expected<LayerInfo> get_layer_info(const std::string &stream_name);

    virtual Expected<uint8_t> get_boundary_channel_index(uint8_t stream_index, hailo_stream_direction_t direction,
        const std::string &layer_name) = 0;

    const ConfigureNetworkParams m_config_params;
    uint8_t m_net_group_index;
    std::map<std::string, LatencyMeterPtr> m_latency_meter; // Latency meter per network
    std::map<std::string, std::unique_ptr<InputStream>> m_input_streams;
    std::map<std::string, std::unique_ptr<OutputStream>> m_output_streams;
    EventPtr m_network_group_activated_event;
    const NetworkGroupMetadata m_network_group_metadata;
    AccumulatorPtr m_activation_time_accumulator;
    AccumulatorPtr m_deactivation_time_accumulator;
};

} /* namespace hailort */

#endif /* _HAILO_NETWORK_GROUP_INTERNAL_HPP_ */
