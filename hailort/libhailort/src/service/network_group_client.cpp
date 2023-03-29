/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file network_group_client.cpp
 * @brief: Network group client object
 **/

#include "hailo/vstream.hpp"
#include "hailo/hailort_defaults.hpp"

#include "common/utils.hpp"
#include "common/os_utils.hpp"

#include "network_group/network_group_internal.hpp"
#include "net_flow/pipeline/vstream_internal.hpp"
#include "rpc/rpc_definitions.hpp"
#include "rpc_client_utils.hpp"


namespace hailort
{

ConfiguredNetworkGroupClient::ConfiguredNetworkGroupClient(std::unique_ptr<HailoRtRpcClient> client, uint32_t handle) :
    m_client(std::move(client)),
    m_handle(handle)
{
    auto reply = m_client->ConfiguredNetworkGroup_name(m_handle);
    if (!reply) {
        LOGGER__ERROR("get_network_group_name failed with status {}", reply.status());
        return;
    }
    m_network_group_name = reply.value();
}

ConfiguredNetworkGroupClient::~ConfiguredNetworkGroupClient()
{
    auto reply = m_client->ConfiguredNetworkGroup_release(m_handle);
    if (reply != HAILO_SUCCESS) {
        LOGGER__CRITICAL("ConfiguredNetworkGroup_release failed with status: {}", reply);
    }
}

hailo_status ConfiguredNetworkGroupClient::before_fork()
{
    m_client.reset();
    return HAILO_SUCCESS;
}

hailo_status ConfiguredNetworkGroupClient::create_client()
{
    auto expected_client = HailoRtRpcClientUtils::create_client();
    CHECK_EXPECTED_AS_STATUS(expected_client);
    m_client = expected_client.release();
    return HAILO_SUCCESS;
}

hailo_status ConfiguredNetworkGroupClient::after_fork_in_parent()
{
    return create_client();
}

hailo_status ConfiguredNetworkGroupClient::after_fork_in_child()
{
    auto status = create_client();
    CHECK_SUCCESS(status);
    auto expected_dup_handle = m_client->ConfiguredNetworkGroup_dup_handle(OsUtils::get_curr_pid(), m_handle);
    CHECK_EXPECTED_AS_STATUS(expected_dup_handle);
    m_handle = expected_dup_handle.value();
    return HAILO_SUCCESS;
}

Expected<std::unique_ptr<ActivatedNetworkGroup>> ConfiguredNetworkGroupClient::activate(
    const hailo_activate_network_group_params_t &/* network_group_params */)
{
    LOGGER__WARNING("ConfiguredNetworkGroup::activate function is not supported when using multi-process service or HailoRT Scheduler.");
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

/* Network group base functions */
Expected<LatencyMeasurementResult> ConfiguredNetworkGroupClient::get_latency_measurement(const std::string &network_name)
{
    return m_client->ConfiguredNetworkGroup_get_latency_measurement(m_handle, network_name);
}

const std::string &ConfiguredNetworkGroupClient::get_network_group_name() const
{
    return m_network_group_name;
}

const std::string &ConfiguredNetworkGroupClient::name() const
{
    return m_network_group_name;
}

Expected<hailo_stream_interface_t> ConfiguredNetworkGroupClient::get_default_streams_interface()
{
    return m_client->ConfiguredNetworkGroup_get_default_stream_interface(m_handle);
}

std::vector<std::reference_wrapper<InputStream>> ConfiguredNetworkGroupClient::get_input_streams_by_interface(hailo_stream_interface_t)
{
    LOGGER__ERROR("ConfiguredNetworkGroup::get_input_streams_by_interface function is not supported when using multi-process service");
    std::vector<std::reference_wrapper<InputStream>> empty_vec;
    return empty_vec;
}

std::vector<std::reference_wrapper<OutputStream>> ConfiguredNetworkGroupClient::get_output_streams_by_interface(hailo_stream_interface_t)
{
    LOGGER__ERROR("ConfiguredNetworkGroup::get_output_streams_by_interface function is not supported when using multi-process service");
    std::vector<std::reference_wrapper<OutputStream>> empty_vec;
    return empty_vec;
}

ExpectedRef<InputStream> ConfiguredNetworkGroupClient::get_input_stream_by_name(const std::string&)
{
    LOGGER__ERROR("ConfiguredNetworkGroup::get_input_stream_by_name function is not supported when using multi-process service");
    return make_unexpected(HAILO_INVALID_OPERATION);
}

ExpectedRef<OutputStream> ConfiguredNetworkGroupClient::get_output_stream_by_name(const std::string&)
{
    LOGGER__ERROR("ConfiguredNetworkGroup::get_output_stream_by_name function is not supported when using multi-process service");
    return make_unexpected(HAILO_INVALID_OPERATION);
}

Expected<InputStreamRefVector> ConfiguredNetworkGroupClient::get_input_streams_by_network(const std::string&)
{
    LOGGER__ERROR("ConfiguredNetworkGroup::get_input_streams_by_network function is not supported when using multi-process service");
    return make_unexpected(HAILO_INVALID_OPERATION);
}

Expected<OutputStreamRefVector> ConfiguredNetworkGroupClient::get_output_streams_by_network(const std::string&)
{
    LOGGER__ERROR("ConfiguredNetworkGroup::get_output_streams_by_network function is not supported when using multi-process service");
    return make_unexpected(HAILO_INVALID_OPERATION);
}

InputStreamRefVector ConfiguredNetworkGroupClient::get_input_streams()
{
    LOGGER__ERROR("ConfiguredNetworkGroup::get_input_streams function is not supported when using multi-process service");
    InputStreamRefVector empty_vec;
    return empty_vec;
}

OutputStreamRefVector ConfiguredNetworkGroupClient::get_output_streams()
{
    LOGGER__ERROR("ConfiguredNetworkGroup::get_output_streams function is not supported when using multi-process service");
    OutputStreamRefVector empty_vec;
    return empty_vec;
}

Expected<OutputStreamWithParamsVector> ConfiguredNetworkGroupClient::get_output_streams_from_vstream_names(const std::map<std::string, hailo_vstream_params_t>&)
{
    LOGGER__ERROR("ConfiguredNetworkGroup::get_output_streams_from_vstream_names function is not supported when using multi-process service");
    return make_unexpected(HAILO_INVALID_OPERATION);
}

hailo_status ConfiguredNetworkGroupClient::wait_for_activation(const std::chrono::milliseconds&)
{
    LOGGER__WARNING("ConfiguredNetworkGroup::wait_for_activation function is not supported when using multi-process service or HailoRT Scheduler.");
    return HAILO_NOT_IMPLEMENTED;
}

Expected<std::vector<std::vector<std::string>>> ConfiguredNetworkGroupClient::get_output_vstream_groups()
{
    return m_client->ConfiguredNetworkGroup_get_output_vstream_groups(m_handle);
}

Expected<std::vector<std::map<std::string, hailo_vstream_params_t>>> ConfiguredNetworkGroupClient::make_output_vstream_params_groups(
    bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size)
{
    return m_client->ConfiguredNetworkGroup_make_output_vstream_params_groups(m_handle,
        quantized, format_type, timeout_ms, queue_size);
}

Expected<std::map<std::string, hailo_vstream_params_t>> ConfiguredNetworkGroupClient::make_input_vstream_params(
    bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
    const std::string &network_name)
{
    return m_client->ConfiguredNetworkGroup_make_input_vstream_params(m_handle,
        quantized, format_type, timeout_ms, queue_size, network_name);
}

Expected<std::map<std::string, hailo_vstream_params_t>> ConfiguredNetworkGroupClient::make_output_vstream_params(
    bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
    const std::string &network_name)
{
    return m_client->ConfiguredNetworkGroup_make_output_vstream_params(m_handle,
        quantized, format_type, timeout_ms, queue_size, network_name);
}

Expected<std::vector<hailo_stream_info_t>> ConfiguredNetworkGroupClient::get_all_stream_infos(const std::string &network_name) const
{
    return m_client->ConfiguredNetworkGroup_get_all_stream_infos(m_handle, network_name);
}

Expected<std::vector<hailo_network_info_t>> ConfiguredNetworkGroupClient::get_network_infos() const
{
    return m_client->ConfiguredNetworkGroup_get_network_infos(m_handle);
}

Expected<std::vector<hailo_vstream_info_t>> ConfiguredNetworkGroupClient::get_input_vstream_infos(
    const std::string &network_name) const
{
    return m_client->ConfiguredNetworkGroup_get_input_vstream_infos(m_handle, network_name);
}

Expected<std::vector<hailo_vstream_info_t>> ConfiguredNetworkGroupClient::get_output_vstream_infos(
    const std::string &network_name) const
{
    return m_client->ConfiguredNetworkGroup_get_output_vstream_infos(m_handle, network_name);
}

Expected<std::vector<hailo_vstream_info_t>> ConfiguredNetworkGroupClient::get_all_vstream_infos(
    const std::string &network_name) const
{
    return m_client->ConfiguredNetworkGroup_get_all_vstream_infos(m_handle, network_name);
}

bool ConfiguredNetworkGroupClient::is_scheduled() const
{
    auto reply = m_client->ConfiguredNetworkGroup_is_scheduled(m_handle);
    if (reply.status() != HAILO_SUCCESS) {
        LOGGER__ERROR("is_scheduled failed with status {}", reply.status());
        return false;
    }
    return reply.value();
}

hailo_status ConfiguredNetworkGroupClient::set_scheduler_timeout(const std::chrono::milliseconds &timeout, const std::string &network_name)
{
    return m_client->ConfiguredNetworkGroup_set_scheduler_timeout(m_handle, timeout, network_name);
}

hailo_status ConfiguredNetworkGroupClient::set_scheduler_threshold(uint32_t threshold, const std::string &network_name)
{
    return m_client->ConfiguredNetworkGroup_set_scheduler_threshold(m_handle, threshold, network_name);
}

hailo_status ConfiguredNetworkGroupClient::set_scheduler_priority(uint8_t priority, const std::string &network_name)
{
    return m_client->ConfiguredNetworkGroup_set_scheduler_priority(m_handle, priority, network_name);
}

AccumulatorPtr ConfiguredNetworkGroupClient::get_activation_time_accumulator() const
{
    LOGGER__ERROR("ConfiguredNetworkGroup::get_activation_time_accumulator function is not supported when using multi-process service");
    return AccumulatorPtr();
}

AccumulatorPtr ConfiguredNetworkGroupClient::get_deactivation_time_accumulator() const
{
    LOGGER__ERROR("ConfiguredNetworkGroup::get_deactivation_time_accumulator function is not supported when using multi-process service");
    return AccumulatorPtr();
}

bool ConfiguredNetworkGroupClient::is_multi_context() const
{
    auto reply = m_client->ConfiguredNetworkGroup_is_multi_context(m_handle);
    if (reply.status() != HAILO_SUCCESS) {
        LOGGER__ERROR("is_multi_context failed with status {}", reply.status());
        return false;
    }
    return reply.value();
}

const ConfigureNetworkParams ConfiguredNetworkGroupClient::get_config_params() const
{
    auto reply = m_client->ConfiguredNetworkGroup_get_config_params(m_handle);
    if (reply.status() != HAILO_SUCCESS) {
        LOGGER__ERROR("get_config_params failed with status {}", reply.status());
        return ConfigureNetworkParams();
    }
    return reply.value();
}

Expected<std::vector<InputVStream>> ConfiguredNetworkGroupClient::create_input_vstreams(const std::map<std::string, hailo_vstream_params_t> &inputs_params)
{
    auto reply = m_client->InputVStreams_create(m_handle, inputs_params, OsUtils::get_curr_pid());
    CHECK_EXPECTED(reply);
    auto input_vstreams_handles = reply.release();
    std::vector<InputVStream> vstreams;
    vstreams.reserve(input_vstreams_handles.size());

    for (uint32_t handle : input_vstreams_handles) {
        auto vstream_client = InputVStreamClient::create(handle);
        CHECK_EXPECTED(vstream_client);
        auto vstream = VStreamsBuilderUtils::create_input(vstream_client.release());
        vstreams.push_back(std::move(vstream));
    }
    return vstreams;
}

Expected<std::vector<OutputVStream>> ConfiguredNetworkGroupClient::create_output_vstreams(const std::map<std::string, hailo_vstream_params_t> &outputs_params)
{
    auto reply = m_client->OutputVStreams_create(m_handle, outputs_params, OsUtils::get_curr_pid());
    CHECK_EXPECTED(reply);
    auto output_vstreams_handles = reply.release();
    std::vector<OutputVStream> vstreams;
    vstreams.reserve(output_vstreams_handles.size());

    for(uint32_t handle : output_vstreams_handles) {
        auto vstream_client = OutputVStreamClient::create(handle);
        CHECK_EXPECTED(vstream_client);
        auto vstream = VStreamsBuilderUtils::create_output(vstream_client.release());
        vstreams.push_back(std::move(vstream));
    }
    return vstreams;
}

} /* namespace hailort */