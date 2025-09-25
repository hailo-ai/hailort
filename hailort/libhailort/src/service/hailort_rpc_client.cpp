/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailort_rpc_client.cpp
 * @brief Implementation of the hailort rpc client
 **/

#include "common/utils.hpp"

#include "hef/hef_internal.hpp"
#include "hailort_rpc_client.hpp"
#include "net_flow/ops_metadata/yolov8_op_metadata.hpp"
#include "net_flow/ops_metadata/yolov8_bbox_only_op_metadata.hpp"
#include "net_flow/ops_metadata/yolox_op_metadata.hpp"
#include "net_flow/ops_metadata/ssd_op_metadata.hpp"
#include "net_flow/ops_metadata/softmax_op_metadata.hpp"
#include "net_flow/ops_metadata/argmax_op_metadata.hpp"
#include "net_flow/ops_metadata/nms_op_metadata.hpp"
#include "net_flow/ops_metadata/yolov5_op_metadata.hpp"
#include "net_flow/ops_metadata/yolov5_seg_op_metadata.hpp"
#include "net_flow/ops_metadata/yolov5_bbox_only_op_metadata.hpp"

#include <grpcpp/health_check_service_interface.h>


namespace hailort
{

hailo_status HailoRtRpcClient::client_keep_alive(uint32_t pid)
{
    keepalive_Request request;
    request.set_pid(pid);
    empty reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->client_keep_alive(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    return HAILO_SUCCESS;
}

Expected<hailo_version_t> HailoRtRpcClient::get_service_version()
{
    get_service_version_Request request;
    get_service_version_Reply reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->get_service_version(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    auto version_proto = reply.hailo_version();
    hailo_version_t service_version = {version_proto.major_version(), version_proto.minor_version(), version_proto.revision_version()};
    return service_version;
}

Expected<uint32_t> HailoRtRpcClient::VDevice_create(const hailo_vdevice_params_t &params, uint32_t pid) {
    VDevice_create_Request request;
    request.set_pid(pid);
    auto proto_vdevice_params = request.mutable_hailo_vdevice_params();
    proto_vdevice_params->set_device_count(params.device_count);
    auto ids = proto_vdevice_params->mutable_device_ids();
    if (params.device_ids != nullptr) {
        for (size_t i = 0; i < params.device_count; ++i) {
            ids->Add(std::string(params.device_ids[i].id));
        }
    }
    proto_vdevice_params->set_scheduling_algorithm(params.scheduling_algorithm);
    proto_vdevice_params->set_group_id(params.group_id == nullptr ? "" : std::string(params.group_id));

    VDevice_create_Reply reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->VDevice_create(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    return reply.handle();
}

hailo_status HailoRtRpcClient::VDevice_release(const VDeviceIdentifier &identifier, uint32_t pid)
{
    Release_Request request;
    auto proto_identifier = request.mutable_vdevice_identifier();
    VDevice_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_pid(pid);

    Release_Reply reply;
    // Note: In multiple devices app and multiple networks, there are many mapped buffers for each device.
    // Theerefore, the release of the devices might take a longer time to finished un-mapping all the buffers,
    // so we increase the timeout for the VDevice_release context.
    // TODO: HRT-13274
    const std::chrono::minutes release_timeout(2);
    ClientContextWithTimeout context(release_timeout);
    grpc::Status status = m_stub->VDevice_release(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS(static_cast<hailo_status>(reply.status()));
    return HAILO_SUCCESS;
}

Expected<std::unordered_map<std::string, uint32_t>> HailoRtRpcClient::InputVStreams_create(const NetworkGroupIdentifier &identifier,
    const std::map<std::string, hailo_vstream_params_t> &inputs_params, uint32_t pid)
{
    VStream_create_Request request;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_pid(pid);
    auto proto_vstreams_params = request.mutable_vstreams_params();
    for (const auto &name_params_pair : inputs_params) {
        ProtoNamedVStreamParams proto_name_param_pair;
        auto vstream_params = name_params_pair.second;

        proto_name_param_pair.set_name(name_params_pair.first);
        auto proto_vstream_param = proto_name_param_pair.mutable_params();

        auto proto_user_buffer_format = proto_vstream_param->mutable_user_buffer_format();
        auto user_buffer_format = vstream_params.user_buffer_format;
        proto_user_buffer_format->set_type(user_buffer_format.type);
        proto_user_buffer_format->set_order(user_buffer_format.order);
        proto_user_buffer_format->set_flags(user_buffer_format.flags);

        proto_vstream_param->set_timeout_ms(vstream_params.timeout_ms);
        proto_vstream_param->set_queue_size(vstream_params.queue_size);

        proto_vstream_param->set_vstream_stats_flags(vstream_params.vstream_stats_flags);
        proto_vstream_param->set_pipeline_elements_stats_flags(vstream_params.vstream_stats_flags);

        proto_vstreams_params->Add(std::move(proto_name_param_pair));
    }

    VStreams_create_Reply reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->InputVStreams_create(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    std::unordered_map<std::string, uint32_t> input_vstreams_names_to_handles;
    assert(reply.handles_size() == reply.names_size());
    for (int i = 0; i < reply.handles_size(); i++) {
        input_vstreams_names_to_handles.emplace(reply.names(i), reply.handles(i));
    }

    return input_vstreams_names_to_handles;
}

hailo_status HailoRtRpcClient::InputVStream_release(const VStreamIdentifier &identifier, uint32_t pid)
{
    Release_Request request;
    request.set_pid(pid);
    auto proto_identifier = request.mutable_vstream_identifier();
    VStream_convert_identifier_to_proto(identifier, proto_identifier);

    Release_Reply reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->InputVStream_release(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS(static_cast<hailo_status>(reply.status()));
    return HAILO_SUCCESS;
}

Expected<std::unordered_map<std::string, uint32_t>> HailoRtRpcClient::OutputVStreams_create(const NetworkGroupIdentifier &identifier,
        const std::map<std::string, hailo_vstream_params_t> &output_params, uint32_t pid)
{
    VStream_create_Request request;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_pid(pid);
    auto proto_vstreams_params = request.mutable_vstreams_params();
    for (const auto &name_params_pair : output_params) {
        ProtoNamedVStreamParams proto_name_param_pair;
        auto vstream_params = name_params_pair.second;

        proto_name_param_pair.set_name(name_params_pair.first);
        auto proto_vstream_param = proto_name_param_pair.mutable_params();

        auto proto_user_buffer_format = proto_vstream_param->mutable_user_buffer_format();
        auto user_buffer_format = vstream_params.user_buffer_format;
        proto_user_buffer_format->set_type(user_buffer_format.type);
        proto_user_buffer_format->set_order(user_buffer_format.order);
        proto_user_buffer_format->set_flags(user_buffer_format.flags);

        proto_vstream_param->set_timeout_ms(vstream_params.timeout_ms);
        proto_vstream_param->set_queue_size(vstream_params.queue_size);

        proto_vstream_param->set_vstream_stats_flags(vstream_params.vstream_stats_flags);
        proto_vstream_param->set_pipeline_elements_stats_flags(vstream_params.vstream_stats_flags);

        proto_vstreams_params->Add(std::move(proto_name_param_pair));
    }

    VStreams_create_Reply reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->OutputVStreams_create(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    std::unordered_map<std::string, uint32_t> output_vstreams_names_to_handles;
    assert(reply.handles_size() == reply.names_size());
    for (int i = 0; i < reply.handles_size(); i++) {
        output_vstreams_names_to_handles.emplace(reply.names(i), reply.handles(i));
    }

    return output_vstreams_names_to_handles;
}

hailo_status HailoRtRpcClient::OutputVStream_release(const VStreamIdentifier &identifier, uint32_t pid)
{
    Release_Request request;
    request.set_pid(pid);
    auto proto_identifier = request.mutable_vstream_identifier();
    VStream_convert_identifier_to_proto(identifier, proto_identifier);

    Release_Reply reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->OutputVStream_release(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS(static_cast<hailo_status>(reply.status()));
    return HAILO_SUCCESS;
}

Expected<std::vector<uint32_t>> HailoRtRpcClient::VDevice_configure(const VDeviceIdentifier &identifier, const Hef &hef,
    uint32_t pid, const NetworkGroupsParamsMap &configure_params)
{
    VDevice_configure_Request request;
    auto proto_identifier = request.mutable_identifier();
    VDevice_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_pid(pid);
    TRY(auto hef_buffer, hef.pimpl->get_hef_as_buffer());
    request.set_hef(hef_buffer->data(), hef_buffer->size());

    // Serialize NetworkGroupsParamsMap
    for (const auto &name_params_pair : configure_params) {
        auto proto_net_params = request.add_configure_params_map();
        proto_net_params->set_name(name_params_pair.first);

        auto net_configure_params = name_params_pair.second;
        auto proto_network_configure_params = proto_net_params->mutable_params();
        proto_network_configure_params->set_batch_size(net_configure_params.batch_size);
        proto_network_configure_params->set_power_mode(net_configure_params.power_mode);
        proto_network_configure_params->set_latency(net_configure_params.latency);

        // Init stream params map
        for (const auto &name_stream_params_pair : net_configure_params.stream_params_by_name) {
            auto proto_name_streams_params = proto_network_configure_params->add_stream_params_map();
            proto_name_streams_params->set_name(name_stream_params_pair.first);

            auto proto_stream_params = proto_name_streams_params->mutable_params();
            auto stream_params = name_stream_params_pair.second;
            proto_stream_params->set_stream_interface(stream_params.stream_interface);
            proto_stream_params->set_direction(stream_params.direction);
            proto_stream_params->set_flags(stream_params.flags);
        }

        // Init network params map
        for (const auto &name_network_params_pair : net_configure_params.network_params_by_name) {
            auto proto_name_network_params = proto_network_configure_params->add_network_params_map();
            proto_name_network_params->set_name(name_network_params_pair.first);

            auto proto_network_params = proto_name_network_params->mutable_params();
            auto network_params = name_network_params_pair.second;
            proto_network_params->set_batch_size(network_params.batch_size);
        }
    }

    VDevice_configure_Reply reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->VDevice_configure(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));

    std::vector<uint32_t> networks_handles(reply.networks_handles().begin(), reply.networks_handles().end());
    return networks_handles;
}

Expected<ProtoCallbackIdentifier> HailoRtRpcClient::VDevice_get_callback_id(const VDeviceIdentifier &identifier)
{
    VDevice_get_callback_id_Request request;
    auto proto_identifier = request.mutable_identifier();
    VDevice_convert_identifier_to_proto(identifier, proto_identifier);

    VDevice_get_callback_id_Reply reply;
    grpc::ClientContext context;
    grpc::Status status = m_stub->VDevice_get_callback_id(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    if (reply.status() == HAILO_SHUTDOWN_EVENT_SIGNALED) {
        return make_unexpected(HAILO_SHUTDOWN_EVENT_SIGNALED);
    }
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    auto cb_id = reply.callback_id();
    return cb_id;
}

hailo_status HailoRtRpcClient::VDevice_finish_callback_listener(const VDeviceIdentifier &identifier)
{
    VDevice_finish_callback_listener_Request request;
    auto proto_identifier = request.mutable_identifier();
    VDevice_convert_identifier_to_proto(identifier, proto_identifier);

    VDevice_finish_callback_listener_Reply reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->VDevice_finish_callback_listener(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS(static_cast<hailo_status>(reply.status()));
    return HAILO_SUCCESS;
}

Expected<std::vector<std::string>> HailoRtRpcClient::VDevice_get_physical_devices_ids(const VDeviceIdentifier &identifier)
{
    VDevice_get_physical_devices_ids_Request request;
    auto proto_identifier = request.mutable_identifier();
    VDevice_convert_identifier_to_proto(identifier, proto_identifier);

    VDevice_get_physical_devices_ids_Reply reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->VDevice_get_physical_devices_ids(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    std::vector<std::string> result;
    for (auto &device_id_proto : reply.devices_ids()) {
        result.push_back(device_id_proto);
    }
    return result;
}

Expected<std::vector<std::unique_ptr<Device>>> HailoRtRpcClient::VDevice_get_physical_devices(const VDeviceIdentifier &identifier)
{
    std::vector<std::unique_ptr<Device>> devices;

    auto device_ids = VDevice_get_physical_devices_ids(identifier);
    CHECK_EXPECTED(device_ids);
    devices.reserve(device_ids->size());

    for (const auto &device_id : device_ids.value()) {
        auto device = Device::create(device_id);
        CHECK_EXPECTED(device);
        devices.push_back(std::move(device.release())) ;
    }
    return devices;
}

Expected<hailo_stream_interface_t> HailoRtRpcClient::VDevice_get_default_streams_interface(const VDeviceIdentifier &identifier)
{
    VDevice_get_default_streams_interface_Request request;
    auto proto_identifier = request.mutable_identifier();
    VDevice_convert_identifier_to_proto(identifier, proto_identifier);

    VDevice_get_default_streams_interface_Reply reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->VDevice_get_default_streams_interface(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    CHECK_AS_EXPECTED(reply.stream_interface() < HAILO_STREAM_INTERFACE_MAX_ENUM, HAILO_INTERNAL_FAILURE,
        "stream_interface {} out of range", reply.stream_interface());
    return static_cast<hailo_stream_interface_t>(reply.stream_interface());
}

Expected<uint32_t> HailoRtRpcClient::ConfiguredNetworkGroup_dup_handle(const NetworkGroupIdentifier &identifier, uint32_t pid)
{
    ConfiguredNetworkGroup_dup_handle_Request request;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_pid(pid);

    ConfiguredNetworkGroup_dup_handle_Reply reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_dup_handle(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    return reply.handle();
}

hailo_status HailoRtRpcClient::ConfiguredNetworkGroup_release(const NetworkGroupIdentifier &identifier, uint32_t pid)
{
    Release_Request request;
    auto proto_identifier = request.mutable_network_group_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_pid(pid);

    Release_Reply reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_release(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS(static_cast<hailo_status>(reply.status()));
    return HAILO_SUCCESS;
}

std::map<std::string, hailo_vstream_params_t> get_group(const ProtoNamedVStreamParamsMap &named_params_map)
{
    std::map<std::string, hailo_vstream_params_t> result;
    for (auto &named_params : named_params_map.vstream_params_map()) {
        auto name = named_params.name();
        auto proto_params = named_params.params();
        auto proto_user_buffer_format = proto_params.user_buffer_format();
        hailo_format_t user_buffer_format = {
            static_cast<hailo_format_type_t>(proto_user_buffer_format.type()),
            static_cast<hailo_format_order_t>(proto_user_buffer_format.order()),
            static_cast<hailo_format_flags_t>(proto_user_buffer_format.flags())
        };
        hailo_vstream_params_t params = {
            user_buffer_format,
            proto_params.timeout_ms(),
            proto_params.queue_size(),
            static_cast<hailo_vstream_stats_flags_t>(proto_params.vstream_stats_flags()),
            static_cast<hailo_pipeline_elem_stats_flags_t>(proto_params.pipeline_elements_stats_flags())
        };
        result.insert({name, params});
    }
    return result;
}

Expected<std::map<std::string, hailo_vstream_params_t>> HailoRtRpcClient::ConfiguredNetworkGroup_make_input_vstream_params(
    const NetworkGroupIdentifier &identifier, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
    const std::string &network_name)
{
    ConfiguredNetworkGroup_make_input_vstream_params_Request request;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_format_type(format_type);
    request.set_timeout_ms(timeout_ms);
    request.set_queue_size(queue_size);
    request.set_network_name(network_name);

    ConfiguredNetworkGroup_make_input_vstream_params_Reply reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_make_input_vstream_params(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    return get_group(reply.vstream_params_map());
}

Expected<std::vector<std::map<std::string, hailo_vstream_params_t>>> HailoRtRpcClient::ConfiguredNetworkGroup_make_output_vstream_params_groups(
    const NetworkGroupIdentifier &identifier, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size)
{
    ConfiguredNetworkGroup_make_output_vstream_params_groups_Request request;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_format_type(format_type);
    request.set_timeout_ms(timeout_ms);
    request.set_queue_size(queue_size);

    ConfiguredNetworkGroup_make_output_vstream_params_groups_Reply reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_make_output_vstream_params_groups(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    std::vector<std::map<std::string, hailo_vstream_params_t>> result;
    for (auto &map_proto : reply.vstream_params_groups()) {
        auto group = get_group(map_proto);
        result.push_back(group);
    }
    return result;
}

Expected<std::map<std::string, hailo_vstream_params_t>> HailoRtRpcClient::ConfiguredNetworkGroup_make_output_vstream_params(
    const NetworkGroupIdentifier &identifier, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
    const std::string &network_name)
{
    ConfiguredNetworkGroup_make_output_vstream_params_Request request;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_format_type(format_type);
    request.set_timeout_ms(timeout_ms);
    request.set_queue_size(queue_size);
    request.set_network_name(network_name);

    ConfiguredNetworkGroup_make_output_vstream_params_Reply reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_make_output_vstream_params(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    std::map<std::string, hailo_vstream_params_t> result;
    for (int i = 0; i < reply.vstream_params_map().vstream_params_map_size(); ++i) {
        auto name = reply.vstream_params_map().vstream_params_map(i).name();
        auto proto_params = reply.vstream_params_map().vstream_params_map(i).params();
        auto proto_user_buffer_format = proto_params.user_buffer_format();
        hailo_format_t user_buffer_format = {
            static_cast<hailo_format_type_t>(proto_user_buffer_format.type()),
            static_cast<hailo_format_order_t>(proto_user_buffer_format.order()),
            static_cast<hailo_format_flags_t>(proto_user_buffer_format.flags())
        };
        hailo_vstream_params_t params = {
            user_buffer_format,
            proto_params.timeout_ms(),
            proto_params.queue_size(),
            static_cast<hailo_vstream_stats_flags_t>(proto_params.vstream_stats_flags()),
            static_cast<hailo_pipeline_elem_stats_flags_t>(proto_params.pipeline_elements_stats_flags())
        };
        result.insert({name, params});
    }
    return result;
}

Expected<std::string> HailoRtRpcClient::ConfiguredNetworkGroup_get_network_group_name(const NetworkGroupIdentifier &identifier)
{
    return ConfiguredNetworkGroup_name(identifier);
}

Expected<std::string> HailoRtRpcClient::ConfiguredNetworkGroup_name(const NetworkGroupIdentifier &identifier)
{
    ConfiguredNetworkGroup_name_Request request;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);

    ConfiguredNetworkGroup_name_Reply reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_name(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    auto network_group_name = reply.network_group_name();
    return network_group_name;
}

Expected<std::vector<hailo_network_info_t>> HailoRtRpcClient::ConfiguredNetworkGroup_get_network_infos(const NetworkGroupIdentifier &identifier)
{
    ConfiguredNetworkGroup_get_network_infos_Request request;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);

    ConfiguredNetworkGroup_get_network_infos_Reply reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_get_network_infos(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    auto network_infos_proto = reply.network_infos();
    std::vector<hailo_network_info_t> network_infos;
    network_infos.reserve(network_infos_proto.size());
    for (auto& info_proto : network_infos_proto) {
        hailo_network_info_t info;
        strcpy(info.name, info_proto.c_str());
        network_infos.push_back(info);
    }
    return network_infos;
}

Expected<std::vector<hailo_stream_info_t>> HailoRtRpcClient::ConfiguredNetworkGroup_get_all_stream_infos(const NetworkGroupIdentifier &identifier,
    const std::string &network_name)
{
    ConfiguredNetworkGroup_get_all_stream_infos_Request request;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_network_name(network_name);

    ConfiguredNetworkGroup_get_all_stream_infos_Reply reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_get_all_stream_infos(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    std::vector<hailo_stream_info_t> result;
    result.reserve(reply.stream_infos().size());
    for (auto proto_stream_info : reply.stream_infos()) {
        hailo_3d_image_shape_t shape{
            proto_stream_info.stream_shape().shape().height(),
            proto_stream_info.stream_shape().shape().width(),
            proto_stream_info.stream_shape().shape().features(),
        };
        hailo_3d_image_shape_t hw_shape{
            proto_stream_info.stream_shape().hw_shape().height(),
            proto_stream_info.stream_shape().hw_shape().width(),
            proto_stream_info.stream_shape().hw_shape().features(),
        };
        hailo_nms_defuse_info_t nms_defuse_info{
            proto_stream_info.nms_info().defuse_info().class_group_index(),
            {0}
        };
        strcpy(nms_defuse_info.original_name, proto_stream_info.nms_info().defuse_info().original_name().c_str());
        hailo_nms_info_t nms_info{
            proto_stream_info.nms_info().number_of_classes(),
            proto_stream_info.nms_info().max_bboxes_per_class(),
            proto_stream_info.nms_info().max_bboxes_total(),
            proto_stream_info.nms_info().bbox_size(),
            proto_stream_info.nms_info().chunks_per_frame(),
            proto_stream_info.nms_info().is_defused(),
            nms_defuse_info,
            proto_stream_info.nms_info().burst_size(),
            static_cast<hailo_nms_burst_type_t>(proto_stream_info.nms_info().burst_type())
        };
        hailo_format_t format{
            static_cast<hailo_format_type_t>(proto_stream_info.format().type()),
            static_cast<hailo_format_order_t>(proto_stream_info.format().order()),
            static_cast<hailo_format_flags_t>(proto_stream_info.format().flags())
        };
        hailo_quant_info_t quant_info{
            proto_stream_info.quant_info().qp_zp(),
            proto_stream_info.quant_info().qp_scale(),
            proto_stream_info.quant_info().limvals_min(),
            proto_stream_info.quant_info().limvals_max()
        };
        hailo_stream_info_t stream_info;
        if (format.order == HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP) {
            stream_info.nms_info = nms_info;
        } else {
            stream_info.shape = shape;
            stream_info.hw_shape = hw_shape;
        }
        stream_info.hw_data_bytes = proto_stream_info.hw_data_bytes();
        stream_info.hw_frame_size = proto_stream_info.hw_frame_size();
        stream_info.format = format;
        stream_info.direction = static_cast<hailo_stream_direction_t>(proto_stream_info.direction());
        stream_info.index = static_cast<uint8_t>(proto_stream_info.index());
        strcpy(stream_info.name, proto_stream_info.name().c_str());
        stream_info.quant_info = quant_info;
        stream_info.is_mux = proto_stream_info.is_mux();
        result.push_back(stream_info);
    }
    return result;
}

Expected<hailo_stream_interface_t> HailoRtRpcClient::ConfiguredNetworkGroup_get_default_stream_interface(const NetworkGroupIdentifier &identifier)
{
    ConfiguredNetworkGroup_get_default_stream_interface_Request request;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);

    ConfiguredNetworkGroup_get_default_stream_interface_Reply reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_get_default_stream_interface(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    auto stream_interface = static_cast<hailo_stream_interface_t>(reply.stream_interface());
    return stream_interface;
}

hailo_status HailoRtRpcClient::ConfiguredNetworkGroup_shutdown(const NetworkGroupIdentifier &identifier)
{
    ConfiguredNetworkGroup_shutdown_Request request;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);

    ClientContextWithTimeout context;
    ConfiguredNetworkGroup_shutdown_Reply reply;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_shutdown(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    return static_cast<hailo_status>(reply.status());
}

Expected<std::vector<std::vector<std::string>>> HailoRtRpcClient::ConfiguredNetworkGroup_get_output_vstream_groups(const NetworkGroupIdentifier &identifier)
{
    ConfiguredNetworkGroup_get_output_vstream_groups_Request request;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);

    ConfiguredNetworkGroup_get_output_vstream_groups_Reply reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_get_output_vstream_groups(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    auto vstream_groups_proto = reply.output_vstream_groups();
    std::vector<std::vector<std::string>> result;
    result.reserve(vstream_groups_proto.size());
    for (auto& vstream_group_proto : vstream_groups_proto) {
        std::vector<std::string> group;
        group.reserve(vstream_group_proto.vstream_group().size());
        for (auto& name : vstream_group_proto.vstream_group()) {
            group.push_back(name);
        }
        result.push_back(group);
    }
    return result;
}

std::pair<std::string, hailort::net_flow::BufferMetaData> deserialize_buffer_metadata(const ProtoNamedMetadata &op_metadata_proto)
{
    auto &named_params_proto = op_metadata_proto.params();

    hailo_3d_image_shape_t shape = {
        named_params_proto.shape().height(),
        named_params_proto.shape().width(),
        named_params_proto.shape().features()
    };

    hailo_3d_image_shape_t padded_shape = {
        named_params_proto.padded_shape().height(),
        named_params_proto.padded_shape().width(),
        named_params_proto.padded_shape().features()
    };

    hailo_format_t format = {
        static_cast<hailo_format_type_t>(named_params_proto.format().type()),
        static_cast<hailo_format_order_t>(named_params_proto.format().order()),
        static_cast<hailo_format_flags_t>(named_params_proto.format().flags())
    };

    hailo_quant_info_t single_quant_info = {
        named_params_proto.quant_info().qp_zp(),
        named_params_proto.quant_info().qp_scale(),
        named_params_proto.quant_info().limvals_min(),
        named_params_proto.quant_info().limvals_max()
    };

    std::pair<std::string, hailort::net_flow::BufferMetaData> named_metadata_to_insert(op_metadata_proto.name(),
                                                                {shape, padded_shape, format, single_quant_info});
    return named_metadata_to_insert;
}

std::unordered_map<std::string, hailort::net_flow::BufferMetaData> deserialize_inputs_buffer_metadata(const ProtoOpMetadata &ops_metadatas_proto)
{
    std::unordered_map<std::string, hailort::net_flow::BufferMetaData> inputs_metadata;
    auto &inputs_metadata_proto = ops_metadatas_proto.inputs_metadata();
    for (auto &input_metadata_proto : inputs_metadata_proto) {
        auto input_metadata = deserialize_buffer_metadata(input_metadata_proto);
        inputs_metadata.insert(input_metadata);
    }
    return inputs_metadata;
}

std::unordered_map<std::string, hailort::net_flow::BufferMetaData> deserialize_outputs_buffer_metadata(const ProtoOpMetadata &ops_metadatas_proto)
{
    std::unordered_map<std::string, hailort::net_flow::BufferMetaData> outputs_metadata;
    auto &outputs_metadata_proto = ops_metadatas_proto.outputs_metadata();
    for (auto &output_metadata_proto : outputs_metadata_proto) {
        auto output_metadata = deserialize_buffer_metadata(output_metadata_proto);
        outputs_metadata.insert(output_metadata);
    }
    return outputs_metadata;
}

Expected<hailort::net_flow::Yolov8PostProcessConfig> create_yolov8_post_process_config(const ProtoOpMetadata &op_metadata_proto)
{
    auto yolov8_config_proto = op_metadata_proto.yolov8_config();
    std::vector<hailort::net_flow::Yolov8MatchingLayersNames> reg_to_cls_inputs;
    auto &reg_to_cls_inputs_proto = yolov8_config_proto.reg_to_cls_inputs();
    for (auto &reg_to_cls_input_proto : reg_to_cls_inputs_proto) {
        hailort::net_flow::Yolov8MatchingLayersNames yolov8_matching_layers_name;
        yolov8_matching_layers_name.reg = reg_to_cls_input_proto.reg();
        yolov8_matching_layers_name.cls = reg_to_cls_input_proto.cls();
        yolov8_matching_layers_name.stride = reg_to_cls_input_proto.stride();
        reg_to_cls_inputs.push_back(yolov8_matching_layers_name);
    }

    hailort::net_flow::Yolov8PostProcessConfig yolov8_post_process_config = {yolov8_config_proto.image_height(),
                                                            yolov8_config_proto.image_width(), reg_to_cls_inputs};
    return yolov8_post_process_config;
}

Expected<hailort::net_flow::YoloPostProcessConfig> create_yolov5_post_process_config(const ProtoOpMetadata &op_metadata_proto)
{
    auto yolov5_config_proto = op_metadata_proto.yolov5_config();
    std::map<std::string, std::vector<int>> anchors_per_layer;
    auto &yolov5_anchors_list_proto = yolov5_config_proto.yolov5_anchors();
    for (auto &anchors_list_proto : yolov5_anchors_list_proto) {
        std::vector<int> anchors;
        for(auto &anchor : anchors_list_proto.anchors()) {
            anchors.push_back(anchor);
        }
        anchors_per_layer.emplace(anchors_list_proto.layer(), anchors);
    }

    hailort::net_flow::YoloPostProcessConfig yolov5_post_process_config = {yolov5_config_proto.image_height(),
                                                            yolov5_config_proto.image_width(), anchors_per_layer};
    return yolov5_post_process_config;
}

Expected<hailort::net_flow::YoloxPostProcessConfig> create_yolox_post_process_config(const ProtoOpMetadata &op_metadata_proto)
{
    auto yolox_config_proto = op_metadata_proto.yolox_config();
    std::vector<hailort::net_flow::YoloxMatchingLayersNames> input_names;
    auto &yolox_anchors_list_proto = yolox_config_proto.input_names();
    for (auto &input_name : yolox_anchors_list_proto) {
        input_names.push_back({input_name.reg(), input_name.obj(), input_name.cls()});
    }

    hailort::net_flow::YoloxPostProcessConfig yolox_post_process_config = {yolox_config_proto.image_height(),
                                                            yolox_config_proto.image_width(), input_names};
    return yolox_post_process_config;
}

Expected<hailort::net_flow::SSDPostProcessConfig> create_ssd_post_process_config(const ProtoOpMetadata &op_metadata_proto)
{
    auto ssd_config_proto = op_metadata_proto.ssd_config();
    std::map<std::string, std::string> reg_to_cls_inputs;
    auto &ssd_reg_to_cls_proto = ssd_config_proto.reg_to_cls_inputs();
    for (auto &reg_to_cls_input : ssd_reg_to_cls_proto) {
        reg_to_cls_inputs.emplace(reg_to_cls_input.reg(), reg_to_cls_input.cls());
    }

    std::map<std::string, std::vector<float32_t>> anchors_per_layer;
    auto &ssd_anchors_proto = ssd_config_proto.anchors();
    for (auto &ssd_anchors : ssd_anchors_proto) {
        std::vector<float32_t> anchors;
        for (auto &anchor : ssd_anchors.anchors_per_layer()) {
            anchors.push_back(anchor);
        }
        anchors_per_layer.emplace(ssd_anchors.layer(), anchors);
    }

    hailort::net_flow::SSDPostProcessConfig ssd_post_process_config = {ssd_config_proto.image_height(), ssd_config_proto.image_width(),
                                                                        ssd_config_proto.centers_scale_factor(),
                                                                        ssd_config_proto.bbox_dimensions_scale_factor(),
                                                                        ssd_config_proto.ty_index(), ssd_config_proto.tx_index(),
                                                                        ssd_config_proto.th_index(), ssd_config_proto.tw_index(),
                                                                        reg_to_cls_inputs, anchors_per_layer, ssd_config_proto.normalize_boxes()};
    return ssd_post_process_config;
}

Expected<hailort::net_flow::YoloV5SegPostProcessConfig> create_yolov5seg_post_process_config(const ProtoOpMetadata &op_metadata_proto)
{
    auto yolov5seg_config_proto = op_metadata_proto.yolov5seg_config();
    hailort::net_flow::YoloV5SegPostProcessConfig yolov5seg_post_process_config = {yolov5seg_config_proto.mask_threshold(),
        yolov5seg_config_proto.max_accumulated_mask_size(), yolov5seg_config_proto.layer_name()};
    return yolov5seg_post_process_config;
}

Expected<std::vector<net_flow::PostProcessOpMetadataPtr>> deserialize_ops_metadata(const ProtoOpsMetadata &ops_metadatas_proto)
{
    std::vector<hailort::net_flow::PostProcessOpMetadataPtr> ops_metadata_ptr;
    auto ops_metadata_proto = ops_metadatas_proto.ops_metadata();
    for (auto &op_metadata_proto : ops_metadata_proto) {
        auto inputs_metadata = deserialize_inputs_buffer_metadata(op_metadata_proto);
        auto outputs_metadata = deserialize_outputs_buffer_metadata(op_metadata_proto);

        hailort::net_flow::NmsPostProcessConfig nms_post_process_config;
        if ((op_metadata_proto.type() == static_cast<uint32_t>(net_flow::OperationType::YOLOV5)) |
            (op_metadata_proto.type() == static_cast<uint32_t>(net_flow::OperationType::YOLOV8)) |
            (op_metadata_proto.type() == static_cast<uint32_t>(net_flow::OperationType::YOLOX)) |
            (op_metadata_proto.type() == static_cast<uint32_t>(net_flow::OperationType::IOU)) |
            (op_metadata_proto.type() == static_cast<uint32_t>(net_flow::OperationType::SSD)) |
            (op_metadata_proto.type() == static_cast<uint32_t>(net_flow::OperationType::YOLOV5SEG))) {
            // In case this is an NMS PP - initilize the values for the nms post process config
            auto &nms_config_proto = op_metadata_proto.nms_post_process_config();
            nms_post_process_config = {nms_config_proto.nms_score_th(),
                                        nms_config_proto.nms_iou_th(),
                                        nms_config_proto.max_proposals_per_class(),
                                        nms_config_proto.max_proposals_total(),
                                        nms_config_proto.number_of_classes(),
                                        nms_config_proto.background_removal(),
                                        nms_config_proto.background_removal_index(),
                                        nms_config_proto.bbox_only()};
            }

        switch (static_cast<net_flow::OperationType>(op_metadata_proto.type())) {
        case net_flow::OperationType::YOLOV8:
        {
            TRY(auto yolov8_post_process_config, create_yolov8_post_process_config(op_metadata_proto));
            if (nms_post_process_config.bbox_only) {
                TRY(auto yolov8_bbox_only_metadata, hailort::net_flow::Yolov8BboxOnlyOpMetadata::create(inputs_metadata, outputs_metadata, nms_post_process_config,
                        yolov8_post_process_config, op_metadata_proto.network_name()));
                ops_metadata_ptr.push_back(yolov8_bbox_only_metadata);
            } else {
                TRY(auto yolov8_metadata, hailort::net_flow::Yolov8OpMetadata::create(inputs_metadata, outputs_metadata, nms_post_process_config,
                        yolov8_post_process_config, op_metadata_proto.network_name()));
                ops_metadata_ptr.push_back(yolov8_metadata);
            }
            break;
        }

        case net_flow::OperationType::YOLOV5:
        {
            TRY(auto yolov5_post_process_config, create_yolov5_post_process_config(op_metadata_proto));
            if (nms_post_process_config.bbox_only) {
                TRY(auto yolov5_bbox_only_metadata, hailort::net_flow::Yolov5BboxOnlyOpMetadata::create(inputs_metadata, outputs_metadata, nms_post_process_config,
                        yolov5_post_process_config, op_metadata_proto.network_name()));
                ops_metadata_ptr.push_back(yolov5_bbox_only_metadata);
            } else {
                TRY(auto yolov5_metadata, hailort::net_flow::Yolov5OpMetadata::create(inputs_metadata, outputs_metadata, nms_post_process_config,
                        yolov5_post_process_config, op_metadata_proto.network_name()));
                ops_metadata_ptr.push_back(yolov5_metadata);
            }
            break;
        }

        case net_flow::OperationType::YOLOX:
        {
            auto expected_yolox_post_process_config = create_yolox_post_process_config(op_metadata_proto);
            CHECK_EXPECTED(expected_yolox_post_process_config);
            auto expected_yolox_metadata = hailort::net_flow::YoloxOpMetadata::create(inputs_metadata, outputs_metadata, nms_post_process_config,
                                                                                        expected_yolox_post_process_config.value(),
                                                                                        op_metadata_proto.network_name());
            CHECK_EXPECTED(expected_yolox_metadata);
            ops_metadata_ptr.push_back(expected_yolox_metadata.value());
            break;
        }

        case net_flow::OperationType::SSD:
        {
            auto expected_ssd_post_process_config = create_ssd_post_process_config(op_metadata_proto);
            CHECK_EXPECTED(expected_ssd_post_process_config);
            auto expteted_ssd_metadata = hailort::net_flow::SSDOpMetadata::create(inputs_metadata, outputs_metadata, nms_post_process_config,
                                                                                    expected_ssd_post_process_config.value(),
                                                                                    op_metadata_proto.network_name());
            CHECK_EXPECTED(expteted_ssd_metadata);
            ops_metadata_ptr.push_back(expteted_ssd_metadata.value());
            break;
        }

        case net_flow::OperationType::SOFTMAX:
        {
            auto expteted_softmax_metadata = hailort::net_flow::SoftmaxOpMetadata::create(inputs_metadata, outputs_metadata,
                                                                                            op_metadata_proto.network_name());
            CHECK_EXPECTED(expteted_softmax_metadata);
            ops_metadata_ptr.push_back(expteted_softmax_metadata.value());
            break;
        }

        case net_flow::OperationType::ARGMAX:
        {
            auto expteted_argmax_metadata = hailort::net_flow::ArgmaxOpMetadata::create(inputs_metadata, outputs_metadata,
                                                                                        op_metadata_proto.network_name());
            CHECK_EXPECTED(expteted_argmax_metadata);
            ops_metadata_ptr.push_back(expteted_argmax_metadata.value());
            break;
        }

        case net_flow::OperationType::YOLOV5SEG:
        {
            auto expected_yolov5_post_process_config = create_yolov5_post_process_config(op_metadata_proto);
            CHECK_EXPECTED(expected_yolov5_post_process_config);

            auto expected_yolov5seg_post_process_config = create_yolov5seg_post_process_config(op_metadata_proto);
            CHECK_EXPECTED(expected_yolov5seg_post_process_config);

            auto expected_yolov5seg_metadata = hailort::net_flow::Yolov5SegOpMetadata::create(inputs_metadata, outputs_metadata, nms_post_process_config,
                                                                                                expected_yolov5_post_process_config.value(),
                                                                                                expected_yolov5seg_post_process_config.value(),
                                                                                                op_metadata_proto.network_name());
            CHECK_EXPECTED(expected_yolov5seg_metadata);
            ops_metadata_ptr.push_back(expected_yolov5seg_metadata.value());
            break;
        }

        case net_flow::OperationType::IOU:
        {
            auto expected_nms_op_metadata = hailort::net_flow::NmsOpMetadata::create(inputs_metadata, outputs_metadata, nms_post_process_config,
                                                                                        op_metadata_proto.network_name(),
                                                                                        static_cast<hailort::net_flow::OperationType>(op_metadata_proto.type()),
                                                                                        op_metadata_proto.name());
            CHECK_EXPECTED(expected_nms_op_metadata);
            ops_metadata_ptr.push_back(expected_nms_op_metadata.value());
            break;
        }
        }
    }
    return ops_metadata_ptr;
}

LayerInfo deserialize_layer_info(const ProtoLayerInfo &info_proto)
{
    LayerInfo info;
    info.type = static_cast<LayerType>(info_proto.type());
    info.direction = static_cast<hailo_stream_direction_t>(info_proto.direction());
    info.stream_index = static_cast<uint8_t>(info_proto.stream_index());
    info.dma_engine_index = static_cast<uint8_t>(info_proto.dma_engine_index());
    info.name = info_proto.name();
    info.network_name = info_proto.network_name();
    info.network_index = static_cast<uint8_t>(info_proto.network_index());
    info.max_shmifo_size = info_proto.max_shmifo_size();
    info.context_index = static_cast<uint8_t>(info_proto.context_index());
    info.pad_index = info_proto.pad_index();

    // Transformation and shape info
    hailo_3d_image_shape_t shape = {
        info_proto.shape().height(),
        info_proto.shape().width(),
        info_proto.shape().features()
    };
    info.shape = shape;
    
    hailo_3d_image_shape_t hw_shape = {
        info_proto.hw_shape().height(),
        info_proto.hw_shape().width(),
        info_proto.hw_shape().features()
    };
    info.hw_shape = hw_shape;

    info.hw_data_bytes = info_proto.hw_data_bytes();

    hailo_format_t format = {
        static_cast<hailo_format_type_t>(info_proto.format().type()),
        static_cast<hailo_format_order_t>(info_proto.format().order()),
        static_cast<hailo_format_flags_t>(info_proto.format().flags())
    };
    info.format = format;

    hailo_quant_info_t single_quant_info = {
        info_proto.quant_info().qp_zp(),
        info_proto.quant_info().qp_scale(),
        info_proto.quant_info().limvals_min(),
        info_proto.quant_info().limvals_max()
    };
    info.quant_info = single_quant_info;

    for (const auto &quant_info : info_proto.quant_infos()) {
        single_quant_info = {
            quant_info.qp_zp(),
            quant_info.qp_scale(),
            quant_info.limvals_min(),
            quant_info.limvals_max()
        };
        info.quant_infos.push_back(single_quant_info);
    }

    hailo_nms_defuse_info_t nms_defuse_info{
        info_proto.nms_info().defuse_info().class_group_index(),
        {0}
    };
    strcpy(nms_defuse_info.original_name, info_proto.nms_info().defuse_info().original_name().c_str());
    hailo_nms_info_t nms_info{
        info_proto.nms_info().number_of_classes(),
        info_proto.nms_info().max_bboxes_per_class(),
        info_proto.nms_info().max_bboxes_total(),
        info_proto.nms_info().bbox_size(),
        info_proto.nms_info().chunks_per_frame(),
        info_proto.nms_info().is_defused(),
        nms_defuse_info,
        info_proto.nms_info().burst_size(),
        static_cast<hailo_nms_burst_type_t>(info_proto.nms_info().burst_type())
    };
    info.nms_info = nms_info;


    // Mux info
    info.is_mux = info_proto.is_mux();
    for (const auto &pred_proto : info_proto.predecessor()) {
        auto pred = deserialize_layer_info(pred_proto);
        info.predecessor.push_back(pred);
    }
    info.height_gcd = info_proto.height_gcd();
    for (const auto &height_ratio : info_proto.height_ratios()) {
        info.height_ratios.push_back(height_ratio);
    }

    // Multi planes info
    info.is_multi_planar = info_proto.is_multi_planar();
    for (const auto &planes_proto : info_proto.planes()) {
        auto plane = deserialize_layer_info(planes_proto);
        info.planes.push_back(plane);
    }
    info.plane_index = static_cast<uint8_t>(info_proto.plane_index());

    // Defused nms info
    info.is_defused_nms = info_proto.is_defused_nms();
    for (const auto &fused_proto : info_proto.fused_nms_layer()) {
        auto fused = deserialize_layer_info(fused_proto);
        info.fused_nms_layer.push_back(fused);
    }

    return info;
}

hailo_vstream_info_t deserialize_vstream_info(const ProtoVStreamInfo &info_proto)
{
    hailo_vstream_info_t info;
    strcpy(info.name, info_proto.name().c_str());
    strcpy(info.network_name, info_proto.network_name().c_str());
    info.direction = static_cast<hailo_stream_direction_t>(info_proto.direction());
    hailo_format_t format = {
        static_cast<hailo_format_type_t>(info_proto.format().type()),
        static_cast<hailo_format_order_t>(info_proto.format().order()),
        static_cast<hailo_format_flags_t>(info_proto.format().flags())
    };
    info.format = format;
    if (HailoRTCommon::is_nms(format.order)) {
        hailo_nms_shape_t nms_shape = {
            info_proto.nms_shape().number_of_classes(),
            info_proto.nms_shape().max_bboxes_per_class(),
            info_proto.nms_shape().max_bboxes_total(),
            info_proto.nms_shape().max_accumulated_mask_size()
        };
        info.nms_shape = nms_shape;
    } else {
        hailo_3d_image_shape_t shape = {
            info_proto.shape().height(),
            info_proto.shape().width(),
            info_proto.shape().features()
        };
        info.shape = shape;
    }
    hailo_quant_info_t quant_info = {
        info_proto.quant_info().qp_zp(),
        info_proto.quant_info().qp_scale(),
        info_proto.quant_info().limvals_min(),
        info_proto.quant_info().limvals_max()
    };
    info.quant_info = quant_info;
    return info;
}

Expected<std::vector<hailo_vstream_info_t>> deserialize_vstream_infos(const ConfiguredNetworkGroup_get_vstream_infos_Reply &reply)
{
    std::vector<hailo_vstream_info_t> result;
    result.reserve(reply.vstream_infos().size());
    for (auto& info_proto : reply.vstream_infos()) {
        auto info = deserialize_vstream_info(info_proto);
        result.push_back(info);
    }
    return result;
} 

Expected<std::vector<hailo_vstream_info_t>> HailoRtRpcClient::ConfiguredNetworkGroup_get_input_vstream_infos(const NetworkGroupIdentifier &identifier,
    std::string network_name)
{
    ConfiguredNetworkGroup_get_vstream_infos_Request request;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_network_name(network_name);

    ConfiguredNetworkGroup_get_vstream_infos_Reply reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_get_input_vstream_infos(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    return deserialize_vstream_infos(reply);
}

Expected<std::vector<hailo_vstream_info_t>> HailoRtRpcClient::ConfiguredNetworkGroup_get_output_vstream_infos(const NetworkGroupIdentifier &identifier,
    std::string network_name)
{
    ConfiguredNetworkGroup_get_vstream_infos_Request request;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_network_name(network_name);

    ConfiguredNetworkGroup_get_vstream_infos_Reply reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_get_output_vstream_infos(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    return deserialize_vstream_infos(reply);
}

Expected<std::vector<hailo_vstream_info_t>> HailoRtRpcClient::ConfiguredNetworkGroup_get_all_vstream_infos(const NetworkGroupIdentifier &identifier,
    std::string network_name)
{
    ConfiguredNetworkGroup_get_vstream_infos_Request request;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_network_name(network_name);

    ConfiguredNetworkGroup_get_vstream_infos_Reply reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_get_all_vstream_infos(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    return deserialize_vstream_infos(reply);
}

Expected<bool> HailoRtRpcClient::ConfiguredNetworkGroup_is_scheduled(const NetworkGroupIdentifier &identifier)
{
    ConfiguredNetworkGroup_is_scheduled_Request request;
    ConfiguredNetworkGroup_is_scheduled_Reply reply;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_is_scheduled(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    return reply.is_scheduled();
}

hailo_status HailoRtRpcClient::ConfiguredNetworkGroup_set_scheduler_timeout(const NetworkGroupIdentifier &identifier,
    const std::chrono::milliseconds &timeout, const std::string &network_name)
{
    ConfiguredNetworkGroup_set_scheduler_timeout_Request request;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_timeout_ms(static_cast<uint32_t>(timeout.count()));
    request.set_network_name(network_name);

    ConfiguredNetworkGroup_set_scheduler_timeout_Reply reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_set_scheduler_timeout(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    return static_cast<hailo_status>(reply.status());
}

hailo_status HailoRtRpcClient::ConfiguredNetworkGroup_set_scheduler_threshold(const NetworkGroupIdentifier &identifier, uint32_t threshold,
    const std::string &network_name)
{
    ConfiguredNetworkGroup_set_scheduler_threshold_Request request;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_threshold(threshold);
    request.set_network_name(network_name);

    ConfiguredNetworkGroup_set_scheduler_threshold_Reply reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_set_scheduler_threshold(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    return static_cast<hailo_status>(reply.status());
}

hailo_status HailoRtRpcClient::ConfiguredNetworkGroup_set_scheduler_priority(const NetworkGroupIdentifier &identifier, uint8_t priority,
    const std::string &network_name)
{
    ConfiguredNetworkGroup_set_scheduler_priority_Request request;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_priority(priority);
    request.set_network_name(network_name);

    ConfiguredNetworkGroup_set_scheduler_priority_Reply reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_set_scheduler_priority(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    return static_cast<hailo_status>(reply.status());
}

Expected<LatencyMeasurementResult> HailoRtRpcClient::ConfiguredNetworkGroup_get_latency_measurement(const NetworkGroupIdentifier &identifier,
    const std::string &network_name)
{
    ConfiguredNetworkGroup_get_latency_measurement_Request request;
    ConfiguredNetworkGroup_get_latency_measurement_Reply reply;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_network_name(network_name);
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_get_latency_measurement(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    if (HAILO_NOT_AVAILABLE == reply.status()) {
        return make_unexpected(HAILO_NOT_AVAILABLE);
    }
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    LatencyMeasurementResult result{
        std::chrono::nanoseconds(reply.avg_hw_latency())
    };
    return result;
}

Expected<bool> HailoRtRpcClient::ConfiguredNetworkGroup_is_multi_context(const NetworkGroupIdentifier &identifier)
{
    ConfiguredNetworkGroup_is_multi_context_Request request;
    ConfiguredNetworkGroup_is_multi_context_Reply reply;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_is_multi_context(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    return reply.is_multi_context();
}

Expected<ConfigureNetworkParams> HailoRtRpcClient::ConfiguredNetworkGroup_get_config_params(const NetworkGroupIdentifier &identifier)
{
    ConfiguredNetworkGroup_get_config_params_Request request;
    ConfiguredNetworkGroup_get_config_params_Reply reply;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_get_config_params(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    auto proto_configure_params = reply.params();
    ConfigureNetworkParams network_configure_params{};
    network_configure_params.batch_size = static_cast<uint16_t>(proto_configure_params.batch_size());
    network_configure_params.power_mode = static_cast<hailo_power_mode_t>(proto_configure_params.power_mode());
    network_configure_params.latency = static_cast<hailo_latency_measurement_flags_t>(proto_configure_params.latency());
    for (auto &proto_name_streams_params_pair : proto_configure_params.stream_params_map()) {
        auto proto_streams_params = proto_name_streams_params_pair.params();
        auto stream_direction = static_cast<hailo_stream_direction_t>(proto_streams_params.direction());
        hailo_stream_parameters_t stream_params{};
        stream_params.stream_interface = static_cast<hailo_stream_interface_t>(proto_streams_params.stream_interface());
        stream_params.direction = stream_direction;
        stream_params.flags = static_cast<hailo_stream_flags_t>(proto_streams_params.flags());
        if (stream_direction == HAILO_H2D_STREAM) {
            stream_params.pcie_input_params = {0};
        } else {
            stream_params.pcie_output_params = {0};
        }
        network_configure_params.stream_params_by_name.insert({proto_name_streams_params_pair.name(), stream_params});
    }
    for (auto &proto_name_network_params_pair : proto_configure_params.network_params_map()) {
        auto proto_network_params = proto_name_network_params_pair.params();
        hailo_network_parameters_t net_params {
            static_cast<uint16_t>(proto_network_params.batch_size())
        };

        network_configure_params.network_params_by_name.insert({proto_name_network_params_pair.name(), net_params});
    }
    return network_configure_params;
}

Expected<std::vector<std::string>> HailoRtRpcClient::ConfiguredNetworkGroup_get_sorted_output_names(const NetworkGroupIdentifier &identifier)
{
    ConfiguredNetworkGroup_get_sorted_output_names_Request request;
    ConfiguredNetworkGroup_get_sorted_output_names_Reply reply;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_get_sorted_output_names(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    std::vector<std::string> result;
    for (auto &name : reply.sorted_output_names()) {
        result.push_back(name);
    }
    return result;
}

Expected<size_t> HailoRtRpcClient::ConfiguredNetworkGroup_infer_queue_size(const NetworkGroupIdentifier &identifier)
{
    ConfiguredNetworkGroup_infer_queue_size_Request request;
    ConfiguredNetworkGroup_infer_queue_size_Reply reply;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_infer_queue_size(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    auto queue_size = reply.infer_queue_size();
    return queue_size;
}

Expected<std::unique_ptr<LayerInfo>> HailoRtRpcClient::ConfiguredNetworkGroup_get_layer_info(const NetworkGroupIdentifier &identifier, const std::string &stream_name)
{
    ConfiguredNetworkGroup_get_layer_info_Request request;
    ConfiguredNetworkGroup_get_layer_info_Reply reply;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_stream_name(stream_name);
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_get_layer_info(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    auto info_proto = reply.layer_info();
    auto layer = deserialize_layer_info(info_proto);
    auto layer_ptr = make_unique_nothrow<LayerInfo>(std::move(layer));
    CHECK_NOT_NULL_AS_EXPECTED(layer_ptr, HAILO_OUT_OF_HOST_MEMORY);
    return layer_ptr;
}

Expected<std::vector<net_flow::PostProcessOpMetadataPtr>> HailoRtRpcClient::ConfiguredNetworkGroup_get_ops_metadata(const NetworkGroupIdentifier &identifier)
{
    ConfiguredNetworkGroup_get_ops_metadata_Request request;
    ConfiguredNetworkGroup_get_ops_metadata_Reply reply;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_get_ops_metadata(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    auto ops_meta_data_proto = reply.ops_metadata();
    auto ops_metadata = deserialize_ops_metadata(ops_meta_data_proto);
    CHECK_EXPECTED(ops_metadata);
    return ops_metadata;
}

hailo_status HailoRtRpcClient::ConfiguredNetworkGroup_set_nms_score_threshold(const NetworkGroupIdentifier &identifier,
                                                                                const std::string &edge_name, float32_t nms_score_th)
{
    ConfiguredNetworkGroup_set_nms_score_threshold_Request request;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_edge_name(edge_name);
    request.set_nms_score_th(nms_score_th);

    ConfiguredNetworkGroup_set_nms_score_threshold_Reply reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_set_nms_score_threshold(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    return static_cast<hailo_status>(reply.status());
}

hailo_status HailoRtRpcClient::ConfiguredNetworkGroup_set_nms_iou_threshold(const NetworkGroupIdentifier &identifier,
                                                                                const std::string &edge_name, float32_t nms_iou_threshold)
{
    ConfiguredNetworkGroup_set_nms_iou_threshold_Request request;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_edge_name(edge_name);
    request.set_nms_iou_th(nms_iou_threshold);

    ConfiguredNetworkGroup_set_nms_iou_threshold_Reply reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_set_nms_iou_threshold(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    return static_cast<hailo_status>(reply.status());
}

hailo_status HailoRtRpcClient::ConfiguredNetworkGroup_set_nms_max_bboxes_per_class(const NetworkGroupIdentifier &identifier,
    const std::string &edge_name, uint32_t max_bboxes)
{
    ConfiguredNetworkGroup_set_nms_max_bboxes_per_class_Request request;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_edge_name(edge_name);
    request.set_nms_max_bboxes_per_class(max_bboxes);

    ConfiguredNetworkGroup_set_nms_max_bboxes_per_class_Reply reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_set_nms_max_bboxes_per_class(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    return static_cast<hailo_status>(reply.status());
}

hailo_status HailoRtRpcClient::ConfiguredNetworkGroup_set_nms_max_bboxes_total(const NetworkGroupIdentifier &identifier,
    const std::string &edge_name, uint32_t max_bboxes)
{
    ConfiguredNetworkGroup_set_nms_max_bboxes_total_Request request;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_edge_name(edge_name);
    request.set_nms_max_bboxes_total(max_bboxes);
    ConfiguredNetworkGroup_set_nms_max_bboxes_total_Reply reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_set_nms_max_bboxes_total(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    return static_cast<hailo_status>(reply.status());
}

hailo_status HailoRtRpcClient::ConfiguredNetworkGroup_set_nms_max_accumulated_mask_size(const NetworkGroupIdentifier &identifier,
    const std::string &edge_name, uint32_t max_accumulated_mask_size)
{
    ConfiguredNetworkGroup_set_nms_max_accumulated_mask_size_Request request;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_edge_name(edge_name);
    request.set_max_accumulated_mask_size(max_accumulated_mask_size);

    ConfiguredNetworkGroup_set_nms_max_accumulated_mask_size_Reply reply;
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_set_nms_max_accumulated_mask_size(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    return static_cast<hailo_status>(reply.status());
}

Expected<std::vector<std::string>> HailoRtRpcClient::ConfiguredNetworkGroup_get_stream_names_from_vstream_name(const NetworkGroupIdentifier &identifier,
    const std::string &vstream_name)
{
    ConfiguredNetworkGroup_get_stream_names_from_vstream_name_Request request;
    ConfiguredNetworkGroup_get_stream_names_from_vstream_name_Reply reply;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_vstream_name(vstream_name);
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_get_stream_names_from_vstream_name(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    std::vector<std::string> result;
    for (auto &name : reply.streams_names()) {
        result.push_back(name);
    }
    return result;
}

Expected<std::vector<std::string>> HailoRtRpcClient::ConfiguredNetworkGroup_get_vstream_names_from_stream_name(const NetworkGroupIdentifier &identifier, const std::string &stream_name)
{
    ConfiguredNetworkGroup_get_vstream_names_from_stream_name_Request request;
    ConfiguredNetworkGroup_get_vstream_names_from_stream_name_Reply reply;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_stream_name(stream_name);
    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_get_vstream_names_from_stream_name(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    std::vector<std::string> result;
    for (auto &name : reply.vstreams_names()) {
        result.push_back(name);
    }
    return result;
}

hailo_status HailoRtRpcClient::ConfiguredNetworkGroup_infer_async(const NetworkGroupIdentifier &identifier,
   const std::vector<StreamCbParamsPtr> &streams_cb_params,
   const callback_idx_t infer_request_done_cb, const std::unordered_set<std::string> &input_streams_names)
{
    ConfiguredNetworkGroup_infer_async_Request request;
    ConfiguredNetworkGroup_infer_async_Reply reply;
    auto proto_identifier = request.mutable_identifier();
    ConfiguredNetworkGroup_convert_identifier_to_proto(identifier, proto_identifier);
    auto proto_transfer_buffers = request.mutable_transfer_requests();
    for (const auto &stream_cb_params : streams_cb_params) {
        ProtoTransferRequest proto_transfer_request;
        proto_transfer_request.set_cb_idx(stream_cb_params->m_cb_idx);
        proto_transfer_request.set_stream_name(stream_cb_params->m_stream_name);
        auto direction = contains(input_streams_names, stream_cb_params->m_stream_name) ? HAILO_H2D_STREAM : HAILO_D2H_STREAM;
        proto_transfer_request.set_direction(direction);

        if (stream_cb_params->m_is_shm) {
            // Use share memory
            auto shared_memory_identifier = proto_transfer_request.mutable_shared_memory_identifier();
            shared_memory_identifier->set_name(stream_cb_params->m_shm_name);
            shared_memory_identifier->set_size(static_cast<uint32_t>(stream_cb_params->m_size));
        } else {
            // copy data
            proto_transfer_request.set_data(stream_cb_params->m_user_mem_view.data(), stream_cb_params->m_user_mem_view.size());
        }
        proto_transfer_buffers->Add(std::move(proto_transfer_request));
    }
    request.set_infer_request_done_cb_idx(infer_request_done_cb);

    ClientContextWithTimeout context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_infer_async(&context, request, &reply);
    assert(reply.status() < HAILO_STATUS_COUNT);
    if (reply.status() == HAILO_STREAM_ABORT) {
        return static_cast<hailo_status>(reply.status());
    }
    CHECK_GRPC_STATUS(status);
    CHECK_SUCCESS(static_cast<hailo_status>(reply.status()));
    return HAILO_SUCCESS;
}

Expected<bool> HailoRtRpcClient::InputVStream_is_multi_planar(const VStreamIdentifier &identifier)
{
    InputVStream_is_multi_planar_Request request;
    auto proto_identifier = request.mutable_identifier();
    VStream_convert_identifier_to_proto(identifier, proto_identifier);

    ClientContextWithTimeout context;
    InputVStream_is_multi_planar_Reply reply;
    grpc::Status status = m_stub->InputVStream_is_multi_planar(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    auto is_multi_planar = reply.is_multi_planar();
    return is_multi_planar;
}

hailo_status HailoRtRpcClient::InputVStream_write(const VStreamIdentifier &identifier, const hailo_pix_buffer_t &buffer, const std::chrono::milliseconds &timeout)
{
    CHECK(HAILO_PIX_BUFFER_MEMORY_TYPE_USERPTR == buffer.memory_type, HAILO_NOT_SUPPORTED, "Memory type of pix buffer must be of type USERPTR!");

    InputVStream_write_pix_Request request;
    auto proto_identifier = request.mutable_identifier();
    VStream_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_index(buffer.index);
    request.set_number_of_planes(buffer.number_of_planes);
    for (uint32_t i = 0; i < buffer.number_of_planes; i++) {
        request.add_planes_data(buffer.planes[i].user_ptr, buffer.planes[i].bytes_used);
    }

    ClientContextWithTimeout context(timeout);
    InputVStream_write_pix_Reply reply;
    grpc::Status status = m_stub->InputVStream_write_pix(&context, request, &reply);
    CHECK(grpc::StatusCode::DEADLINE_EXCEEDED != status.error_code(), HAILO_TIMEOUT,
        "Interaction between client and service received a timeout ({}ms)", timeout.count());
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    if (reply.status() == HAILO_STREAM_ABORT) {
        return static_cast<hailo_status>(reply.status());
    }
    CHECK_SUCCESS(static_cast<hailo_status>(reply.status()));
    return HAILO_SUCCESS;
}

hailo_status HailoRtRpcClient::InputVStream_write(const VStreamIdentifier &identifier, const MemoryView &buffer, const std::chrono::milliseconds &timeout)
{
    InputVStream_write_Request request;
    auto proto_identifier = request.mutable_identifier();
    VStream_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_data(buffer.data(), buffer.size());

    ClientContextWithTimeout context(timeout);
    InputVStream_write_Reply reply;
    grpc::Status status = m_stub->InputVStream_write(&context, request, &reply);
    CHECK(grpc::StatusCode::DEADLINE_EXCEEDED != status.error_code(), HAILO_TIMEOUT,
        "Interaction between client and service received a timeout ({}ms)", timeout.count());
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    if (reply.status() == HAILO_STREAM_ABORT) {
        return static_cast<hailo_status>(reply.status());
    }
    CHECK_SUCCESS(static_cast<hailo_status>(reply.status()));
    return HAILO_SUCCESS;
}

hailo_status HailoRtRpcClient::OutputVStream_read(const VStreamIdentifier &identifier, MemoryView buffer, const std::chrono::milliseconds &timeout)
{
    OutputVStream_read_Request request;
    auto proto_identifier = request.mutable_identifier();
    VStream_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_size(static_cast<uint32_t>(buffer.size()));

    ClientContextWithTimeout context(timeout);
    OutputVStream_read_Reply reply;
    grpc::Status status = m_stub->OutputVStream_read(&context, request, &reply);
    if (grpc::StatusCode::DEADLINE_EXCEEDED == status.error_code()) {
        LOGGER__ERROR("Interaction between client and service received a timeout ({}ms)", timeout.count());
        return HAILO_TIMEOUT;
    }
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    if (reply.status() == HAILO_STREAM_ABORT) {
        return static_cast<hailo_status>(reply.status());
    }
    CHECK_SUCCESS(static_cast<hailo_status>(reply.status()));
    memcpy(buffer.data(), reply.data().data(), buffer.size());
    return HAILO_SUCCESS;
}

Expected<size_t> HailoRtRpcClient::InputVStream_get_frame_size(const VStreamIdentifier &identifier)
{
    VStream_get_frame_size_Request request;
    auto proto_identifier = request.mutable_identifier();
    VStream_convert_identifier_to_proto(identifier, proto_identifier);

    ClientContextWithTimeout context;
    VStream_get_frame_size_Reply reply;
    grpc::Status status = m_stub->InputVStream_get_frame_size(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    return reply.frame_size();
}

Expected<size_t> HailoRtRpcClient::OutputVStream_get_frame_size(const VStreamIdentifier &identifier)
{
    VStream_get_frame_size_Request request;
    auto proto_identifier = request.mutable_identifier();
    VStream_convert_identifier_to_proto(identifier, proto_identifier);

    ClientContextWithTimeout context;
    VStream_get_frame_size_Reply reply;
    grpc::Status status = m_stub->OutputVStream_get_frame_size(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    return reply.frame_size();
}

hailo_status HailoRtRpcClient::InputVStream_flush(const VStreamIdentifier &identifier)
{
    InputVStream_flush_Request request;
    auto proto_identifier = request.mutable_identifier();
    VStream_convert_identifier_to_proto(identifier, proto_identifier);

    ClientContextWithTimeout context;
    InputVStream_flush_Reply reply;
    grpc::Status status = m_stub->InputVStream_flush(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    return static_cast<hailo_status>(reply.status());
}

Expected<std::string> HailoRtRpcClient::InputVStream_name(const VStreamIdentifier &identifier)
{
    VStream_name_Request request;
    auto proto_identifier = request.mutable_identifier();
    VStream_convert_identifier_to_proto(identifier, proto_identifier);

    ClientContextWithTimeout context;
    VStream_name_Reply reply;
    grpc::Status status = m_stub->InputVStream_name(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    auto name = reply.name();
    return name;
}

Expected<std::string> HailoRtRpcClient::OutputVStream_name(const VStreamIdentifier &identifier)
{
    VStream_name_Request request;
    auto proto_identifier = request.mutable_identifier();
    VStream_convert_identifier_to_proto(identifier, proto_identifier);

    ClientContextWithTimeout context;
    VStream_name_Reply reply;
    grpc::Status status = m_stub->OutputVStream_name(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    auto name = reply.name();
    return name;
}

Expected<std::string> HailoRtRpcClient::InputVStream_network_name(const VStreamIdentifier &identifier)
{
    VStream_network_name_Request request;
    auto proto_identifier = request.mutable_identifier();
    VStream_convert_identifier_to_proto(identifier, proto_identifier);

    ClientContextWithTimeout context;
    VStream_network_name_Reply reply;
    grpc::Status status = m_stub->InputVStream_network_name(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    auto name = reply.network_name();
    return name;
}

Expected<std::string> HailoRtRpcClient::OutputVStream_network_name(const VStreamIdentifier &identifier)
{
    VStream_network_name_Request request;
    auto proto_identifier = request.mutable_identifier();
    VStream_convert_identifier_to_proto(identifier, proto_identifier);

    ClientContextWithTimeout context;
    VStream_network_name_Reply reply;
    grpc::Status status = m_stub->OutputVStream_network_name(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    auto name = reply.network_name();
    return name;
}

hailo_status HailoRtRpcClient::InputVStream_abort(const VStreamIdentifier &identifier)
{
    VStream_abort_Request request;
    auto proto_identifier = request.mutable_identifier();
    VStream_convert_identifier_to_proto(identifier, proto_identifier);

    ClientContextWithTimeout context;
    VStream_abort_Reply reply;
    grpc::Status status = m_stub->InputVStream_abort(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    return static_cast<hailo_status>(reply.status());
}

hailo_status HailoRtRpcClient::OutputVStream_abort(const VStreamIdentifier &identifier)
{
    VStream_abort_Request request;
    auto proto_identifier = request.mutable_identifier();
    VStream_convert_identifier_to_proto(identifier, proto_identifier);

    ClientContextWithTimeout context;
    VStream_abort_Reply reply;
    grpc::Status status = m_stub->OutputVStream_abort(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    return static_cast<hailo_status>(reply.status());
}

hailo_status HailoRtRpcClient::InputVStream_resume(const VStreamIdentifier &identifier)
{
    VStream_resume_Request request;
    auto proto_identifier = request.mutable_identifier();
    VStream_convert_identifier_to_proto(identifier, proto_identifier);

    ClientContextWithTimeout context;
    VStream_resume_Reply reply;
    grpc::Status status = m_stub->InputVStream_resume(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    return static_cast<hailo_status>(reply.status());
}

hailo_status HailoRtRpcClient::OutputVStream_resume(const VStreamIdentifier &identifier)
{
    VStream_resume_Request request;
    auto proto_identifier = request.mutable_identifier();
    VStream_convert_identifier_to_proto(identifier, proto_identifier);

    ClientContextWithTimeout context;
    VStream_resume_Reply reply;
    grpc::Status status = m_stub->OutputVStream_resume(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    return static_cast<hailo_status>(reply.status());
}

hailo_status HailoRtRpcClient::InputVStream_stop_and_clear(const VStreamIdentifier &identifier)
{
    VStream_stop_and_clear_Request request;
    auto proto_identifier = request.mutable_identifier();
    VStream_convert_identifier_to_proto(identifier, proto_identifier);

    ClientContextWithTimeout context;
    VStream_stop_and_clear_Reply reply;
    grpc::Status status = m_stub->InputVStream_stop_and_clear(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    return static_cast<hailo_status>(reply.status());
}

hailo_status HailoRtRpcClient::OutputVStream_stop_and_clear(const VStreamIdentifier &identifier)
{
    VStream_stop_and_clear_Request request;
    auto proto_identifier = request.mutable_identifier();
    VStream_convert_identifier_to_proto(identifier, proto_identifier);

    ClientContextWithTimeout context;
    VStream_stop_and_clear_Reply reply;
    grpc::Status status = m_stub->OutputVStream_stop_and_clear(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    return static_cast<hailo_status>(reply.status());
}

hailo_status HailoRtRpcClient::InputVStream_start_vstream(const VStreamIdentifier &identifier)
{
    VStream_start_vstream_Request request;
    auto proto_identifier = request.mutable_identifier();
    VStream_convert_identifier_to_proto(identifier, proto_identifier);

    ClientContextWithTimeout context;
    VStream_start_vstream_Reply reply;
    grpc::Status status = m_stub->InputVStream_start_vstream(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    return static_cast<hailo_status>(reply.status());
}

hailo_status HailoRtRpcClient::OutputVStream_start_vstream(const VStreamIdentifier &identifier)
{
    VStream_start_vstream_Request request;
    auto proto_identifier = request.mutable_identifier();
    VStream_convert_identifier_to_proto(identifier, proto_identifier);

    ClientContextWithTimeout context;
    VStream_start_vstream_Reply reply;
    grpc::Status status = m_stub->OutputVStream_start_vstream(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    return static_cast<hailo_status>(reply.status());
}

Expected<hailo_format_t> HailoRtRpcClient::InputVStream_get_user_buffer_format(const VStreamIdentifier &identifier)
{
    VStream_get_user_buffer_format_Request request;
    auto proto_identifier = request.mutable_identifier();
    VStream_convert_identifier_to_proto(identifier, proto_identifier);

    ClientContextWithTimeout context;
    VStream_get_user_buffer_format_Reply reply;
    grpc::Status status = m_stub->InputVStream_get_user_buffer_format(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));

    auto user_buffer_format_proto = reply.user_buffer_format();
    hailo_format_t format{
        static_cast<hailo_format_type_t>(user_buffer_format_proto.type()),
        static_cast<hailo_format_order_t>(user_buffer_format_proto.order()),
        static_cast<hailo_format_flags_t>(user_buffer_format_proto.flags())
    };

    return format;
}

Expected<hailo_format_t> HailoRtRpcClient::OutputVStream_get_user_buffer_format(const VStreamIdentifier &identifier)
{
    VStream_get_user_buffer_format_Request request;
    auto proto_identifier = request.mutable_identifier();
    VStream_convert_identifier_to_proto(identifier, proto_identifier);

    ClientContextWithTimeout context;
    VStream_get_user_buffer_format_Reply reply;
    grpc::Status status = m_stub->OutputVStream_get_user_buffer_format(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));

    auto user_buffer_format_proto = reply.user_buffer_format();
    hailo_format_t format{
        static_cast<hailo_format_type_t>(user_buffer_format_proto.type()),
        static_cast<hailo_format_order_t>(user_buffer_format_proto.order()),
        static_cast<hailo_format_flags_t>(user_buffer_format_proto.flags())
    };

    return format;
}

Expected<hailo_vstream_info_t> HailoRtRpcClient::InputVStream_get_info(const VStreamIdentifier &identifier)
{
    VStream_get_info_Request request;
    auto proto_identifier = request.mutable_identifier();
    VStream_convert_identifier_to_proto(identifier, proto_identifier);

    ClientContextWithTimeout context;
    VStream_get_info_Reply reply;
    grpc::Status status = m_stub->InputVStream_get_info(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    auto info_proto = reply.vstream_info();
    return deserialize_vstream_info(info_proto);
}
Expected<hailo_vstream_info_t> HailoRtRpcClient::OutputVStream_get_info(const VStreamIdentifier &identifier)
{
    VStream_get_info_Request request;
    auto proto_identifier = request.mutable_identifier();
    VStream_convert_identifier_to_proto(identifier, proto_identifier);

    ClientContextWithTimeout context;
    VStream_get_info_Reply reply;
    grpc::Status status = m_stub->OutputVStream_get_info(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    auto info_proto = reply.vstream_info();
    return deserialize_vstream_info(info_proto);
}

Expected<bool> HailoRtRpcClient::InputVStream_is_aborted(const VStreamIdentifier &identifier)
{
    VStream_is_aborted_Request request;
    auto proto_identifier = request.mutable_identifier();
    VStream_convert_identifier_to_proto(identifier, proto_identifier);

    ClientContextWithTimeout context;
    VStream_is_aborted_Reply reply;
    grpc::Status status = m_stub->InputVStream_is_aborted(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    auto is_aborted = reply.is_aborted();
    return is_aborted;
}

Expected<bool> HailoRtRpcClient::OutputVStream_is_aborted(const VStreamIdentifier &identifier)
{
    VStream_is_aborted_Request request;
    auto proto_identifier = request.mutable_identifier();
    VStream_convert_identifier_to_proto(identifier, proto_identifier);

    ClientContextWithTimeout context;
    VStream_is_aborted_Reply reply;
    grpc::Status status = m_stub->OutputVStream_is_aborted(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    auto is_aborted = reply.is_aborted();
    return is_aborted;
}

hailo_status HailoRtRpcClient::OutputVStream_set_nms_score_threshold(const VStreamIdentifier &identifier, float32_t threshold)
{
    VStream_set_nms_score_threshold_Request request;
    auto proto_identifier = request.mutable_identifier();
    VStream_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_threshold(threshold);

    ClientContextWithTimeout context;
    VStream_set_nms_score_threshold_Reply reply;
    grpc::Status status = m_stub->OutputVStream_set_nms_score_threshold(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    return static_cast<hailo_status>(reply.status());
}
hailo_status HailoRtRpcClient::OutputVStream_set_nms_iou_threshold(const VStreamIdentifier &identifier, float32_t threshold)
{
    VStream_set_nms_iou_threshold_Request request;
    auto proto_identifier = request.mutable_identifier();
    VStream_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_threshold(threshold);

    ClientContextWithTimeout context;
    VStream_set_nms_iou_threshold_Reply reply;
    grpc::Status status = m_stub->OutputVStream_set_nms_iou_threshold(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    return static_cast<hailo_status>(reply.status());
}

hailo_status HailoRtRpcClient::OutputVStream_set_nms_max_proposals_per_class(const VStreamIdentifier &identifier, uint32_t max_proposals_per_class)
{
    VStream_set_nms_max_proposals_per_class_Request request;
    auto proto_identifier = request.mutable_identifier();
    VStream_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_max_proposals_per_class(max_proposals_per_class);

    ClientContextWithTimeout context;
    VStream_set_nms_max_proposals_per_class_Reply reply;
    grpc::Status status = m_stub->OutputVStream_set_nms_max_proposals_per_class(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    return static_cast<hailo_status>(reply.status());
}

hailo_status HailoRtRpcClient::OutputVStream_set_nms_max_accumulated_mask_size(const VStreamIdentifier &identifier, uint32_t max_accumulated_mask_size)
{
    VStream_set_nms_max_accumulated_mask_size_Request request;
    auto proto_identifier = request.mutable_identifier();
    VStream_convert_identifier_to_proto(identifier, proto_identifier);
    request.set_max_accumulated_mask_size(max_accumulated_mask_size);

    ClientContextWithTimeout context;
    VStream_set_nms_max_accumulated_mask_size_Reply reply;
    grpc::Status status = m_stub->OutputVStream_set_nms_max_accumulated_mask_size(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    return static_cast<hailo_status>(reply.status());
}

void HailoRtRpcClient::VDevice_convert_identifier_to_proto(const VDeviceIdentifier &identifier, ProtoVDeviceIdentifier *proto_identifier)
{
    proto_identifier->set_vdevice_handle(identifier.m_vdevice_handle);
}

void HailoRtRpcClient::ConfiguredNetworkGroup_convert_identifier_to_proto(const NetworkGroupIdentifier &identifier, ProtoConfiguredNetworkGroupIdentifier *proto_identifier)
{
    proto_identifier->set_network_group_handle(identifier.m_network_group_handle);
    proto_identifier->set_vdevice_handle(identifier.m_vdevice_identifier.m_vdevice_handle);
}

void HailoRtRpcClient::VStream_convert_identifier_to_proto(const VStreamIdentifier &identifier, ProtoVStreamIdentifier *proto_identifier)
{
    proto_identifier->set_vdevice_handle(identifier.m_network_group_identifier.m_vdevice_identifier.m_vdevice_handle);
    proto_identifier->set_network_group_handle(identifier.m_network_group_identifier.m_network_group_handle);
    proto_identifier->set_vstream_handle(identifier.m_vstream_handle);
}

}