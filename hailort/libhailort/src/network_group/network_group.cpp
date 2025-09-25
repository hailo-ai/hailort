/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file network_group.cpp
 * @brief: Configured Network Group and Activated Network Group
 **/

#include "hailo/hailort.h"
#include "hailo/transform.hpp"
#include "hailo/vstream.hpp"
#include "hailo/hailort_defaults.hpp"

#include "common/utils.hpp"
#include "common/runtime_statistics_internal.hpp"
#include "common/os_utils.hpp"

#include "network_group/network_group_internal.hpp"
#include "eth/eth_stream.hpp"
#include "vdma/vdma_stream.hpp"
#include "mipi/mipi_stream.hpp"
#include "device_common/control.hpp"
#include "net_flow/pipeline/vstream_builder.hpp"
#include "net_flow/ops_metadata/yolov5_seg_op_metadata.hpp"
#include "core_op/resource_manager/resource_manager.hpp"
#include "utils/buffer_storage.hpp"
#include "hef/hef_internal.hpp"

#ifdef HAILO_SUPPORT_MULTI_PROCESS
#include "service/network_group_client.hpp"
#endif // HAILO_SUPPORT_MULTI_PROCESS

namespace hailort
{

class ActivatedNetworkGroupImpl : public ActivatedNetworkGroup {
public:

    static Expected<std::unique_ptr<ActivatedNetworkGroup>> create(ConfiguredNetworkGroupBase &cng)
    {
        auto status = HAILO_UNINITIALIZED;
        std::unique_ptr<ActivatedNetworkGroup> ang = make_unique_nothrow<ActivatedNetworkGroupImpl>(cng, status);
        CHECK_NOT_NULL_AS_EXPECTED(ang, HAILO_OUT_OF_HOST_MEMORY);
        if (HAILO_STREAM_ABORT == status) {
            LOGGER__ERROR("Network group activation failed because some of the low level streams are aborted. Make sure to run clear_abort before activating!");
            return make_unexpected(status);
        }
        CHECK_SUCCESS_AS_EXPECTED(status);
        return ang;
    }

    virtual ~ActivatedNetworkGroupImpl()
    {
        if (m_is_activated) {
            auto status = m_cng.deactivate_impl();
            if (HAILO_SUCCESS != status) {
                LOGGER__ERROR("Failed deactivate {}", status);
            }
            m_is_activated = false;
        }
    }

    ActivatedNetworkGroupImpl(const ActivatedNetworkGroupImpl &) = delete;
    ActivatedNetworkGroupImpl &operator=(const ActivatedNetworkGroupImpl &) = delete;
    ActivatedNetworkGroupImpl(ActivatedNetworkGroupImpl &&) = delete;
    ActivatedNetworkGroupImpl &operator=(ActivatedNetworkGroupImpl &&) = delete;

    virtual const std::string &get_network_group_name() const override
    {
        return m_cng.get_network_group_name();
    }

    virtual Expected<Buffer> get_intermediate_buffer(const IntermediateBufferKey &key) override
    {
        return m_cng.get_intermediate_buffer(key);
    }

    virtual uint32_t get_invalid_frames_count() override
    {
        uint32_t total_invalid_frames_count = 0;
        for (auto& output_stream : m_cng.get_output_streams()) {
            total_invalid_frames_count += output_stream.get().get_invalid_frames_count();
        }
        return total_invalid_frames_count;
    }

