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
Expected<LatencyMeasurementResult> ConfiguredNetworkGroupBase::get_latency_measurement(const std::string &network_name)
{
    return get_core_op()->get_latency_measurement(network_name);
}

Expected<OutputStreamWithParamsVector> ConfiguredNetworkGroupBase::get_output_streams_from_vstream_names(
    const std::map<std::string, hailo_vstream_params_t> &outputs_params)
{
    return get_core_op()->get_output_streams_from_vstream_names(outputs_params);
}

Expected<OutputStreamPtrVector> ConfiguredNetworkGroupBase::get_output_streams_by_vstream_name(const std::string &name)
{
    return get_core_op()->get_output_streams_by_vstream_name(name);
}

Expected<LayerInfo> ConfiguredNetworkGroupBase::get_layer_info(const std::string &stream_name)
{
    return get_core_op()->get_layer_info(stream_name);
}

ConfiguredNetworkGroupBase::ConfiguredNetworkGroupBase(
    const ConfigureNetworkParams &config_params, std::vector<std::shared_ptr<CoreOp>> &&core_ops,
    std::vector<std::shared_ptr<NetFlowElement>> &&net_flow_ops) :
        m_config_params(config_params),
        m_core_ops(std::move(core_ops)),
        m_net_flow_ops(std::move(net_flow_ops))
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
    return get_core_op_metadata()->core_op_name();
}

