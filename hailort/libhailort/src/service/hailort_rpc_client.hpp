/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailort_rpc_client.hpp
 * @brief TODO
 **/

#ifndef HAILO_HAILORT_RPC_CLIENT_HPP_
#define HAILO_HAILORT_RPC_CLIENT_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/device.hpp"
#include "rpc/rpc_definitions.hpp"
#include "service/buffer_pool_per_stream.hpp"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4244 4267 4127)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#endif
#include <grpcpp/grpcpp.h>
#include "hailort_rpc.grpc.pb.h"
#if defined(_MSC_VER)
#pragma warning( pop )
#else
#pragma GCC diagnostic pop
#endif
#include <memory>

namespace hailort
{

// Higher then default-hrt-timeout so we can differentiate errors
static const std::chrono::milliseconds CONTEXT_TIMEOUT(HAILO_DEFAULT_VSTREAM_TIMEOUT_MS + 500);
using callback_idx_t = uint32_t;
using StreamCallback = std::function<void(hailo_status)>;

class StreamCbParams
{
public:
    StreamCbParams() :
        m_is_shm(false), m_cb_idx(INVALID_CB_INDEX), m_stream_name(INVALID_STREAM_NAME) {}

    StreamCbParams(callback_idx_t cb_idx, std::string stream_name, StreamCallback callback, MemoryView user_mem_view) :
        m_is_shm(false), m_cb_idx(cb_idx), m_stream_name(stream_name), m_size(user_mem_view.size()),
            m_callback(callback), m_user_mem_view(user_mem_view) {}

    StreamCbParams(callback_idx_t cb_idx, std::string stream_name, StreamCallback callback, MemoryView user_mem_view,
        const std::string &shm_name, AcquiredBufferPtr acquired_shm_buffer) :
        m_is_shm(true), m_cb_idx(cb_idx), m_stream_name(stream_name), m_size(user_mem_view.size()), m_callback(callback),
        m_user_mem_view(user_mem_view), m_shm_name(shm_name), m_acquired_shm_buffer(acquired_shm_buffer) {}

    bool m_is_shm;
    callback_idx_t m_cb_idx;
    std::string m_stream_name;
    size_t m_size;
    StreamCallback m_callback;

    // TODO: HRT-14821 - Try make it a uinion
    MemoryView m_user_mem_view;
    std::string m_shm_name;
    AcquiredBufferPtr m_acquired_shm_buffer;
};
using StreamCbParamsPtr = std::shared_ptr<StreamCbParams>;

class ClientContextWithTimeout : public grpc::ClientContext {
public:
    ClientContextWithTimeout(const std::chrono::milliseconds context_timeout = CONTEXT_TIMEOUT)
    {
        auto timeout = get_request_timeout(context_timeout);

        set_deadline(std::chrono::system_clock::now() + timeout);
    }
    
    // TODO: HRT-16034: make common function with hrpc client.
    std::chrono::milliseconds get_request_timeout(const std::chrono::milliseconds default_timeout)
    {
    auto timeout_seconds = get_env_variable(HAILO_REQUEST_TIMEOUT_SECONDS);
    if (timeout_seconds) {
        return std::chrono::seconds(std::stoi(timeout_seconds.value()));
    }
    return default_timeout;
    }
};

class HailoRtRpcClient final {
public:
    HailoRtRpcClient(std::shared_ptr<grpc::Channel> channel)
        : m_stub(ProtoHailoRtRpc::NewStub(channel)) {}

    hailo_status client_keep_alive(uint32_t pid);
    Expected<hailo_version_t> get_service_version();

    Expected<uint32_t> VDevice_create(const hailo_vdevice_params_t &params, uint32_t pid);
    hailo_status VDevice_release(const VDeviceIdentifier &identifier, uint32_t pid);
    Expected<std::vector<std::string>> VDevice_get_physical_devices_ids(const VDeviceIdentifier &identifier);
    Expected<std::vector<std::unique_ptr<Device>>> VDevice_get_physical_devices(const VDeviceIdentifier &identifier);
    Expected<hailo_stream_interface_t> VDevice_get_default_streams_interface(const VDeviceIdentifier &identifier);
    Expected<std::vector<uint32_t>> VDevice_configure(const VDeviceIdentifier &identifier, const Hef &hef, uint32_t pid, const NetworkGroupsParamsMap &configure_params={});
    Expected<ProtoCallbackIdentifier> VDevice_get_callback_id(const VDeviceIdentifier &identifier);
    hailo_status VDevice_finish_callback_listener(const VDeviceIdentifier &identifier);