    ActivatedNetworkGroupImpl(ConfiguredNetworkGroupBase &cng, hailo_status &status) :
        m_cng(cng)
    {
        auto activate_status = m_cng.activate_impl();
        if (HAILO_STREAM_ABORT == activate_status) {
            LOGGER__INFO("Network group activation failed because it was aborted by user");
            status = activate_status;
            return;
        }
        if (HAILO_SUCCESS != activate_status) {
            LOGGER__ERROR("Failed activate {}", activate_status);
            status = activate_status;
            return;
        }

        m_is_activated = true;
        status = HAILO_SUCCESS;
    }

private:
    ConfiguredNetworkGroupBase &m_cng;
    bool m_is_activated = false;
};

ConfiguredNetworkGroup::ConfiguredNetworkGroup() :
    m_infer_requests_mutex(),
    m_ongoing_transfers(0),
    m_cv()
{}

Expected<std::shared_ptr<ConfiguredNetworkGroup>> ConfiguredNetworkGroup::duplicate_network_group_client(uint32_t ng_handle, uint32_t vdevice_handle,
    const std::string &network_group_name)
{
#ifdef HAILO_SUPPORT_MULTI_PROCESS
    auto net_group_client = ConfiguredNetworkGroupClient::duplicate_network_group_client(ng_handle, vdevice_handle, network_group_name);
    CHECK_EXPECTED(net_group_client);

    return std::shared_ptr<ConfiguredNetworkGroup>(net_group_client.release());
#else
    (void)ng_handle;
    (void)vdevice_handle;
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

Expected<uint32_t> ConfiguredNetworkGroup::get_vdevice_client_handle() const
{
    LOGGER__ERROR("`get_vdevice_client_handle()` is valid only when working with HailoRT Service!");
    return make_unexpected(HAILO_INVALID_OPERATION);
}

hailo_status ConfiguredNetworkGroup::before_fork()
{
    return HAILO_SUCCESS;
}

hailo_status ConfiguredNetworkGroup::after_fork_in_parent()
{
    return HAILO_SUCCESS;
}

hailo_status ConfiguredNetworkGroup::after_fork_in_child()
{
    return HAILO_SUCCESS;
}

Expected<std::unique_ptr<ActivatedNetworkGroup>> ConfiguredNetworkGroup::activate()
{
    return activate(HailoRTDefaults::get_active_network_group_params());
}

hailo_status ConfiguredNetworkGroup::wait_for_ongoing_callbacks_count_under(const size_t threshold)
{
    std::unique_lock<std::mutex> lock(m_infer_requests_mutex);
    bool done = m_cv.wait_for(lock, DEFAULT_TRANSFER_TIMEOUT, [&, threshold](){
        return (m_ongoing_transfers.load() < threshold);
    });
    CHECK(done, HAILO_TIMEOUT);

    return HAILO_SUCCESS;
}

void ConfiguredNetworkGroup::decrease_ongoing_callbacks()
{
    {
        std::unique_lock<std::mutex> lock(m_infer_requests_mutex);
        m_ongoing_transfers--;
    }
    m_cv.notify_all();
}

void ConfiguredNetworkGroup::increase_ongoing_callbacks()
{
    std::unique_lock<std::mutex> lock(m_infer_requests_mutex);
    m_ongoing_transfers++;
}

Expected<std::shared_ptr<ConfiguredNetworkGroupBase>> ConfiguredNetworkGroupBase::create(const ConfigureNetworkParams &config_params,
    std::vector<std::shared_ptr<CoreOp>> &&core_ops, NetworkGroupMetadata &&metadata)
{
    auto net_group_ptr = std::shared_ptr<ConfiguredNetworkGroupBase>(new (std::nothrow)
        ConfiguredNetworkGroupBase(config_params, std::move(core_ops), std::move(metadata)));
    CHECK_NOT_NULL_AS_EXPECTED(net_group_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return net_group_ptr;
}

Expected<std::unique_ptr<ActivatedNetworkGroup>> ConfiguredNetworkGroupBase::activate(
    const hailo_activate_network_group_params_t &network_group_params)
{
    // Params are reserved for later use.
    (void)network_group_params;
    return ActivatedNetworkGroupImpl::create(*this);
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

Expected<std::vector<net_flow::PostProcessOpMetadataPtr>> ConfiguredNetworkGroupBase::get_ops_metadata()
{
    return std::vector<net_flow::PostProcessOpMetadataPtr>(m_network_group_metadata.m_ops_metadata);
}

Expected<std::unique_ptr<LayerInfo>> ConfiguredNetworkGroupBase::get_layer_info(const std::string &stream_name)
{
    auto layer_info = get_core_op()->get_layer_info(stream_name);
    CHECK_EXPECTED(layer_info);
    auto res = make_unique_nothrow<LayerInfo>(layer_info.release());
    CHECK_NOT_NULL_AS_EXPECTED(res, HAILO_OUT_OF_HOST_MEMORY);
    return res;
}

Expected<net_flow::PostProcessOpMetadataPtr> ConfiguredNetworkGroupBase::get_op_meta_data(const std::string &edge_name)
{
    auto expected_ops_metadata = get_ops_metadata();
    CHECK_EXPECTED(expected_ops_metadata);
    auto ops_metadata = expected_ops_metadata.release();

    auto matching_metadata = std::find_if(ops_metadata.begin(), ops_metadata.end(),
        [&edge_name] (const auto &metadata) {
            for (const auto &metadata_output_pair : metadata->outputs_metadata()) {
                if (metadata_output_pair.first == edge_name) {
                    return true;
                }
            }
            return false;
        });
    CHECK_AS_EXPECTED(matching_metadata != ops_metadata.end(), HAILO_INVALID_ARGUMENT,
        "There is no post-process metadata for '{}'", edge_name);
    auto metadata = (*matching_metadata);
    return metadata;
}

Expected<std::shared_ptr<net_flow::NmsOpMetadata>> ConfiguredNetworkGroupBase::get_nms_meta_data(const std::string &edge_name)
{
    auto matching_metadata = get_op_meta_data(edge_name);
    CHECK_EXPECTED(matching_metadata);

    auto nms_metadata = std::dynamic_pointer_cast<net_flow::NmsOpMetadata>(*matching_metadata);
    CHECK((nms_metadata != nullptr), HAILO_INVALID_ARGUMENT,
        "Failed to get nms metadata for `{}`. Op's metadata is not nms metadata", edge_name);
    return nms_metadata;
}

hailo_status ConfiguredNetworkGroupBase::set_nms_score_threshold(const std::string &edge_name, float32_t nms_score_threshold)
{
    auto expected_nms_op_metadata = get_nms_meta_data(edge_name);
    CHECK_EXPECTED_AS_STATUS(expected_nms_op_metadata);
    expected_nms_op_metadata.value()->nms_config().nms_score_th = nms_score_threshold;
    return HAILO_SUCCESS;
}

hailo_status ConfiguredNetworkGroupBase::set_nms_iou_threshold(const std::string &edge_name, float32_t iou_threshold)
{
    auto expected_nms_op_metadata = get_nms_meta_data(edge_name);
    CHECK_EXPECTED_AS_STATUS(expected_nms_op_metadata);
    expected_nms_op_metadata.value()->nms_config().nms_iou_th = iou_threshold;
    return HAILO_SUCCESS;
}

hailo_status ConfiguredNetworkGroupBase::set_nms_max_bboxes_per_class(const std::string &edge_name, uint32_t max_bboxes_per_class)
{
    auto expected_nms_op_metadata = get_nms_meta_data(edge_name);
    CHECK_EXPECTED_AS_STATUS(expected_nms_op_metadata);
    expected_nms_op_metadata.value()->nms_config().max_proposals_per_class = max_bboxes_per_class;
    return HAILO_SUCCESS;
}

hailo_status ConfiguredNetworkGroupBase::set_nms_max_bboxes_total(const std::string &edge_name, uint32_t max_bboxes_total)
{
    auto expected_nms_op_metadata = get_nms_meta_data(edge_name);
    CHECK_EXPECTED_AS_STATUS(expected_nms_op_metadata);
    expected_nms_op_metadata.value()->nms_config().max_proposals_total = max_bboxes_total;
    return HAILO_SUCCESS;
}

hailo_status ConfiguredNetworkGroupBase::set_nms_max_accumulated_mask_size(const std::string &edge_name, uint32_t max_accumulated_mask_size)
{
    auto expected_op_metadata = get_op_meta_data(edge_name);
    CHECK_EXPECTED_AS_STATUS(expected_op_metadata);

    auto nms_metadata = std::dynamic_pointer_cast<net_flow::Yolov5SegOpMetadata>(expected_op_metadata.value());
    CHECK((nms_metadata != nullptr), HAILO_INVALID_ARGUMENT,
        "Failed to `set_nms_max_accumulated_mask_size` for `{}`. Op's metadata is not YOLOv5-Seg metadata", edge_name);

    nms_metadata->yolov5seg_config().max_accumulated_mask_size = max_accumulated_mask_size;
    return HAILO_SUCCESS;
}

ConfiguredNetworkGroupBase::ConfiguredNetworkGroupBase(
    const ConfigureNetworkParams &config_params, std::vector<std::shared_ptr<CoreOp>> &&core_ops,
    NetworkGroupMetadata &&metadata) :
        ConfiguredNetworkGroup(),
        m_config_params(config_params),
        m_core_ops(std::move(core_ops)),
        m_network_group_metadata(std::move(metadata)),
        m_is_forked(false)
{}

const std::string &ConfiguredNetworkGroupBase::get_network_group_name() const
{
    return m_network_group_metadata.name();
}

const std::string &ConfiguredNetworkGroupBase::name() const
{
    return m_network_group_metadata.name();
}

hailo_status ConfiguredNetworkGroupBase::activate_low_level_streams()
{
    return get_core_op()->activate_low_level_streams();
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

hailo_status ConfiguredNetworkGroupBase::activate_impl(uint16_t dynamic_batch_size)
{
    return get_core_op()->activate(dynamic_batch_size);
}

hailo_status ConfiguredNetworkGroupBase::deactivate_impl()
{
    return get_core_op()->deactivate();
}

hailo_status ConfiguredNetworkGroupBase::shutdown()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (!m_is_shutdown) {
        m_is_shutdown = true;
        return get_core_op()->shutdown();
    }
    return HAILO_SUCCESS;
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
    bool /*unused*/, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size)
{
    auto params = make_output_vstream_params({}, format_type, timeout_ms, queue_size);
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
    bool /*unused*/, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
    const std::string &network_name)
{
    auto input_vstream_infos = m_network_group_metadata.get_input_vstream_infos(network_name);
    CHECK_EXPECTED(input_vstream_infos);

    std::map<std::string, hailo_vstream_params_t> res;
    auto status = Hef::Impl::fill_missing_vstream_params_with_default(res, input_vstream_infos.value(),
        format_type, timeout_ms, queue_size);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return res;
}

Expected<std::map<std::string, hailo_vstream_params_t>> ConfiguredNetworkGroupBase::make_output_vstream_params(
    bool /*unused*/, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
    const std::string &network_name)
{
    auto output_vstream_infos = m_network_group_metadata.get_output_vstream_infos(network_name);
    CHECK_EXPECTED(output_vstream_infos);
    std::map<std::string, hailo_vstream_params_t> res;
    auto status = Hef::Impl::fill_missing_vstream_params_with_default(res, output_vstream_infos.value(), 
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
        std::vector<std::shared_ptr<InputStreamBase>> streams;
        auto &vstream_name = name_params_pair.first;
        auto vstream_params = name_params_pair.second;

        auto stream_names = m_network_group_metadata.get_stream_names_from_vstream_name(vstream_name);
        CHECK_EXPECTED(stream_names);

        const auto vstream_info = input_vstream_infos_map.find(vstream_name);
        CHECK_AS_EXPECTED(vstream_info != input_vstream_infos_map.end(), HAILO_NOT_FOUND,
            "Failed to find vstream info of {}", vstream_name);

        for (const auto &stream_name : stream_names.value()){
            auto input_stream_expected = get_shared_input_stream_by_name(stream_name);
            CHECK_EXPECTED(input_stream_expected);

            auto input_stream = input_stream_expected.release();
            streams.push_back(input_stream);
        }

        if (streams.size() > 1) {
            auto expanded_user_buffer_format =
                VStreamsBuilderUtils::expand_user_buffer_format_autos_multi_planar(vstream_info->second, vstream_params.user_buffer_format);
            vstream_params.user_buffer_format = expanded_user_buffer_format;
        } else {
            vstream_params = expand_vstream_params_autos(streams.back()->get_info(), vstream_params);
        }
        auto inputs = VStreamsBuilderUtils::create_inputs(streams, vstream_info->second, vstream_params);
        CHECK_EXPECTED(inputs);

        vstreams.insert(vstreams.end(), std::make_move_iterator(inputs->begin()), std::make_move_iterator(inputs->end()));
    }
    return vstreams;
}

Expected<std::vector<OutputVStream>> ConfiguredNetworkGroupBase::create_output_vstreams(const std::map<std::string, hailo_vstream_params_t> &vstreams_params)
{
    std::vector<OutputVStream> vstreams;
    vstreams.reserve(vstreams_params.size());

    for (const auto &vstream_param : vstreams_params) {
        if (HAILO_FORMAT_ORDER_HAILO_NMS_BY_SCORE == vstream_param.second.user_buffer_format.order) {
            LOGGER__ERROR("Output format order HAILO_NMS_BY_SCORE is not supported for vstreams.");
            return make_unexpected(HAILO_NOT_SUPPORTED);
        }
    }

    auto all_output_streams_expected = get_output_streams_from_vstream_names(vstreams_params);
    CHECK_EXPECTED(all_output_streams_expected);
    auto all_output_streams = all_output_streams_expected.release();
    auto output_vstream_infos = get_output_vstream_infos();
    CHECK_EXPECTED(output_vstream_infos);
    auto output_vstream_infos_map = vstream_infos_vector_to_map(output_vstream_infos.release());

    // Building DBs that connect output_vstreams, output_streams and ops.
    // Note: Assuming each post process op has a unique output streams.
    //       In other words, not possible for an output stream to be connected to more than one op
    std::unordered_map<std::string, net_flow::PostProcessOpMetadataPtr> post_process_metadata;
    std::unordered_map<stream_name_t, op_name_t> op_inputs_to_op_name;
    for (auto &metadata : m_network_group_metadata.m_ops_metadata) {
        post_process_metadata.insert({metadata->get_name(), metadata});
        for (auto &input_name : metadata->get_input_names()) {
            op_inputs_to_op_name.insert({input_name, metadata->get_name()});
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
            post_process_metadata, op_inputs_to_op_name, output_vstream_infos_map);
        CHECK_EXPECTED(outputs);
        vstreams.insert(vstreams.end(), std::make_move_iterator(outputs->begin()), std::make_move_iterator(outputs->end()));
    }

    return vstreams;
}

hailo_status ConfiguredNetworkGroupBase::before_fork()
{
    // On fork, we wrap the streams object with some wrapper allowing multi process
    // support.
    if (!m_is_forked) {
        for (auto &core_op : m_core_ops) {
            auto status = core_op->wrap_streams_for_remote_process();
            CHECK_SUCCESS(status);
        }
        m_is_forked = true;
    }

    return HAILO_SUCCESS;
}

Expected<Buffer> ConfiguredNetworkGroupBase::get_intermediate_buffer(const IntermediateBufferKey &key)
{
    return get_core_op()->get_intermediate_buffer(key);
}

Expected<size_t> ConfiguredNetworkGroupBase::infer_queue_size() const
{
    return get_core_op()->infer_queue_size();
}

hailo_status ConfiguredNetworkGroupBase::infer_async(const NamedBuffersCallbacks &named_buffers_callbacks,
    const std::function<void(hailo_status)> &infer_request_done_cb)
{
    InferRequest infer_request{};
    for (auto &named_buffer_callback : named_buffers_callbacks) {
        const auto &name = named_buffer_callback.first;
        const auto &callback = named_buffer_callback.second.second;
        if (BufferType::VIEW == named_buffer_callback.second.first.buffer_type) {
            const auto &buffer = named_buffer_callback.second.first.view;
            infer_request.transfers.emplace(name, TransferRequest{buffer, callback});
        } else if (BufferType::DMA_BUFFER == named_buffer_callback.second.first.buffer_type) {
            const auto &dma_buffer = named_buffer_callback.second.first.dma_buffer;
            infer_request.transfers.emplace(name, TransferRequest{dma_buffer, callback});
        } else {
            LOGGER__ERROR("infer_async does not support buffers with type {}", static_cast<int>(named_buffer_callback.second.first.buffer_type));
            return HAILO_INVALID_ARGUMENT;
        }
    }
    infer_request.callback = [this, infer_request_done_cb](hailo_status status){
        if (status == HAILO_STREAM_ABORT) {
            LOGGER__INFO("Infer request was aborted by user");
        }
        else if (status != HAILO_SUCCESS) {
            LOGGER__ERROR("Infer request callback failed with status = {}", status);
        }

        infer_request_done_cb(status);
        decrease_ongoing_callbacks();
    };

    increase_ongoing_callbacks(); // Increase before lunch, as the cb may be called before we got the chance to increase the counter
    std::unique_lock<std::mutex> lock(m_mutex);
    auto status = get_core_op()->infer_async(std::move(infer_request));
    if (status != HAILO_SUCCESS) {
        // If we got error in `infer_async()`, then the callbacks will not be called.
        decrease_ongoing_callbacks();
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

Expected<uint32_t> ConfiguredNetworkGroupBase::get_cache_length() const
{
    CHECK(m_core_ops.size() == 1, HAILO_INVALID_OPERATION,
        "get_cache_length() is not supported for multi core-op network groups");

    return m_core_ops[0]->get_cache_length();

}

Expected<uint32_t> ConfiguredNetworkGroupBase::get_cache_read_length() const
{
    CHECK(m_core_ops.size() == 1, HAILO_INVALID_OPERATION,
        "get_cache_read_length() is not supported for multi core-op network groups");

    return m_core_ops[0]->get_cache_read_length();

}

Expected<uint32_t> ConfiguredNetworkGroupBase::get_cache_write_length() const
{
    CHECK(m_core_ops.size() == 1, HAILO_INVALID_OPERATION,
        "get_cache_write_length() is not supported for multi core-op network groups");

    return m_core_ops[0]->get_cache_write_length();
}

Expected<uint32_t> ConfiguredNetworkGroupBase::get_cache_entry_size(uint32_t cache_id) const
{
    CHECK(m_core_ops.size() == 1, HAILO_INVALID_OPERATION,
        "get_cache_entry_size() is not supported for multi core-op network groups");

    return m_core_ops[0]->get_cache_entry_size(cache_id);
}

hailo_status ConfiguredNetworkGroupBase::init_cache(uint32_t read_offset)
{
    CHECK(m_core_ops.size() == 1, HAILO_INVALID_OPERATION,
        "init_cache() is not supported for multi core-op network groups");

    return m_core_ops[0]->init_cache(read_offset);
}

hailo_status ConfiguredNetworkGroupBase::update_cache_offset(int32_t offset_delta_entries)
{
    CHECK(m_core_ops.size() == 1, HAILO_INVALID_OPERATION,
        "update_cache_offset() is not supported for multi core-op network groups");

    return m_core_ops[0]->update_cache_offset(offset_delta_entries);
}

Expected<std::vector<uint32_t>> ConfiguredNetworkGroupBase::get_cache_ids() const
{
    CHECK(m_core_ops.size() == 1, HAILO_INVALID_OPERATION,
        "get_cache_ids() is not supported for multi core-op network groups");

    return m_core_ops[0]->get_cache_ids();
}

Expected<Buffer> ConfiguredNetworkGroupBase::read_cache_buffer(uint32_t cache_id)
{
    CHECK(m_core_ops.size() == 1, HAILO_INVALID_OPERATION,
        "read_cache_buffer() is not supported for multi core-op network groups");

    return m_core_ops[0]->read_cache_buffer(cache_id);
}

hailo_status ConfiguredNetworkGroupBase::write_cache_buffer(uint32_t cache_id, MemoryView buffer)
{
    CHECK(m_core_ops.size() == 1, HAILO_INVALID_OPERATION,
        "write_cache_buffer() is not supported for multi core-op network groups");

    return m_core_ops[0]->write_cache_buffer(cache_id, buffer);
}

} /* namespace hailort */
