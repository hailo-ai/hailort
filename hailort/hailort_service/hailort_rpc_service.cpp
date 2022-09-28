/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailort_rpc_service.cpp
 * @brief Implementation of the hailort rpc service
 **/

#include "hailort_rpc_service.hpp"
#include "rpc/rpc_definitions.hpp"
#include "service_resource_manager.hpp"
#include "common/utils.hpp"
#include "hailo/network_group.hpp"
#include "hailo/vdevice.hpp"
#include "hailo/vstream.hpp"
#include "hailo/hailort_common.hpp"
#include <syslog.h>

namespace hailort
{

grpc::Status HailoRtRpcService::client_keep_alive(grpc::ServerContext *ctx, const keepalive_Request *request,
    empty*)
{
    auto client_id = request->process_id();
    while (!ctx->IsCancelled()) {
        sleep(hailort::HAILO_KEEPALIVE_INTERVAL_SEC);
    }
    LOGGER__INFO("Client disconnected, pid: {}", client_id);
    syslog(LOG_NOTICE, "Client disconnected, pid: %i", client_id);
    ServiceResourceManager<OutputVStream>::get_instance().release_by_pid(client_id);
    ServiceResourceManager<InputVStream>::get_instance().release_by_pid(client_id);
    ServiceResourceManager<ConfiguredNetworkGroup>::get_instance().release_by_pid(client_id);
    ServiceResourceManager<VDevice>::get_instance().release_by_pid(client_id);
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::get_service_version(grpc::ServerContext*, const get_service_version_Request*,
        get_service_version_Reply *reply)
{
    hailo_version_t service_version = {};
    auto status = hailo_get_library_version(&service_version);
    CHECK_SUCCESS_AS_RPC_STATUS(status, reply);
    auto hailo_version_proto = reply->mutable_hailo_version();
    hailo_version_proto->set_major_version(service_version.major);
    hailo_version_proto->set_minor_version(service_version.minor);
    hailo_version_proto->set_revision_version(service_version.revision);
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::VDevice_create(grpc::ServerContext *, const VDevice_create_Request *request,
    VDevice_create_Reply *reply)
{
    // Deserialization
    const auto params_proto = request->hailo_vdevice_params();
    std::vector<hailo_device_id_t> device_ids;
    device_ids.reserve(params_proto.device_ids().size());
    for (auto device_id_str : params_proto.device_ids()) {
        auto device_id_struct = HailoRTCommon::to_device_id(device_id_str);
        CHECK_SUCCESS_AS_RPC_STATUS(device_id_struct.status(), reply);
        device_ids.push_back(device_id_struct.release());
    }

    hailo_vdevice_params_t params = {
        .device_count = params_proto.device_count(),
        .device_infos = nullptr,
        .device_ids = device_ids.data(),
        .scheduling_algorithm = static_cast<hailo_scheduling_algorithm_e>(params_proto.scheduling_algorithm()),
        .group_id = params_proto.group_id().c_str(),
        .multi_process_service = false
    };

    auto vdevice = VDevice::create(params);
    CHECK_EXPECTED_AS_RPC_STATUS(vdevice, reply);

    auto &manager = ServiceResourceManager<VDevice>::get_instance();
    auto handle = manager.register_resource(request->pid(), std::move(vdevice.release()));
    reply->set_handle(handle);
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::VDevice_release(grpc::ServerContext*, const Release_Request *request,
    Release_Reply *reply)
{
    auto &manager = ServiceResourceManager<VDevice>::get_instance();
    auto status = manager.release_resource(request->handle());
    reply->set_status(static_cast<uint32_t>(status));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::VDevice_configure(grpc::ServerContext*, const VDevice_configure_Request *request,
    VDevice_configure_Reply *reply)
{
    auto hef_as_string = request->hef();
    auto hef_memview = MemoryView::create_const(hef_as_string.c_str(), hef_as_string.length());
    auto hef = Hef::create(hef_memview);
    CHECK_SUCCESS_AS_RPC_STATUS(hef.status(), reply);

    NetworkGroupsParamsMap configure_params_map;
    for (auto &name_configure_params_pair : request->configure_params_map()) {
        ConfigureNetworkParams network_configure_params;
        auto proto_configure_params = name_configure_params_pair.params();
        network_configure_params.batch_size = static_cast<uint16_t>(proto_configure_params.batch_size());
        network_configure_params.power_mode = static_cast<hailo_power_mode_t>(proto_configure_params.power_mode());
        network_configure_params.latency = static_cast<hailo_latency_measurement_flags_t>(proto_configure_params.latency());

        // Init streams params
        for (auto &proto_name_streams_params_pair : proto_configure_params.stream_params_map()) {
            auto proto_streams_params = proto_name_streams_params_pair.params();
            auto stream_direction = static_cast<hailo_stream_direction_t>(proto_streams_params.direction());
            hailo_stream_parameters_t stream_params;
            if (stream_direction == HAILO_H2D_STREAM) {
                stream_params = {
                    .stream_interface = static_cast<hailo_stream_interface_t>(proto_streams_params.stream_interface()),
                    .direction = stream_direction,
                    {.pcie_input_params = {
                        .reserved = 0
                    }}
                };
            } else {
                stream_params = {
                    .stream_interface = static_cast<hailo_stream_interface_t>(proto_streams_params.stream_interface()),
                    .direction = stream_direction,
                    {.pcie_output_params = {
                        .reserved = 0
                    }}
                };
            }
            network_configure_params.stream_params_by_name.insert({proto_name_streams_params_pair.name(), stream_params});
        }

        // Init networks params
        for (auto &proto_name_network_params_pair : proto_configure_params.network_params_map()) {
            auto proto_network_params = proto_name_network_params_pair.params();
            hailo_network_parameters_t net_params {
                .batch_size = static_cast<uint16_t>(proto_network_params.batch_size())
            };

            network_configure_params.network_params_by_name.insert({proto_name_network_params_pair.name(), net_params});
        }

        configure_params_map.insert({name_configure_params_pair.name(), network_configure_params});
    }

    auto lambda = [](std::shared_ptr<VDevice> vdevice, Hef &hef, NetworkGroupsParamsMap &configure_params_map) {
        return vdevice->configure(hef, configure_params_map);
    };
    auto &vdevice_manager = ServiceResourceManager<VDevice>::get_instance();
    auto networks = vdevice_manager.execute<Expected<ConfiguredNetworkGroupVector>>(request->handle(), lambda, hef.release(), configure_params_map);
    CHECK_SUCCESS_AS_RPC_STATUS(networks.status(), reply);

    auto &networks_manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    for (auto network : networks.value()) {
        auto handle = networks_manager.register_resource(request->pid(), network);
        reply->add_networks_handles(handle);
    }

    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::VDevice_get_physical_devices_ids(grpc::ServerContext*,
    const VDevice_get_physical_devices_ids_Request* request, VDevice_get_physical_devices_ids_Reply* reply)
{
    auto lambda = [](std::shared_ptr<VDevice> vdevice) {
        return vdevice->get_physical_devices_ids();
    };
    auto &vdevice_manager = ServiceResourceManager<VDevice>::get_instance();
    auto expected_devices_ids = vdevice_manager.execute<Expected<std::vector<std::string>>>(request->handle(), lambda);
    CHECK_EXPECTED_AS_RPC_STATUS(expected_devices_ids, reply);
    auto devices_ids = expected_devices_ids.value();
    auto devices_ids_proto = reply->mutable_devices_ids();
    for (auto &device_id : devices_ids) {
        devices_ids_proto->Add(std::move(device_id));
    }
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_release(grpc::ServerContext*, const Release_Request *request,
    Release_Reply *reply)
{
    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto status = manager.release_resource(request->handle());
    reply->set_status(static_cast<uint32_t>(status));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_make_input_vstream_params(grpc::ServerContext*,
    const ConfiguredNetworkGroup_make_input_vstream_params_Request *request,
    ConfiguredNetworkGroup_make_input_vstream_params_Reply *reply)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng, bool quantized,
        hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size, std::string network_name) {
            return cng->make_input_vstream_params(quantized, format_type, timeout_ms, queue_size, network_name);
    };
    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto expected_params = manager.execute<Expected<std::map<std::string, hailo_vstream_params_t>>>(request->handle(), lambda, request->quantized(), static_cast<hailo_format_type_t>(request->format_type()),
        request->timeout_ms(), request->queue_size(), request->network_name());
    CHECK_EXPECTED_AS_RPC_STATUS(expected_params, reply);
    auto params_map = reply->mutable_vstream_params_map();
    for (auto& name_to_params : expected_params.value()) {
        NamedVStreamParams named_params;
        named_params.set_name(name_to_params.first);
        auto params = name_to_params.second;
        auto proto_params = named_params.mutable_params();
        auto proto_user_buffer_format = proto_params->mutable_user_buffer_format();
        proto_user_buffer_format->set_type(params.user_buffer_format.type);
        proto_user_buffer_format->set_order(params.user_buffer_format.order);
        proto_user_buffer_format->set_flags(params.user_buffer_format.flags);
        proto_params->set_timeout_ms(params.timeout_ms);
        proto_params->set_queue_size(params.queue_size);
        proto_params->set_vstream_stats_flags(params.vstream_stats_flags);
        proto_params->set_pipeline_elements_stats_flags(params.pipeline_elements_stats_flags);
        params_map->Add(std::move(named_params));
    }
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;    
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_make_output_vstream_params(grpc::ServerContext*,
    const ConfiguredNetworkGroup_make_output_vstream_params_Request *request,
    ConfiguredNetworkGroup_make_output_vstream_params_Reply *reply)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng, bool quantized,
        hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size, std::string network_name) {
            return cng->make_output_vstream_params(quantized, format_type, timeout_ms, queue_size, network_name);
    };
    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto expected_params = manager.execute<Expected<std::map<std::string, hailo_vstream_params_t>>>(request->handle(), 
        lambda, request->quantized(), static_cast<hailo_format_type_t>(request->format_type()),
        request->timeout_ms(), request->queue_size(), request->network_name());
    CHECK_EXPECTED_AS_RPC_STATUS(expected_params, reply);
    auto params_map = reply->mutable_vstream_params_map();
    for (auto& name_to_params : expected_params.value()) {
        NamedVStreamParams named_params;
        named_params.set_name(name_to_params.first);
        auto params = name_to_params.second;
        auto proto_params = named_params.mutable_params();
        auto proto_user_buffer_format = proto_params->mutable_user_buffer_format();
        proto_user_buffer_format->set_type(params.user_buffer_format.type);
        proto_user_buffer_format->set_order(params.user_buffer_format.order);
        proto_user_buffer_format->set_flags(params.user_buffer_format.flags);
        proto_params->set_timeout_ms(params.timeout_ms);
        proto_params->set_queue_size(params.queue_size);
        proto_params->set_vstream_stats_flags(params.vstream_stats_flags);
        proto_params->set_pipeline_elements_stats_flags(params.pipeline_elements_stats_flags);
        params_map->Add(std::move(named_params));
    }
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;    
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_get_default_stream_interface(grpc::ServerContext*,
    const ConfiguredNetworkGroup_get_default_stream_interface_Request *request,
    ConfiguredNetworkGroup_get_default_stream_interface_Reply *reply)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng) {
            return cng->get_default_streams_interface();
    };
    auto &net_group_manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto expected_stream_interface = net_group_manager.execute<Expected<hailo_stream_interface_t>>(request->handle(), lambda);
    CHECK_EXPECTED_AS_RPC_STATUS(expected_stream_interface, reply);
    reply->set_stream_interface(static_cast<uint32_t>(expected_stream_interface.value()));
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_get_output_vstream_groups(grpc::ServerContext*,
    const ConfiguredNetworkGroup_get_output_vstream_groups_Request *request,
    ConfiguredNetworkGroup_get_output_vstream_groups_Reply *reply)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng) {
            return cng->get_output_vstream_groups();
    };
    auto &net_group_manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto expected_output_vstream_groups = net_group_manager.execute<Expected<std::vector<std::vector<std::string>>>>(request->handle(), lambda);
    CHECK_EXPECTED_AS_RPC_STATUS(expected_output_vstream_groups, reply);
    auto output_vstream_groups = expected_output_vstream_groups.value();
    auto groups_proto = reply->mutable_output_vstream_groups();
    for (auto& group : output_vstream_groups) {
        VStreamGroup group_proto;
        for (auto& name : group) {
            auto vstream_group_proto = group_proto.mutable_vstream_group();
            vstream_group_proto->Add(std::move(name));
        }
        groups_proto->Add(std::move(group_proto));
    }
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

void serialize_vstream_infos(ConfiguredNetworkGroup_get_vstream_infos_Reply *reply,
    const std::vector<hailo_vstream_info_t> &infos)
{
    auto vstream_infos_proto = reply->mutable_vstream_infos();
    for (auto& info : infos) {
        VStreamInfo info_proto;
        info_proto.set_name(std::string(info.name));
        info_proto.set_network_name(std::string(info.network_name));
        info_proto.set_direction(static_cast<uint32_t>(info.direction));
        auto format_proto = info_proto.mutable_format();
        format_proto->set_flags(info.format.flags);
        format_proto->set_order(info.format.order);
        format_proto->set_type(info.format.type);
        if (info.format.order == HAILO_FORMAT_ORDER_HAILO_NMS) {
            auto nms_shape_proto = info_proto.mutable_nms_shape();
            nms_shape_proto->set_number_of_classes(info.nms_shape.number_of_classes);
            nms_shape_proto->set_max_bbox_per_class(info.nms_shape.max_bboxes_per_class);
        } else {
            auto shape_proto = info_proto.mutable_shape();
            shape_proto->set_height(info.shape.height);
            shape_proto->set_width(info.shape.width);
            shape_proto->set_features(info.shape.features);
        }
        auto quant_info_proto = info_proto.mutable_quant_info();
        quant_info_proto->set_qp_zp(info.quant_info.qp_zp);
        quant_info_proto->set_qp_scale(info.quant_info.qp_scale);
        quant_info_proto->set_limvals_min(info.quant_info.limvals_min);
        quant_info_proto->set_limvals_max(info.quant_info.limvals_max);
        vstream_infos_proto->Add(std::move(info_proto));
    }
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_get_input_vstream_infos(grpc::ServerContext*,
    const ConfiguredNetworkGroup_get_vstream_infos_Request *request,
    ConfiguredNetworkGroup_get_vstream_infos_Reply *reply)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng, std::string network_name) {
            return cng->get_input_vstream_infos(network_name);
    };
    auto &net_group_manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto expected_vstream_infos = net_group_manager.execute<Expected<std::vector<hailo_vstream_info_t>>>(request->handle(), lambda, request->network_name());
    CHECK_EXPECTED_AS_RPC_STATUS(expected_vstream_infos, reply);
    serialize_vstream_infos(reply, expected_vstream_infos.value());
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_get_output_vstream_infos(grpc::ServerContext*,
    const ConfiguredNetworkGroup_get_vstream_infos_Request *request,
    ConfiguredNetworkGroup_get_vstream_infos_Reply *reply)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng, std::string network_name) {
            return cng->get_output_vstream_infos(network_name);
    };
    auto &net_group_manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto expected_vstream_infos = net_group_manager.execute<Expected<std::vector<hailo_vstream_info_t>>>(request->handle(), lambda, request->network_name());
    CHECK_EXPECTED_AS_RPC_STATUS(expected_vstream_infos, reply);
    serialize_vstream_infos(reply, expected_vstream_infos.value());
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_get_all_vstream_infos(grpc::ServerContext*,
    const ConfiguredNetworkGroup_get_vstream_infos_Request *request,
    ConfiguredNetworkGroup_get_vstream_infos_Reply *reply)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng, std::string network_name) {
            return cng->get_all_vstream_infos(network_name);
    };
    auto &net_group_manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto expected_vstream_infos = net_group_manager.execute<Expected<std::vector<hailo_vstream_info_t>>>(request->handle(), lambda, request->network_name());
    CHECK_EXPECTED_AS_RPC_STATUS(expected_vstream_infos, reply);
    serialize_vstream_infos(reply, expected_vstream_infos.value());
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_set_scheduler_timeout(grpc::ServerContext*,
    const ConfiguredNetworkGroup_set_scheduler_timeout_Request *request,
    ConfiguredNetworkGroup_set_scheduler_timeout_Reply *reply)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng, std::chrono::milliseconds timeout_ms) {
            return cng->set_scheduler_timeout(timeout_ms);
    };
    auto &net_group_manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto status = net_group_manager.execute<hailo_status>(request->handle(), lambda, static_cast<std::chrono::milliseconds>(request->timeout_ms()));
    reply->set_status(status);
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_set_scheduler_threshold(grpc::ServerContext*,
    const ConfiguredNetworkGroup_set_scheduler_threshold_Request *request,
    ConfiguredNetworkGroup_set_scheduler_threshold_Reply *reply)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng, uint32_t threshold, std::string network_name) {
            return cng->set_scheduler_threshold(threshold, network_name);
    };
    auto &net_group_manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto status = net_group_manager.execute<hailo_status>(request->handle(), lambda, request->threshold(),
        request->network_name());
    reply->set_status(status);
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::InputVStreams_create(grpc::ServerContext *, const VStream_create_Request *request,
    VStreams_create_Reply *reply)
{
    std::map<std::string, hailo_vstream_params_t> inputs_params;
    for (auto& param_proto : request->vstreams_params()) {
        auto vstream_params_proto = param_proto.params();
        auto user_buffer_format_proto = vstream_params_proto.user_buffer_format();
        hailo_format_t format;
        format.flags = hailo_format_flags_t(user_buffer_format_proto.flags());
        format.order = hailo_format_order_t(user_buffer_format_proto.order());
        format.type = hailo_format_type_t(user_buffer_format_proto.type());
        hailo_vstream_params_t params = {
            .user_buffer_format = format,
            .timeout_ms = vstream_params_proto.timeout_ms(),
            .queue_size = vstream_params_proto.queue_size(),
            .vstream_stats_flags = hailo_vstream_stats_flags_t(vstream_params_proto.vstream_stats_flags()),
            .pipeline_elements_stats_flags = hailo_pipeline_elem_stats_flags_t(vstream_params_proto.pipeline_elements_stats_flags())
        };
        inputs_params.emplace(param_proto.name(), std::move(params));
    }

    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng, const std::map<std::string, hailo_vstream_params_t> &inputs_params) {
            return cng->create_input_vstreams(inputs_params);
    };
    auto &net_group_manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto vstreams_expected = net_group_manager.execute<Expected<std::vector<InputVStream>>>(request->net_group(), lambda, inputs_params);
    CHECK_EXPECTED_AS_RPC_STATUS(vstreams_expected, reply);
    auto vstreams = vstreams_expected.release();
    auto &manager = ServiceResourceManager<InputVStream>::get_instance();
    auto client_pid = request->pid();

    for (size_t i = 0; i < vstreams.size(); i++) {
        auto handle = manager.register_resource(client_pid, make_shared_nothrow<InputVStream>(std::move(vstreams[i])));
        reply->add_handles(handle);
    }
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::InputVStream_release(grpc::ServerContext *, const Release_Request *request,
    Release_Reply *reply)
{
    auto &manager = ServiceResourceManager<InputVStream>::get_instance();
    auto status = manager.release_resource(request->handle());
    reply->set_status(static_cast<uint32_t>(status));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::OutputVStreams_create(grpc::ServerContext *, const VStream_create_Request *request,
    VStreams_create_Reply *reply)
{
    std::map<std::string, hailo_vstream_params_t> output_params;
    for (auto& param_proto : request->vstreams_params()) {
        auto vstream_params_proto = param_proto.params();
        auto user_buffer_format_proto = vstream_params_proto.user_buffer_format();
        hailo_format_t format;
        format.flags = hailo_format_flags_t(user_buffer_format_proto.flags());
        format.order = hailo_format_order_t(user_buffer_format_proto.order());
        format.type = hailo_format_type_t(user_buffer_format_proto.type());
        hailo_vstream_params_t params = {
            .user_buffer_format = format,
            .timeout_ms = vstream_params_proto.timeout_ms(),
            .queue_size = vstream_params_proto.queue_size(),
            .vstream_stats_flags = hailo_vstream_stats_flags_t(vstream_params_proto.vstream_stats_flags()),
            .pipeline_elements_stats_flags = hailo_pipeline_elem_stats_flags_t(vstream_params_proto.pipeline_elements_stats_flags())
        };
        output_params.emplace(param_proto.name(), std::move(params));
    }

    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng, const std::map<std::string, hailo_vstream_params_t> &output_params) {
            return cng->create_output_vstreams(output_params);
    };
    auto &net_group_manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto vstreams_expected = net_group_manager.execute<Expected<std::vector<OutputVStream>>>(request->net_group(), lambda, output_params);
    CHECK_EXPECTED_AS_RPC_STATUS(vstreams_expected, reply);
    auto vstreams = vstreams_expected.release();
    auto &manager = ServiceResourceManager<OutputVStream>::get_instance();
    auto client_pid = request->pid();

    for (size_t i = 0; i < vstreams.size(); i++) {
        auto handle = manager.register_resource(client_pid, make_shared_nothrow<OutputVStream>(std::move(vstreams[i])));
        reply->add_handles(handle);
    }
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::OutputVStream_release(grpc::ServerContext *, const Release_Request *request,
    Release_Reply *reply)
{
    auto &manager = ServiceResourceManager<OutputVStream>::get_instance();
    auto status = manager.release_resource(request->handle());
    reply->set_status(static_cast<uint32_t>(status));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_get_name(grpc::ServerContext*,
    const ConfiguredNetworkGroup_get_name_Request *request,
    ConfiguredNetworkGroup_get_name_Reply *reply)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng) {
            return cng->name();
    };
    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto network_group_name = manager.execute<std::string>(request->handle(), lambda);
    reply->set_network_group_name(network_group_name);
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::InputVStream_write(grpc::ServerContext*, const InputVStream_write_Request *request,
        InputVStream_write_Reply *reply)
{
    auto buffer_expected = Buffer::create_shared(request->data().length());
    CHECK_EXPECTED_AS_RPC_STATUS(buffer_expected, reply);
    std::vector<uint8_t> data(request->data().begin(), request->data().end());
    auto lambda = [](std::shared_ptr<InputVStream> input_vstream, const MemoryView &buffer) {
            return input_vstream->write(std::move(buffer));
    };
    auto &manager = ServiceResourceManager<InputVStream>::get_instance();
    auto status = manager.execute<hailo_status>(request->handle(), lambda, MemoryView::create_const(data.data(), data.size()));
    CHECK_SUCCESS_AS_RPC_STATUS(status,  reply, "VStream write failed");
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_get_network_infos(grpc::ServerContext*,
    const ConfiguredNetworkGroup_get_network_infos_Request *request,
    ConfiguredNetworkGroup_get_network_infos_Reply *reply)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng) {
            return cng->get_network_infos();
    };
    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto expected_network_infos = manager.execute<Expected<std::vector<hailo_network_info_t>>>(request->handle(), lambda);
    CHECK_EXPECTED_AS_RPC_STATUS(expected_network_infos, reply);
    auto infos_proto = reply->mutable_network_infos();
    for (auto& info : expected_network_infos.value()) {
        infos_proto->Add(std::string(info.name));
    }
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::OutputVStream_read(grpc::ServerContext*, const OutputVStream_read_Request *request,
    OutputVStream_read_Reply *reply)
{
    std::vector<uint8_t> data(request->size());
    auto lambda = [](std::shared_ptr<OutputVStream> output_vstream, MemoryView &buffer) {
            return output_vstream->read(std::move(buffer));
    };
    auto &manager = ServiceResourceManager<OutputVStream>::get_instance();
    auto status = manager.execute<hailo_status>(request->handle(), lambda, MemoryView(data.data(), data.size()));
    CHECK_SUCCESS_AS_RPC_STATUS(status,  reply, "VStream read failed");
    reply->set_data(data.data(), data.size());
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_get_all_stream_infos(grpc::ServerContext*,
    const ConfiguredNetworkGroup_get_all_stream_infos_Request *request,
    ConfiguredNetworkGroup_get_all_stream_infos_Reply *reply)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng) {
            return cng->get_all_stream_infos();
    };
    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto expected_stream_infos = manager.execute<Expected<std::vector<hailo_stream_info_t>>>(request->handle(), lambda);
    CHECK_EXPECTED_AS_RPC_STATUS(expected_stream_infos, reply);
    auto proto_stream_infos = reply->mutable_stream_infos();
    for (auto& stream_info : expected_stream_infos.value()) {
        StreamInfo proto_stream_info;
        if (stream_info.format.order == HAILO_FORMAT_ORDER_HAILO_NMS) {
            auto proto_nms_info = proto_stream_info.mutable_nms_info();
            proto_nms_info->set_number_of_classes(stream_info.nms_info.number_of_classes);
            proto_nms_info->set_max_bboxes_per_class(stream_info.nms_info.max_bboxes_per_class);
            proto_nms_info->set_bbox_size(stream_info.nms_info.bbox_size);
            proto_nms_info->set_chunks_per_frame(stream_info.nms_info.chunks_per_frame);
            proto_nms_info->set_is_defused(stream_info.nms_info.is_defused);
            auto proto_nms_info_defuse_info = proto_nms_info->mutable_defuse_info();
            proto_nms_info_defuse_info->set_class_group_index(stream_info.nms_info.defuse_info.class_group_index);
            proto_nms_info_defuse_info->set_original_name(std::string(stream_info.nms_info.defuse_info.original_name));
        } else {
            auto proto_stream_shape = proto_stream_info.mutable_stream_shape();
            auto proto_stream_shape_shape = proto_stream_shape->mutable_shape();
            proto_stream_shape_shape->set_height(stream_info.shape.height);
            proto_stream_shape_shape->set_width(stream_info.shape.width);
            proto_stream_shape_shape->set_features(stream_info.shape.features);
            auto proto_stream_shape_hw_shape = proto_stream_shape->mutable_hw_shape();
            proto_stream_shape_hw_shape->set_height(stream_info.hw_shape.height);
            proto_stream_shape_hw_shape->set_width(stream_info.hw_shape.width);
            proto_stream_shape_hw_shape->set_features(stream_info.hw_shape.features);
        }
        proto_stream_info.set_hw_data_bytes(stream_info.hw_data_bytes);
        proto_stream_info.set_hw_frame_size(stream_info.hw_frame_size);
        auto proto_stream_info_format = proto_stream_info.mutable_format();
        proto_stream_info_format->set_type(stream_info.format.type);
        proto_stream_info_format->set_order(stream_info.format.order);
        proto_stream_info_format->set_flags(stream_info.format.flags);
        proto_stream_info.set_direction(static_cast<uint32_t>(stream_info.direction));
        proto_stream_info.set_index(stream_info.index);
        proto_stream_info.set_name(stream_info.name);
        auto proto_quant_info = proto_stream_info.mutable_quant_info();
        proto_quant_info->set_qp_zp(stream_info.quant_info.qp_zp);
        proto_quant_info->set_qp_scale(stream_info.quant_info.qp_scale);
        proto_quant_info->set_limvals_min(stream_info.quant_info.limvals_min);
        proto_quant_info->set_limvals_max(stream_info.quant_info.limvals_max);
        proto_stream_info.set_is_mux(stream_info.is_mux);
        proto_stream_infos->Add(std::move(proto_stream_info));
    }
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_get_latency_measurement(grpc::ServerContext*,
    const ConfiguredNetworkGroup_get_latency_measurement_Request *request,
    ConfiguredNetworkGroup_get_latency_measurement_Reply *reply)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng, const std::string &network_name) {
            return cng->get_latency_measurement(network_name);
    };
    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto expected_latency_result = manager.execute<Expected<LatencyMeasurementResult>>(request->handle(), lambda, request->network_name());
    CHECK_EXPECTED_AS_RPC_STATUS(expected_latency_result, reply);
    reply->set_avg_hw_latency(static_cast<uint32_t>(expected_latency_result.value().avg_hw_latency.count()));
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::InputVStream_get_frame_size(grpc::ServerContext*, const VStream_get_frame_size_Request *request,
    VStream_get_frame_size_Reply *reply)
{
    auto lambda = [](std::shared_ptr<InputVStream> input_vstream) {
            return input_vstream->get_frame_size();
    };
    auto &manager = ServiceResourceManager<InputVStream>::get_instance();
    auto frame_size = manager.execute<size_t>(request->handle(), lambda);
    reply->set_frame_size(static_cast<uint32_t>(frame_size));
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::OutputVStream_get_frame_size(grpc::ServerContext*, const VStream_get_frame_size_Request *request,
    VStream_get_frame_size_Reply *reply)
{
    auto lambda = [](std::shared_ptr<OutputVStream> output_vstream) {
            return output_vstream->get_frame_size();
    };
    auto &manager = ServiceResourceManager<OutputVStream>::get_instance();
    auto frame_size = manager.execute<size_t>(request->handle(), lambda);
    reply->set_frame_size(static_cast<uint32_t>(frame_size));
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::InputVStream_flush(grpc::ServerContext*, const InputVStream_flush_Request *request,
    InputVStream_flush_Reply *reply)
{
    auto lambda = [](std::shared_ptr<InputVStream> input_vstream) {
            return input_vstream->flush();
    };
    auto &manager = ServiceResourceManager<InputVStream>::get_instance();
    auto flush_status = manager.execute<hailo_status>(request->handle(), lambda);
    reply->set_status(static_cast<uint32_t>(flush_status));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::InputVStream_name(grpc::ServerContext*, const VStream_name_Request *request,
    VStream_name_Reply *reply)
{
    auto lambda = [](std::shared_ptr<InputVStream> input_vstream) {
            return input_vstream->name();
    };
    auto &manager = ServiceResourceManager<InputVStream>::get_instance();
    auto name = manager.execute<std::string>(request->handle(), lambda);
    reply->set_name(name);
    reply->set_status(HAILO_SUCCESS);
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::OutputVStream_name(grpc::ServerContext*, const VStream_name_Request *request,
    VStream_name_Reply *reply)
{
    auto lambda = [](std::shared_ptr<OutputVStream> output_vstream) {
            return output_vstream->name();
    };
    auto &manager = ServiceResourceManager<OutputVStream>::get_instance();
    auto name = manager.execute<std::string>(request->handle(), lambda);
    reply->set_name(name);
    reply->set_status(HAILO_SUCCESS);
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::InputVStream_abort(grpc::ServerContext*, const VStream_abort_Request *request,
    VStream_abort_Reply *reply)
{
    auto lambda = [](std::shared_ptr<InputVStream> input_vstream) {
            return input_vstream->abort();
    };
    auto &manager = ServiceResourceManager<InputVStream>::get_instance();
    auto status = manager.execute<hailo_status>(request->handle(), lambda);
    reply->set_status(status);
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::OutputVStream_abort(grpc::ServerContext*, const VStream_abort_Request *request,
    VStream_abort_Reply *reply)
{
    auto lambda = [](std::shared_ptr<OutputVStream> output_vstream) {
            return output_vstream->abort();
    };
    auto &manager = ServiceResourceManager<OutputVStream>::get_instance();
    auto status = manager.execute<hailo_status>(request->handle(), lambda);
    reply->set_status(status);
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::InputVStream_resume(grpc::ServerContext*, const VStream_resume_Request *,
    VStream_resume_Reply *)
{
    // TODO - HRT-7892
    // auto lambda = [](std::shared_ptr<InputVStream> input_vstream) {
    //         return input_vstream->resume();
    // };
    // auto &manager = ServiceResourceManager<InputVStream>::get_instance();
    // auto status = manager.execute<hailo_status>(request->handle(), lambda);
    // reply->set_status(status);
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::OutputVStream_resume(grpc::ServerContext*, const VStream_resume_Request *,
    VStream_resume_Reply *)
{
    // TODO - HRT-7892
    // auto lambda = [](std::shared_ptr<OutputVStream> output_vstream) {
    //         return output_vstream->resume();
    // };
    // auto &manager = ServiceResourceManager<OutputVStream>::get_instance();
    // auto status = manager.execute<hailo_status>(request->handle(), lambda);
    // reply->set_status(status);
    return grpc::Status::OK;
}

}

