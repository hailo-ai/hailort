/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailort_rpc_service.cpp
 * @brief Implementation of the hailort rpc service
 **/

#include "hailo/network_group.hpp"
#include "hailo/vdevice.hpp"
#include "hailo/vstream.hpp"
#include "hailo/hailort_common.hpp"

#include "common/utils.hpp"
#include "common/os_utils.hpp"

#include "hailort_rpc_service.hpp"
#include "rpc/rpc_definitions.hpp"
#include "service_resource_manager.hpp"

#include <thread>

namespace hailort
{

HailoRtRpcService::HailoRtRpcService()
    : ProtoHailoRtRpc::Service()
{
    m_keep_alive = make_unique_nothrow<std::thread>([this] () {
        this->keep_alive();
    });
}

hailo_status HailoRtRpcService::abort_input_vstream(uint32_t handle)
{
    if (is_input_vstream_aborted(handle)) {
        return HAILO_SUCCESS;
    }

    auto lambda = [](std::shared_ptr<InputVStream> input_vstream) {
        return input_vstream->abort();
    };
    auto &manager = ServiceResourceManager<InputVStream>::get_instance();
    auto status = manager.execute<hailo_status>(handle, lambda);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to abort input vstream with status {}", status);
    }
    return status;
}

hailo_status HailoRtRpcService::abort_output_vstream(uint32_t handle)
{
    if (is_output_vstream_aborted(handle)) {
        return HAILO_SUCCESS;
    }

    auto lambda = [](std::shared_ptr<OutputVStream> output_vstream) {
        return output_vstream->abort();
    };
    auto &manager = ServiceResourceManager<OutputVStream>::get_instance();
    auto status = manager.execute<hailo_status>(handle, lambda);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to abort output vstream with status {}", status);
    }
    return status;
}

bool HailoRtRpcService::is_input_vstream_aborted(uint32_t handle)
{
    auto lambda = [](std::shared_ptr<InputVStream> input_vstream) {
        return input_vstream->is_aborted();
    };
    auto &manager = ServiceResourceManager<InputVStream>::get_instance();
    return manager.execute<bool>(handle, lambda);
}

bool HailoRtRpcService::is_output_vstream_aborted(uint32_t handle)
{
    auto lambda = [](std::shared_ptr<OutputVStream> output_vstream) {
        return output_vstream->is_aborted();
    };
    auto &manager = ServiceResourceManager<OutputVStream>::get_instance();
    return manager.execute<bool>(handle, lambda);
}

hailo_status HailoRtRpcService::resume_input_vstream(uint32_t handle)
{
    if (!is_input_vstream_aborted(handle)) {
        return HAILO_SUCCESS;
    }

    auto lambda = [](std::shared_ptr<InputVStream> input_vstream) {
        return input_vstream->resume();
    };
    auto &manager = ServiceResourceManager<InputVStream>::get_instance();
    auto status = manager.execute<hailo_status>(handle, lambda);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to resume input vstream with status {}", status);
    }
    return status;
}

hailo_status HailoRtRpcService::resume_output_vstream(uint32_t handle)
{
    if (!is_output_vstream_aborted(handle)) {
        return HAILO_SUCCESS;
    }

    auto lambda = [](std::shared_ptr<OutputVStream> output_vstream) {
        return output_vstream->resume();
    };
    auto &manager = ServiceResourceManager<OutputVStream>::get_instance();
    auto status = manager.execute<hailo_status>(handle, lambda);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to resume output vstream with status {}", status);
    }
    return status;
}

// TODO: Add a named templated release functions for InputVStream and OutputVStream to call abort before release.
void HailoRtRpcService::abort_vstreams_by_pids(std::set<uint32_t> &pids)
{
    auto inputs_handles = ServiceResourceManager<InputVStream>::get_instance().resources_handles_by_pids(pids);
    auto outputs_handles = ServiceResourceManager<OutputVStream>::get_instance().resources_handles_by_pids(pids);
    for (auto &input_handle : inputs_handles) {
        abort_input_vstream(input_handle);
    }
    for (auto &output_handle : outputs_handles) {
        abort_output_vstream(output_handle);
    }
}


