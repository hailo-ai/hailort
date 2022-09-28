/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file network_group_wrapper.cpp
 * @brief: Network Group Wrapper
 **/

#include "network_group_wrapper.hpp"

namespace hailort
{

const std::string &ConfiguredNetworkGroupWrapper::get_network_group_name() const
{
    return m_configured_network_group->get_network_group_name();
}

const std::string &ConfiguredNetworkGroupWrapper::name() const
{
    return m_configured_network_group->name();
}

Expected<hailo_stream_interface_t> ConfiguredNetworkGroupWrapper::get_default_streams_interface()
{
    return m_configured_network_group->get_default_streams_interface();
}

std::vector<std::reference_wrapper<InputStream>> ConfiguredNetworkGroupWrapper::get_input_streams_by_interface(hailo_stream_interface_t stream_interface)
{
    return m_configured_network_group->get_input_streams_by_interface(stream_interface);
}

std::vector<std::reference_wrapper<OutputStream>> ConfiguredNetworkGroupWrapper::get_output_streams_by_interface(hailo_stream_interface_t stream_interface)
{
    return m_configured_network_group->get_output_streams_by_interface(stream_interface);
}

ExpectedRef<InputStream> ConfiguredNetworkGroupWrapper::get_input_stream_by_name(const std::string& name)
{
    return m_configured_network_group->get_input_stream_by_name(name);
}
ExpectedRef<OutputStream> ConfiguredNetworkGroupWrapper::get_output_stream_by_name(const std::string& name)
{
    return m_configured_network_group->get_output_stream_by_name(name);
}

Expected<InputStreamRefVector> ConfiguredNetworkGroupWrapper::get_input_streams_by_network(const std::string &network_name)
{
    return m_configured_network_group->get_input_streams_by_network(network_name);
}

Expected<OutputStreamRefVector> ConfiguredNetworkGroupWrapper::get_output_streams_by_network(const std::string &network_name)
{
    return m_configured_network_group->get_output_streams_by_network(network_name);
}

InputStreamRefVector ConfiguredNetworkGroupWrapper::get_input_streams()
{
    return m_configured_network_group->get_input_streams();
}

OutputStreamRefVector ConfiguredNetworkGroupWrapper::get_output_streams()
{
    return m_configured_network_group->get_output_streams();
}

Expected<LatencyMeasurementResult> ConfiguredNetworkGroupWrapper::get_latency_measurement(const std::string &network_name)
{
    return m_configured_network_group->get_latency_measurement(network_name);
}

Expected<OutputStreamWithParamsVector> ConfiguredNetworkGroupWrapper::get_output_streams_from_vstream_names(
    const std::map<std::string, hailo_vstream_params_t> &outputs_params)
{
    return m_configured_network_group->get_output_streams_from_vstream_names(outputs_params);
}

Expected<std::unique_ptr<ActivatedNetworkGroup>> ConfiguredNetworkGroupWrapper::activate_internal(const hailo_activate_network_group_params_t &network_group_params, uint16_t dynamic_batch_size)
{
    return m_configured_network_group->activate_internal(network_group_params, dynamic_batch_size);
}

hailo_status ConfiguredNetworkGroupWrapper::wait_for_activation(const std::chrono::milliseconds &timeout)
{
    return m_configured_network_group->wait_for_activation(timeout);
}

Expected<std::map<std::string, hailo_vstream_params_t>> ConfiguredNetworkGroupWrapper::make_input_vstream_params(
    bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
    const std::string &network_name)
{
    return m_configured_network_group->make_input_vstream_params(quantized, format_type, timeout_ms, queue_size, network_name);
}
Expected<std::map<std::string, hailo_vstream_params_t>> ConfiguredNetworkGroupWrapper::make_output_vstream_params(
    bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
    const std::string &network_name)
{
    return m_configured_network_group->make_output_vstream_params(quantized, format_type, timeout_ms, queue_size, network_name);
}

Expected<std::vector<std::map<std::string, hailo_vstream_params_t>>> ConfiguredNetworkGroupWrapper::make_output_vstream_params_groups(
    bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size)
{
    return m_configured_network_group->make_output_vstream_params_groups(quantized, format_type, timeout_ms, queue_size);
}

Expected<std::vector<std::vector<std::string>>> ConfiguredNetworkGroupWrapper::get_output_vstream_groups()
{
    return m_configured_network_group->get_output_vstream_groups();
}

Expected<std::vector<hailo_network_info_t>> ConfiguredNetworkGroupWrapper::get_network_infos() const
{
    return m_configured_network_group->get_network_infos();
}

Expected<std::vector<hailo_stream_info_t>> ConfiguredNetworkGroupWrapper::get_all_stream_infos(const std::string &network_name) const
{
    return m_configured_network_group->get_all_stream_infos(network_name);
}

Expected<std::vector<hailo_vstream_info_t>> ConfiguredNetworkGroupWrapper::get_input_vstream_infos(const std::string &network_name) const
{
    return m_configured_network_group->get_input_vstream_infos(network_name);
}

Expected<std::vector<hailo_vstream_info_t>> ConfiguredNetworkGroupWrapper::get_output_vstream_infos(const std::string &network_name) const
{
    return m_configured_network_group->get_output_vstream_infos(network_name);
}

Expected<std::vector<hailo_vstream_info_t>> ConfiguredNetworkGroupWrapper::get_all_vstream_infos(const std::string &network_name) const
{
    return m_configured_network_group->get_all_vstream_infos(network_name);
}

hailo_status ConfiguredNetworkGroupWrapper::set_scheduler_timeout(const std::chrono::milliseconds &timeout, const std::string &network_name)
{
    return m_configured_network_group->set_scheduler_timeout(timeout, network_name);
}
hailo_status ConfiguredNetworkGroupWrapper::set_scheduler_threshold(uint32_t threshold, const std::string &network_name)
{
    return m_configured_network_group->set_scheduler_threshold(threshold, network_name);
}

AccumulatorPtr ConfiguredNetworkGroupWrapper::get_activation_time_accumulator() const
{
    return m_configured_network_group->get_activation_time_accumulator();
}

AccumulatorPtr ConfiguredNetworkGroupWrapper::get_deactivation_time_accumulator() const
{
    return m_configured_network_group->get_deactivation_time_accumulator();
}

bool ConfiguredNetworkGroupWrapper::is_multi_context() const
{
    return m_configured_network_group->is_multi_context();
}
const ConfigureNetworkParams ConfiguredNetworkGroupWrapper::get_config_params() const
{
    return m_configured_network_group->get_config_params();
}

std::shared_ptr<ConfiguredNetworkGroupBase> ConfiguredNetworkGroupWrapper::get_configured_network() const
{
    return m_configured_network_group;
}

ConfiguredNetworkGroupWrapper::ConfiguredNetworkGroupWrapper(std::shared_ptr<ConfiguredNetworkGroupBase> configured_network_group) :
    m_configured_network_group(configured_network_group)
{}

Expected<ConfiguredNetworkGroupWrapper> ConfiguredNetworkGroupWrapper::create(std::shared_ptr<ConfiguredNetworkGroupBase> configured_network_group)
{
    return ConfiguredNetworkGroupWrapper(configured_network_group);
}

Expected<ConfiguredNetworkGroupWrapper> ConfiguredNetworkGroupWrapper::clone()
{
    auto wrapper = create(m_configured_network_group);
    CHECK_EXPECTED(wrapper);

    return wrapper;
}

Expected<std::unique_ptr<ActivatedNetworkGroup>> ConfiguredNetworkGroupWrapper::activate(const hailo_activate_network_group_params_t &network_group_params)
{
    return m_configured_network_group->activate(network_group_params);
}

Expected<std::vector<InputVStream>> ConfiguredNetworkGroupWrapper::create_input_vstreams(const std::map<std::string, hailo_vstream_params_t> &inputs_params)
{
    return m_configured_network_group->create_input_vstreams(inputs_params);
}

Expected<std::vector<OutputVStream>> ConfiguredNetworkGroupWrapper::create_output_vstreams(const std::map<std::string, hailo_vstream_params_t> &outputs_params)
{
    return m_configured_network_group->create_output_vstreams(outputs_params);
}

} /* namespace hailort */