const std::string &ConfiguredNetworkGroupBase::name() const
{
    return get_core_op_metadata()->core_op_name();
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

bool ConfiguredNetworkGroupBase::is_multi_context() const
{
    return get_core_op()->is_multi_context();
}

const ConfigureNetworkParams ConfiguredNetworkGroupBase::get_config_params() const
{
    return get_core_op()->get_config_params();
}

Expected<std::vector<std::string>> ConfiguredNetworkGroupBase::get_vstream_names_from_stream_name(const std::string &stream_name)
{
    return get_core_op()->get_vstream_names_from_stream_name(stream_name);
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
    return get_core_op()->get_output_vstream_groups();
}

Expected<std::vector<std::map<std::string, hailo_vstream_params_t>>> ConfiguredNetworkGroupBase::make_output_vstream_params_groups(
    bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size)
{
    return get_core_op()->make_output_vstream_params_groups(quantized, format_type, timeout_ms, queue_size);
}

Expected<std::map<std::string, hailo_vstream_params_t>> ConfiguredNetworkGroupBase::make_input_vstream_params(
    bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
    const std::string &network_name)
{
    return get_core_op()->make_input_vstream_params(quantized, format_type, timeout_ms, queue_size, network_name);
}

Expected<std::map<std::string, hailo_vstream_params_t>> ConfiguredNetworkGroupBase::make_output_vstream_params(
    bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
    const std::string &network_name)
{
    return get_core_op()->make_output_vstream_params(quantized, format_type, timeout_ms, queue_size, network_name);
}

Expected<std::vector<hailo_network_info_t>> ConfiguredNetworkGroupBase::get_network_infos() const
{
    return get_core_op()->get_network_infos();
}

Expected<std::vector<hailo_stream_info_t>> ConfiguredNetworkGroupBase::get_all_stream_infos(
    const std::string &network_name) const
{
    return get_core_op()->get_all_stream_infos(network_name);
}

Expected<std::vector<hailo_vstream_info_t>> ConfiguredNetworkGroupBase::get_input_vstream_infos(
    const std::string &network_name) const
{
    return get_core_op()->get_input_vstream_infos(network_name);
}

Expected<std::vector<hailo_vstream_info_t>> ConfiguredNetworkGroupBase::get_output_vstream_infos(
    const std::string &network_name) const
{
    return get_core_op()->get_output_vstream_infos(network_name);
}

Expected<std::vector<hailo_vstream_info_t>> ConfiguredNetworkGroupBase::get_all_vstream_infos(
    const std::string &network_name) const
{
    return get_core_op()->get_all_vstream_infos(network_name);
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

Expected<std::vector<OutputVStream>> ConfiguredNetworkGroupBase::create_output_vstreams(const std::map<std::string, hailo_vstream_params_t> &outputs_params)
{
    std::vector<OutputVStream> vstreams;
    vstreams.reserve(outputs_params.size());
    auto output_streams = get_output_streams_from_vstream_names(outputs_params);
    CHECK_EXPECTED(output_streams);

    auto output_vstream_infos = get_output_vstream_infos();
    CHECK_EXPECTED(output_vstream_infos);
    auto output_vstream_infos_map = vstream_infos_vector_to_map(output_vstream_infos.release());

    // We iterate through all output streams, and if they are nms, we collect them together by their original stream name.
    // We need this step because all nms output streams of the same original stream need to be fused together

    std::unordered_map<std::string, std::shared_ptr<NetFlowElement>> post_process_nms_ops;
    std::set<std::string> post_process_stream_inputs;
    for (auto &op : m_net_flow_ops) {
        post_process_nms_ops.insert({op->name, op});
        post_process_stream_inputs.insert(op->input_streams.begin(), op->input_streams.end());
    }
    std::map<std::string, std::pair<OutputStreamPtrVector, hailo_vstream_params_t>> nms_op_output_streams;
    std::map<std::string, std::pair<OutputStreamPtrVector, hailo_vstream_params_t>> nms_output_streams;
    for (auto &stream_params_pair : output_streams.value()) {
        if ((HAILO_FORMAT_ORDER_HAILO_NMS == stream_params_pair.first->get_info().format.order && stream_params_pair.first->get_info().nms_info.is_defused) &&
            (outputs_params.end() != outputs_params.find(stream_params_pair.first->get_info().nms_info.defuse_info.original_name))) {
                auto original_name = stream_params_pair.first->get_info().nms_info.defuse_info.original_name;
                nms_output_streams.emplace(original_name, std::pair<OutputStreamPtrVector, hailo_vstream_params_t>(
                    OutputStreamPtrVector(), outputs_params.at(original_name)));
                nms_output_streams[original_name].first.push_back(stream_params_pair.first);
        } else if (post_process_stream_inputs.count(stream_params_pair.first->get_info().name)) {
            for (auto &op : m_net_flow_ops) {
                if (op->input_streams.count(stream_params_pair.first->get_info().name)) {
                    assert(op->op->outputs_metadata().size() == 1);
                    nms_op_output_streams.emplace(op->name, std::pair<OutputStreamPtrVector, hailo_vstream_params_t>(
                        OutputStreamPtrVector(), outputs_params.at(op->op->outputs_metadata().begin()->first)));
                    nms_op_output_streams[op->name].first.push_back(stream_params_pair.first);
                }
            }
        } else {
            auto outputs = VStreamsBuilderUtils::create_outputs(stream_params_pair.first, stream_params_pair.second, output_vstream_infos_map);
            CHECK_EXPECTED(outputs);
            vstreams.insert(vstreams.end(), std::make_move_iterator(outputs->begin()), std::make_move_iterator(outputs->end()));
        }
    }
    for (auto &nms_output_stream_pair : nms_output_streams) {
        auto outputs = VStreamsBuilderUtils::create_output_nms(nms_output_stream_pair.second.first, nms_output_stream_pair.second.second,
            output_vstream_infos_map);
        CHECK_EXPECTED(outputs);
        vstreams.insert(vstreams.end(), std::make_move_iterator(outputs->begin()), std::make_move_iterator(outputs->end()));
    }
    for (auto &nms_output_stream_pair : nms_op_output_streams) {
        auto op = post_process_nms_ops.at(nms_output_stream_pair.first);
        auto outputs = VStreamsBuilderUtils::create_output_post_process_nms(nms_output_stream_pair.second.first,
            nms_output_stream_pair.second.second, output_vstream_infos_map,
            *op);
        CHECK_EXPECTED(outputs);
        vstreams.insert(vstreams.end(), std::make_move_iterator(outputs->begin()), std::make_move_iterator(outputs->end()));
    }

    get_core_op()->set_vstreams_multiplexer_callbacks(vstreams);

    return vstreams;
}

} /* namespace hailort */
