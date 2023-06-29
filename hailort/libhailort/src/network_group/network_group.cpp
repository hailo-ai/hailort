/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file network_group.cpp
 * @brief: Configured Network Group and Activated Network Group
 **/

#include "hailo/transform.hpp"
#include "hailo/vstream.hpp"
#include "hailo/hailort_defaults.hpp"

#include "common/utils.hpp"
#include "common/runtime_statistics_internal.hpp"

#include "network_group/network_group_internal.hpp"
#include "hef/hef_internal.hpp"
#include "eth/eth_stream.hpp"
#include "vdma/vdma_stream.hpp"
#include "mipi/mipi_stream.hpp"
#include "device_common/control.hpp"
#include "net_flow/pipeline/vstream_internal.hpp"
#include "core_op/resource_manager/resource_manager.hpp"


namespace hailort
{

Expected<std::shared_ptr<ConfiguredNetworkGroup>> ConfiguredNetworkGroup::duplicate_network_group_client(uint32_t handle, const std::string &network_group_name)
{
#ifdef HAILO_SUPPORT_MULTI_PROCESS
    auto net_group_client = ConfiguredNetworkGroupClient::duplicate_network_group_client(handle, network_group_name);
    CHECK_EXPECTED(net_group_client);
    
    return std::shared_ptr<ConfiguredNetworkGroup>(net_group_client.release());
#else
    (void)handle;
    (void)network_group_name;
    LOGGER__ERROR("`duplicate_network_group_client()` requires service compilation with HAILO_BUILD_SERVICE");
    return make_unexpected(HAILO_INVALID_OPERATION);
#endif // HAILO_SUPPORT_MULTI_PROCESS
}

Expected<uint32_t> ConfiguredNetworkGroup::get_client_handle() const
{
    LOGGER__ERROR("`get_client_handle()` is valid only when working with HailoRT Service!");
    return make_unexpected(HAILO_INVALID_OPERATION);
}

Expected<std::unique_ptr<ActivatedNetworkGroup>> ConfiguredNetworkGroup::activate()
{
    const auto network_group_params = HailoRTDefaults::get_active_network_group_params();
    return activate(network_group_params);
}

Expected<std::unique_ptr<ActivatedNetworkGroup>> ConfiguredNetworkGroupBase::activate(
    const hailo_activate_network_group_params_t &network_group_params)
{
    return get_core_op()->activate(network_group_params);
}

/* Network group base functions */
Expected<HwInferResults> ConfiguredNetworkGroupBase::run_hw_infer_estimator()
{
    return get_core_op()->run_hw_infer_estimator();
}

Expected<LatencyMeasurementResult> ConfiguredNetworkGroupBase::get_latency_measurement(const std::string &network_name)
{
    return get_core_op()->get_latency_measurement(network_name);
}

Expected<OutputStreamWithParamsVector> ConfiguredNetworkGroupBase::get_output_streams_from_vstream_names(
    const std::map<std::string, hailo_vstream_params_t> &outputs_params)
{
    OutputStreamWithParamsVector results;
    std::unordered_map<std::string, hailo_vstream_params_t> outputs_edges_params;
    for (auto &name_params_pair : outputs_params) {
        auto stream_names = m_network_group_metadata.get_stream_names_from_vstream_name(name_params_pair.first);
        CHECK_EXPECTED(stream_names);

        for (auto &stream_name : stream_names.value()) {
            auto stream = get_shared_output_stream_by_name(stream_name);
            CHECK_EXPECTED(stream);
            if (stream.value()->get_info().is_mux) {
                outputs_edges_params.emplace(name_params_pair);
            }
            else {
                NameToVStreamParamsMap name_to_params = {name_params_pair};
                results.emplace_back(stream.value(), name_to_params);
            }
        }
    }
    // Add non mux streams to result
    hailo_status status = add_mux_streams_by_edges_names(results, outputs_edges_params); 
    CHECK_SUCCESS_AS_EXPECTED(status);

    return results;
}

// This function adds to results the OutputStreams that correspond to the edges in outputs_edges_params.
// If an edge name appears in outputs_edges_params then all of its predecessors must appear in outputs_edges_params as well, Otherwise, an error is returned.
// We use the set seen_edges in order to mark the edges already evaluated by one of its' predecessor.
hailo_status ConfiguredNetworkGroupBase::add_mux_streams_by_edges_names(OutputStreamWithParamsVector &results,
    const std::unordered_map<std::string, hailo_vstream_params_t> &outputs_edges_params)
{
    std::unordered_set<std::string> seen_edges;
    for (auto &name_params_pair : outputs_edges_params) {
        if (seen_edges.end() != seen_edges.find(name_params_pair.first)) {
            // Edge has already been seen by one of its predecessors
            continue;
        }
        auto output_streams = get_output_streams_by_vstream_name(name_params_pair.first);
        CHECK_EXPECTED_AS_STATUS(output_streams);
        CHECK(output_streams->size() == 1, HAILO_INVALID_ARGUMENT,
            "mux streams cannot be separated into multiple streams");
        auto output_stream = output_streams.release()[0];

        // TODO: Find a better way to get the mux edges without creating OutputDemuxer
        auto expected_demuxer = OutputDemuxer::create(*output_stream);
        CHECK_EXPECTED_AS_STATUS(expected_demuxer);

        NameToVStreamParamsMap name_to_params;
        for (auto &edge : expected_demuxer.value()->get_edges_stream_info()) {
            auto edge_name_params_pair = outputs_edges_params.find(edge.name);
            CHECK(edge_name_params_pair != outputs_edges_params.end(), HAILO_INVALID_ARGUMENT,
                "All edges of stream {} must be in output vstream params. edge {} is missing.",
                name_params_pair.first, edge.name);
            seen_edges.insert(edge.name);
            name_to_params.insert(*edge_name_params_pair);
        }
        results.emplace_back(output_stream, name_to_params);
    }
    return HAILO_SUCCESS;
}

Expected<OutputStreamPtrVector> ConfiguredNetworkGroupBase::get_output_streams_by_vstream_name(const std::string &name)
{
    auto stream_names = m_network_group_metadata.get_stream_names_from_vstream_name(name);
    CHECK_EXPECTED(stream_names);

    OutputStreamPtrVector output_streams;
    output_streams.reserve(stream_names->size());
    for (const auto &stream_name : stream_names.value()) {
        auto stream = get_shared_output_stream_by_name(stream_name);
        CHECK_EXPECTED(stream);
        output_streams.emplace_back(stream.value());
    }

    return output_streams;
}

Expected<LayerInfo> ConfiguredNetworkGroupBase::get_layer_info(const std::string &stream_name)
{
    return get_core_op()->get_layer_info(stream_name);
}

ConfiguredNetworkGroupBase::ConfiguredNetworkGroupBase(
    const ConfigureNetworkParams &config_params, std::vector<std::shared_ptr<CoreOp>> &&core_ops,
    NetworkGroupMetadata &&metadata) :
        m_config_params(config_params),
        m_core_ops(std::move(core_ops)),
        m_network_group_metadata(std::move(metadata))
{}

// static func
uint16_t ConfiguredNetworkGroupBase::get_smallest_configured_batch_size(const ConfigureNetworkParams &config_params)
{
    // There are two possible situations:
    // 1) All networks in the network group have the same configured (and hence smallest) batch_size =>
    //    We return that batch size.
    // 2) Not all of the networks have the same configured (and hence smallest) batch_size. Currently, when
    //    using dynamic_batch_sizes, all networks will use the same dynamic_batch_size (until HRT-6535 is done).
    //    Hence, we must not set a dynamic_batch_size to a value greater than the smallest configured network
    //    batch_size (e.g. all the resources allocated are for at most the configured network batch_size).

    /* We iterate over all network's batch_sizes to get the non-default min.
       Ignoring HAILO_DEFAULT_BATCH_SIZE as it is not a real batch-value,
       but indicating the scheduler should optimize batches by himself */
    uint16_t min_batch_size = UINT16_MAX;
    for (const auto &network_params_pair : config_params.network_params_by_name) {
        if ((HAILO_DEFAULT_BATCH_SIZE != network_params_pair.second.batch_size) &&
            (network_params_pair.second.batch_size < min_batch_size)) {
            min_batch_size = network_params_pair.second.batch_size;
        }
    }
    return (UINT16_MAX == min_batch_size) ? DEFAULT_ACTUAL_BATCH_SIZE : min_batch_size;
}

Expected<std::unique_ptr<ActivatedNetworkGroup>> ConfiguredNetworkGroupBase::activate_with_batch(uint16_t dynamic_batch_size,
    bool resume_pending_stream_transfers)
{
    return get_core_op()->activate_with_batch(dynamic_batch_size, resume_pending_stream_transfers);
}

const std::string &ConfiguredNetworkGroupBase::get_network_group_name() const
{
    return m_network_group_metadata.name();
}

const std::string &ConfiguredNetworkGroupBase::name() const
{
    return m_network_group_metadata.name();
}

hailo_status ConfiguredNetworkGroupBase::activate_low_level_streams(uint16_t dynamic_batch_size, bool resume_pending_stream_transfers)
{
    return get_core_op()->activate_low_level_streams(dynamic_batch_size, resume_pending_stream_transfers);
}

hailo_status ConfiguredNetworkGroupBase::deactivate_low_level_streams()
{
    return get_core_op()->deactivate_low_level_streams();
}

std::shared_ptr<CoreOp> ConfiguredNetworkGroupBase::get_core_op() const
{
    assert(m_core_ops.size() == 1);
    return m_core_ops[0];
}

const std::shared_ptr<CoreOpMetadata> ConfiguredNetworkGroupBase::get_core_op_metadata() const
{
    assert(m_core_ops.size() == 1);
    return m_core_ops[0]->metadata();
}

Expected<uint16_t> ConfiguredNetworkGroupBase::get_stream_batch_size(const std::string &stream_name)
{
    return get_core_op()->get_stream_batch_size(stream_name);
}

Expected<std::vector<std::string>> ConfiguredNetworkGroupBase::get_sorted_output_names()
{
    auto res = m_network_group_metadata.get_sorted_output_names();
    return res;
}

Expected<std::vector<std::string>> ConfiguredNetworkGroupBase::get_stream_names_from_vstream_name(const std::string &vstream_name)
{
    auto res = m_network_group_metadata.get_stream_names_from_vstream_name(vstream_name);
    return res;
}

Expected<std::vector<std::string>> ConfiguredNetworkGroupBase::get_vstream_names_from_stream_name(const std::string &stream_name)
{
    auto res = m_network_group_metadata.get_vstream_names_from_stream_name(stream_name);
    return res;
}

bool ConfiguredNetworkGroupBase::is_multi_context() const
{
    return get_core_op()->is_multi_context();
}

const ConfigureNetworkParams ConfiguredNetworkGroupBase::get_config_params() const
{
    return get_core_op()->get_config_params();
}

const SupportedFeatures &ConfiguredNetworkGroupBase::get_supported_features()
{
    return get_core_op()->get_supported_features();
}

hailo_status ConfiguredNetworkGroupBase::create_input_stream_from_config_params(Device &device,
    const hailo_stream_parameters_t &stream_params, const std::string &stream_name)
{
    return get_core_op()->create_input_stream_from_config_params(device, stream_params, stream_name);
}

hailo_status ConfiguredNetworkGroupBase::create_vdma_input_stream(Device &device, const std::string &stream_name,
    const LayerInfo &layer_info, const hailo_stream_parameters_t &stream_params)
{
    return get_core_op()->create_vdma_input_stream(device, stream_name, layer_info, stream_params);
}

hailo_status ConfiguredNetworkGroupBase::create_output_stream_from_config_params(Device &device,
    const hailo_stream_parameters_t &stream_params, const std::string &stream_name)
{
    return get_core_op()->create_output_stream_from_config_params(device, stream_params, stream_name);
}

hailo_status ConfiguredNetworkGroupBase::create_vdma_output_stream(Device &device, const std::string &stream_name,
    const LayerInfo &layer_info, const hailo_stream_parameters_t &stream_params)
{
    return get_core_op()->create_vdma_output_stream(device, stream_name, layer_info, stream_params);
}

hailo_status ConfiguredNetworkGroupBase::create_streams_from_config_params(Device &device)
{
    return get_core_op()->create_streams_from_config_params(device);
}

Expected<InputStreamRefVector> ConfiguredNetworkGroupBase::get_input_streams_by_network(const std::string &network_name)
{
    return get_core_op()->get_input_streams_by_network(network_name);
}

Expected<OutputStreamRefVector> ConfiguredNetworkGroupBase::get_output_streams_by_network(const std::string &network_name)
{
    return get_core_op()->get_output_streams_by_network(network_name);
}

InputStreamRefVector ConfiguredNetworkGroupBase::get_input_streams()
{
    return get_core_op()->get_input_streams();
}

OutputStreamRefVector ConfiguredNetworkGroupBase::get_output_streams()
{
    return get_core_op()->get_output_streams();
}

ExpectedRef<InputStream> ConfiguredNetworkGroupBase::get_input_stream_by_name(const std::string& name)
{
    return get_core_op()->get_input_stream_by_name(name);
}

ExpectedRef<OutputStream> ConfiguredNetworkGroupBase::get_output_stream_by_name(const std::string& name)
{
    return get_core_op()->get_output_stream_by_name(name);
}

std::vector<std::reference_wrapper<InputStream>> ConfiguredNetworkGroupBase::get_input_streams_by_interface(
    hailo_stream_interface_t stream_interface)
{
    return get_core_op()->get_input_streams_by_interface(stream_interface);
}

std::vector<std::reference_wrapper<OutputStream>> ConfiguredNetworkGroupBase::get_output_streams_by_interface(
    hailo_stream_interface_t stream_interface)
{
    return get_core_op()->get_output_streams_by_interface(stream_interface);
}

hailo_status ConfiguredNetworkGroupBase::wait_for_activation(const std::chrono::milliseconds &timeout)
{
    return get_core_op()->wait_for_activation(timeout);
}

Expected<std::vector<std::vector<std::string>>> ConfiguredNetworkGroupBase::get_output_vstream_groups()
{
    std::vector<std::vector<std::string>> results;

    for (auto output_stream : get_output_streams()) {
        auto vstreams_group = m_network_group_metadata.get_vstream_names_from_stream_name(output_stream.get().name());
        CHECK_EXPECTED(vstreams_group);
        results.push_back(vstreams_group.release());
    }

    return results;
}

Expected<std::vector<std::map<std::string, hailo_vstream_params_t>>> ConfiguredNetworkGroupBase::make_output_vstream_params_groups(
    bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size)
{
    auto params = make_output_vstream_params(quantized, format_type, timeout_ms, queue_size);
    CHECK_EXPECTED(params);

    auto groups = get_output_vstream_groups();
    CHECK_EXPECTED(groups);

    std::vector<std::map<std::string, hailo_vstream_params_t>> results(groups->size(), std::map<std::string, hailo_vstream_params_t>());

    size_t pipeline_group_index = 0;
    for (const auto &group : groups.release()) {
        for (const auto &name_pair : params.value()) {
            if (contains(group, name_pair.first)) {
                results[pipeline_group_index].insert(name_pair);
            }
        }
        pipeline_group_index++;
    }

    return results;
}

Expected<std::map<std::string, hailo_vstream_params_t>> ConfiguredNetworkGroupBase::make_input_vstream_params(
    bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
    const std::string &network_name)
{
    auto input_vstream_infos = m_network_group_metadata.get_input_vstream_infos(network_name);
    CHECK_EXPECTED(input_vstream_infos);

    std::map<std::string, hailo_vstream_params_t> res;
    auto status = Hef::Impl::fill_missing_vstream_params_with_default(res, input_vstream_infos.value(), quantized, 
        format_type, timeout_ms, queue_size);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return res;
}

Expected<std::map<std::string, hailo_vstream_params_t>> ConfiguredNetworkGroupBase::make_output_vstream_params(
    bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
    const std::string &network_name)
{
    auto output_vstream_infos = m_network_group_metadata.get_output_vstream_infos(network_name);
    CHECK_EXPECTED(output_vstream_infos);
    std::map<std::string, hailo_vstream_params_t> res;
    auto status = Hef::Impl::fill_missing_vstream_params_with_default(res, output_vstream_infos.value(), quantized, 
        format_type, timeout_ms, queue_size);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return res;
}

Expected<std::vector<hailo_network_info_t>> ConfiguredNetworkGroupBase::get_network_infos() const
{
    return m_network_group_metadata.get_network_infos();
}

Expected<std::vector<hailo_stream_info_t>> ConfiguredNetworkGroupBase::get_all_stream_infos(
    const std::string &network_name) const
{
    return get_core_op_metadata()->get_all_stream_infos(network_name);
}

Expected<std::vector<hailo_vstream_info_t>> ConfiguredNetworkGroupBase::get_input_vstream_infos(
    const std::string &network_name) const
{
    return m_network_group_metadata.get_input_vstream_infos(network_name);
}

Expected<std::vector<hailo_vstream_info_t>> ConfiguredNetworkGroupBase::get_output_vstream_infos(
    const std::string &network_name) const
{
    return m_network_group_metadata.get_output_vstream_infos(network_name);
}

Expected<std::vector<hailo_vstream_info_t>> ConfiguredNetworkGroupBase::get_all_vstream_infos(
    const std::string &network_name) const
{
    return m_network_group_metadata.get_all_vstream_infos(network_name);
}

AccumulatorPtr ConfiguredNetworkGroupBase::get_activation_time_accumulator() const
{
    return get_core_op()->get_activation_time_accumulator();
}

AccumulatorPtr ConfiguredNetworkGroupBase::get_deactivation_time_accumulator() const
{
    return get_core_op()->get_deactivation_time_accumulator();
}

static hailo_vstream_params_t expand_vstream_params_autos(const hailo_stream_info_t &stream_info,
    const hailo_vstream_params_t &vstream_params)
{
    auto local_vstream_params = vstream_params;
    local_vstream_params.user_buffer_format = HailoRTDefaults::expand_auto_format(vstream_params.user_buffer_format,
        stream_info.format);
    return local_vstream_params;
}

static std::map<std::string, hailo_vstream_info_t> vstream_infos_vector_to_map(std::vector<hailo_vstream_info_t> &&vstream_info_vector)
{
    std::map<std::string, hailo_vstream_info_t> vstream_infos_map;
    for (const auto &vstream_info : vstream_info_vector) {
        vstream_infos_map.emplace(std::string(vstream_info.name), vstream_info);
    }

    return vstream_infos_map;
}

Expected<std::vector<InputVStream>> ConfiguredNetworkGroupBase::create_input_vstreams(const std::map<std::string, hailo_vstream_params_t> &inputs_params)
{
    auto input_vstream_infos = get_input_vstream_infos();
    CHECK_EXPECTED(input_vstream_infos);
    auto input_vstream_infos_map = vstream_infos_vector_to_map(input_vstream_infos.release());

    std::vector<InputVStream> vstreams;
    vstreams.reserve(inputs_params.size());
    for (const auto &name_params_pair : inputs_params) {
        auto input_stream_expected = get_shared_input_stream_by_name(name_params_pair.first);
        CHECK_EXPECTED(input_stream_expected);
        auto input_stream = input_stream_expected.release();

        const auto vstream_info = input_vstream_infos_map.find(name_params_pair.first);
        CHECK_AS_EXPECTED(vstream_info != input_vstream_infos_map.end(), HAILO_NOT_FOUND,
            "Failed to find vstream info of {}", name_params_pair.first);

        const auto vstream_params = expand_vstream_params_autos(input_stream->get_info(), name_params_pair.second);
        auto inputs = VStreamsBuilderUtils::create_inputs(input_stream, vstream_info->second, vstream_params);
        CHECK_EXPECTED(inputs);

        vstreams.insert(vstreams.end(), std::make_move_iterator(inputs->begin()), std::make_move_iterator(inputs->end()));
    }
    return vstreams;
}

Expected<std::vector<OutputVStream>> ConfiguredNetworkGroupBase::create_output_vstreams(const std::map<std::string, hailo_vstream_params_t> &vstreams_params)
{
    std::vector<OutputVStream> vstreams;
    vstreams.reserve(vstreams_params.size());

    auto all_output_streams_expected = get_output_streams_from_vstream_names(vstreams_params);
    CHECK_EXPECTED(all_output_streams_expected);
    auto all_output_streams = all_output_streams_expected.release();
    auto output_vstream_infos = get_output_vstream_infos();
    CHECK_EXPECTED(output_vstream_infos);
    auto output_vstream_infos_map = vstream_infos_vector_to_map(output_vstream_infos.release());

    // Building DBs that connect output_vstreams, output_streams and ops.
    // Note: Assuming each post process op has a unique output streams.
    //       In other words, not possible for an output stream to be connected to more than one op
    std::unordered_map<std::string, std::shared_ptr<NetFlowElement>> post_process_ops;
    std::unordered_map<stream_name_t, op_name_t> op_inputs_to_op_name;
    for (auto &op : m_network_group_metadata.m_net_flow_ops) {
        post_process_ops.insert({op->name, op});
        for (auto &input_stream : op->input_streams) {
            op_inputs_to_op_name.insert({input_stream, op->name});
        }
    }

    // streams_added is a vector which holds all stream names which vstreams connected to them were already added (for demux cases)
    std::vector<std::string> streams_added;
    for (auto &vstream_params : vstreams_params) {
        auto output_streams = get_output_streams_by_vstream_name(vstream_params.first);
        CHECK_EXPECTED(output_streams);
        if (contains(streams_added, static_cast<std::string>(output_streams.value()[0]->get_info().name))) {
            continue;
        }
        for (auto &output_stream : output_streams.value()) {
            streams_added.push_back(output_stream->get_info().name);
        }

        auto outputs = VStreamsBuilderUtils::create_output_vstreams_from_streams(all_output_streams, output_streams.value(), vstream_params.second,
            post_process_ops, op_inputs_to_op_name, output_vstream_infos_map);
        CHECK_EXPECTED(outputs);
        vstreams.insert(vstreams.end(), std::make_move_iterator(outputs->begin()), std::make_move_iterator(outputs->end()));
    }

    get_core_op()->set_vstreams_multiplexer_callbacks(vstreams);
    return vstreams;
}

} /* namespace hailort */
