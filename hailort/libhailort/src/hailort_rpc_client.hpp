/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailort_rpc_client.hpp
 * @brief TODO
 **/

#ifndef HAILO_HAILORT_RPC_CLIENT_HPP_
#define HAILO_HAILORT_RPC_CLIENT_HPP_

#include "hailo/expected.hpp"
#include "hailo/hailort.hpp"
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

class HailoRtRpcClient final {
public:
    HailoRtRpcClient(std::shared_ptr<grpc::Channel> channel)
        : m_stub(HailoRtRpc::NewStub(channel)) {}

    hailo_status client_keep_alive(uint32_t process_id);
    Expected<hailo_version_t> get_service_version();

    Expected<uint32_t> VDevice_create(const hailo_vdevice_params_t &params, uint32_t pid);
    hailo_status VDevice_release(uint32_t handle);
    Expected<std::vector<std::string>> VDevice_get_physical_devices_ids(uint32_t handle);
    Expected<std::vector<uint32_t>> VDevice_configure(uint32_t vdevice_handle, const Hef &hef, uint32_t pid, const NetworkGroupsParamsMap &configure_params={});
    hailo_status ConfiguredNetworkGroup_release(uint32_t handle);
    Expected<std::map<std::string, hailo_vstream_params_t>> ConfiguredNetworkGroup_make_input_vstream_params(uint32_t handle,
        bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
        const std::string &network_name);
    Expected<std::map<std::string, hailo_vstream_params_t>> ConfiguredNetworkGroup_make_output_vstream_params(uint32_t handle,
        bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
        const std::string &network_name);
    Expected<std::string> ConfiguredNetworkGroup_get_network_group_name(uint32_t handle);
    Expected<std::string> ConfiguredNetworkGroup_get_name(uint32_t handle);
    Expected<std::vector<hailo_network_info_t>> ConfiguredNetworkGroup_get_network_infos(uint32_t handle);
    Expected<std::vector<hailo_stream_info_t>> ConfiguredNetworkGroup_get_all_stream_infos(uint32_t handle, const std::string &network_name);
    Expected<hailo_stream_interface_t> ConfiguredNetworkGroup_get_default_stream_interface(uint32_t handle);
    Expected<std::vector<std::vector<std::string>>> ConfiguredNetworkGroup_get_output_vstream_groups(uint32_t handle);
    Expected<std::vector<hailo_vstream_info_t>> ConfiguredNetworkGroup_get_input_vstream_infos(uint32_t handle, std::string network_name);
    Expected<std::vector<hailo_vstream_info_t>> ConfiguredNetworkGroup_get_output_vstream_infos(uint32_t handle, std::string network_name);
    Expected<std::vector<hailo_vstream_info_t>> ConfiguredNetworkGroup_get_all_vstream_infos(uint32_t handle, std::string network_name);
    hailo_status ConfiguredNetworkGroup_set_scheduler_timeout(uint32_t handle, const std::chrono::milliseconds &timeout,
        const std::string &network_name);
    hailo_status ConfiguredNetworkGroup_set_scheduler_threshold(uint32_t handle, uint32_t threshold, const std::string &network_name);
    Expected<LatencyMeasurementResult> ConfiguredNetworkGroup_get_latency_measurement(uint32_t handle, const std::string &network_name);

    Expected<std::vector<uint32_t>> InputVStreams_create(uint32_t net_group_handle,
        const std::map<std::string, hailo_vstream_params_t> &inputs_params, uint32_t pid);
    hailo_status InputVStream_release(uint32_t handle);
    Expected<std::vector<uint32_t>> OutputVStreams_create(uint32_t net_group_handle,
        const std::map<std::string, hailo_vstream_params_t> &output_params, uint32_t pid);
    hailo_status OutputVStream_release(uint32_t handle);
    hailo_status InputVStream_write(uint32_t handle, const MemoryView &buffer);
    hailo_status OutputVStream_read(uint32_t handle, MemoryView buffer);
    Expected<size_t> InputVStream_get_frame_size(uint32_t handle);
    Expected<size_t> OutputVStream_get_frame_size(uint32_t handle);

    hailo_status InputVStream_flush(uint32_t handle);

    Expected<std::string> InputVStream_name(uint32_t handle);
    Expected<std::string> OutputVStream_name(uint32_t handle);

    hailo_status InputVStream_abort(uint32_t handle);
    hailo_status OutputVStream_abort(uint32_t handle);

    hailo_status InputVStream_resume(uint32_t handle);
    hailo_status OutputVStream_resume(uint32_t handle);

private:
    std::unique_ptr<HailoRtRpc::Stub> m_stub;
};

}

#endif // HAILO_HAILORT_CLIENT_RPC_HPP_