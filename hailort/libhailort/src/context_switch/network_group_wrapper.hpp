/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file network_group_wrapper.hpp
 * @brief Class declaration for ConfiguredNetworkGroupWrapper, a wrapper around ConfiguredNetworkGroupBase which is used
 *        to support multiple ConfiguredNetworkGroup objects that encapsulate the same actual configured network group.
 **/

#ifndef _HAILO_NETWORK_GROUP_WRAPPER_HPP_
#define _HAILO_NETWORK_GROUP_WRAPPER_HPP_

#include "hailo/hailort.h"
#include "hailo/network_group.hpp"
#include "hailo/vstream.hpp"
#include "network_group_internal.hpp"

namespace hailort
{

class ConfiguredNetworkGroupWrapper : public ConfiguredNetworkGroup
{
public:
    virtual ~ConfiguredNetworkGroupWrapper() = default;
    ConfiguredNetworkGroupWrapper(const ConfiguredNetworkGroupWrapper &other) = delete;
    ConfiguredNetworkGroupWrapper &operator=(const ConfiguredNetworkGroupWrapper &other) = delete;
    ConfiguredNetworkGroupWrapper &operator=(ConfiguredNetworkGroupWrapper &&other) = delete;
    ConfiguredNetworkGroupWrapper(ConfiguredNetworkGroupWrapper &&other) = default;

    static Expected<ConfiguredNetworkGroupWrapper> create(std::shared_ptr<ConfiguredNetworkGroupBase> configured_network_group);
    Expected<ConfiguredNetworkGroupWrapper> clone();

    virtual const std::string &get_network_group_name() const override;
    virtual const std::string &name() const override;
    virtual Expected<hailo_stream_interface_t> get_default_streams_interface() override;

    virtual std::vector<std::reference_wrapper<InputStream>> get_input_streams_by_interface(hailo_stream_interface_t stream_interface) override;
    virtual std::vector<std::reference_wrapper<OutputStream>> get_output_streams_by_interface(hailo_stream_interface_t stream_interface) override;
    virtual ExpectedRef<InputStream> get_input_stream_by_name(const std::string& name) override;
    virtual ExpectedRef<OutputStream> get_output_stream_by_name(const std::string& name) override;
    virtual Expected<InputStreamRefVector> get_input_streams_by_network(const std::string &network_name="") override;
    virtual Expected<OutputStreamRefVector> get_output_streams_by_network(const std::string &network_name="") override;
    virtual InputStreamRefVector get_input_streams() override;
    virtual OutputStreamRefVector get_output_streams() override;
    virtual Expected<LatencyMeasurementResult> get_latency_measurement(const std::string &network_name="") override;
    virtual Expected<OutputStreamWithParamsVector> get_output_streams_from_vstream_names(
        const std::map<std::string, hailo_vstream_params_t> &outputs_params) override;

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

    virtual Expected<std::vector<hailo_network_info_t>> get_network_infos() const override;
    virtual Expected<std::vector<hailo_stream_info_t>> get_all_stream_infos(const std::string &network_name="") const override;
    virtual Expected<std::vector<hailo_vstream_info_t>> get_input_vstream_infos(const std::string &network_name="") const override;
    virtual Expected<std::vector<hailo_vstream_info_t>> get_output_vstream_infos(const std::string &network_name="") const override;
    virtual Expected<std::vector<hailo_vstream_info_t>> get_all_vstream_infos(const std::string &network_name="") const override;

    virtual hailo_status set_scheduler_timeout(const std::chrono::milliseconds &timeout, const std::string &network_name) override;
    virtual hailo_status set_scheduler_threshold(uint32_t threshold, const std::string &network_name) override;

    virtual AccumulatorPtr get_activation_time_accumulator() const override;
    virtual AccumulatorPtr get_deactivation_time_accumulator() const override;
    virtual bool is_multi_context() const override;
    virtual const ConfigureNetworkParams get_config_params() const override;

    std::shared_ptr<ConfiguredNetworkGroupBase> get_configured_network() const;

    virtual Expected<std::vector<InputVStream>> create_input_vstreams(const std::map<std::string, hailo_vstream_params_t> &inputs_params);
    virtual Expected<std::vector<OutputVStream>> create_output_vstreams(const std::map<std::string, hailo_vstream_params_t> &outputs_params);

protected:
    virtual Expected<std::unique_ptr<ActivatedNetworkGroup>> activate_internal(
        const hailo_activate_network_group_params_t &network_group_params, uint16_t dynamic_batch_size) override;

private:
    ConfiguredNetworkGroupWrapper(std::shared_ptr<ConfiguredNetworkGroupBase> configured_network_group);

    std::shared_ptr<ConfiguredNetworkGroupBase> m_configured_network_group;
};

}

#endif /* _HAILO_NETWORK_GROUP_WRAPPER_HPP_ */