    Expected<uint32_t> ConfiguredNetworkGroup_dup_handle(const NetworkGroupIdentifier &identifier, uint32_t pid);
    hailo_status ConfiguredNetworkGroup_release(const NetworkGroupIdentifier &identifier, uint32_t pid);
    Expected<std::map<std::string, hailo_vstream_params_t>> ConfiguredNetworkGroup_make_input_vstream_params(const NetworkGroupIdentifier &identifier,
        hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
        const std::string &network_name);
    Expected<std::map<std::string, hailo_vstream_params_t>> ConfiguredNetworkGroup_make_output_vstream_params(const NetworkGroupIdentifier &identifier,
        hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
        const std::string &network_name);
    Expected<std::string> ConfiguredNetworkGroup_get_network_group_name(const NetworkGroupIdentifier &identifier);
    Expected<std::string> ConfiguredNetworkGroup_name(const NetworkGroupIdentifier &identifier);
    Expected<std::vector<hailo_network_info_t>> ConfiguredNetworkGroup_get_network_infos(const NetworkGroupIdentifier &identifier);
    Expected<std::vector<hailo_stream_info_t>> ConfiguredNetworkGroup_get_all_stream_infos(const NetworkGroupIdentifier &identifier, const std::string &network_name);
    Expected<hailo_stream_interface_t> ConfiguredNetworkGroup_get_default_stream_interface(const NetworkGroupIdentifier &identifier);
    Expected<std::vector<std::map<std::string, hailo_vstream_params_t>>> ConfiguredNetworkGroup_make_output_vstream_params_groups(const NetworkGroupIdentifier &identifier,
        hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size);
    hailo_status ConfiguredNetworkGroup_shutdown(const NetworkGroupIdentifier &identifier);
    Expected<std::vector<std::vector<std::string>>> ConfiguredNetworkGroup_get_output_vstream_groups(const NetworkGroupIdentifier &identifier);
    Expected<std::vector<hailo_vstream_info_t>> ConfiguredNetworkGroup_get_input_vstream_infos(const NetworkGroupIdentifier &identifier, std::string network_name);
    Expected<std::vector<hailo_vstream_info_t>> ConfiguredNetworkGroup_get_output_vstream_infos(const NetworkGroupIdentifier &identifier, std::string network_name);
    Expected<std::vector<hailo_vstream_info_t>> ConfiguredNetworkGroup_get_all_vstream_infos(const NetworkGroupIdentifier &identifier, std::string network_name);
    Expected<bool> ConfiguredNetworkGroup_is_scheduled(const NetworkGroupIdentifier &identifier);
    hailo_status ConfiguredNetworkGroup_set_scheduler_timeout(const NetworkGroupIdentifier &identifier, const std::chrono::milliseconds &timeout,
        const std::string &network_name);
    hailo_status ConfiguredNetworkGroup_set_scheduler_threshold(const NetworkGroupIdentifier &identifier, uint32_t threshold, const std::string &network_name);
    hailo_status ConfiguredNetworkGroup_set_scheduler_priority(const NetworkGroupIdentifier &identifier, uint8_t priority, const std::string &network_name);
    Expected<LatencyMeasurementResult> ConfiguredNetworkGroup_get_latency_measurement(const NetworkGroupIdentifier &identifier, const std::string &network_name);
    Expected<bool> ConfiguredNetworkGroup_is_multi_context(const NetworkGroupIdentifier &identifier);
    Expected<ConfigureNetworkParams> ConfiguredNetworkGroup_get_config_params(const NetworkGroupIdentifier &identifier);
    Expected<std::vector<std::string>> ConfiguredNetworkGroup_get_sorted_output_names(const NetworkGroupIdentifier &identifier);
    Expected<size_t> ConfiguredNetworkGroup_infer_queue_size(const NetworkGroupIdentifier &identifier);
    Expected<std::unique_ptr<LayerInfo>> ConfiguredNetworkGroup_get_layer_info(const NetworkGroupIdentifier &identifier, const std::string &stream_name);
    Expected<std::vector<net_flow::PostProcessOpMetadataPtr>> ConfiguredNetworkGroup_get_ops_metadata(const NetworkGroupIdentifier &identifier);
    hailo_status ConfiguredNetworkGroup_set_nms_score_threshold(const NetworkGroupIdentifier &identifier, const std::string &edge_name, float32_t nms_score_th);
    hailo_status ConfiguredNetworkGroup_set_nms_iou_threshold(const NetworkGroupIdentifier &identifier, const std::string &edge_name, float32_t iou_th);
    hailo_status ConfiguredNetworkGroup_set_nms_max_bboxes_per_class(const NetworkGroupIdentifier &identifier, const std::string &edge_name, uint32_t max_bboxes);
    hailo_status ConfiguredNetworkGroup_set_nms_max_bboxes_total(const NetworkGroupIdentifier &identifier, const std::string &edge_name, uint32_t max_bboxes);
    hailo_status ConfiguredNetworkGroup_set_nms_max_accumulated_mask_size(const NetworkGroupIdentifier &identifier, const std::string &edge_name, uint32_t max_accumulated_mask_size);
    Expected<std::vector<std::string>> ConfiguredNetworkGroup_get_stream_names_from_vstream_name(const NetworkGroupIdentifier &identifier, const std::string &vstream_name);
    Expected<std::vector<std::string>> ConfiguredNetworkGroup_get_vstream_names_from_stream_name(const NetworkGroupIdentifier &identifier, const std::string &stream_name);
    hailo_status ConfiguredNetworkGroup_infer_async(const NetworkGroupIdentifier &identifier,
        const std::vector<std::tuple<callback_idx_t, std::string, MemoryView>> &cb_idx_to_stream_buffer,
        const callback_idx_t infer_request_done_cb, const std::unordered_set<std::string> &input_streams_names);
    hailo_status ConfiguredNetworkGroup_infer_async(const NetworkGroupIdentifier &identifier,
        const std::vector<StreamCbParamsPtr> &strems_cb_params, const callback_idx_t infer_request_done_cb,
        const std::unordered_set<std::string> &input_streams_names);

