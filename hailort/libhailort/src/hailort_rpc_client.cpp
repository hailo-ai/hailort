/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailort_rpc_client.cpp
 * @brief Implementation of the hailort rpc client
 **/

#include "hailort_rpc_client.hpp"
#include "common/utils.hpp"
#include "hef_internal.hpp"

#include <grpcpp/health_check_service_interface.h>


namespace hailort
{

hailo_status HailoRtRpcClient::client_keep_alive(uint32_t process_id)
{
    keepalive_Request request;
    request.set_process_id(process_id);
    empty reply;
    grpc::ClientContext context;
    grpc::Status status = m_stub->client_keep_alive(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    return HAILO_SUCCESS;
}

Expected<hailo_version_t> HailoRtRpcClient::get_service_version()
{
    get_service_version_Request request;
    get_service_version_Reply reply;
    grpc::ClientContext context;
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
    grpc::ClientContext context;
    grpc::Status status = m_stub->VDevice_create(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    return reply.handle();
}

hailo_status HailoRtRpcClient::VDevice_release(uint32_t handle)
{
    Release_Request request;
    request.set_handle(handle);

    Release_Reply reply;
    grpc::ClientContext context;
    grpc::Status status = m_stub->VDevice_release(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS(static_cast<hailo_status>(reply.status()));
    return HAILO_SUCCESS;
}

Expected<std::vector<uint32_t>> HailoRtRpcClient::InputVStreams_create(uint32_t net_group_handle,
    const std::map<std::string, hailo_vstream_params_t> &inputs_params, uint32_t pid)
{
    VStream_create_Request request;
    request.set_net_group(net_group_handle);
    request.set_pid(pid);
    auto proto_vstreams_params = request.mutable_vstreams_params();
    for (const auto &name_params_pair : inputs_params) {
        NamedVStreamParams proto_name_param_pair;
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
    grpc::ClientContext context;
    grpc::Status status = m_stub->InputVStreams_create(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    std::vector<uint32_t> input_vstreams_handles;
    input_vstreams_handles.reserve(reply.handles_size());
    for (auto &handle : *reply.mutable_handles()) {
        input_vstreams_handles.push_back(handle);
    }
    return input_vstreams_handles;
}

hailo_status HailoRtRpcClient::InputVStream_release(uint32_t handle)
{
    Release_Request request;
    request.set_handle(handle);

    Release_Reply reply;
    grpc::ClientContext context;
    grpc::Status status = m_stub->InputVStream_release(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS(static_cast<hailo_status>(reply.status()));
    return HAILO_SUCCESS;
}

Expected<std::vector<uint32_t>> HailoRtRpcClient::OutputVStreams_create(uint32_t net_group_handle,
        const std::map<std::string, hailo_vstream_params_t> &output_params, uint32_t pid)
{
    VStream_create_Request request;
    request.set_net_group(net_group_handle);
    request.set_pid(pid);
    auto proto_vstreams_params = request.mutable_vstreams_params();
    for (const auto &name_params_pair : output_params) {
        NamedVStreamParams proto_name_param_pair;
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
    grpc::ClientContext context;
    grpc::Status status = m_stub->OutputVStreams_create(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    std::vector<uint32_t> output_vstreams_handles;
    output_vstreams_handles.reserve(reply.handles_size());
    for (auto &handle : *reply.mutable_handles()) {
        output_vstreams_handles.push_back(handle);
    }
    return output_vstreams_handles;
}

hailo_status HailoRtRpcClient::OutputVStream_release(uint32_t handle)
{
    Release_Request request;
    request.set_handle(handle);

    Release_Reply reply;
    grpc::ClientContext context;
    grpc::Status status = m_stub->OutputVStream_release(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS(static_cast<hailo_status>(reply.status()));
    return HAILO_SUCCESS;
}

Expected<std::vector<uint32_t>> HailoRtRpcClient::VDevice_configure(uint32_t vdevice_handle, const Hef &hef,
    uint32_t pid, const NetworkGroupsParamsMap &configure_params)
{
    VDevice_configure_Request request;
    request.set_handle(vdevice_handle);
    request.set_pid(pid);
    auto hef_memview = hef.pimpl->get_hef_memview();
    request.set_hef(hef_memview.data(), hef_memview.size());

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
    grpc::ClientContext context;
    grpc::Status status = m_stub->VDevice_configure(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));

    std::vector<uint32_t> networks_handles(reply.networks_handles().begin(), reply.networks_handles().end());
    return networks_handles;
}

Expected<std::vector<std::string>> HailoRtRpcClient::VDevice_get_physical_devices_ids(uint32_t handle)
{
    VDevice_get_physical_devices_ids_Request request;
    request.set_handle(handle);

    VDevice_get_physical_devices_ids_Reply reply;
    grpc::ClientContext context;
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

hailo_status HailoRtRpcClient::ConfiguredNetworkGroup_release(uint32_t handle)
{
    Release_Request request;
    request.set_handle(handle);

    Release_Reply reply;
    grpc::ClientContext context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_release(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS(static_cast<hailo_status>(reply.status()));
    return HAILO_SUCCESS;
}

Expected<std::map<std::string, hailo_vstream_params_t>> HailoRtRpcClient::ConfiguredNetworkGroup_make_input_vstream_params(
    uint32_t handle, bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
    const std::string &network_name)
{
    ConfiguredNetworkGroup_make_input_vstream_params_Request request;
    request.set_handle(handle);
    request.set_quantized(quantized);
    request.set_format_type(format_type);
    request.set_timeout_ms(timeout_ms);
    request.set_queue_size(queue_size);
    request.set_network_name(network_name);

    ConfiguredNetworkGroup_make_input_vstream_params_Reply reply;
    grpc::ClientContext context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_make_input_vstream_params(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    std::map<std::string, hailo_vstream_params_t> result;
    for (int i = 0; i < reply.vstream_params_map_size(); ++i) {
        auto name = reply.vstream_params_map(i).name();
        auto proto_params = reply.vstream_params_map(i).params();
        auto proto_user_buffer_format = proto_params.user_buffer_format();
        hailo_format_t user_buffer_format = {
            .type = static_cast<hailo_format_type_t>(proto_user_buffer_format.type()),
            .order = static_cast<hailo_format_order_t>(proto_user_buffer_format.order()),
            .flags = static_cast<hailo_format_flags_t>(proto_user_buffer_format.flags())
        };
        hailo_vstream_params_t params = {
            .user_buffer_format = user_buffer_format,
            .timeout_ms = proto_params.timeout_ms(),
            .queue_size = proto_params.queue_size(),
            .vstream_stats_flags = static_cast<hailo_vstream_stats_flags_t>(proto_params.vstream_stats_flags()),
            .pipeline_elements_stats_flags = static_cast<hailo_pipeline_elem_stats_flags_t>(proto_params.pipeline_elements_stats_flags())
        };
        result.insert({name, params});
    }
    return result;
}

Expected<std::map<std::string, hailo_vstream_params_t>> HailoRtRpcClient::ConfiguredNetworkGroup_make_output_vstream_params(
    uint32_t handle, bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
    const std::string &network_name)
{
    ConfiguredNetworkGroup_make_output_vstream_params_Request request;
    request.set_handle(handle);
    request.set_quantized(quantized);
    request.set_format_type(format_type);
    request.set_timeout_ms(timeout_ms);
    request.set_queue_size(queue_size);
    request.set_network_name(network_name);

    ConfiguredNetworkGroup_make_output_vstream_params_Reply reply;
    grpc::ClientContext context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_make_output_vstream_params(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    std::map<std::string, hailo_vstream_params_t> result;
    for (int i = 0; i < reply.vstream_params_map_size(); ++i) {
        auto name = reply.vstream_params_map(i).name();
        auto proto_params = reply.vstream_params_map(i).params();
        auto proto_user_buffer_format = proto_params.user_buffer_format();
        hailo_format_t user_buffer_format = {
            .type = static_cast<hailo_format_type_t>(proto_user_buffer_format.type()),
            .order = static_cast<hailo_format_order_t>(proto_user_buffer_format.order()),
            .flags = static_cast<hailo_format_flags_t>(proto_user_buffer_format.flags())
        };
        hailo_vstream_params_t params = {
            .user_buffer_format = user_buffer_format,
            .timeout_ms = proto_params.timeout_ms(),
            .queue_size = proto_params.queue_size(),
            .vstream_stats_flags = static_cast<hailo_vstream_stats_flags_t>(proto_params.vstream_stats_flags()),
            .pipeline_elements_stats_flags = static_cast<hailo_pipeline_elem_stats_flags_t>(proto_params.pipeline_elements_stats_flags())
        };
        result.insert({name, params});
    }
    return result;
}

Expected<std::string> HailoRtRpcClient::ConfiguredNetworkGroup_get_network_group_name(uint32_t handle)
{
    return ConfiguredNetworkGroup_get_name(handle);
}

Expected<std::string> HailoRtRpcClient::ConfiguredNetworkGroup_get_name(uint32_t handle)
{
    ConfiguredNetworkGroup_get_name_Request request;
    request.set_handle(handle);

    ConfiguredNetworkGroup_get_name_Reply reply;
    grpc::ClientContext context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_get_name(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    auto network_group_name = reply.network_group_name();
    return network_group_name;
}

Expected<std::vector<hailo_network_info_t>> HailoRtRpcClient::ConfiguredNetworkGroup_get_network_infos(uint32_t handle)
{
    ConfiguredNetworkGroup_get_network_infos_Request request;
    request.set_handle(handle);

    ConfiguredNetworkGroup_get_network_infos_Reply reply;
    grpc::ClientContext context;
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

Expected<std::vector<hailo_stream_info_t>> HailoRtRpcClient::ConfiguredNetworkGroup_get_all_stream_infos(uint32_t handle,
    const std::string &network_name)
{
    ConfiguredNetworkGroup_get_all_stream_infos_Request request;
    request.set_handle(handle);
    request.set_network_name(network_name);

    ConfiguredNetworkGroup_get_all_stream_infos_Reply reply;
    grpc::ClientContext context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_get_all_stream_infos(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    std::vector<hailo_stream_info_t> result;
    result.reserve(reply.stream_infos().size());
    for (auto proto_stream_info : reply.stream_infos()) {
        hailo_3d_image_shape_t shape{
            .height = proto_stream_info.stream_shape().shape().height(),
            .width = proto_stream_info.stream_shape().shape().width(),
            .features = proto_stream_info.stream_shape().shape().features(),
        };
        hailo_3d_image_shape_t hw_shape{
            .height = proto_stream_info.stream_shape().hw_shape().height(),
            .width = proto_stream_info.stream_shape().hw_shape().width(),
            .features = proto_stream_info.stream_shape().hw_shape().features(),
        };
        hailo_nms_defuse_info_t nms_defuse_info{
            .class_group_index = proto_stream_info.nms_info().defuse_info().class_group_index(),
            .original_name = {0}
        };
        strcpy(nms_defuse_info.original_name, proto_stream_info.nms_info().defuse_info().original_name().c_str());
        hailo_nms_info_t nms_info{
            .number_of_classes = proto_stream_info.nms_info().number_of_classes(),
            .max_bboxes_per_class = proto_stream_info.nms_info().max_bboxes_per_class(),
            .bbox_size = proto_stream_info.nms_info().bbox_size(),
            .chunks_per_frame = proto_stream_info.nms_info().chunks_per_frame(),
            .is_defused = proto_stream_info.nms_info().is_defused(),
            .defuse_info = nms_defuse_info,
        };
        hailo_format_t format{
            .type = static_cast<hailo_format_type_t>(proto_stream_info.format().type()),
            .order = static_cast<hailo_format_order_t>(proto_stream_info.format().order()),
            .flags = static_cast<hailo_format_flags_t>(proto_stream_info.format().flags())
        };
        hailo_quant_info_t quant_info{
            .qp_zp = proto_stream_info.quant_info().qp_zp(),
            .qp_scale = proto_stream_info.quant_info().qp_scale(),
            .limvals_min = proto_stream_info.quant_info().limvals_min(),
            .limvals_max = proto_stream_info.quant_info().limvals_max()
        };
        hailo_stream_info_t stream_info;
        if (format.order == HAILO_FORMAT_ORDER_HAILO_NMS) {
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

Expected<hailo_stream_interface_t> HailoRtRpcClient::ConfiguredNetworkGroup_get_default_stream_interface(uint32_t handle)
{
    ConfiguredNetworkGroup_get_default_stream_interface_Request request;
    request.set_handle(handle);

    ConfiguredNetworkGroup_get_default_stream_interface_Reply reply;
    grpc::ClientContext context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_get_default_stream_interface(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    auto stream_interface = static_cast<hailo_stream_interface_t>(reply.stream_interface());
    return stream_interface;
}

Expected<std::vector<std::vector<std::string>>> HailoRtRpcClient::ConfiguredNetworkGroup_get_output_vstream_groups(uint32_t handle)
{
    ConfiguredNetworkGroup_get_output_vstream_groups_Request request;
    request.set_handle(handle);

    ConfiguredNetworkGroup_get_output_vstream_groups_Reply reply;
    grpc::ClientContext context;
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

Expected<std::vector<hailo_vstream_info_t>> deserialize_vstream_infos(const ConfiguredNetworkGroup_get_vstream_infos_Reply &reply)
{
    std::vector<hailo_vstream_info_t> result;
    result.reserve(reply.vstream_infos().size());
    for (auto& info_proto : reply.vstream_infos()) {
        hailo_vstream_info_t info;
        strcpy(info.name, info_proto.name().c_str());
        strcpy(info.network_name, info_proto.network_name().c_str());
        info.direction = static_cast<hailo_stream_direction_t>(info_proto.direction());
        hailo_format_t format = {
            .type = static_cast<hailo_format_type_t>(info_proto.format().type()),
            .order = static_cast<hailo_format_order_t>(info_proto.format().order()),
            .flags = static_cast<hailo_format_flags_t>(info_proto.format().flags())
        };
        info.format = format;
        if (format.order == HAILO_FORMAT_ORDER_HAILO_NMS) {
            hailo_nms_shape_t nms_shape = {
                .number_of_classes = info_proto.nms_shape().number_of_classes(),
                .max_bboxes_per_class = info_proto.nms_shape().max_bbox_per_class()
            };
            info.nms_shape = nms_shape;
        } else {
            hailo_3d_image_shape_t shape = {
                .height = info_proto.shape().height(),
                .width = info_proto.shape().width(),
                .features = info_proto.shape().features()
            };
            info.shape = shape;
        }
        hailo_quant_info_t quant_info = {
            .qp_zp = info_proto.quant_info().qp_zp(),
            .qp_scale = info_proto.quant_info().qp_scale(),
            .limvals_min = info_proto.quant_info().limvals_min(),
            .limvals_max = info_proto.quant_info().limvals_max()
        };
        info.quant_info = quant_info;
        result.push_back(info);
    }
    return result;
} 

Expected<std::vector<hailo_vstream_info_t>> HailoRtRpcClient::ConfiguredNetworkGroup_get_input_vstream_infos(uint32_t handle,
    std::string network_name)
{
    ConfiguredNetworkGroup_get_vstream_infos_Request request;
    request.set_handle(handle);
    request.set_network_name(network_name);

    ConfiguredNetworkGroup_get_vstream_infos_Reply reply;
    grpc::ClientContext context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_get_input_vstream_infos(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    return deserialize_vstream_infos(reply);
}

Expected<std::vector<hailo_vstream_info_t>> HailoRtRpcClient::ConfiguredNetworkGroup_get_output_vstream_infos(uint32_t handle,
    std::string network_name)
{
    ConfiguredNetworkGroup_get_vstream_infos_Request request;
    request.set_handle(handle);
    request.set_network_name(network_name);

    ConfiguredNetworkGroup_get_vstream_infos_Reply reply;
    grpc::ClientContext context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_get_output_vstream_infos(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    return deserialize_vstream_infos(reply);
}

Expected<std::vector<hailo_vstream_info_t>> HailoRtRpcClient::ConfiguredNetworkGroup_get_all_vstream_infos(uint32_t handle,
    std::string network_name)
{
    ConfiguredNetworkGroup_get_vstream_infos_Request request;
    request.set_handle(handle);
    request.set_network_name(network_name);

    ConfiguredNetworkGroup_get_vstream_infos_Reply reply;
    grpc::ClientContext context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_get_all_vstream_infos(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    return deserialize_vstream_infos(reply);
}

hailo_status HailoRtRpcClient::ConfiguredNetworkGroup_set_scheduler_timeout(uint32_t handle,
    const std::chrono::milliseconds &timeout, const std::string &network_name)
{
    ConfiguredNetworkGroup_set_scheduler_timeout_Request request;
    request.set_handle(handle);
    request.set_timeout_ms(static_cast<uint32_t>(timeout.count()));
    request.set_network_name(network_name);

    ConfiguredNetworkGroup_set_scheduler_timeout_Reply reply;
    grpc::ClientContext context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_set_scheduler_timeout(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    return static_cast<hailo_status>(reply.status());
}

hailo_status HailoRtRpcClient::ConfiguredNetworkGroup_set_scheduler_threshold(uint32_t handle, uint32_t threshold,
    const std::string &network_name)
{
    ConfiguredNetworkGroup_set_scheduler_threshold_Request request;
    request.set_handle(handle);
    request.set_threshold(threshold);
    request.set_network_name(network_name);

    ConfiguredNetworkGroup_set_scheduler_threshold_Reply reply;
    grpc::ClientContext context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_set_scheduler_threshold(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    return static_cast<hailo_status>(reply.status());
}

Expected<LatencyMeasurementResult> HailoRtRpcClient::ConfiguredNetworkGroup_get_latency_measurement(uint32_t handle,
    const std::string &network_name)
{
    ConfiguredNetworkGroup_get_latency_measurement_Request request;
    ConfiguredNetworkGroup_get_latency_measurement_Reply reply;
    request.set_handle(handle);
    request.set_network_name(network_name);
    grpc::ClientContext context;
    grpc::Status status = m_stub->ConfiguredNetworkGroup_get_latency_measurement(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    LatencyMeasurementResult result{
        .avg_hw_latency = std::chrono::nanoseconds(reply.avg_hw_latency())
    };
    return result;
}

hailo_status HailoRtRpcClient::InputVStream_write(uint32_t handle, const MemoryView &buffer)
{
    InputVStream_write_Request request;
    request.set_handle(handle);
    request.set_data(buffer.data(), buffer.size());
    grpc::ClientContext context;
    InputVStream_write_Reply reply;
    grpc::Status status = m_stub->InputVStream_write(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    if (reply.status() == HAILO_STREAM_INTERNAL_ABORT) {
        return static_cast<hailo_status>(reply.status());
    }
    CHECK_SUCCESS(static_cast<hailo_status>(reply.status()));
    return HAILO_SUCCESS;
}

hailo_status HailoRtRpcClient::OutputVStream_read(uint32_t handle, MemoryView buffer)
{
    OutputVStream_read_Request request;
    request.set_handle(handle);
    request.set_size(static_cast<uint32_t>(buffer.size()));
    grpc::ClientContext context;
    OutputVStream_read_Reply reply;
    grpc::Status status = m_stub->OutputVStream_read(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    if (reply.status() == HAILO_STREAM_INTERNAL_ABORT) {
        return static_cast<hailo_status>(reply.status());
    }
    CHECK_SUCCESS(static_cast<hailo_status>(reply.status()));
    memcpy(buffer.data(), reply.data().data(), buffer.size());
    return HAILO_SUCCESS;
}

Expected<size_t> HailoRtRpcClient::InputVStream_get_frame_size(uint32_t handle)
{
    VStream_get_frame_size_Request request;
    request.set_handle(handle);
    grpc::ClientContext context;
    VStream_get_frame_size_Reply reply;
    grpc::Status status = m_stub->InputVStream_get_frame_size(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    return reply.frame_size();
}

Expected<size_t> HailoRtRpcClient::OutputVStream_get_frame_size(uint32_t handle)
{
    VStream_get_frame_size_Request request;
    request.set_handle(handle);
    grpc::ClientContext context;
    VStream_get_frame_size_Reply reply;
    grpc::Status status = m_stub->OutputVStream_get_frame_size(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    return reply.frame_size();
}

hailo_status HailoRtRpcClient::InputVStream_flush(uint32_t handle)
{
    InputVStream_flush_Request request;
    request.set_handle(handle);
    grpc::ClientContext context;
    InputVStream_flush_Reply reply;
    grpc::Status status = m_stub->InputVStream_flush(&context, request, &reply);
    CHECK_GRPC_STATUS(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    return static_cast<hailo_status>(reply.status());
}

Expected<std::string> HailoRtRpcClient::InputVStream_name(uint32_t handle)
{
    VStream_name_Request request;
    request.set_handle(handle);
    grpc::ClientContext context;
    VStream_name_Reply reply;
    grpc::Status status = m_stub->InputVStream_name(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    auto name = reply.name();
    return name;
}

Expected<std::string> HailoRtRpcClient::OutputVStream_name(uint32_t handle)
{
    VStream_name_Request request;
    request.set_handle(handle);
    grpc::ClientContext context;
    VStream_name_Reply reply;
    grpc::Status status = m_stub->OutputVStream_name(&context, request, &reply);
    CHECK_GRPC_STATUS_AS_EXPECTED(status);
    assert(reply.status() < HAILO_STATUS_COUNT);
    CHECK_SUCCESS_AS_EXPECTED(static_cast<hailo_status>(reply.status()));
    auto name = reply.name();
    return name;
}

hailo_status HailoRtRpcClient::InputVStream_abort(uint32_t handle)
{
    VStream_abort_Request request;
    request.set_handle(handle);
    grpc::ClientContext context;
    VStream_abort_Reply reply;
    grpc::Status status = m_stub->InputVStream_abort(&context, request, &reply);
    CHECK(status.ok(), HAILO_RPC_FAILED, "InputVStream_abort: RPC failed");
    assert(reply.status() < HAILO_STATUS_COUNT);
    return static_cast<hailo_status>(reply.status());
}

hailo_status HailoRtRpcClient::OutputVStream_abort(uint32_t handle)
{
    VStream_abort_Request request;
    request.set_handle(handle);
    grpc::ClientContext context;
    VStream_abort_Reply reply;
    grpc::Status status = m_stub->OutputVStream_abort(&context, request, &reply);
    CHECK(status.ok(), HAILO_RPC_FAILED, "OutputVStream_abort: RPC failed");
    assert(reply.status() < HAILO_STATUS_COUNT);
    return static_cast<hailo_status>(reply.status());
}

hailo_status HailoRtRpcClient::InputVStream_resume(uint32_t handle)
{
    VStream_resume_Request request;
    request.set_handle(handle);
    grpc::ClientContext context;
    VStream_resume_Reply reply;
    grpc::Status status = m_stub->InputVStream_resume(&context, request, &reply);
    CHECK(status.ok(), HAILO_RPC_FAILED, "InputVStream_resume: RPC failed");
    assert(reply.status() < HAILO_STATUS_COUNT);
    return static_cast<hailo_status>(reply.status());
}

hailo_status HailoRtRpcClient::OutputVStream_resume(uint32_t handle)
{
    VStream_resume_Request request;
    request.set_handle(handle);
    grpc::ClientContext context;
    VStream_resume_Reply reply;
    grpc::Status status = m_stub->OutputVStream_resume(&context, request, &reply);
    CHECK(status.ok(), HAILO_RPC_FAILED, "OutputVStream_resume: RPC failed");
    assert(reply.status() < HAILO_STATUS_COUNT);
    return static_cast<hailo_status>(reply.status());
}

}