void HailoRtRpcService::remove_disconnected_clients()
{
    std::this_thread::sleep_for(hailort::HAILO_KEEPALIVE_INTERVAL / 2);
    auto now = std::chrono::high_resolution_clock::now();
    std::set<uint32_t> pids_to_remove;
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        for (auto pid_to_last_alive : m_clients_pids) {
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - pid_to_last_alive.second);
            if (duration > hailort::HAILO_KEEPALIVE_INTERVAL) {
                auto client_pid = pid_to_last_alive.first;
                pids_to_remove.insert(client_pid);
            }
        }

        // We abort vstreams before releasing them to avoid cases where the vstream is stuck in execute of a
        // blocking operation (which will be finished with timeout).
        // To release the vstream the ServiceResourceManager is waiting for the resource_mutex which is also locked in execute.
        abort_vstreams_by_pids(pids_to_remove);
        for (auto &client_pid : pids_to_remove) {
            ServiceResourceManager<OutputVStream>::get_instance().release_by_pid(client_pid);
            ServiceResourceManager<InputVStream>::get_instance().release_by_pid(client_pid);
            ServiceResourceManager<ConfiguredNetworkGroup>::get_instance().release_by_pid(client_pid);
            ServiceResourceManager<VDevice>::get_instance().release_by_pid(client_pid);

            LOGGER__INFO("Client disconnected, pid: {}", client_pid);
            HAILORT_OS_LOG_INFO("Client disconnected, pid: {}", client_pid);
            m_clients_pids.erase(client_pid);
        }
    }
}


void HailoRtRpcService::keep_alive()
{
    while (true) {
        remove_disconnected_clients();
    }
}