    Expected<std::unordered_map<std::string, uint32_t>> InputVStreams_create(const NetworkGroupIdentifier &identifier,
        const std::map<std::string, hailo_vstream_params_t> &inputs_params, uint32_t pid);
    Expected<std::unordered_map<std::string, uint32_t>> OutputVStreams_create(const NetworkGroupIdentifier &identifier,
        const std::map<std::string, hailo_vstream_params_t> &output_params, uint32_t pid);

    Expected<uint32_t> InputVStream_dup_handle(const VStreamIdentifier &identifier, uint32_t pid);
    Expected<uint32_t> OutputVStream_dup_handle(const VStreamIdentifier &identifier, uint32_t pid);
    hailo_status InputVStream_release(const VStreamIdentifier &identifier, uint32_t pid);

    hailo_status OutputVStream_release(const VStreamIdentifier &identifier, uint32_t pid);
    Expected<bool> InputVStream_is_multi_planar(const VStreamIdentifier &identifier);
    hailo_status InputVStream_write(const VStreamIdentifier &identifier, const MemoryView &buffer, const std::chrono::milliseconds &timeout);
    hailo_status InputVStream_write(const VStreamIdentifier &identifier, const hailo_pix_buffer_t &buffer, const std::chrono::milliseconds &timeout);
    hailo_status OutputVStream_read(const VStreamIdentifier &identifier, MemoryView buffer, const std::chrono::milliseconds &timeout);
    Expected<size_t> InputVStream_get_frame_size(const VStreamIdentifier &identifier);
    Expected<size_t> OutputVStream_get_frame_size(const VStreamIdentifier &identifier);

    hailo_status InputVStream_flush(const VStreamIdentifier &identifier);

    Expected<std::string> InputVStream_name(const VStreamIdentifier &identifier);
    Expected<std::string> OutputVStream_name(const VStreamIdentifier &identifier);

    Expected<std::string> InputVStream_network_name(const VStreamIdentifier &identifier);
    Expected<std::string> OutputVStream_network_name(const VStreamIdentifier &identifier);

    hailo_status InputVStream_abort(const VStreamIdentifier &identifier);
    hailo_status OutputVStream_abort(const VStreamIdentifier &identifier);
    hailo_status InputVStream_resume(const VStreamIdentifier &identifier);
    hailo_status OutputVStream_resume(const VStreamIdentifier &identifier);
    hailo_status InputVStream_stop_and_clear(const VStreamIdentifier &identifier);
    hailo_status OutputVStream_stop_and_clear(const VStreamIdentifier &identifier);
    hailo_status InputVStream_start_vstream(const VStreamIdentifier &identifier);
    hailo_status OutputVStream_start_vstream(const VStreamIdentifier &identifier);

    Expected<hailo_format_t> InputVStream_get_user_buffer_format(const VStreamIdentifier &identifier);
    Expected<hailo_format_t> OutputVStream_get_user_buffer_format(const VStreamIdentifier &identifier);

    Expected<hailo_vstream_info_t> InputVStream_get_info(const VStreamIdentifier &identifier);
    Expected<hailo_vstream_info_t> OutputVStream_get_info(const VStreamIdentifier &identifier);

    Expected<bool> InputVStream_is_aborted(const VStreamIdentifier &identifier);
    Expected<bool> OutputVStream_is_aborted(const VStreamIdentifier &identifier);

    hailo_status OutputVStream_set_nms_score_threshold(const VStreamIdentifier &identifier, float32_t threshold);
    hailo_status OutputVStream_set_nms_iou_threshold(const VStreamIdentifier &identifier, float32_t threshold);
    hailo_status OutputVStream_set_nms_max_proposals_per_class(const VStreamIdentifier &identifier, uint32_t max_proposals_per_class);
    hailo_status OutputVStream_set_nms_max_accumulated_mask_size(const VStreamIdentifier &identifier, uint32_t max_accumulated_mask_size);

private:
    void VDevice_convert_identifier_to_proto(const VDeviceIdentifier &identifier, ProtoVDeviceIdentifier *proto_identifier);
    void ConfiguredNetworkGroup_convert_identifier_to_proto(const NetworkGroupIdentifier &identifier, ProtoConfiguredNetworkGroupIdentifier *proto_identifier);
    void VStream_convert_identifier_to_proto(const VStreamIdentifier &identifier, ProtoVStreamIdentifier *proto_identifier);

    std::unique_ptr<ProtoHailoRtRpc::Stub> m_stub;
};

}

#endif // HAILO_HAILORT_RPC_CLIENT_HPP_