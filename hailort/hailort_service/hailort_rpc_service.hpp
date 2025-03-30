/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailort_rpc_service.hpp
 * @brief TODO
 **/

#ifndef HAILO_HAILORT_RPC_SERVICE_HPP_
#define HAILO_HAILORT_RPC_SERVICE_HPP_

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4244 4267 4127)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include <grpcpp/grpcpp.h>
#include "hailort_rpc.grpc.pb.h"
#if defined(_MSC_VER)
#pragma warning( pop ) 
#else
#pragma GCC diagnostic pop
#endif

#include "hailo/hailort.h"
#include "hailo/network_group.hpp"
#include "vdevice_callbacks_queue.hpp"

#include <thread>

namespace hailort
{

class HailoRtRpcService final : public ProtoHailoRtRpc::Service {
public:
    HailoRtRpcService();

    virtual grpc::Status client_keep_alive(grpc::ServerContext *ctx, const keepalive_Request *request,
        empty*) override;
    virtual grpc::Status get_service_version(grpc::ServerContext *, const get_service_version_Request *request,
        get_service_version_Reply *reply) override;

    virtual grpc::Status VDevice_create(grpc::ServerContext *, const VDevice_create_Request *request,
        VDevice_create_Reply *reply) override;
    virtual grpc::Status VDevice_release(grpc::ServerContext *, const Release_Request *request,
        Release_Reply* reply) override;
    virtual grpc::Status VDevice_configure(grpc::ServerContext*, const VDevice_configure_Request* request,
        VDevice_configure_Reply* reply) override;
    virtual grpc::Status VDevice_get_physical_devices_ids(grpc::ServerContext*, const VDevice_get_physical_devices_ids_Request* request,
        VDevice_get_physical_devices_ids_Reply* reply) override;
    virtual grpc::Status VDevice_get_default_streams_interface(grpc::ServerContext*, const VDevice_get_default_streams_interface_Request* request,
        VDevice_get_default_streams_interface_Reply* reply) override;
    virtual grpc::Status VDevice_get_callback_id(grpc::ServerContext*, const VDevice_get_callback_id_Request* request,
        VDevice_get_callback_id_Reply* reply) override;
    virtual grpc::Status VDevice_finish_callback_listener(grpc::ServerContext*, const VDevice_finish_callback_listener_Request* request,
        VDevice_finish_callback_listener_Reply* reply) override;

