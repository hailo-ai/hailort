/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
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
#endif
#include <grpcpp/grpcpp.h>
#include "hailort_rpc.grpc.pb.h"
#if defined(_MSC_VER)
#pragma warning( pop ) 
#else
#pragma GCC diagnostic pop
#endif

namespace hailort
{

class HailoRtRpcService final : public ProtoHailoRtRpc::Service {

public:
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

    virtual grpc::Status InputVStreams_create(grpc::ServerContext *, const VStream_create_Request *request,
         VStreams_create_Reply *reply) override;
    virtual grpc::Status InputVStream_release(grpc::ServerContext * , const Release_Request *request,
        Release_Reply *reply) override;
    virtual grpc::Status OutputVStreams_create(grpc::ServerContext *, const VStream_create_Request *request,
         VStreams_create_Reply *reply) override;
    virtual grpc::Status OutputVStream_release(grpc::ServerContext *, const Release_Request *request,
        Release_Reply *reply) override;
    virtual grpc::Status InputVStream_write(grpc::ServerContext*, const InputVStream_write_Request *request,
        InputVStream_write_Reply *reply) override;
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
    virtual grpc::Status ConfiguredNetworkGroup_get_output_vstream_groups(grpc::ServerContext*,
        const ConfiguredNetworkGroup_get_output_vstream_groups_Request *request,
        ConfiguredNetworkGroup_get_output_vstream_groups_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_get_input_vstream_infos(grpc::ServerContext*,
        const ConfiguredNetworkGroup_get_vstream_infos_Request *request,
        ConfiguredNetworkGroup_get_vstream_infos_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_get_all_vstream_infos(grpc::ServerContext*,
        const ConfiguredNetworkGroup_get_vstream_infos_Request *request,
        ConfiguredNetworkGroup_get_vstream_infos_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_set_scheduler_timeout(grpc::ServerContext*,
        const ConfiguredNetworkGroup_set_scheduler_timeout_Request *request,
        ConfiguredNetworkGroup_set_scheduler_timeout_Reply *reply) override;
    virtual grpc::Status ConfiguredNetworkGroup_set_scheduler_threshold(grpc::ServerContext*,
        const ConfiguredNetworkGroup_set_scheduler_threshold_Request *request,
        ConfiguredNetworkGroup_set_scheduler_threshold_Reply *reply) override;
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
};

}

#endif // HAILO_HAILORT_RPC_SERVICE_HPP_