grpc::Status HailoRtRpcService::client_keep_alive(grpc::ServerContext*, const keepalive_Request *request,
    empty*)
{
    auto client_id = request->pid();
    std::unique_lock<std::mutex> lock(m_mutex);
    m_clients_pids[client_id] = std::chrono::high_resolution_clock::now();
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

grpc::Status HailoRtRpcService::VDevice_dup_handle(grpc::ServerContext*, const dup_handle_Request *request,
    dup_handle_Reply* reply)
{
    auto &manager = ServiceResourceManager<VDevice>::get_instance();
    auto handle = manager.dup_handle(request->pid(), request->handle());
    reply->set_handle(handle);
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::VDevice_create(grpc::ServerContext *, const VDevice_create_Request *request,
    VDevice_create_Reply *reply)
{
    remove_disconnected_clients();

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
        params_proto.device_count(),
        device_ids.data(),
        static_cast<hailo_scheduling_algorithm_e>(params_proto.scheduling_algorithm()),
        params_proto.group_id().c_str(),
        false
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
    manager.release_resource(request->handle(), request->pid());
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
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
        ConfigureNetworkParams network_configure_params{};
        auto proto_configure_params = name_configure_params_pair.params();
        network_configure_params.batch_size = static_cast<uint16_t>(proto_configure_params.batch_size());
        network_configure_params.power_mode = static_cast<hailo_power_mode_t>(proto_configure_params.power_mode());
        network_configure_params.latency = static_cast<hailo_latency_measurement_flags_t>(proto_configure_params.latency());

        // Init streams params
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

        // Init networks params
        for (auto &proto_name_network_params_pair : proto_configure_params.network_params_map()) {
            auto proto_network_params = proto_name_network_params_pair.params();
            hailo_network_parameters_t net_params {
                static_cast<uint16_t>(proto_network_params.batch_size())
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

grpc::Status HailoRtRpcService::VDevice_get_default_streams_interface(grpc::ServerContext*,
    const VDevice_get_default_streams_interface_Request* request, VDevice_get_default_streams_interface_Reply* reply)
{
    auto lambda = [](std::shared_ptr<VDevice> vdevice) {
        return vdevice->get_default_streams_interface();
    };
    auto &vdevice_manager = ServiceResourceManager<VDevice>::get_instance();
    auto stream_interface = vdevice_manager.execute<Expected<hailo_stream_interface_t>>(request->handle(), lambda);
    CHECK_EXPECTED_AS_RPC_STATUS(stream_interface, reply);
    reply->set_stream_interface(*stream_interface);
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_dup_handle(grpc::ServerContext*, const dup_handle_Request *request,
    dup_handle_Reply* reply)
{
    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto handle = manager.dup_handle(request->pid(), request->handle());
    reply->set_handle(handle);
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_release(grpc::ServerContext*, const Release_Request *request,
    Release_Reply *reply)
{
    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    manager.release_resource(request->handle(), request->pid());
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

ProtoNamedVStreamParams get_named_params(const std::string &name, const hailo_vstream_params_t &params)
{
    ProtoNamedVStreamParams named_params;
    named_params.set_name(name);
    auto proto_params = named_params.mutable_params();
    auto proto_user_buffer_format = proto_params->mutable_user_buffer_format();
    proto_user_buffer_format->set_type(params.user_buffer_format.type);
    proto_user_buffer_format->set_order(params.user_buffer_format.order);
    proto_user_buffer_format->set_flags(params.user_buffer_format.flags);
    proto_params->set_timeout_ms(params.timeout_ms);
    proto_params->set_queue_size(params.queue_size);
    proto_params->set_vstream_stats_flags(params.vstream_stats_flags);
    proto_params->set_pipeline_elements_stats_flags(params.pipeline_elements_stats_flags);
    return named_params;
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
    auto params_map_impl = params_map->mutable_vstream_params_map();
    for (auto& name_to_params : expected_params.value()) {
        auto named_params = get_named_params(name_to_params.first, name_to_params.second);
        params_map_impl->Add(std::move(named_params));
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
    auto params_map_impl = params_map->mutable_vstream_params_map();
    for (auto& name_to_params : expected_params.value()) {
        auto named_params = get_named_params(name_to_params.first, name_to_params.second);
        params_map_impl->Add(std::move(named_params));
    }
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_make_output_vstream_params_groups(grpc::ServerContext*,
    const ConfiguredNetworkGroup_make_output_vstream_params_groups_Request *request,
    ConfiguredNetworkGroup_make_output_vstream_params_groups_Reply *reply)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng, bool quantized,
        hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size) {
            return cng->make_output_vstream_params_groups(quantized, format_type, timeout_ms, queue_size);
    };
    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto expected_params = manager.execute<Expected<std::vector<std::map<std::string, hailo_vstream_params_t>>>>(request->handle(), 
        lambda, request->quantized(), static_cast<hailo_format_type_t>(request->format_type()),
        request->timeout_ms(), request->queue_size());
    CHECK_EXPECTED_AS_RPC_STATUS(expected_params, reply);
    auto params_map_vector = reply->mutable_vstream_params_groups();
    for (auto &params_map : expected_params.value()) {
        ProtoNamedVStreamParamsMap params_map_proto;
        auto params_map_impl_proto = params_map_proto.mutable_vstream_params_map();
        for (auto& name_to_params : params_map) {
            auto named_params = get_named_params(name_to_params.first, name_to_params.second);
            params_map_impl_proto->Add(std::move(named_params));
        }
        params_map_vector->Add(std::move(params_map_proto));
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
        ProtoVStreamGroup group_proto;
        for (auto& name : group) {
            auto vstream_group_proto = group_proto.mutable_vstream_group();
            vstream_group_proto->Add(std::move(name));
        }
        groups_proto->Add(std::move(group_proto));
    }
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

void serialize_vstream_info(const hailo_vstream_info_t &info, ProtoVStreamInfo *info_proto)
{
    info_proto->set_name(std::string(info.name));
    info_proto->set_network_name(std::string(info.network_name));
    info_proto->set_direction(static_cast<uint32_t>(info.direction));
    auto format_proto = info_proto->mutable_format();
    format_proto->set_flags(info.format.flags);
    format_proto->set_order(info.format.order);
    format_proto->set_type(info.format.type);
    if (info.format.order == HAILO_FORMAT_ORDER_HAILO_NMS) {
        auto nms_shape_proto = info_proto->mutable_nms_shape();
        nms_shape_proto->set_number_of_classes(info.nms_shape.number_of_classes);
        nms_shape_proto->set_max_bbox_per_class(info.nms_shape.max_bboxes_per_class);
    } else {
        auto shape_proto = info_proto->mutable_shape();
        shape_proto->set_height(info.shape.height);
        shape_proto->set_width(info.shape.width);
        shape_proto->set_features(info.shape.features);
    }
    auto quant_info_proto = info_proto->mutable_quant_info();
    quant_info_proto->set_qp_zp(info.quant_info.qp_zp);
    quant_info_proto->set_qp_scale(info.quant_info.qp_scale);
    quant_info_proto->set_limvals_min(info.quant_info.limvals_min);
    quant_info_proto->set_limvals_max(info.quant_info.limvals_max);
}

void serialize_vstream_infos(ConfiguredNetworkGroup_get_vstream_infos_Reply *reply,
    const std::vector<hailo_vstream_info_t> &infos)
{
    auto vstream_infos_proto = reply->mutable_vstream_infos();
    for (auto& info : infos) {
        ProtoVStreamInfo info_proto;
        serialize_vstream_info(info, &info_proto);
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

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_is_scheduled(grpc::ServerContext*,
    const ConfiguredNetworkGroup_is_scheduled_Request *request,
    ConfiguredNetworkGroup_is_scheduled_Reply *reply)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng) {
        return cng->is_scheduled();
    };
    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto is_scheduled = manager.execute<bool>(request->handle(), lambda);
    reply->set_is_scheduled(static_cast<bool>(is_scheduled));
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_set_scheduler_timeout(grpc::ServerContext*,
    const ConfiguredNetworkGroup_set_scheduler_timeout_Request *request,
    ConfiguredNetworkGroup_set_scheduler_timeout_Reply *reply)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng, std::chrono::milliseconds timeout_ms, std::string network_name) {
            return cng->set_scheduler_timeout(timeout_ms, network_name);
    };
    auto &net_group_manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto status = net_group_manager.execute<hailo_status>(request->handle(), lambda, static_cast<std::chrono::milliseconds>(request->timeout_ms()),
        request->network_name());
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

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_set_scheduler_priority(grpc::ServerContext*,
    const ConfiguredNetworkGroup_set_scheduler_priority_Request *request,
    ConfiguredNetworkGroup_set_scheduler_priority_Reply *reply)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng, uint8_t priority, std::string network_name) {
            return cng->set_scheduler_priority(priority, network_name);
    };
    auto &net_group_manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto status = net_group_manager.execute<hailo_status>(request->handle(), lambda, static_cast<uint8_t>(request->priority()),
        request->network_name());
    reply->set_status(status);
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_get_config_params(grpc::ServerContext*,
    const ConfiguredNetworkGroup_get_config_params_Request *request,
    ConfiguredNetworkGroup_get_config_params_Reply *reply)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng) {
            return cng->get_config_params();
    };
    auto &net_group_manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto expected_params = net_group_manager.execute<Expected<ConfigureNetworkParams>>(request->handle(), lambda);
    CHECK_EXPECTED_AS_RPC_STATUS(expected_params, reply);
    auto net_configure_params = expected_params.value();
    auto proto_network_configure_params = reply->mutable_params();
    proto_network_configure_params->set_batch_size(net_configure_params.batch_size);
    proto_network_configure_params->set_power_mode(net_configure_params.power_mode);
    proto_network_configure_params->set_latency(net_configure_params.latency);
    for (const auto &name_stream_params_pair : net_configure_params.stream_params_by_name) {
        auto proto_name_streams_params = proto_network_configure_params->add_stream_params_map();
        proto_name_streams_params->set_name(name_stream_params_pair.first);

        auto proto_stream_params = proto_name_streams_params->mutable_params();
        auto stream_params = name_stream_params_pair.second;
        proto_stream_params->set_stream_interface(stream_params.stream_interface);
        proto_stream_params->set_direction(stream_params.direction);
    }
    for (const auto &name_network_params_pair : net_configure_params.network_params_by_name) {
        auto proto_name_network_params = proto_network_configure_params->add_network_params_map();
        proto_name_network_params->set_name(name_network_params_pair.first);

        auto proto_network_params = proto_name_network_params->mutable_params();
        auto network_params = name_network_params_pair.second;
        proto_network_params->set_batch_size(network_params.batch_size);
    }
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
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
            format,
            vstream_params_proto.timeout_ms(),
            vstream_params_proto.queue_size(),
            hailo_vstream_stats_flags_t(vstream_params_proto.vstream_stats_flags()),
            hailo_pipeline_elem_stats_flags_t(vstream_params_proto.pipeline_elements_stats_flags())
        };
        inputs_params.emplace(param_proto.name(), std::move(params));
    }
    auto network_group_handle = request->net_group();
    auto client_pid = request->pid();

    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng, const std::map<std::string, hailo_vstream_params_t> &inputs_params) {
            return cng->create_input_vstreams(inputs_params);
    };
    auto &net_group_manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto vstreams_expected = net_group_manager.execute<Expected<std::vector<InputVStream>>>(network_group_handle, lambda, inputs_params);
    CHECK_EXPECTED_AS_RPC_STATUS(vstreams_expected, reply);
    auto vstreams = vstreams_expected.release();

    auto &manager = ServiceResourceManager<InputVStream>::get_instance();
    for (size_t i = 0; i < vstreams.size(); i++) {
        auto handle = manager.register_resource(client_pid, make_shared_nothrow<InputVStream>(std::move(vstreams[i])));
        reply->add_handles(handle);
    }
    net_group_manager.dup_handle(client_pid, network_group_handle);

    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::InputVStream_release(grpc::ServerContext *, const Release_Request *request,
    Release_Reply *reply)
{
    auto &manager = ServiceResourceManager<InputVStream>::get_instance();
    manager.release_resource(request->handle(), request->pid());
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
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
            format,
            vstream_params_proto.timeout_ms(),
            vstream_params_proto.queue_size(),
            hailo_vstream_stats_flags_t(vstream_params_proto.vstream_stats_flags()),
            hailo_pipeline_elem_stats_flags_t(vstream_params_proto.pipeline_elements_stats_flags())
        };
        output_params.emplace(param_proto.name(), std::move(params));
    }

    auto network_group_handle = request->net_group();
    auto client_pid = request->pid();

    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng, const std::map<std::string, hailo_vstream_params_t> &output_params) {
            return cng->create_output_vstreams(output_params);
    };
    auto &net_group_manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto vstreams_expected = net_group_manager.execute<Expected<std::vector<OutputVStream>>>(network_group_handle, lambda, output_params);
    CHECK_EXPECTED_AS_RPC_STATUS(vstreams_expected, reply);
    auto vstreams = vstreams_expected.release();

    auto &manager = ServiceResourceManager<OutputVStream>::get_instance();
    for (size_t i = 0; i < vstreams.size(); i++) {
        auto handle = manager.register_resource(client_pid, make_shared_nothrow<OutputVStream>(std::move(vstreams[i])));
        reply->add_handles(handle);
    }
    net_group_manager.dup_handle(client_pid, network_group_handle);

    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::OutputVStream_release(grpc::ServerContext *, const Release_Request *request,
    Release_Reply *reply)
{
    auto was_aborted = is_output_vstream_aborted(request->handle());
    abort_output_vstream(request->handle());
    auto &manager = ServiceResourceManager<OutputVStream>::get_instance();
    auto resource = manager.release_resource(request->handle(), request->pid());
    auto status = HAILO_SUCCESS;
    if (resource && (!was_aborted)) {
        status = resource->resume();
        if (HAILO_SUCCESS != status) {
            LOGGER__INFO("Failed to resume output vstream {} after destruction", resource->name());
        }
    }
    reply->set_status(static_cast<uint32_t>(status));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_name(grpc::ServerContext*,
    const ConfiguredNetworkGroup_name_Request *request,
    ConfiguredNetworkGroup_name_Reply *reply)
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

    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        LOGGER__INFO("User aborted VStream write.");
        reply->set_status(static_cast<uint32_t>(HAILO_STREAM_ABORTED_BY_USER));
        return grpc::Status::OK;
    }
    CHECK_SUCCESS_AS_RPC_STATUS(status,  reply, "VStream write failed");
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::InputVStream_dup_handle(grpc::ServerContext*, const dup_handle_Request *request,
    dup_handle_Reply *reply)
{
    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto handle = manager.dup_handle(request->pid(), request->handle());
    reply->set_handle(handle);
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::OutputVStream_dup_handle(grpc::ServerContext*, const dup_handle_Request *request,
    dup_handle_Reply *reply)
{
    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto handle = manager.dup_handle(request->pid(), request->handle());
    reply->set_handle(handle);
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
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        LOGGER__INFO("User aborted VStream read.");
        reply->set_status(static_cast<uint32_t>(HAILO_STREAM_ABORTED_BY_USER));
        return grpc::Status::OK;
    }
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
        ProtoStreamInfo proto_stream_info;
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
            proto_nms_info->set_burst_size(stream_info.nms_info.burst_size);
            proto_nms_info->set_burst_type(static_cast<ProtoNmsBurstType>(proto_stream_info.nms_info().burst_type()));
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
    if (HAILO_NOT_AVAILABLE == expected_latency_result.status()) {
        reply->set_status(static_cast<uint32_t>(HAILO_NOT_AVAILABLE));
    } else {
        CHECK_EXPECTED_AS_RPC_STATUS(expected_latency_result, reply);
        reply->set_avg_hw_latency(static_cast<uint32_t>(expected_latency_result.value().avg_hw_latency.count()));
        reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    }
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_is_multi_context(grpc::ServerContext*,
    const ConfiguredNetworkGroup_is_multi_context_Request *request,
    ConfiguredNetworkGroup_is_multi_context_Reply *reply)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng) {
            return cng->is_multi_context();
    };
    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto is_multi_context = manager.execute<bool>(request->handle(), lambda);
    reply->set_is_multi_context(static_cast<bool>(is_multi_context));
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_get_sorted_output_names(grpc::ServerContext*,
    const ConfiguredNetworkGroup_get_sorted_output_names_Request *request,
    ConfiguredNetworkGroup_get_sorted_output_names_Reply *reply)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng) {
            return cng->get_sorted_output_names();
    };
    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto sorted_output_names_expected = manager.execute<Expected<std::vector<std::string>>>(request->handle(), lambda);
    CHECK_EXPECTED_AS_RPC_STATUS(sorted_output_names_expected, reply);
    auto sorted_output_names_proto = reply->mutable_sorted_output_names();
    for (auto &name : sorted_output_names_expected.value()) {
        sorted_output_names_proto->Add(std::move(name));
    }
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_get_stream_names_from_vstream_name(grpc::ServerContext*,
    const ConfiguredNetworkGroup_get_stream_names_from_vstream_name_Request *request,
    ConfiguredNetworkGroup_get_stream_names_from_vstream_name_Reply *reply)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng, const std::string &vstream_name) {
            return cng->get_stream_names_from_vstream_name(vstream_name);
    };
    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto streams_names_expected = manager.execute<Expected<std::vector<std::string>>>(request->handle(), lambda, request->vstream_name());
    CHECK_EXPECTED_AS_RPC_STATUS(streams_names_expected, reply);
    auto streams_names_proto = reply->mutable_streams_names();
    for (auto &name : streams_names_expected.value()) {
        streams_names_proto->Add(std::move(name));
    }
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_get_vstream_names_from_stream_name(grpc::ServerContext*,
    const ConfiguredNetworkGroup_get_vstream_names_from_stream_name_Request *request,
    ConfiguredNetworkGroup_get_vstream_names_from_stream_name_Reply *reply)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng, const std::string &stream_name) {
            return cng->get_vstream_names_from_stream_name(stream_name);
    };
    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto vstreams_names_expected = manager.execute<Expected<std::vector<std::string>>>(request->handle(), lambda, request->stream_name());
    CHECK_EXPECTED_AS_RPC_STATUS(vstreams_names_expected, reply);
    auto vstreams_names_proto = reply->mutable_vstreams_names();
    for (auto &name : vstreams_names_expected.value()) {
        vstreams_names_proto->Add(std::move(name));
    }
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
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
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
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::InputVStream_network_name(grpc::ServerContext*, const VStream_network_name_Request *request,
    VStream_network_name_Reply *reply)
{
    auto lambda = [](std::shared_ptr<InputVStream> input_vstream) {
            return input_vstream->network_name();
    };
    auto &manager = ServiceResourceManager<InputVStream>::get_instance();
    auto name = manager.execute<std::string>(request->handle(), lambda);
    reply->set_network_name(name);
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::OutputVStream_network_name(grpc::ServerContext*, const VStream_network_name_Request *request,
    VStream_network_name_Reply *reply)
{
    auto lambda = [](std::shared_ptr<OutputVStream> output_vstream) {
            return output_vstream->network_name();
    };
    auto &manager = ServiceResourceManager<OutputVStream>::get_instance();
    auto name = manager.execute<std::string>(request->handle(), lambda);
    reply->set_network_name(name);
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::InputVStream_abort(grpc::ServerContext*, const VStream_abort_Request *request,
    VStream_abort_Reply *reply)
{
    auto status = abort_input_vstream(request->handle());
    reply->set_status(status);
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::OutputVStream_abort(grpc::ServerContext*, const VStream_abort_Request *request,
    VStream_abort_Reply *reply)
{
    auto status = abort_output_vstream(request->handle());
    reply->set_status(status);
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::InputVStream_resume(grpc::ServerContext*, const VStream_resume_Request *request,
    VStream_resume_Reply *reply)
{
    auto lambda = [](std::shared_ptr<InputVStream> input_vstream) {
            return input_vstream->resume();
    };
    auto &manager = ServiceResourceManager<InputVStream>::get_instance();
    auto status = manager.execute<hailo_status>(request->handle(), lambda);
    reply->set_status(status);
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::OutputVStream_resume(grpc::ServerContext*, const VStream_resume_Request *request,
    VStream_resume_Reply *reply)
{
    auto lambda = [](std::shared_ptr<OutputVStream> output_vstream) {
            return output_vstream->resume();
    };
    auto &manager = ServiceResourceManager<OutputVStream>::get_instance();
    auto status = manager.execute<hailo_status>(request->handle(), lambda);
    reply->set_status(status);
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::InputVStream_stop_and_clear(grpc::ServerContext*, const VStream_stop_and_clear_Request *request,
    VStream_stop_and_clear_Reply *reply)
{
    auto lambda = [](std::shared_ptr<InputVStream> input_vstream) {
            return input_vstream->stop_and_clear();
    };
    auto &manager = ServiceResourceManager<InputVStream>::get_instance();
    auto status = manager.execute<hailo_status>(request->handle(), lambda);
    reply->set_status(status);
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::OutputVStream_stop_and_clear(grpc::ServerContext*, const VStream_stop_and_clear_Request *request,
    VStream_stop_and_clear_Reply *reply)
{
    auto lambda = [](std::shared_ptr<OutputVStream> output_vstream) {
            return output_vstream->stop_and_clear();
    };
    auto &manager = ServiceResourceManager<OutputVStream>::get_instance();
    auto status = manager.execute<hailo_status>(request->handle(), lambda);
    reply->set_status(status);
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::InputVStream_start_vstream(grpc::ServerContext*, const VStream_start_vstream_Request *request,
    VStream_start_vstream_Reply *reply)
{
    auto lambda = [](std::shared_ptr<InputVStream> input_vstream) {
            return input_vstream->start_vstream();
    };
    auto &manager = ServiceResourceManager<InputVStream>::get_instance();
    auto status = manager.execute<hailo_status>(request->handle(), lambda);
    reply->set_status(status);
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::OutputVStream_start_vstream(grpc::ServerContext*, const VStream_start_vstream_Request *request,
    VStream_start_vstream_Reply *reply)
{
    auto lambda = [](std::shared_ptr<OutputVStream> output_vstream) {
            return output_vstream->start_vstream();
    };
    auto &manager = ServiceResourceManager<OutputVStream>::get_instance();
    auto status = manager.execute<hailo_status>(request->handle(), lambda);
    reply->set_status(status);
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::InputVStream_get_user_buffer_format(grpc::ServerContext*, const VStream_get_user_buffer_format_Request *request,
    VStream_get_user_buffer_format_Reply *reply)
{
    auto lambda = [](std::shared_ptr<InputVStream> input_vstream) {
            return input_vstream->get_user_buffer_format();
    };
    auto &manager = ServiceResourceManager<InputVStream>::get_instance();
    auto format = manager.execute<hailo_format_t>(request->handle(), lambda);
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));

    auto proto_user_buffer_format = reply->mutable_user_buffer_format();
    proto_user_buffer_format->set_type(format.type);
    proto_user_buffer_format->set_order(format.order);
    proto_user_buffer_format->set_flags(format.flags);

    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::OutputVStream_get_user_buffer_format(grpc::ServerContext*, const VStream_get_user_buffer_format_Request *request,
    VStream_get_user_buffer_format_Reply *reply)
{
    auto lambda = [](std::shared_ptr<OutputVStream> output_vstream) {
            return output_vstream->get_user_buffer_format();
    };
    auto &manager = ServiceResourceManager<OutputVStream>::get_instance();
    auto format = manager.execute<hailo_format_t>(request->handle(), lambda);
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));

    auto proto_user_buffer_format = reply->mutable_user_buffer_format();
    proto_user_buffer_format->set_type(format.type);
    proto_user_buffer_format->set_order(format.order);
    proto_user_buffer_format->set_flags(format.flags);

    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::InputVStream_get_info(grpc::ServerContext*, const VStream_get_info_Request *request,
    VStream_get_info_Reply *reply)
{
    auto lambda = [](std::shared_ptr<InputVStream> input_vstream) {
            return input_vstream->get_info();
    };
    auto &manager = ServiceResourceManager<InputVStream>::get_instance();
    auto info = manager.execute<hailo_vstream_info_t>(request->handle(), lambda);
    auto info_proto = reply->mutable_vstream_info();
    serialize_vstream_info(info, info_proto);
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::OutputVStream_get_info(grpc::ServerContext*, const VStream_get_info_Request *request,
    VStream_get_info_Reply *reply)
{
    auto lambda = [](std::shared_ptr<OutputVStream> output_vstream) {
            return output_vstream->get_info();
    };
    auto &manager = ServiceResourceManager<OutputVStream>::get_instance();
    auto info = manager.execute<hailo_vstream_info_t>(request->handle(), lambda);
    auto info_proto = reply->mutable_vstream_info();
    serialize_vstream_info(info, info_proto);
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::InputVStream_is_aborted(grpc::ServerContext*, const VStream_is_aborted_Request *request,
    VStream_is_aborted_Reply *reply)
{
    auto lambda = [](std::shared_ptr<OutputVStream> input_vstream) {
            return input_vstream->is_aborted();
    };
    auto &manager = ServiceResourceManager<OutputVStream>::get_instance();
    auto is_aborted = manager.execute<bool>(request->handle(), lambda);
    reply->set_is_aborted(is_aborted);
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::OutputVStream_is_aborted(grpc::ServerContext*, const VStream_is_aborted_Request *request,
    VStream_is_aborted_Reply *reply)
{
    auto lambda = [](std::shared_ptr<InputVStream> input_vstream) {
            return input_vstream->is_aborted();
    };
    auto &manager = ServiceResourceManager<InputVStream>::get_instance();
    auto is_aborted = manager.execute<bool>(request->handle(), lambda);
    reply->set_is_aborted(is_aborted);
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

}