    virtual grpc::Status InputVStreams_create(grpc::ServerContext *, const VStream_create_Request *request,
         VStreams_create_Reply *reply) override;
    virtual grpc::Status InputVStream_release(grpc::ServerContext * , const Release_Request *request,
        Release_Reply *reply) override;
    virtual grpc::Status OutputVStreams_create(grpc::ServerContext *, const VStream_create_Request *request,
         VStreams_create_Reply *reply) override;
    virtual grpc::Status OutputVStream_release(grpc::ServerContext *, const Release_Request *request,
        Release_Reply *reply) override;
    virtual grpc::Status InputVStream_is_multi_planar(grpc::ServerContext*, const InputVStream_is_multi_planar_Request *request,
        InputVStream_is_multi_planar_Reply *reply) override;
    virtual grpc::Status InputVStream_write(grpc::ServerContext*, const InputVStream_write_Request *request,
        InputVStream_write_Reply *reply) override;
    virtual grpc::Status InputVStream_write_pix(grpc::ServerContext*, const InputVStream_write_pix_Request *request,
        InputVStream_write_pix_Reply *reply) override;
    virtual grpc::Status OutputVStream_read(grpc::ServerContext*, const OutputVStream_read_Request *request,
        OutputVStream_read_Reply *reply) override;
    virtual grpc::Status InputVStream_get_frame_size(grpc::ServerContext*, const VStream_get_frame_size_Request *request,
        VStream_get_frame_size_Reply *reply) override;
    virtual grpc::Status OutputVStream_get_frame_size(grpc::ServerContext*, const VStream_get_frame_size_Request *request,
        VStream_get_frame_size_Reply *reply) override;
    virtual grpc::Status InputVStream_flush(grpc::ServerContext*, const InputVStream_flush_Request *request,
        InputVStream_flush_Reply *reply) override;
    virtual grpc::Status InputVStream_name(grpc::ServerContext*, const VStream_name_Request *request,
        VStream_name_Reply *reply) override;
    virtual grpc::Status OutputVStream_name(grpc::ServerContext*, const VStream_name_Request *request,
        VStream_name_Reply *reply) override;
    virtual grpc::Status InputVStream_network_name(grpc::ServerContext*, const VStream_network_name_Request *request,
        VStream_network_name_Reply *reply) override;
    virtual grpc::Status OutputVStream_network_name(grpc::ServerContext*, const VStream_network_name_Request *request,
        VStream_network_name_Reply *reply) override;
    virtual grpc::Status InputVStream_abort(grpc::ServerContext*, const VStream_abort_Request *request,
        VStream_abort_Reply *reply) override;
    virtual grpc::Status OutputVStream_abort(grpc::ServerContext*, const VStream_abort_Request *request,
        VStream_abort_Reply *reply) override;
    virtual grpc::Status InputVStream_resume(grpc::ServerContext*, const VStream_resume_Request *request,
        VStream_resume_Reply *reply) override;
    virtual grpc::Status OutputVStream_resume(grpc::ServerContext*, const VStream_resume_Request *request,
        VStream_resume_Reply *reply) override;
    virtual grpc::Status InputVStream_get_user_buffer_format(grpc::ServerContext*, const VStream_get_user_buffer_format_Request *request,
        VStream_get_user_buffer_format_Reply *reply) override;
    virtual grpc::Status OutputVStream_get_user_buffer_format(grpc::ServerContext*, const VStream_get_user_buffer_format_Request *request,
        VStream_get_user_buffer_format_Reply *reply) override;
    virtual grpc::Status InputVStream_get_info(grpc::ServerContext*, const VStream_get_info_Request *request,
        VStream_get_info_Reply *reply) override;
    virtual grpc::Status OutputVStream_get_info(grpc::ServerContext*, const VStream_get_info_Request *request,
        VStream_get_info_Reply *reply) override;
    virtual grpc::Status InputVStream_stop_and_clear(grpc::ServerContext *ctx, const VStream_stop_and_clear_Request *request,
        VStream_stop_and_clear_Reply*) override;
    virtual grpc::Status OutputVStream_stop_and_clear(grpc::ServerContext *ctx, const VStream_stop_and_clear_Request *request,
        VStream_stop_and_clear_Reply*) override;
    virtual grpc::Status InputVStream_start_vstream(grpc::ServerContext *ctx, const VStream_start_vstream_Request *request,
        VStream_start_vstream_Reply*) override;
    virtual grpc::Status OutputVStream_start_vstream(grpc::ServerContext *ctx, const VStream_start_vstream_Request *request,
        VStream_start_vstream_Reply*) override;
    virtual grpc::Status InputVStream_is_aborted(grpc::ServerContext *ctx, const VStream_is_aborted_Request *request,
        VStream_is_aborted_Reply*) override;
    virtual grpc::Status OutputVStream_is_aborted(grpc::ServerContext *ctx, const VStream_is_aborted_Request *request,
        VStream_is_aborted_Reply*) override;
    virtual grpc::Status OutputVStream_set_nms_score_threshold(grpc::ServerContext *ctx,
        const VStream_set_nms_score_threshold_Request *request, VStream_set_nms_score_threshold_Reply*) override;
    virtual grpc::Status OutputVStream_set_nms_iou_threshold(grpc::ServerContext *ctx,
        const VStream_set_nms_iou_threshold_Request *request, VStream_set_nms_iou_threshold_Reply*) override;
    virtual grpc::Status OutputVStream_set_nms_max_proposals_per_class(grpc::ServerContext *ctx,
        const VStream_set_nms_max_proposals_per_class_Request *request, VStream_set_nms_max_proposals_per_class_Reply*) override;
    virtual grpc::Status OutputVStream_set_nms_max_accumulated_mask_size(grpc::ServerContext *ctx,
        const VStream_set_nms_max_accumulated_mask_size_Request *request, VStream_set_nms_max_accumulated_mask_size_Reply*) override;

