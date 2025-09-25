/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file network_group_client.cpp
 * @brief: Network group client object
 **/

#include "network_group_client.hpp"

#include "hailo/vstream.hpp"
#include "hailo/hailort_defaults.hpp"

#include "common/utils.hpp"
#include "common/os_utils.hpp"
#include "common/shared_memory_buffer.hpp"
#include "common/internal_env_vars.hpp"
#include "utils/buffer_storage.hpp"

#include "net_flow/pipeline/vstream_builder.hpp"
#include "net_flow/ops/nms_post_process.hpp"
#include "rpc_client_utils.hpp"

namespace hailort
{

static bool should_use_shared_memory()
{
    return ((!is_env_variable_on(HAILO_SERVICE_SHARED_MEMORY_ENV_VAR)) &&
        (!get_env_variable(HAILORT_SERVICE_ADDRESS_ENV_VAR)));
}

Expected<std::shared_ptr<ConfiguredNetworkGroupClient>> ConfiguredNetworkGroupClient::create(
    std::unique_ptr<HailoRtRpcClient> client, NetworkGroupIdentifier &&identifier)
{
    TRY(auto ng_name, client->ConfiguredNetworkGroup_name(identifier));
    TRY(auto streams_infos, client->ConfiguredNetworkGroup_get_all_stream_infos(identifier, ng_name));
    TRY(auto min_buffer_pool_size, client->ConfiguredNetworkGroup_infer_queue_size(identifier));

    std::unordered_set<stream_name_t> input_streams_names;
    std::unordered_set<stream_name_t> output_streams_names;
    TRY(auto cng_buffer_pool, BufferPoolPerStream::create());
    for (auto &stream_info : streams_infos) {
        if (should_use_shared_memory()) {
            cng_buffer_pool->allocate_pool(stream_info.name, identifier, stream_info.hw_frame_size, min_buffer_pool_size);
        }

        if (stream_info.direction == HAILO_H2D_STREAM) {
            input_streams_names.insert(stream_info.name);
        } else {
            output_streams_names.insert(stream_info.name);
        }
    }

    auto network_group_ptr = make_shared_nothrow<ConfiguredNetworkGroupClient>(std::move(client),
        std::move(identifier), ng_name, std::move(input_streams_names), std::move(output_streams_names),
        cng_buffer_pool);
    CHECK_NOT_NULL_AS_EXPECTED(network_group_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return network_group_ptr;
}

ConfiguredNetworkGroupClient::ConfiguredNetworkGroupClient(std::unique_ptr<HailoRtRpcClient> client,
    NetworkGroupIdentifier &&identifier, const std::string &network_group_name,
        std::unordered_set<std::string> &&input_streams_names, std::unordered_set<std::string> &&output_streams_names,
        std::shared_ptr<BufferPoolPerStream> buffer_pool_per_stream) :
    ConfiguredNetworkGroup(),
    m_client(std::move(client)),
    m_identifier(identifier),
    m_network_group_name(network_group_name),
    m_current_cb_index(0),
    m_input_streams_names(std::move(input_streams_names)),
    m_output_streams_names(std::move(output_streams_names)),
    m_buffer_pool_per_stream(std::move(buffer_pool_per_stream)),
    m_is_shutdown(false)
{}

ConfiguredNetworkGroupClient::ConfiguredNetworkGroupClient(NetworkGroupIdentifier &&identifier, const std::string &network_group_name) :
    ConfiguredNetworkGroup(),
    m_identifier(identifier),
    m_network_group_name(network_group_name),
    m_current_cb_index(0),
    m_is_shutdown(false)
{}

Expected<std::shared_ptr<ConfiguredNetworkGroupClient>> ConfiguredNetworkGroupClient::duplicate_network_group_client(uint32_t ng_handle, uint32_t vdevice_handle,
    const std::string &network_group_name)
{
    auto duplicated_net_group = std::shared_ptr<ConfiguredNetworkGroupClient>(new (std::nothrow)
        ConfiguredNetworkGroupClient(NetworkGroupIdentifier(ng_handle, vdevice_handle), network_group_name));
    CHECK_ARG_NOT_NULL_AS_EXPECTED(duplicated_net_group);

    auto status = duplicated_net_group->create_client();
    CHECK_SUCCESS_AS_EXPECTED(status);

    status = duplicated_net_group->dup_handle();
    CHECK_SUCCESS_AS_EXPECTED(status);

    return duplicated_net_group;
}

ConfiguredNetworkGroupClient::~ConfiguredNetworkGroupClient()
{
    auto reply = m_client->ConfiguredNetworkGroup_release(m_identifier, OsUtils::get_curr_pid());
    if (reply != HAILO_SUCCESS) {
        LOGGER__CRITICAL("ConfiguredNetworkGroup_release failed with status: {}", reply);
    }
    execute_callbacks_on_error(HAILO_INTERNAL_FAILURE); // At this point there should'nt be any callbacks left. if there are any, raise HAILO_INTERNAL_FAILURE

    auto status = wait_for_ongoing_callbacks_count_under(1);
    if (HAILO_SUCCESS != status) {
        LOGGER__CRITICAL("Failed to wait for callbacks to finish");
    }

    status = m_buffer_pool_per_stream->shutdown();
    if (HAILO_SUCCESS != status) {
        LOGGER__CRITICAL("Failed to shutdown for network group buffers pool");
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

    auto expected_dup_handle = m_client->ConfiguredNetworkGroup_dup_handle(m_identifier, OsUtils::get_curr_pid());
    CHECK_EXPECTED_AS_STATUS(expected_dup_handle);

    return HAILO_SUCCESS;
}

hailo_status ConfiguredNetworkGroupClient::dup_handle()
{
    auto expected_dup_handle = m_client->ConfiguredNetworkGroup_dup_handle(m_identifier, OsUtils::get_curr_pid());
    CHECK_EXPECTED_AS_STATUS(expected_dup_handle);

    return HAILO_SUCCESS;
}

Expected<std::unique_ptr<ActivatedNetworkGroup>> ConfiguredNetworkGroupClient::activate(
    const hailo_activate_network_group_params_t &/* network_group_params */)
{
    LOGGER__WARNING("ConfiguredNetworkGroup::activate function is not supported when using multi-process service or HailoRT Scheduler.");
    return make_unexpected(HAILO_INVALID_OPERATION);
}

/* Network group base functions */
Expected<LatencyMeasurementResult> ConfiguredNetworkGroupClient::get_latency_measurement(const std::string &network_name)
{
    return m_client->ConfiguredNetworkGroup_get_latency_measurement(m_identifier, network_name);
}

const std::string& ConfiguredNetworkGroupClient::get_network_group_name() const
{
    return m_network_group_name;
}

const std::string& ConfiguredNetworkGroupClient::name() const
{
    return m_network_group_name;
}

Expected<hailo_stream_interface_t> ConfiguredNetworkGroupClient::get_default_streams_interface()
{
    return m_client->ConfiguredNetworkGroup_get_default_stream_interface(m_identifier);
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
    return HAILO_INVALID_OPERATION;
}

hailo_status ConfiguredNetworkGroupClient::shutdown()
{
    std::unique_lock<std::mutex> lock_shutdown(m_shutdown_mutex);
    m_is_shutdown = true;

    auto status = m_client->ConfiguredNetworkGroup_shutdown(m_identifier);
    CHECK_SUCCESS(status, "Failed to shutdown");

    status = wait_for_ongoing_callbacks_count_under(1);
    CHECK_SUCCESS(status, "Failed to wait for callbacks to finish");

    status = m_buffer_pool_per_stream->shutdown();
    if (HAILO_SUCCESS != status) {
        LOGGER__CRITICAL("Failed to shutdown for network group buffers pool");
    }

    return status;
}

Expected<std::vector<std::vector<std::string>>> ConfiguredNetworkGroupClient::get_output_vstream_groups()
{
    return m_client->ConfiguredNetworkGroup_get_output_vstream_groups(m_identifier);
}

Expected<std::vector<std::map<std::string, hailo_vstream_params_t>>> ConfiguredNetworkGroupClient::make_output_vstream_params_groups(
    bool /*unused*/, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size)
{
    return m_client->ConfiguredNetworkGroup_make_output_vstream_params_groups(m_identifier,
        format_type, timeout_ms, queue_size);
}

Expected<std::map<std::string, hailo_vstream_params_t>> ConfiguredNetworkGroupClient::make_input_vstream_params(
    bool /*unused*/, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
    const std::string &network_name)
{
    return m_client->ConfiguredNetworkGroup_make_input_vstream_params(m_identifier,
        format_type, timeout_ms, queue_size, network_name);
}

Expected<std::map<std::string, hailo_vstream_params_t>> ConfiguredNetworkGroupClient::make_output_vstream_params(
    bool /*unused*/, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
    const std::string &network_name)
{
    return m_client->ConfiguredNetworkGroup_make_output_vstream_params(m_identifier,
        format_type, timeout_ms, queue_size, network_name);
}

Expected<std::vector<hailo_stream_info_t>> ConfiguredNetworkGroupClient::get_all_stream_infos(const std::string &network_name) const
{
    return m_client->ConfiguredNetworkGroup_get_all_stream_infos(m_identifier, network_name);
}

Expected<std::vector<hailo_network_info_t>> ConfiguredNetworkGroupClient::get_network_infos() const
{
    return m_client->ConfiguredNetworkGroup_get_network_infos(m_identifier);
}

Expected<std::vector<hailo_vstream_info_t>> ConfiguredNetworkGroupClient::get_input_vstream_infos(
    const std::string &network_name) const
{
    return m_client->ConfiguredNetworkGroup_get_input_vstream_infos(m_identifier, network_name);
}

Expected<std::vector<hailo_vstream_info_t>> ConfiguredNetworkGroupClient::get_output_vstream_infos(
    const std::string &network_name) const
{
    return m_client->ConfiguredNetworkGroup_get_output_vstream_infos(m_identifier, network_name);
}

Expected<std::vector<hailo_vstream_info_t>> ConfiguredNetworkGroupClient::get_all_vstream_infos(
    const std::string &network_name) const
{
    return m_client->ConfiguredNetworkGroup_get_all_vstream_infos(m_identifier, network_name);
}

bool ConfiguredNetworkGroupClient::is_scheduled() const
{
    auto reply = m_client->ConfiguredNetworkGroup_is_scheduled(m_identifier);
    if (reply.status() != HAILO_SUCCESS) {
        LOGGER__ERROR("is_scheduled failed with status {}", reply.status());
        return false;
    }
    return reply.value();
}

hailo_status ConfiguredNetworkGroupClient::set_scheduler_timeout(const std::chrono::milliseconds &timeout, const std::string &network_name)
{
    return m_client->ConfiguredNetworkGroup_set_scheduler_timeout(m_identifier, timeout, network_name);
}

hailo_status ConfiguredNetworkGroupClient::set_scheduler_threshold(uint32_t threshold, const std::string &network_name)
{
    return m_client->ConfiguredNetworkGroup_set_scheduler_threshold(m_identifier, threshold, network_name);
}

hailo_status ConfiguredNetworkGroupClient::set_scheduler_priority(uint8_t priority, const std::string &network_name)
{
    return m_client->ConfiguredNetworkGroup_set_scheduler_priority(m_identifier, priority, network_name);
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
    auto reply = m_client->ConfiguredNetworkGroup_is_multi_context(m_identifier);
    if (reply.status() != HAILO_SUCCESS) {
        LOGGER__ERROR("is_multi_context failed with status {}", reply.status());
        return false;
    }
    return reply.value();
}

Expected<HwInferResults> ConfiguredNetworkGroupClient::run_hw_infer_estimator()
{
    LOGGER__ERROR("ConfiguredNetworkGroupClient::run_hw_infer_estimator function is not supported when using multi-process service.");
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

const ConfigureNetworkParams ConfiguredNetworkGroupClient::get_config_params() const
{
    auto reply = m_client->ConfiguredNetworkGroup_get_config_params(m_identifier);
    if (reply.status() != HAILO_SUCCESS) {
        LOGGER__ERROR("get_config_params failed with status {}", reply.status());
        return ConfigureNetworkParams();
    }
    return reply.value();
}

Expected<std::vector<std::string>> ConfiguredNetworkGroupClient::get_sorted_output_names()
{
    return m_client->ConfiguredNetworkGroup_get_sorted_output_names(m_identifier);
}

Expected<std::vector<std::string>> ConfiguredNetworkGroupClient::get_stream_names_from_vstream_name(const std::string &vstream_name)
{
    return m_client->ConfiguredNetworkGroup_get_stream_names_from_vstream_name(m_identifier, vstream_name);
}

Expected<std::vector<std::string>> ConfiguredNetworkGroupClient::get_vstream_names_from_stream_name(const std::string &stream_name)
{
    return m_client->ConfiguredNetworkGroup_get_vstream_names_from_stream_name(m_identifier, stream_name);
}

Expected<std::vector<InputVStream>> ConfiguredNetworkGroupClient::create_input_vstreams(const std::map<std::string, hailo_vstream_params_t> &inputs_params)
{
    auto reply = m_client->InputVStreams_create(m_identifier, inputs_params, OsUtils::get_curr_pid());
    CHECK_EXPECTED(reply);
    auto input_vstreams_names_to_handles = reply.release();
    std::vector<InputVStream> vstreams;
    vstreams.reserve(input_vstreams_names_to_handles.size());

    for(const auto &name_handle_pair : input_vstreams_names_to_handles) {
        auto timeout = std::chrono::milliseconds(inputs_params.at(name_handle_pair.first).timeout_ms);
        auto vstream_client = InputVStreamClient::create(VStreamIdentifier(m_identifier, name_handle_pair.second), timeout);
        CHECK_EXPECTED(vstream_client);
        auto vstream = VStreamsBuilderUtils::create_input(vstream_client.release());
        vstreams.push_back(std::move(vstream));
    }
    return vstreams;
}

Expected<std::vector<OutputVStream>> ConfiguredNetworkGroupClient::create_output_vstreams(const std::map<std::string, hailo_vstream_params_t> &outputs_params)
{
    auto reply = m_client->OutputVStreams_create(m_identifier, outputs_params, OsUtils::get_curr_pid());
    CHECK_EXPECTED(reply);
    auto output_vstreams_names_to_handles = reply.release();
    std::vector<OutputVStream> vstreams;
    vstreams.reserve(output_vstreams_names_to_handles.size());

    for(const auto &name_handle_pair : output_vstreams_names_to_handles) {
        auto timeout = std::chrono::milliseconds(outputs_params.at(name_handle_pair.first).timeout_ms);
        auto vstream_client = OutputVStreamClient::create(VStreamIdentifier(m_identifier, name_handle_pair.second), timeout);
        CHECK_EXPECTED(vstream_client);
        auto vstream = VStreamsBuilderUtils::create_output(vstream_client.release());
        vstreams.push_back(std::move(vstream));
    }
    return vstreams;
}

Expected<size_t> ConfiguredNetworkGroupClient::infer_queue_size() const
{
    return m_client->ConfiguredNetworkGroup_infer_queue_size(m_identifier);
}

Expected<std::unique_ptr<LayerInfo>> ConfiguredNetworkGroupClient::get_layer_info(const std::string &stream_name)
{
    return m_client->ConfiguredNetworkGroup_get_layer_info(m_identifier, stream_name);
}

Expected<std::vector<net_flow::PostProcessOpMetadataPtr>> ConfiguredNetworkGroupClient::get_ops_metadata()
{
    return m_client->ConfiguredNetworkGroup_get_ops_metadata(m_identifier);
}

hailo_status ConfiguredNetworkGroupClient::set_nms_score_threshold(const std::string &edge_name, float32_t nms_score_threshold)
{
    return m_client->ConfiguredNetworkGroup_set_nms_score_threshold(m_identifier, edge_name, nms_score_threshold);
}

hailo_status ConfiguredNetworkGroupClient::set_nms_iou_threshold(const std::string &edge_name, float32_t iou_threshold)
{
    return m_client->ConfiguredNetworkGroup_set_nms_iou_threshold(m_identifier, edge_name, iou_threshold);
}

hailo_status ConfiguredNetworkGroupClient::set_nms_max_bboxes_per_class(const std::string &edge_name, uint32_t max_bboxes_per_class)
{
    return m_client->ConfiguredNetworkGroup_set_nms_max_bboxes_per_class(m_identifier, edge_name, max_bboxes_per_class);
}

hailo_status ConfiguredNetworkGroupClient::set_nms_max_bboxes_total(const std::string &edge_name, uint32_t max_bboxes_total)
{
    return m_client->ConfiguredNetworkGroup_set_nms_max_bboxes_total(m_identifier, edge_name, max_bboxes_total);
}

hailo_status ConfiguredNetworkGroupClient::set_nms_max_accumulated_mask_size(const std::string &edge_name, uint32_t max_accumulated_mask_size)
{
    return m_client->ConfiguredNetworkGroup_set_nms_max_accumulated_mask_size(m_identifier, edge_name, max_accumulated_mask_size);
}

// TODO: support kv-cache over service (HRT-13968)
hailo_status ConfiguredNetworkGroupClient::init_cache(uint32_t /* read_offset */)
{
    return HAILO_NOT_IMPLEMENTED;
}

hailo_status ConfiguredNetworkGroupClient::update_cache_offset(int32_t /* offset_delta_entries */)
{
    return HAILO_NOT_IMPLEMENTED;
}

Expected<std::vector<uint32_t>> ConfiguredNetworkGroupClient::get_cache_ids() const
{
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

Expected<Buffer> ConfiguredNetworkGroupClient::read_cache_buffer(uint32_t)
{
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

hailo_status ConfiguredNetworkGroupClient::write_cache_buffer(uint32_t, MemoryView)
{
    return HAILO_NOT_IMPLEMENTED;
}

hailo_status ConfiguredNetworkGroupClient::execute_callback(const ProtoCallbackIdentifier &cb_id)
{
    if (cb_id.cb_type() == CALLBACK_TYPE_TRANSFER) {
        return execute_transfer_callback(cb_id);
    } else if (cb_id.cb_type() == CALLBACK_TYPE_INFER_REQUEST) {
        return execute_infer_request_callback(cb_id);
    } else {
        LOGGER__ERROR("Got invalid callback type = {}", cb_id.cb_type());
        return HAILO_INTERNAL_FAILURE;
    }
}

void ConfiguredNetworkGroupClient::execute_callbacks_on_error(hailo_status error_status)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    for (auto cb_pair : m_idx_to_callbacks) {
        cb_pair.second->m_callback(error_status);
    }
    m_idx_to_callbacks.clear();
    for (auto cb_pair : m_infer_request_idx_to_callbacks) {
        cb_pair.second(error_status);
    }
    m_infer_request_idx_to_callbacks.clear();
}

hailo_status ConfiguredNetworkGroupClient::execute_infer_request_callback(const ProtoCallbackIdentifier &cb_id)
{
    std::function<void(hailo_status)> cb;
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        CHECK(contains(m_infer_request_idx_to_callbacks, cb_id.cb_idx()), HAILO_NOT_FOUND, "Failed to find cb with index {}", cb_id.cb_idx());
        cb = m_infer_request_idx_to_callbacks.at(cb_id.cb_idx());
        m_infer_request_idx_to_callbacks.erase(cb_id.cb_idx());
    }
    cb(static_cast<hailo_status>(cb_id.status()));

    return HAILO_SUCCESS;
}

hailo_status ConfiguredNetworkGroupClient::copy_data_from_shm_buffer(StreamCbParamsPtr stream_callback, const ProtoCallbackIdentifier &cb_id)
{
    CHECK(cb_id.has_shared_memory_identifier(), HAILO_INVALID_OPERATION,
        "Shared memory env var '{}' is on but callback does not contain shared memory identifier",
        HAILO_SERVICE_SHARED_MEMORY_ENV_VAR);
    memcpy(stream_callback->m_user_mem_view.data(),
        stream_callback->m_acquired_shm_buffer->data(), stream_callback->m_acquired_shm_buffer->size());

    return HAILO_SUCCESS;
}

hailo_status ConfiguredNetworkGroupClient::execute_transfer_callback(const ProtoCallbackIdentifier &cb_id)
{
    StreamCbParamsPtr stream_callback;
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        CHECK(contains(m_idx_to_callbacks, cb_id.cb_idx()), HAILO_NOT_FOUND, "Failed to find cb with index {}", cb_id.cb_idx());
        stream_callback = m_idx_to_callbacks.at(cb_id.cb_idx());
        m_idx_to_callbacks.erase(cb_id.cb_idx());
    }

    const auto &stream_name = cb_id.stream_name();
    CHECK((stream_callback->m_stream_name == stream_name), HAILO_INTERNAL_FAILURE,
        "Callback identifier does not match stream name {}", stream_name);

    if (contains(m_output_streams_names, stream_callback->m_stream_name)) {
        if (should_use_shared_memory()) {
            auto status = copy_data_from_shm_buffer(stream_callback, cb_id);
            CHECK_SUCCESS(status);
        } else {
            memcpy(stream_callback->m_user_mem_view.data(), cb_id.data().data(), cb_id.data().size());
        }
    }

    stream_callback->m_callback(static_cast<hailo_status>(cb_id.status()));

    return HAILO_SUCCESS;
}

callback_idx_t ConfiguredNetworkGroupClient::get_unique_callback_idx()
{
    return m_current_cb_index.fetch_add(1);
}

Expected<std::vector<StreamCbParamsPtr>> ConfiguredNetworkGroupClient::create_streams_callbacks_params(const NamedBuffersCallbacks &named_buffers_callbacks)
{
    std::vector<StreamCbParamsPtr> streams_cb_params;
    streams_cb_params.reserve(named_buffers_callbacks.size());
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        for (const auto &name_buffer_cb : named_buffers_callbacks) {
            StreamCbParams stream_cb_params;
            auto cb_idx = get_unique_callback_idx();
            auto &stream_name = name_buffer_cb.first;
            CHECK_AS_EXPECTED(BufferType::VIEW == name_buffer_cb.second.first.buffer_type, HAILO_INVALID_OPERATION,
                "Using dmabuf is not supported when working with hailort_service");

            if (should_use_shared_memory()) {
                // Copy to shared memory buffer
                TRY(auto stream_pool, m_buffer_pool_per_stream->get_pool(stream_name));
                TRY(auto acquired_buffer, AcquiredBuffer::acquire_from_pool(stream_pool));

                CHECK_AS_EXPECTED(name_buffer_cb.second.first.view.size() == acquired_buffer->size(), HAILO_INVALID_ARGUMENT,
                    "For stream '{}', passed buffer size is {} (expected {})", stream_name, name_buffer_cb.second.first.view.size(),
                    acquired_buffer->size());

                if (contains(m_input_streams_names, stream_name)) {
                    memcpy(acquired_buffer->data(), name_buffer_cb.second.first.view.data(), name_buffer_cb.second.first.view.size());
                }

                TRY(auto shm_name, acquired_buffer->buffer()->storage().shm_name());
                stream_cb_params = StreamCbParams(cb_idx, stream_name, name_buffer_cb.second.second,
                    name_buffer_cb.second.first.view, shm_name, acquired_buffer);
            } else {
                stream_cb_params = StreamCbParams(cb_idx, stream_name, name_buffer_cb.second.second,
                    name_buffer_cb.second.first.view);
            }

            auto cb_params_ptr = make_shared_nothrow<StreamCbParams>(std::move(stream_cb_params));
            CHECK_NOT_NULL_AS_EXPECTED(cb_params_ptr, HAILO_OUT_OF_HOST_MEMORY);

            streams_cb_params.emplace_back(cb_params_ptr);
        }
    }

    return streams_cb_params;
}

hailo_status ConfiguredNetworkGroupClient::infer_async(const NamedBuffersCallbacks &named_buffers_callbacks,
    const std::function<void(hailo_status)> &infer_request_done_cb)
{
    std::unique_lock<std::mutex> lock_shutdown(m_shutdown_mutex);
    if (m_is_shutdown) {
        LOGGER__INFO("Trying to infer on a shutdown network group");
        infer_request_done_cb(HAILO_STREAM_ABORT);
        return HAILO_STREAM_ABORT;
    }
    auto streams_cb_params = create_streams_callbacks_params(named_buffers_callbacks);
    if (!streams_cb_params) {
        infer_request_done_cb(streams_cb_params.status());
        return make_unexpected(streams_cb_params.status());
    } else {
        std::unique_lock<std::mutex> lock(m_mutex);
        for (auto &stream_cb_params : streams_cb_params.value()) {
            m_idx_to_callbacks.emplace(stream_cb_params->m_cb_idx, stream_cb_params);
        }
    }

    auto infer_request_callback = [this, infer_request_done_cb](hailo_status status){
        if (status == HAILO_STREAM_ABORT) {
            LOGGER__INFO("Infer request was aborted by user");
        }
        else if (status != HAILO_SUCCESS) {
            LOGGER__ERROR("Infer request callback failed with status = {}", status);
        }

        infer_request_done_cb(status);
        decrease_ongoing_callbacks();
    };

    auto infer_request_cb_idx = 0;
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        infer_request_cb_idx = get_unique_callback_idx();
        m_infer_request_idx_to_callbacks.emplace(infer_request_cb_idx, infer_request_callback);
    }

    increase_ongoing_callbacks(); // Increase before lunch, as the cb may be called before we got the chance to increase the counter
    auto status = m_client->ConfiguredNetworkGroup_infer_async(m_identifier, streams_cb_params.value(),
        infer_request_cb_idx, m_input_streams_names);

    if (HAILO_SUCCESS != status) {
        // If we got error in `infer_async()`, then the callbacks will not be called in the service domain.
        // remove them from the cb lists so they wont be called in the client domain as well.
        std::unique_lock<std::mutex> lock(m_mutex);
        for (auto &stream_cb_params : streams_cb_params.value()) {
            m_idx_to_callbacks.erase(stream_cb_params->m_cb_idx);
        }
        m_infer_request_idx_to_callbacks.erase(infer_request_cb_idx);
        decrease_ongoing_callbacks();
    }

    if (status == HAILO_STREAM_ABORT) {
        LOGGER__INFO("Infer request was aborted by user");
        return status;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

} /* namespace hailort */