    virtual grpc::Status ConfiguredNetworkGroup_dup_handle(grpc::ServerContext *ctx, const ConfiguredNetworkGroup_dup_handle_Request *request,
        ConfiguredNetworkGroup_dup_handle_Reply*) override;
    virtual grpc::Status ConfiguredNetworkGroup_release(grpc::ServerContext*, const Release_Request* request,
        Release_Reply* reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_make_input_vstream_params(grpc::ServerContext*,
        const ConfiguredNetworkGroup_make_input_vstream_params_Request *request,
        ConfiguredNetworkGroup_make_input_vstream_params_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_make_output_vstream_params(grpc::ServerContext*,
        const ConfiguredNetworkGroup_make_output_vstream_params_Request *request,
        ConfiguredNetworkGroup_make_output_vstream_params_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_make_output_vstream_params_groups(grpc::ServerContext*,
        const ConfiguredNetworkGroup_make_output_vstream_params_groups_Request *request,
        ConfiguredNetworkGroup_make_output_vstream_params_groups_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_name(grpc::ServerContext*,
        const ConfiguredNetworkGroup_name_Request *request,
        ConfiguredNetworkGroup_name_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_get_network_infos(grpc::ServerContext*,
        const ConfiguredNetworkGroup_get_network_infos_Request *request,
        ConfiguredNetworkGroup_get_network_infos_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_get_all_stream_infos(grpc::ServerContext*,
        const ConfiguredNetworkGroup_get_all_stream_infos_Request *request,
        ConfiguredNetworkGroup_get_all_stream_infos_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_get_default_stream_interface(grpc::ServerContext*,
        const ConfiguredNetworkGroup_get_default_stream_interface_Request *request,
        ConfiguredNetworkGroup_get_default_stream_interface_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_shutdown(grpc::ServerContext*,
        const ConfiguredNetworkGroup_shutdown_Request *request,
        ConfiguredNetworkGroup_shutdown_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_get_output_vstream_groups(grpc::ServerContext*,
        const ConfiguredNetworkGroup_get_output_vstream_groups_Request *request,
        ConfiguredNetworkGroup_get_output_vstream_groups_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_get_input_vstream_infos(grpc::ServerContext*,
        const ConfiguredNetworkGroup_get_vstream_infos_Request *request,
        ConfiguredNetworkGroup_get_vstream_infos_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_get_all_vstream_infos(grpc::ServerContext*,
        const ConfiguredNetworkGroup_get_vstream_infos_Request *request,
        ConfiguredNetworkGroup_get_vstream_infos_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_is_scheduled(grpc::ServerContext*,
        const ConfiguredNetworkGroup_is_scheduled_Request *request,
        ConfiguredNetworkGroup_is_scheduled_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_set_scheduler_timeout(grpc::ServerContext*,
        const ConfiguredNetworkGroup_set_scheduler_timeout_Request *request,
        ConfiguredNetworkGroup_set_scheduler_timeout_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_set_scheduler_threshold(grpc::ServerContext*,
        const ConfiguredNetworkGroup_set_scheduler_threshold_Request *request,
        ConfiguredNetworkGroup_set_scheduler_threshold_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_set_scheduler_priority(grpc::ServerContext*,
        const ConfiguredNetworkGroup_set_scheduler_priority_Request *request,
        ConfiguredNetworkGroup_set_scheduler_priority_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_get_output_vstream_infos(grpc::ServerContext*,
        const ConfiguredNetworkGroup_get_vstream_infos_Request *request,
        ConfiguredNetworkGroup_get_vstream_infos_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_get_latency_measurement(grpc::ServerContext*,
        const ConfiguredNetworkGroup_get_latency_measurement_Request *request,
        ConfiguredNetworkGroup_get_latency_measurement_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_is_multi_context(grpc::ServerContext*,
        const ConfiguredNetworkGroup_is_multi_context_Request *request,
        ConfiguredNetworkGroup_is_multi_context_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_get_config_params(grpc::ServerContext*,
        const ConfiguredNetworkGroup_get_config_params_Request *request,
        ConfiguredNetworkGroup_get_config_params_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_get_sorted_output_names(grpc::ServerContext*,
        const ConfiguredNetworkGroup_get_sorted_output_names_Request *request,
        ConfiguredNetworkGroup_get_sorted_output_names_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_infer_queue_size(grpc::ServerContext*,
        const ConfiguredNetworkGroup_infer_queue_size_Request *request,
        ConfiguredNetworkGroup_infer_queue_size_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_get_layer_info(grpc::ServerContext*,
        const ConfiguredNetworkGroup_get_layer_info_Request *request,
        ConfiguredNetworkGroup_get_layer_info_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_get_ops_metadata(grpc::ServerContext*,
        const ConfiguredNetworkGroup_get_ops_metadata_Request *request,
        ConfiguredNetworkGroup_get_ops_metadata_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_set_nms_score_threshold(grpc::ServerContext*,
        const ConfiguredNetworkGroup_set_nms_score_threshold_Request *request,
        ConfiguredNetworkGroup_set_nms_score_threshold_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_set_nms_iou_threshold(grpc::ServerContext*,
        const ConfiguredNetworkGroup_set_nms_iou_threshold_Request *request,
        ConfiguredNetworkGroup_set_nms_iou_threshold_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_set_nms_max_bboxes_per_class(grpc::ServerContext*,
        const ConfiguredNetworkGroup_set_nms_max_bboxes_per_class_Request *request,
        ConfiguredNetworkGroup_set_nms_max_bboxes_per_class_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_set_nms_max_bboxes_total(grpc::ServerContext*,
        const ConfiguredNetworkGroup_set_nms_max_bboxes_total_Request *request,
        ConfiguredNetworkGroup_set_nms_max_bboxes_total_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_set_nms_max_accumulated_mask_size(grpc::ServerContext*,
        const ConfiguredNetworkGroup_set_nms_max_accumulated_mask_size_Request *request,
        ConfiguredNetworkGroup_set_nms_max_accumulated_mask_size_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_get_stream_names_from_vstream_name(grpc::ServerContext*,
        const ConfiguredNetworkGroup_get_stream_names_from_vstream_name_Request *request,
        ConfiguredNetworkGroup_get_stream_names_from_vstream_name_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_get_vstream_names_from_stream_name(grpc::ServerContext*,
        const ConfiguredNetworkGroup_get_vstream_names_from_stream_name_Request *request,
        ConfiguredNetworkGroup_get_vstream_names_from_stream_name_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_infer_async(grpc::ServerContext*,
        const ConfiguredNetworkGroup_infer_async_Request *request,
        ConfiguredNetworkGroup_infer_async_Reply *reply) override;

private:
    void keep_alive();
    hailo_status flush_input_vstream(uint32_t handle);
    hailo_status abort_input_vstream(uint32_t handle);
    hailo_status abort_output_vstream(uint32_t handle);
    void abort_vstreams_by_ids(std::set<uint32_t> &pids);
    void release_configured_network_groups_by_id(uint32_t client_pid);
    void remove_disconnected_clients();
    void update_client_id_timestamp(uint32_t pid);
    Expected<size_t> infer_queue_size(uint32_t ng_handle);
    Expected<std::vector<hailo_stream_info_t>> get_all_stream_infos(uint32_t ng_handle);
    Expected<std::vector<hailo_vstream_info_t>> get_all_vstream_infos(uint32_t ng_handle);
    Expected<std::string> output_vstream_name(uint32_t vstream_handle);
    Expected<uint32_t> create_buffer_pool_for_ng(uint32_t vdevice_handle, uint32_t request_pid);
    hailo_status allocate_pool_for_raw_streams(uint32_t ng_handle);
    void release_resources_on_error(std::vector<uint32_t> ng_handles, std::vector<uint32_t> buffer_pool_handles, uint32_t pid);
    Expected<NamedBuffersCallbacks> prepare_named_buffers_callbacks(uint32_t vdevice_handle,
        uint32_t ng_handle, std::shared_ptr<ConfiguredNetworkGroup_infer_async_Request> infer_async_request);
    hailo_status add_input_named_buffer(const ProtoTransferRequest &proto_stream_transfer_request, uint32_t vdevice_handle,
        uint32_t ng_handle, std::shared_ptr<ConfiguredNetworkGroup_infer_async_Request> infer_async_request,
        NamedBuffersCallbacks &named_buffers_callbacks);
    hailo_status add_output_named_buffer(const ProtoTransferRequest &proto_stream_transfer_request, uint32_t vdevice_handle,
        uint32_t ng_handle, NamedBuffersCallbacks &named_buffers_callbacks);
    void enqueue_cb_identifier(uint32_t vdevice_handle, ProtoCallbackIdentifier &&cb_identifier);
    hailo_status return_buffer_to_cng_pool(uint32_t ng_handle, const std::string &output_name, BufferPtr buffer);
    Expected<BufferPtr> acquire_buffer_from_cng_pool(uint32_t ng_handle, const std::string &output_name);
    Expected<size_t> output_vstream_frame_size(uint32_t vstream_handle);
    hailo_status update_buffer_size_in_pool(uint32_t vstream_handle, uint32_t network_group_handle);
    void shutdown_configured_network_groups_by_ids(std::set<uint32_t> &pids);
    void shutdown_buffer_pool_by_ids(std::set<uint32_t> &pids);
    void shutdown_vdevice_cb_queue_by_ids(std::set<uint32_t> &pids);
    hailo_status shutdown_cng_buffer_pool(uint32_t network_group_handle);
    hailo_status shutdown_vdevice_cb_queue(uint32_t vdevice_handle);
    hailo_status shutdown_configured_network_group(uint32_t vdevice_handle);

    std::mutex m_keep_alive_mutex;
    std::map<uint32_t, std::chrono::time_point<std::chrono::high_resolution_clock>> m_clients_pids;
    std::unique_ptr<std::thread> m_keep_alive;

    std::mutex m_vdevice_mutex;
};

}

#endif // HAILO_HAILORT_RPC_SERVICE_HPP_