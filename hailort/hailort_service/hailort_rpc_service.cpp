/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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
#include "cng_buffer_pool.hpp"
#include "rpc/rpc_definitions.hpp"
#include "service_resource_manager.hpp"
#include "net_flow/ops_metadata/op_metadata.hpp"
#include "net_flow/ops_metadata/nms_op_metadata.hpp"
#include "net_flow/ops_metadata/yolov8_op_metadata.hpp"
#include "net_flow/ops_metadata/ssd_op_metadata.hpp"
#include "net_flow/ops_metadata/yolox_op_metadata.hpp"
#include "net_flow/ops_metadata/yolov5_op_metadata.hpp"
#include "net_flow/ops_metadata/yolov5_seg_op_metadata.hpp"

#include "hef/layer_info.hpp"

#include <thread>


#define MAX_GRPC_BUFFER_SIZE (2ULL * 1024 * 1024 * 1024) // 2GB
namespace hailort
{

HailoRtRpcService::HailoRtRpcService()
    : ProtoHailoRtRpc::Service()
{
    m_keep_alive = make_unique_nothrow<std::thread>([this] () {
        this->keep_alive();
    });
}

hailo_status HailoRtRpcService::flush_input_vstream(uint32_t handle)
{
    auto lambda = [](std::shared_ptr<InputVStream> input_vstream) {
        return input_vstream->flush();
    };
    auto &manager = ServiceResourceManager<InputVStream>::get_instance();
    auto status = manager.execute(handle, lambda);
    CHECK_SUCCESS(status, "Failed to flush input vstream with status {}", status);

    return HAILO_SUCCESS;
}


hailo_status HailoRtRpcService::abort_input_vstream(uint32_t handle)
{
    auto lambda = [](std::shared_ptr<InputVStream> input_vstream) {
        return input_vstream->abort();
    };
    auto &manager = ServiceResourceManager<InputVStream>::get_instance();
    auto status = manager.execute(handle, lambda);
    CHECK_SUCCESS(status, "Failed to abort input vstream with status {}", status);

    return HAILO_SUCCESS;
}

hailo_status HailoRtRpcService::abort_output_vstream(uint32_t handle)
{
    auto lambda = [](std::shared_ptr<OutputVStream> output_vstream) {
        return output_vstream->abort();
    };
    auto &manager = ServiceResourceManager<OutputVStream>::get_instance();
    auto status = manager.execute(handle, lambda);
    CHECK_SUCCESS(status, "Failed to abort output vstream with status {}", status);

    return HAILO_SUCCESS;
}

// TODO: Add a named templated release functions for InputVStream and OutputVStream to call abort before release.
void HailoRtRpcService::abort_vstreams_by_ids(std::set<uint32_t> &pids)
{
    auto inputs_handles = ServiceResourceManager<InputVStream>::get_instance().resources_handles_by_ids(pids);
    auto outputs_handles = ServiceResourceManager<OutputVStream>::get_instance().resources_handles_by_ids(pids);
    for (auto &input_handle : inputs_handles) {
        abort_input_vstream(input_handle);
    }
    for (auto &output_handle : outputs_handles) {
        abort_output_vstream(output_handle);
    }
}

hailo_status HailoRtRpcService::shutdown_configured_network_group(uint32_t vdevice_handle)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng) {
        return cng->shutdown();
    };

    auto &cng_manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto status = cng_manager.execute(vdevice_handle, lambda);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}


void HailoRtRpcService::shutdown_configured_network_groups_by_ids(std::set<uint32_t> &pids)
{
    auto cng_handles = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance().resources_handles_by_ids(pids);
    for (auto &handle : cng_handles) {
        auto status = shutdown_configured_network_group(handle);
        if (status != HAILO_SUCCESS) {
            LOGGER__ERROR("Failed to shutdown configured network group queue with handle={}, status={}", handle, status);
        }
    }
}

void HailoRtRpcService::shutdown_buffer_pool_by_ids(std::set<uint32_t> &pids)
{
    auto buffer_pools_handles = ServiceResourceManager<ServiceNetworkGroupBufferPool>::get_instance().resources_handles_by_ids(pids);
    for (auto &handle : buffer_pools_handles) {
        auto status = shutdown_cng_buffer_pool(handle);
        if (status != HAILO_SUCCESS) {
            LOGGER__ERROR("Failed to shutdown cng buffer pool with handle={}, status={}", handle, status);
        }
    }
}

void HailoRtRpcService::shutdown_vdevice_cb_queue_by_ids(std::set<uint32_t> &pids)
{
    auto vdevice_cb_queue_handles = ServiceResourceManager<VDeviceCallbacksQueue>::get_instance().resources_handles_by_ids(pids);
    for (auto &handle : vdevice_cb_queue_handles) {
        auto status = shutdown_vdevice_cb_queue(handle);
        if (status != HAILO_SUCCESS) {
            LOGGER__ERROR("Failed to shutdown vdevice callbacks queue with handle={}, status={}", handle, status);
        }
    }
}

void HailoRtRpcService::remove_disconnected_clients()
{
    auto now = std::chrono::high_resolution_clock::now();
    std::set<uint32_t> pids_to_remove;
    {
        std::unique_lock<std::mutex> lock(m_keep_alive_mutex);
        for (auto pid_to_last_alive : m_clients_pids) {
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - pid_to_last_alive.second);
            if (duration > hailort::HAILO_KEEPALIVE_INTERVAL) {
                auto client_pid = pid_to_last_alive.first;
                pids_to_remove.insert(client_pid);
            }
        }

        // Only for performance optimization (notice code is inside a locked mutex here)
        if (pids_to_remove.empty()) {
            return;
        }

        // We abort vstreams before releasing them to avoid cases where the vstream is stuck in execute of a
        // blocking operation (which will be finished with timeout).
        // To release the vstream the ServiceResourceManager is waiting for the resource_mutex which is also locked in execute.
        abort_vstreams_by_ids(pids_to_remove);

        // It is important to shutdown the cb Queue before the NG shutdown, as ongoing callbacks might continue to try to enqueue
        shutdown_vdevice_cb_queue_by_ids(pids_to_remove);
        shutdown_configured_network_groups_by_ids(pids_to_remove);
        shutdown_buffer_pool_by_ids(pids_to_remove);
        for (auto &client_pid : pids_to_remove) {
            ServiceResourceManager<OutputVStream>::get_instance().release_by_id(client_pid);
            ServiceResourceManager<InputVStream>::get_instance().release_by_id(client_pid);
            ServiceResourceManager<ConfiguredNetworkGroup>::get_instance().release_by_id(client_pid);
            ServiceResourceManager<VDeviceCallbacksQueue>::get_instance().release_by_id(client_pid);
            ServiceResourceManager<ServiceNetworkGroupBufferPool>::get_instance().release_by_id(client_pid);
            ServiceResourceManager<VDevice>::get_instance().release_by_id(client_pid);

            LOGGER__INFO("Client disconnected, pid: {}", client_pid);
            HAILORT_OS_LOG_INFO("Client disconnected, pid: {}", client_pid);
            m_clients_pids.erase(client_pid);
        }
    }
}

void HailoRtRpcService::keep_alive()
{
    while (true) {
        std::this_thread::sleep_for(hailort::HAILO_KEEPALIVE_INTERVAL / 2);
        remove_disconnected_clients();
    }
}

void HailoRtRpcService::update_client_id_timestamp(uint32_t pid)
{
    std::unique_lock<std::mutex> lock(m_keep_alive_mutex);
    m_clients_pids[pid] = std::chrono::high_resolution_clock::now();
}

grpc::Status HailoRtRpcService::client_keep_alive(grpc::ServerContext*, const keepalive_Request *request,
    empty*)
{
    auto client_id = request->pid();
    update_client_id_timestamp(client_id);
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

    update_client_id_timestamp(request->pid());
    std::unique_lock<std::mutex> lock(m_vdevice_mutex);
    auto &vdevice_manager = ServiceResourceManager<VDevice>::get_instance();
    auto &cb_queue_manager = ServiceResourceManager<VDeviceCallbacksQueue>::get_instance();

    auto vdevice_handle = vdevice_manager.register_resource(request->pid(), std::move(vdevice.release()));

    auto cb_queue = VDeviceCallbacksQueue::create(MAX_QUEUE_SIZE);
    if (HAILO_SUCCESS != cb_queue.status()) {
        // cb_queue_handle and vdevice_handle indexes must be the same
        cb_queue_manager.advance_current_handle_index();
    }
    CHECK_EXPECTED_AS_RPC_STATUS(cb_queue, reply);

    auto cb_queue_handle = cb_queue_manager.register_resource(request->pid(), std::move(cb_queue.release()));
    if (cb_queue_handle != vdevice_handle) {
        LOGGER__ERROR("cb_queue_handle = {} must be equal to vdevice_handle ={}", cb_queue_handle, vdevice_handle);
        reply->set_status(HAILO_INTERNAL_FAILURE);
        return grpc::Status::OK;
    }

    reply->set_handle(vdevice_handle);
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

hailo_status HailoRtRpcService::shutdown_vdevice_cb_queue(uint32_t vdevice_handle)
{
    auto lambda = [](std::shared_ptr<VDeviceCallbacksQueue> cb_queue) {
        return cb_queue->shutdown();
    };

    auto &cb_queue_manager = ServiceResourceManager<VDeviceCallbacksQueue>::get_instance();
    auto status = cb_queue_manager.execute(vdevice_handle, lambda);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

grpc::Status HailoRtRpcService::VDevice_release(grpc::ServerContext*, const Release_Request *request,
    Release_Reply *reply)
{
    auto status = shutdown_vdevice_cb_queue(request->vdevice_identifier().vdevice_handle());
    CHECK_SUCCESS_AS_RPC_STATUS(status, reply);

    auto &cb_queue_manager = ServiceResourceManager<VDeviceCallbacksQueue>::get_instance();
    cb_queue_manager.release_resource(request->vdevice_identifier().vdevice_handle(), request->pid());

    auto &manager = ServiceResourceManager<VDevice>::get_instance();
    manager.release_resource(request->vdevice_identifier().vdevice_handle(), request->pid());
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

void HailoRtRpcService::release_resources_on_error(std::vector<uint32_t> ng_handles, std::vector<uint32_t> buffer_pool_handles, uint32_t pid)
{
    auto &networks_manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    for (auto &current_ng_handle : ng_handles) {
        networks_manager.release_resource(current_ng_handle, pid);
    }
    for (auto &buffer_pool_handle : buffer_pool_handles) {
        auto status = shutdown_cng_buffer_pool(buffer_pool_handle);
        if (status != HAILO_SUCCESS) {
            LOGGER__ERROR("Failed to shutdown cng buffer pool with handle={}, status={}", buffer_pool_handle, status);
        }
        networks_manager.release_resource(buffer_pool_handle, pid);
    }
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

    update_client_id_timestamp(request->pid());
    std::unique_lock<std::mutex> lock(m_vdevice_mutex);
    auto lambda = [](std::shared_ptr<VDevice> vdevice, Hef &hef, NetworkGroupsParamsMap &configure_params_map) {
        return vdevice->configure(hef, configure_params_map);
    };
    auto &vdevice_manager = ServiceResourceManager<VDevice>::get_instance();
    auto networks = vdevice_manager.execute<Expected<ConfiguredNetworkGroupVector>>(request->identifier().vdevice_handle(), lambda,
        hef.release(), configure_params_map);
    CHECK_EXPECTED_AS_RPC_STATUS(networks, reply);

    auto &networks_manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    std::vector<uint32_t> ng_handles;
    std::vector<uint32_t> buffer_pool_handles;

    for (auto network : networks.value()) {
        auto ng_handle = networks_manager.register_resource(request->pid(), network);
        ng_handles.push_back(ng_handle);

        bool allocate_for_raw_streams = false;
        // The network_group's buffer pool is used for the read's buffers,
        // On async flow - we allocate for raw-streams. This way they are already pre-allocated and mapped to the device
        if ((configure_params_map.size() > 0) &&
            (configure_params_map.begin()->second.stream_params_by_name.begin()->second.flags == HAILO_STREAM_FLAGS_ASYNC)) {
            // We assume that if 1 stream is marked as ASYNC, they all are
            allocate_for_raw_streams = true;
        }
        auto cng_buffer_pool_handle_exp = create_buffer_pool_for_ng(request->identifier().vdevice_handle(), request->pid());
        // We check status like this, since need to release all the resources if one of the buffer pools failed to create
        if (cng_buffer_pool_handle_exp.status() != HAILO_SUCCESS) {
            release_resources_on_error(ng_handles, buffer_pool_handles, request->pid());
        }
        CHECK_EXPECTED_AS_RPC_STATUS(cng_buffer_pool_handle_exp, reply);
        buffer_pool_handles.push_back(cng_buffer_pool_handle_exp.value());
        if (cng_buffer_pool_handle_exp.value() != ng_handle) {
            LOGGER__ERROR("cng_buffer_pool_handle = {} must be different from network_group_handle = {}", cng_buffer_pool_handle_exp.value(), ng_handle);
            release_resources_on_error(ng_handles, buffer_pool_handles, request->pid());
            CHECK_SUCCESS_AS_RPC_STATUS(HAILO_INTERNAL_FAILURE, reply);
        }

        if (allocate_for_raw_streams) {
            auto status = allocate_pool_for_raw_streams(ng_handle);
            // We check status like this, since need to release all the resources if one of the buffer pools failed to create
            if (status != HAILO_SUCCESS) {
                release_resources_on_error(ng_handles, buffer_pool_handles, request->pid());
            }
            CHECK_SUCCESS_AS_RPC_STATUS(status, reply);
        }

        reply->add_networks_handles(ng_handle);
    }

    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

Expected<uint32_t> HailoRtRpcService::create_buffer_pool_for_ng(uint32_t vdevice_handle, uint32_t request_pid)
{
    auto &cng_buffer_pool_manager = ServiceResourceManager<ServiceNetworkGroupBufferPool>::get_instance();

    auto cng_buffer_pool_exp = ServiceNetworkGroupBufferPool::create(vdevice_handle);
    if (HAILO_SUCCESS != cng_buffer_pool_exp.status()) {
        // cng_buffer_pool_handle and network_group_handle indexes must be the same
        cng_buffer_pool_manager.advance_current_handle_index();
        return make_unexpected(cng_buffer_pool_exp.status());
    }
    auto cng_buffer_pool = cng_buffer_pool_exp.release();

    auto cng_buffer_pool_handle = cng_buffer_pool_manager.register_resource(request_pid, cng_buffer_pool);
    return cng_buffer_pool_handle;
}

hailo_status HailoRtRpcService::allocate_pool_for_raw_streams(uint32_t ng_handle)
{
    auto &cng_buffer_pool_manager = ServiceResourceManager<ServiceNetworkGroupBufferPool>::get_instance();
    // For Async API - The buffer size in the pool will be the stream's hw frame size as used in the infer_model pipeline
    TRY(const auto min_buffer_pool_size, infer_queue_size(ng_handle));
    TRY(const auto streams_infos, get_all_stream_infos(ng_handle));

    for (const auto &stream_info : streams_infos) {
        if (stream_info.direction == HAILO_D2H_STREAM) {
            auto allocate_lambda = [&](std::shared_ptr<ServiceNetworkGroupBufferPool> cng_buffer_pool) {
                return cng_buffer_pool->allocate_pool(stream_info.name, HAILO_DMA_BUFFER_DIRECTION_D2H,
                    stream_info.hw_frame_size, min_buffer_pool_size);
            };
            CHECK_SUCCESS(cng_buffer_pool_manager.execute(ng_handle, allocate_lambda));
        }
    }

    return HAILO_SUCCESS;
}

grpc::Status HailoRtRpcService::VDevice_get_physical_devices_ids(grpc::ServerContext*,
    const VDevice_get_physical_devices_ids_Request* request, VDevice_get_physical_devices_ids_Reply* reply)
{
    auto lambda = [](std::shared_ptr<VDevice> vdevice) {
        return vdevice->get_physical_devices_ids();
    };
    auto &vdevice_manager = ServiceResourceManager<VDevice>::get_instance();
    auto expected_devices_ids = vdevice_manager.execute<Expected<std::vector<std::string>>>(request->identifier().vdevice_handle(), lambda);
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
    auto stream_interface = vdevice_manager.execute<Expected<hailo_stream_interface_t>>(request->identifier().vdevice_handle(), lambda);
    CHECK_EXPECTED_AS_RPC_STATUS(stream_interface, reply);
    reply->set_stream_interface(*stream_interface);
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::VDevice_get_callback_id(grpc::ServerContext*,
    const VDevice_get_callback_id_Request* request, VDevice_get_callback_id_Reply* reply)
{
    auto lambda = [](std::shared_ptr<VDeviceCallbacksQueue> cb_queue) {
        // TODO: HRT-12360 - Add a `dequeue_all` function that returns all the cb_ids currently in the queue.
        // (Need to think on the shutdown case)
        return cb_queue->dequeue();
    };

    auto &cb_queue_manager = ServiceResourceManager<VDeviceCallbacksQueue>::get_instance();
    auto cb_id_expected = cb_queue_manager.execute<Expected<ProtoCallbackIdentifier>>(request->identifier().vdevice_handle(), lambda);
    if (cb_id_expected.status() == HAILO_SHUTDOWN_EVENT_SIGNALED) {
        reply->set_status(static_cast<uint32_t>(HAILO_SHUTDOWN_EVENT_SIGNALED));
        return grpc::Status::OK;
    }
    CHECK_EXPECTED_AS_RPC_STATUS(cb_id_expected, reply);
    auto proto_callback_id = reply->mutable_callback_id();
    *proto_callback_id = cb_id_expected.release();
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::VDevice_finish_callback_listener(grpc::ServerContext*,
    const VDevice_finish_callback_listener_Request* request, VDevice_finish_callback_listener_Reply* reply)
{
    auto lambda = [](std::shared_ptr<VDeviceCallbacksQueue> cb_queue) {
        return cb_queue->shutdown();
    };

    auto &cb_queue_manager = ServiceResourceManager<VDeviceCallbacksQueue>::get_instance();
    auto status = cb_queue_manager.execute(request->identifier().vdevice_handle(), lambda);
    CHECK_SUCCESS_AS_RPC_STATUS(status, reply);
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_dup_handle(grpc::ServerContext*, const ConfiguredNetworkGroup_dup_handle_Request *request,
    ConfiguredNetworkGroup_dup_handle_Reply* reply)
{
    auto &vdevice_manager = ServiceResourceManager<VDevice>::get_instance();
    vdevice_manager.dup_handle(request->identifier().vdevice_handle(), request->pid());

    auto &ng_manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto handle = ng_manager.dup_handle(request->identifier().network_group_handle(), request->pid());
    CHECK_EXPECTED_AS_RPC_STATUS(handle, reply);

    reply->set_handle(handle.release());
    return grpc::Status::OK;
}

ProtoCallbackIdentifier serialize_callback_identifier(uint32_t vdevice_handle, uint32_t ng_handle,
    callback_type_t cb_type, const std::string &stream_name, uint32_t cb_idx,  hailo_status status, BufferPtr buffer = nullptr)
{
    ProtoCallbackIdentifier cb_identifier;
    cb_identifier.set_vdevice_handle(vdevice_handle);
    cb_identifier.set_network_group_handle(ng_handle);
    cb_identifier.set_cb_type(cb_type);
    cb_identifier.set_stream_name(stream_name);
    cb_identifier.set_cb_idx(cb_idx);
    cb_identifier.set_status(status);
    if (buffer != nullptr) {
        cb_identifier.set_data(buffer->data(), buffer->size());
    }

    return cb_identifier;
}

ProtoCallbackIdentifier serialize_callback_identifier_shm(uint32_t vdevice_handle, uint32_t ng_handle, callback_type_t cb_type,
    const std::string &stream_name, uint32_t cb_idx,  hailo_status status, const ProtoShmBufferIdentifier &shm_buffer_identifier)
{
    ProtoCallbackIdentifier cb_identifier;
    cb_identifier.set_vdevice_handle(vdevice_handle);
    cb_identifier.set_network_group_handle(ng_handle);
    cb_identifier.set_cb_type(cb_type);
    cb_identifier.set_stream_name(stream_name);
    cb_identifier.set_cb_idx(cb_idx);
    cb_identifier.set_status(status);

    auto proto_shm_identifier = cb_identifier.mutable_shared_memory_identifier();
    proto_shm_identifier->set_name(shm_buffer_identifier.name());
    proto_shm_identifier->set_size(shm_buffer_identifier.size());

    return cb_identifier;
}

hailo_status HailoRtRpcService::shutdown_cng_buffer_pool(uint32_t buffer_pool_handle)
{
    auto buffer_shutdown_lambda = [](std::shared_ptr<ServiceNetworkGroupBufferPool> cng_buffer_pool) {
        return cng_buffer_pool->shutdown();
    };

    auto &buffer_pool_manager = ServiceResourceManager<ServiceNetworkGroupBufferPool>::get_instance();
    auto status = buffer_pool_manager.execute(buffer_pool_handle, buffer_shutdown_lambda);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_release(grpc::ServerContext*, const Release_Request *request,
    Release_Reply *reply)
{
    auto status = shutdown_cng_buffer_pool(request->network_group_identifier().network_group_handle());
    CHECK_SUCCESS_AS_RPC_STATUS(status, reply);

    auto &buffer_pool_manager = ServiceResourceManager<ServiceNetworkGroupBufferPool>::get_instance();
    buffer_pool_manager.release_resource(request->network_group_identifier().network_group_handle(), request->pid());

    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    manager.release_resource(request->network_group_identifier().network_group_handle(), request->pid());
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

hailo_status HailoRtRpcService::add_input_named_buffer(const ProtoTransferRequest &proto_stream_transfer_request,
    uint32_t vdevice_handle, uint32_t ng_handle, std::shared_ptr<ConfiguredNetworkGroup_infer_async_Request> infer_async_request,
    NamedBuffersCallbacks &named_buffers_callbacks)
{
    // Prepare input buffer
    BufferPtr buffer;
    MemoryView mem_view;
    if (proto_stream_transfer_request.has_shared_memory_identifier()) {
        TRY(buffer, Buffer::create_shared(proto_stream_transfer_request.shared_memory_identifier().size(),
            BufferStorageParams::open_shared_memory(proto_stream_transfer_request.shared_memory_identifier().name())));
        mem_view = MemoryView(*buffer);
    } else {
        auto *data = reinterpret_cast<const uint8_t*>(proto_stream_transfer_request.data().c_str());
        if (reinterpret_cast<size_t>(data) % HailoRTCommon::HW_DATA_ALIGNMENT == 0) {
            // Input buffers is aligned to 8
            mem_view = MemoryView::create_const(data, proto_stream_transfer_request.data().size());
        } else {
            // The memory is not aligned to 8, therefore we need to copy the data into a buffer
            TRY(buffer, Buffer::create_shared(data, proto_stream_transfer_request.data().size(),
                BufferStorageParams::create_dma()));
            mem_view = MemoryView(*buffer);
        }
    }

    // Preparing callback
    auto &stream_name = proto_stream_transfer_request.stream_name();
    CHECK(stream_name != INVALID_STREAM_NAME, HAILO_INTERNAL_FAILURE, "Got invalid stream name");
    
    auto cb_idx = proto_stream_transfer_request.cb_idx();
    CHECK(cb_idx != INVALID_CB_INDEX, HAILO_INTERNAL_FAILURE, "Got invalid callback index");

    std::function<void(hailo_status)> transfer_done = [this, vdevice_handle, ng_handle, cb_idx, stream_name, buffer, infer_async_request]
        (hailo_status status)
    {
        // We pass the request (which is shared_ptr) to the callback in order to keep the input's memory alive until inference is done.
        (void)infer_async_request;
        (void)buffer;

        auto cb_identifier = serialize_callback_identifier(vdevice_handle, ng_handle, CALLBACK_TYPE_TRANSFER,
            stream_name, cb_idx, status);
        enqueue_cb_identifier(vdevice_handle, std::move(cb_identifier));
    };

    BufferRepresentation buffer_representation {};
    buffer_representation.buffer_type = BufferType::VIEW;
    buffer_representation.view = mem_view;

    named_buffers_callbacks.emplace(stream_name, std::make_pair(buffer_representation, transfer_done));
    return HAILO_SUCCESS;
}

hailo_status HailoRtRpcService::add_output_named_buffer(const ProtoTransferRequest &proto_stream_transfer_request, uint32_t vdevice_handle,
    uint32_t ng_handle, NamedBuffersCallbacks &named_buffers_callbacks)
{
    auto &stream_name = proto_stream_transfer_request.stream_name();
    CHECK(stream_name != INVALID_STREAM_NAME, HAILO_INTERNAL_FAILURE, "Got invalid stream name");

    // Prepare output buffer
    BufferPtr buffer;
    bool is_shared_mem = proto_stream_transfer_request.has_shared_memory_identifier();
    auto shm_identifier = proto_stream_transfer_request.shared_memory_identifier();
    
    if (is_shared_mem) {
        TRY(buffer, Buffer::create_shared(shm_identifier.size(),
            BufferStorageParams::open_shared_memory(shm_identifier.name())));
    } else {
        TRY(buffer, acquire_buffer_from_cng_pool(ng_handle, stream_name));
    }

    // Prepare callback
    auto cb_idx = proto_stream_transfer_request.cb_idx();
    CHECK(cb_idx != INVALID_CB_INDEX, HAILO_INTERNAL_FAILURE, "Got invalid callback index");
    
    std::function<void(hailo_status)> transfer_done = [this, vdevice_handle, ng_handle, cb_idx, stream_name, buffer,
        is_shared_mem, shm_identifier]
        (hailo_status status)
    {
        ProtoCallbackIdentifier cb_identifier;
        if (is_shared_mem) {
            cb_identifier = serialize_callback_identifier_shm(vdevice_handle, ng_handle, CALLBACK_TYPE_TRANSFER,
                stream_name, cb_idx, status, shm_identifier);
        } else {
            cb_identifier = serialize_callback_identifier(vdevice_handle, ng_handle, CALLBACK_TYPE_TRANSFER,
                stream_name, cb_idx, status, buffer);
            return_buffer_to_cng_pool(ng_handle, stream_name, buffer);
        }
        enqueue_cb_identifier(vdevice_handle, std::move(cb_identifier));
    };

    BufferRepresentation buffer_representation {};
    buffer_representation.buffer_type = BufferType::VIEW;
    buffer_representation.view = MemoryView(*buffer);

    named_buffers_callbacks.emplace(stream_name, std::make_pair(buffer_representation, transfer_done));
    return HAILO_SUCCESS;
}

Expected<NamedBuffersCallbacks> HailoRtRpcService::prepare_named_buffers_callbacks(uint32_t vdevice_handle,
    uint32_t ng_handle, std::shared_ptr<ConfiguredNetworkGroup_infer_async_Request> infer_async_request)
{
    NamedBuffersCallbacks named_buffers_callbacks;
    for (const auto &proto_stream_transfer_request : infer_async_request->transfer_requests()) {
        auto direction = proto_stream_transfer_request.direction();
        auto status = HAILO_SUCCESS;
        if (direction == HAILO_H2D_STREAM) {
            status = add_input_named_buffer(proto_stream_transfer_request, vdevice_handle, ng_handle, infer_async_request, named_buffers_callbacks);
        } else {
            status = add_output_named_buffer(proto_stream_transfer_request, vdevice_handle, ng_handle, named_buffers_callbacks);
        }
        CHECK_SUCCESS_AS_EXPECTED(status);
    }

    return named_buffers_callbacks;
}

void HailoRtRpcService::enqueue_cb_identifier(uint32_t vdevice_handle, ProtoCallbackIdentifier &&cb_identifier)
{
    auto lambda = [](std::shared_ptr<VDeviceCallbacksQueue> cb_queue, ProtoCallbackIdentifier &cb_identifier) {
        return cb_queue->enqueue(std::move(cb_identifier));
    };

    auto &cb_queue_manager = ServiceResourceManager<VDeviceCallbacksQueue>::get_instance();
    auto status = cb_queue_manager.execute(vdevice_handle, lambda, std::move(cb_identifier));
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == status) {
        LOGGER__TRACE("Failed to enqueue callback to VDeviceCallbacksQueue '{}' because it is shutdown", vdevice_handle);
    } else if (status != HAILO_SUCCESS) {
        LOGGER__ERROR("Failed to enqueue callback to VDeviceCallbacksQueue '{}' with status={}", vdevice_handle, status);
    }
}

hailo_status HailoRtRpcService::return_buffer_to_cng_pool(uint32_t ng_handle, const std::string &output_name, BufferPtr buffer)
{
    auto &cng_buffer_pool_manager = ServiceResourceManager<ServiceNetworkGroupBufferPool>::get_instance();
    auto lambda_return_to_pool = [](std::shared_ptr<ServiceNetworkGroupBufferPool> cng_buffer_pool,
        const std::string &stream_name, BufferPtr buffer) {
        return cng_buffer_pool->return_to_pool(stream_name, buffer);
    };
    auto status = cng_buffer_pool_manager.execute(ng_handle, lambda_return_to_pool,
        output_name, buffer);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

Expected<BufferPtr> HailoRtRpcService::acquire_buffer_from_cng_pool(uint32_t ng_handle, const std::string &output_name)
{
    auto &cng_buffer_pool_manager = ServiceResourceManager<ServiceNetworkGroupBufferPool>::get_instance();
    auto lambda_acquire_buffer = [](std::shared_ptr<ServiceNetworkGroupBufferPool> cng_buffer_pool, const std::string &output_name) {
        return cng_buffer_pool->acquire_buffer(output_name);
    };
    TRY(auto buffer,
        cng_buffer_pool_manager.execute<Expected<BufferPtr>>(
            ng_handle, lambda_acquire_buffer, output_name)
    );

    return buffer;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_infer_async(grpc::ServerContext*,
    const ConfiguredNetworkGroup_infer_async_Request *raw_request, ConfiguredNetworkGroup_infer_async_Reply *reply)
{
    // Moving ownership of the request, so we can use the request's memory as the input buffers instead of allocating new memory for it.
    auto request = make_shared_nothrow<ConfiguredNetworkGroup_infer_async_Request>(std::move(*raw_request));

    auto vdevice_handle = request->identifier().vdevice_handle();
    auto ng_handle = request->identifier().network_group_handle();
    auto infer_request_done_cb_idx = request->infer_request_done_cb_idx();

    // Prepare buffers
    auto named_buffers_callbacks = prepare_named_buffers_callbacks(vdevice_handle, ng_handle, request);
    CHECK_EXPECTED_AS_RPC_STATUS(named_buffers_callbacks, reply);

    // Prepare request finish callback
    auto infer_request_done_cb = [this, vdevice_handle, ng_handle, infer_request_done_cb_idx](hailo_status status) {
        auto cb_identifier = serialize_callback_identifier(vdevice_handle, ng_handle, CALLBACK_TYPE_INFER_REQUEST,
            "", infer_request_done_cb_idx, status);
        enqueue_cb_identifier(vdevice_handle, std::move(cb_identifier));
    };

    // Run infer async
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng, NamedBuffersCallbacks &named_buffers_callbacks,
        const std::function<void(hailo_status)> &infer_request_done_cb) {
            return cng->infer_async(named_buffers_callbacks, infer_request_done_cb);
    };

    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto status = manager.execute(request->identifier().network_group_handle(), lambda, named_buffers_callbacks.release(), infer_request_done_cb);
    if (HAILO_STREAM_ABORT == status) {
        LOGGER__INFO("User aborted inference");
        reply->set_status(static_cast<uint32_t>(HAILO_STREAM_ABORT));
        return grpc::Status::OK;
    }
    CHECK_SUCCESS_AS_RPC_STATUS(status, reply);

    reply->set_status(status);
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
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng,
        hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size, std::string network_name) {
            return cng->make_input_vstream_params({}, format_type, timeout_ms, queue_size, network_name);
    };
    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto expected_params = manager.execute<Expected<std::map<std::string, hailo_vstream_params_t>>>(request->identifier().network_group_handle(),
        lambda, static_cast<hailo_format_type_t>(request->format_type()), request->timeout_ms(), request->queue_size(), request->network_name());
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
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng,
        hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size, std::string network_name) {
            return cng->make_output_vstream_params({}, format_type, timeout_ms, queue_size, network_name);
    };
    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto expected_params = manager.execute<Expected<std::map<std::string, hailo_vstream_params_t>>>(request->identifier().network_group_handle(), 
        lambda, static_cast<hailo_format_type_t>(request->format_type()), request->timeout_ms(), request->queue_size(), request->network_name());
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
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng,
        hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size) {
            return cng->make_output_vstream_params_groups({}, format_type, timeout_ms, queue_size);
    };
    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto expected_params = manager.execute<Expected<std::vector<std::map<std::string, hailo_vstream_params_t>>>>(
        request->identifier().network_group_handle(), lambda, static_cast<hailo_format_type_t>(request->format_type()),
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
    auto expected_stream_interface = net_group_manager.execute<Expected<hailo_stream_interface_t>>(request->identifier().network_group_handle(),
        lambda);
    CHECK_EXPECTED_AS_RPC_STATUS(expected_stream_interface, reply);

    reply->set_stream_interface(static_cast<uint32_t>(expected_stream_interface.value()));
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_shutdown(grpc::ServerContext*,
    const ConfiguredNetworkGroup_shutdown_Request *request, ConfiguredNetworkGroup_shutdown_Reply *reply)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng) {
        return cng->shutdown();
    };
    auto &net_group_manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto status = net_group_manager.execute(
        request->identifier().network_group_handle(), lambda);
    CHECK_SUCCESS_AS_RPC_STATUS(status, reply);

    reply->set_status(status);
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
    auto expected_output_vstream_groups = net_group_manager.execute<Expected<std::vector<std::vector<std::string>>>>(
        request->identifier().network_group_handle(), lambda);
    CHECK_EXPECTED_AS_RPC_STATUS(expected_output_vstream_groups, reply);

    auto output_vstream_groups = expected_output_vstream_groups.release();
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
    if (HailoRTCommon::is_nms(info.format.order)) {
        auto nms_shape_proto = info_proto->mutable_nms_shape();
        nms_shape_proto->set_number_of_classes(info.nms_shape.number_of_classes);
        nms_shape_proto->set_max_bboxes_total(info.nms_shape.max_bboxes_total);
        nms_shape_proto->set_max_bboxes_per_class(info.nms_shape.max_bboxes_per_class);
        nms_shape_proto->set_max_accumulated_mask_size(info.nms_shape.max_accumulated_mask_size);
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

void serialize_layer_info(const LayerInfo &layer_info, ProtoLayerInfo *layer_info_proto)
{
    layer_info_proto->set_type(static_cast<uint32_t>(layer_info.type));
    layer_info_proto->set_direction(static_cast<uint32_t>(layer_info.direction));
    layer_info_proto->set_stream_index(layer_info.stream_index);
    layer_info_proto->set_dma_engine_index(layer_info.dma_engine_index);
    layer_info_proto->set_name(std::string(layer_info.name));
    layer_info_proto->set_network_name(std::string(layer_info.network_name));
    layer_info_proto->set_network_index(layer_info.network_index);
    layer_info_proto->set_max_shmifo_size(layer_info.max_shmifo_size);
    layer_info_proto->set_context_index(layer_info.context_index);
    layer_info_proto->set_pad_index(layer_info.pad_index);

    // Transformation and shape info
    auto shape_proto = layer_info_proto->mutable_shape();
    shape_proto->set_height(layer_info.shape.height);
    shape_proto->set_width(layer_info.shape.width);
    shape_proto->set_features(layer_info.shape.features);

    auto hw_shape_proto = layer_info_proto->mutable_hw_shape();
    hw_shape_proto->set_height(layer_info.hw_shape.height);
    hw_shape_proto->set_width(layer_info.hw_shape.width);
    hw_shape_proto->set_features(layer_info.hw_shape.features);

    layer_info_proto->set_hw_data_bytes(layer_info.hw_data_bytes);

    auto format_proto = layer_info_proto->mutable_format();
    format_proto->set_flags(layer_info.format.flags);
    format_proto->set_order(layer_info.format.order);
    format_proto->set_type(layer_info.format.type);

    auto single_quant_info_proto = layer_info_proto->mutable_quant_info();
    single_quant_info_proto->set_qp_zp(layer_info.quant_info.qp_zp);
    single_quant_info_proto->set_qp_scale(layer_info.quant_info.qp_scale);
    single_quant_info_proto->set_limvals_min(layer_info.quant_info.limvals_min);
    single_quant_info_proto->set_limvals_max(layer_info.quant_info.limvals_max);

    auto quant_infos_proto = layer_info_proto->mutable_quant_infos();
    for (const auto &quant_info : layer_info.quant_infos) {
        ProtoQuantInfo proto_quant_info;
        proto_quant_info.set_qp_zp(quant_info.qp_zp);
        proto_quant_info.set_qp_scale(quant_info.qp_scale);
        proto_quant_info.set_limvals_min(quant_info.limvals_min);
        proto_quant_info.set_limvals_max(quant_info.limvals_max);
        quant_infos_proto->Add(std::move(proto_quant_info));
    }

    auto proto_nms_info = layer_info_proto->mutable_nms_info();
    proto_nms_info->set_number_of_classes(layer_info.nms_info.number_of_classes);
    proto_nms_info->set_max_bboxes_per_class(layer_info.nms_info.max_bboxes_per_class);
    proto_nms_info->set_bbox_size(layer_info.nms_info.bbox_size);
    proto_nms_info->set_chunks_per_frame(layer_info.nms_info.chunks_per_frame);
    proto_nms_info->set_is_defused(layer_info.nms_info.is_defused);
    auto proto_nms_info_defuse_info = proto_nms_info->mutable_defuse_info();
    proto_nms_info_defuse_info->set_class_group_index(layer_info.nms_info.defuse_info.class_group_index);
    proto_nms_info_defuse_info->set_original_name(std::string(layer_info.nms_info.defuse_info.original_name));
    proto_nms_info->set_burst_size(layer_info.nms_info.burst_size);
    proto_nms_info->set_burst_type(static_cast<ProtoNmsBurstType>(layer_info.nms_info.burst_type));

    // Mux info
    layer_info_proto->set_is_mux(layer_info.is_mux);

    auto predecessor_proto = layer_info_proto->mutable_predecessor();
    for (const auto &pred : layer_info.predecessor) {
        ProtoLayerInfo proto_pred;
        serialize_layer_info(pred, &proto_pred);
        predecessor_proto->Add(std::move(proto_pred));
    }

    layer_info_proto->set_height_gcd(layer_info.height_gcd);

    auto ratios_proto = layer_info_proto->mutable_height_ratios();
    for (const auto &height_ratio : layer_info.height_ratios) {
        ratios_proto->Add(height_ratio);
    }

    // Multi planes info
    layer_info_proto->set_is_multi_planar(layer_info.is_multi_planar);

    auto planes_proto = layer_info_proto->mutable_planes();
    for (const auto &pred : layer_info.planes) {
        ProtoLayerInfo proto_pred;
        serialize_layer_info(pred, &proto_pred);
        planes_proto->Add(std::move(proto_pred));
    }

    layer_info_proto->set_plane_index(layer_info.plane_index);

    // Defused nms info
    layer_info_proto->set_is_defused_nms(layer_info.is_defused_nms);

    auto fused_proto = layer_info_proto->mutable_fused_nms_layer();
    for (const auto &fused : layer_info.fused_nms_layer) {
        ProtoLayerInfo proto_fused_layer;
        serialize_layer_info(fused, &proto_fused_layer);
        fused_proto->Add(std::move(proto_fused_layer));
    }
}

void serialize_buffer_metadata(const std::pair<std::string, hailort::net_flow::BufferMetaData> &pair, ProtoNamedMetadata *named_metadata_proto)
{
    named_metadata_proto->set_name(pair.first);

    auto metadata_params_proto = named_metadata_proto->mutable_params();

    auto shape_proto = metadata_params_proto->mutable_shape();
    shape_proto->set_height(pair.second.shape.height);
    shape_proto->set_width(pair.second.shape.width);
    shape_proto->set_features(pair.second.shape.features);

    auto padded_shape_proto = metadata_params_proto->mutable_padded_shape();
    padded_shape_proto->set_height(pair.second.padded_shape.height);
    padded_shape_proto->set_width(pair.second.padded_shape.width);
    padded_shape_proto->set_features(pair.second.padded_shape.features);

    auto format_proto = metadata_params_proto->mutable_format();
    format_proto->set_type(pair.second.format.type);
    format_proto->set_order(pair.second.format.order);
    format_proto->set_flags(pair.second.format.flags);

    auto quant_info_proto = metadata_params_proto->mutable_quant_info();
    quant_info_proto->set_qp_zp(pair.second.quant_info.qp_zp);
    quant_info_proto->set_qp_scale(pair.second.quant_info.qp_scale);
    quant_info_proto->set_limvals_min(pair.second.quant_info.limvals_min);
    quant_info_proto->set_limvals_max(pair.second.quant_info.limvals_max);
}

void serialize_input_metadata(const std::unordered_map<std::string, hailort::net_flow::BufferMetaData> &inputs_metadata, ProtoOpMetadata *op_metadata_proto)
{
    for (const auto &pair : inputs_metadata) {
        auto input_metadata_proto = op_metadata_proto->add_inputs_metadata();
        serialize_buffer_metadata(pair, input_metadata_proto);
    }
}

void serialize_output_metadata(const std::unordered_map<std::string, hailort::net_flow::BufferMetaData> &outputs_metadata, ProtoOpMetadata *op_metadata_proto)
{
    for (const auto &pair : outputs_metadata) {
        auto output_metadata_proto = op_metadata_proto->add_outputs_metadata();
        serialize_buffer_metadata(pair, output_metadata_proto);
    }
}

void serialize_yolov5_op_metadata(hailort::net_flow::OpMetadata &op_metadata, ProtoOpMetadata *op_metadata_proto)
{
    hailort::net_flow::Yolov5OpMetadata* yolov5_op_metadata = static_cast<hailort::net_flow::Yolov5OpMetadata*>(&op_metadata);
    auto &yolov5_config = yolov5_op_metadata->yolov5_config();
    auto yolov5_config_proto = op_metadata_proto->mutable_yolov5_config();

    yolov5_config_proto->set_image_height(yolov5_config.image_height);
    yolov5_config_proto->set_image_width(yolov5_config.image_width);

    auto yolov5_config_anchors_list_proto = yolov5_config_proto->mutable_yolov5_anchors();
    for (auto &layer_anchors_pair : yolov5_config.anchors) {
        ProtoYolov5Anchors yolov5_anchors_proto;
        yolov5_anchors_proto.set_layer(layer_anchors_pair.first);
        auto yolov5_anchors_list_proto = yolov5_anchors_proto.mutable_anchors();
        for (auto &anchor : layer_anchors_pair.second) {
            yolov5_anchors_list_proto->Add(anchor);
        }
        yolov5_config_anchors_list_proto->Add(std::move(yolov5_anchors_proto));
    }
}

void serialize_ssd_op_metadata(hailort::net_flow::OpMetadata &op_metadata, ProtoOpMetadata *op_metadata_proto)
{
    hailort::net_flow::SSDOpMetadata* ssd_op_metadata = static_cast<hailort::net_flow::SSDOpMetadata*>(&op_metadata);
    auto &ssd_config = ssd_op_metadata->ssd_config();
    auto ssd_config_proto = op_metadata_proto->mutable_ssd_config();

    ssd_config_proto->set_image_height(ssd_config.image_height);
    ssd_config_proto->set_image_width(ssd_config.image_width);
    ssd_config_proto->set_centers_scale_factor(ssd_config.centers_scale_factor);
    ssd_config_proto->set_bbox_dimensions_scale_factor(ssd_config.bbox_dimensions_scale_factor);
    ssd_config_proto->set_ty_index(ssd_config.ty_index);
    ssd_config_proto->set_tx_index(ssd_config.tx_index);
    ssd_config_proto->set_th_index(ssd_config.th_index);
    ssd_config_proto->set_tw_index(ssd_config.tw_index);
    ssd_config_proto->set_normalize_boxes(ssd_config.normalize_boxes);

    auto ssd_reg_to_cls_list_proto = ssd_config_proto->mutable_reg_to_cls_inputs();
    for (auto &reg_to_cls_input : ssd_config.reg_to_cls_inputs) {
        ProtoSSDRegToClsInputs ssd_reg_to_cls_proto;
        ssd_reg_to_cls_proto.set_reg(reg_to_cls_input.first);
        ssd_reg_to_cls_proto.set_cls(reg_to_cls_input.second);
        ssd_reg_to_cls_list_proto->Add(std::move(ssd_reg_to_cls_proto));
    }

    auto ssd_anchors_list_proto = ssd_config_proto->mutable_anchors();
    for (auto &anchors : ssd_config.anchors) {
        ProtoSSDAnchors ssd_anchors_proto;
        ssd_anchors_proto.set_layer(anchors.first);
        auto ssd_anchors_per_layer_proto = ssd_anchors_proto.mutable_anchors_per_layer();
        for (auto anchor : anchors.second) {
            ssd_anchors_per_layer_proto->Add(anchor);
        }
        ssd_anchors_list_proto->Add(std::move(ssd_anchors_proto));
    }
}

void serialize_yolov8_op_metadata(hailort::net_flow::OpMetadata &op_metadata, ProtoOpMetadata *op_metadata_proto)
{
    hailort::net_flow::Yolov8OpMetadata* yolov8_op_metadata = static_cast<hailort::net_flow::Yolov8OpMetadata*>(&op_metadata);
    auto &yolov8_config = yolov8_op_metadata->yolov8_config();
    auto yolov8_config_proto = op_metadata_proto->mutable_yolov8_config();

    yolov8_config_proto->set_image_height(yolov8_config.image_height);
    yolov8_config_proto->set_image_width(yolov8_config.image_width);

    auto yolov8_reg_to_cls_list_proto = yolov8_config_proto->mutable_reg_to_cls_inputs();
    for (auto &reg_to_cls_input : yolov8_config.reg_to_cls_inputs) {
        ProtoYolov8MatchingLayersNames yolov8_matching_later_names_proto;
        yolov8_matching_later_names_proto.set_reg(reg_to_cls_input.reg);
        yolov8_matching_later_names_proto.set_cls(reg_to_cls_input.cls);
        yolov8_matching_later_names_proto.set_stride(reg_to_cls_input.stride);
        yolov8_reg_to_cls_list_proto->Add(std::move(yolov8_matching_later_names_proto));
    }
}

void serialize_yolox_op_metadata(hailort::net_flow::OpMetadata &op_metadata, ProtoOpMetadata *op_metadata_proto)
{
    hailort::net_flow::YoloxOpMetadata* yolox_op_metadata = static_cast<hailort::net_flow::YoloxOpMetadata*>(&op_metadata);
    auto &yolox_config = yolox_op_metadata->yolox_config();
    auto yolox_config_proto = op_metadata_proto->mutable_yolox_config();

    yolox_config_proto->set_image_height(yolox_config.image_height);
    yolox_config_proto->set_image_width(yolox_config.image_width);

    auto yolox_reg_to_cls_list_proto = yolox_config_proto->mutable_input_names();
    for (auto &input_name : yolox_config.input_names) {
        ProtoYoloxMatchingLayersNames yolox_input_name_proto;
        yolox_input_name_proto.set_reg(input_name.reg);
        yolox_input_name_proto.set_obj(input_name.obj);
        yolox_input_name_proto.set_cls(input_name.cls);
        yolox_reg_to_cls_list_proto->Add(std::move(yolox_input_name_proto));
    }
}

void serialize_yolov5seg_op_metadata(hailort::net_flow::OpMetadata &op_metadata, ProtoOpMetadata *op_metadata_proto)
{
    hailort::net_flow::Yolov5SegOpMetadata* yolov5seg_op_metadata = static_cast<hailort::net_flow::Yolov5SegOpMetadata*>(&op_metadata);

    auto &yolov5_config = yolov5seg_op_metadata->yolov5_config();
    auto yolov5_config_proto = op_metadata_proto->mutable_yolov5_config();

    yolov5_config_proto->set_image_height(yolov5_config.image_height);
    yolov5_config_proto->set_image_width(yolov5_config.image_width);

    auto yolov5_config_anchors_list_proto = yolov5_config_proto->mutable_yolov5_anchors();
    for (auto &layer_anchors_pair : yolov5_config.anchors) {
        ProtoYolov5Anchors yolov5_anchors_proto;
        yolov5_anchors_proto.set_layer(layer_anchors_pair.first);
        auto yolov5_anchors_list_proto = yolov5_anchors_proto.mutable_anchors();
        for (auto &anchor : layer_anchors_pair.second) {
            yolov5_anchors_list_proto->Add(anchor);
        }
        yolov5_config_anchors_list_proto->Add(std::move(yolov5_anchors_proto));
    }

    auto &yolov5seg_config = yolov5seg_op_metadata->yolov5seg_config();
    auto yolov5seg_config_proto = op_metadata_proto->mutable_yolov5seg_config();

    yolov5seg_config_proto->set_mask_threshold(yolov5seg_config.mask_threshold);
    yolov5seg_config_proto->set_layer_name(yolov5seg_config.proto_layer_name);
    yolov5seg_config_proto->set_max_accumulated_mask_size(yolov5seg_config.max_accumulated_mask_size);
}

void serialize_op_matadata(hailort::net_flow::OpMetadata &op_metadata, ProtoOpMetadata *op_metadata_proto)
{
    op_metadata_proto->set_name(std::string(op_metadata.get_name()));
    op_metadata_proto->set_type(static_cast<uint32_t>(op_metadata.type()));

    // Init + set values for inputs_metadata
    auto &inputs_metadata = op_metadata.inputs_metadata();
    serialize_input_metadata(inputs_metadata, op_metadata_proto);

    // Init + set values for outputs_metadata
    auto &outputs_metadata = op_metadata.outputs_metadata();
    serialize_output_metadata(outputs_metadata, op_metadata_proto);

    if ((op_metadata.type() == net_flow::OperationType::YOLOX) | (op_metadata.type() == net_flow::OperationType::YOLOV5) |
        (op_metadata.type() == net_flow::OperationType::YOLOV8) | (op_metadata.type() == net_flow::OperationType::SSD) |
        (op_metadata.type() == net_flow::OperationType::YOLOV5SEG) | (op_metadata.type() == net_flow::OperationType::IOU)) {
        // NMS fields
        hailort::net_flow::NmsOpMetadata* nms_op_metadata = static_cast<hailort::net_flow::NmsOpMetadata*>(&op_metadata);
        auto &nms_config = nms_op_metadata->nms_config();
        auto nms_config_proto = op_metadata_proto->mutable_nms_post_process_config();
        nms_config_proto->set_nms_score_th(nms_config.nms_score_th);
        nms_config_proto->set_nms_iou_th(nms_config.nms_iou_th);
        nms_config_proto->set_max_proposals_total(nms_config.max_proposals_total);
        nms_config_proto->set_max_proposals_per_class(nms_config.max_proposals_per_class);
        nms_config_proto->set_number_of_classes(nms_config.number_of_classes);
        nms_config_proto->set_background_removal(nms_config.background_removal);
        nms_config_proto->set_background_removal_index(nms_config.background_removal_index);
        nms_config_proto->set_bbox_only(nms_config.bbox_only);
    }

    switch (op_metadata.type()) {
    case net_flow::OperationType::YOLOV5: {
        serialize_yolov5_op_metadata(op_metadata, op_metadata_proto);
        break;
    }
    case net_flow::OperationType::SSD: {
        serialize_ssd_op_metadata(op_metadata, op_metadata_proto);
        break;
    }
    case net_flow::OperationType::YOLOV8: {
        serialize_yolov8_op_metadata(op_metadata, op_metadata_proto);
        break;
    }
    case net_flow::OperationType::YOLOX: {
        serialize_yolox_op_metadata(op_metadata, op_metadata_proto);
        break;
    }

    case net_flow::OperationType::YOLOV5SEG: {
        serialize_yolov5seg_op_metadata(op_metadata, op_metadata_proto);
        break;
    }
    default: {
        // IOU, SOFTMAX, ARGMAX - nothing to do, no additional members
    }
    }
}

void serialize_ops_metadata(std::vector<net_flow::PostProcessOpMetadataPtr> &ops_metadata, ProtoOpsMetadata *ops_metadata_proto)
{
    auto ops_metadata_list_proto = ops_metadata_proto->mutable_ops_metadata();
    for (auto& op_metadata : ops_metadata) {
        ProtoOpMetadata op_metadata_proto;
        serialize_op_matadata(*op_metadata, &op_metadata_proto);
        ops_metadata_list_proto->Add(std::move(op_metadata_proto));
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
    auto expected_vstream_infos = net_group_manager.execute<Expected<std::vector<hailo_vstream_info_t>>>(
        request->identifier().network_group_handle(), lambda, request->network_name());
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
    auto expected_vstream_infos = net_group_manager.execute<Expected<std::vector<hailo_vstream_info_t>>>(
        request->identifier().network_group_handle(), lambda, request->network_name());
    CHECK_EXPECTED_AS_RPC_STATUS(expected_vstream_infos, reply);

    serialize_vstream_infos(reply, expected_vstream_infos.value());
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_get_all_vstream_infos(grpc::ServerContext*,
    const ConfiguredNetworkGroup_get_vstream_infos_Request *request,
    ConfiguredNetworkGroup_get_vstream_infos_Reply *reply)
{
    auto expected_vstream_infos = get_all_vstream_infos(request->identifier().network_group_handle());
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
    auto is_scheduled = manager.execute<Expected<bool>>(request->identifier().network_group_handle(), lambda);
    CHECK_EXPECTED_AS_RPC_STATUS(is_scheduled, reply);

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
    auto status = net_group_manager.execute(request->identifier().network_group_handle(), lambda,
        static_cast<std::chrono::milliseconds>(request->timeout_ms()), request->network_name());
    CHECK_SUCCESS_AS_RPC_STATUS(status, reply);

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
    auto status = net_group_manager.execute(request->identifier().network_group_handle(), lambda,
        request->threshold(), request->network_name());
    CHECK_SUCCESS_AS_RPC_STATUS(status, reply);

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
    auto status = net_group_manager.execute(request->identifier().network_group_handle(), lambda,
        static_cast<uint8_t>(request->priority()), request->network_name());
    CHECK_SUCCESS_AS_RPC_STATUS(status, reply);

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
    auto expected_params = net_group_manager.execute<Expected<ConfigureNetworkParams>>(request->identifier().network_group_handle(), lambda);
    CHECK_EXPECTED_AS_RPC_STATUS(expected_params, reply);

    auto net_configure_params = expected_params.release();
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
    auto network_group_handle = request->identifier().network_group_handle();
    auto client_pid = request->pid();

    update_client_id_timestamp(client_pid);
    auto &vdevice_manager = ServiceResourceManager<VDevice>::get_instance();
    vdevice_manager.dup_handle(request->identifier().vdevice_handle(), client_pid);

    auto &net_group_manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    net_group_manager.dup_handle(network_group_handle, client_pid);

    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng, const std::map<std::string, hailo_vstream_params_t> &inputs_params) {
        return cng->create_input_vstreams(inputs_params);
    };
    auto vstreams_expected = net_group_manager.execute<Expected<std::vector<InputVStream>>>(network_group_handle, lambda, inputs_params);
    CHECK_EXPECTED_AS_RPC_STATUS(vstreams_expected, reply);
    auto vstreams = vstreams_expected.release();

    auto &vstreams_manager = ServiceResourceManager<InputVStream>::get_instance();
    for (size_t i = 0; i < vstreams.size(); i++) {
        reply->add_names(vstreams[i].name());
        auto handle = vstreams_manager.register_resource(client_pid, make_shared_nothrow<InputVStream>(std::move(vstreams[i])));
        reply->add_handles(handle);
    }

    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::InputVStream_release(grpc::ServerContext *, const Release_Request *request,
    Release_Reply *reply)
{
    auto vstream_handle = request->vstream_identifier().vstream_handle();
    auto &manager = ServiceResourceManager<InputVStream>::get_instance();
    manager.release_resource(vstream_handle, request->pid());
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

    auto network_group_handle = request->identifier().network_group_handle();
    auto client_pid = request->pid();

    update_client_id_timestamp(client_pid);
    auto &vdevice_manager = ServiceResourceManager<VDevice>::get_instance();
    vdevice_manager.dup_handle(request->identifier().vdevice_handle(), client_pid);

    auto &net_group_manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    net_group_manager.dup_handle(network_group_handle, client_pid);

    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng, const std::map<std::string, hailo_vstream_params_t> &output_params) {
        return cng->create_output_vstreams(output_params);
    };
    auto vstreams_expected = net_group_manager.execute<Expected<std::vector<OutputVStream>>>(network_group_handle, lambda, output_params);
    CHECK_EXPECTED_AS_RPC_STATUS(vstreams_expected, reply);
    auto vstreams = vstreams_expected.release();

    // The network_group's buffer pool is used for the read's buffers.
    auto &cng_buffer_pool_manager = ServiceResourceManager<ServiceNetworkGroupBufferPool>::get_instance();
    auto &vstream_manager = ServiceResourceManager<OutputVStream>::get_instance();
    for (size_t i = 0; i < vstreams.size(); i++) {
        auto allocate_lambda = [&](std::shared_ptr<ServiceNetworkGroupBufferPool> cng_buffer_pool) {
            return cng_buffer_pool->allocate_pool(vstreams[i].name(), HAILO_DMA_BUFFER_DIRECTION_D2H,
                vstreams[i].get_frame_size(), output_params.at(vstreams[i].name()).queue_size);
        };
        CHECK_SUCCESS_AS_RPC_STATUS(cng_buffer_pool_manager.execute(network_group_handle, allocate_lambda), reply);
        reply->add_names(vstreams[i].name());
        auto handle = vstream_manager.register_resource(client_pid, make_shared_nothrow<OutputVStream>(std::move(vstreams[i])));
        reply->add_handles(handle);
    }

    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::OutputVStream_release(grpc::ServerContext *, const Release_Request *request,
    Release_Reply *reply)
{
    auto vstream_handle = request->vstream_identifier().vstream_handle();
    auto &manager = ServiceResourceManager<OutputVStream>::get_instance();
    manager.release_resource(vstream_handle, request->pid());
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
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
    auto network_group_name = manager.execute<Expected<std::string>>(request->identifier().network_group_handle(), lambda);
    CHECK_EXPECTED_AS_RPC_STATUS(network_group_name, reply);

    reply->set_network_group_name(network_group_name.release());
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::InputVStream_is_multi_planar(grpc::ServerContext*, const InputVStream_is_multi_planar_Request *request,
        InputVStream_is_multi_planar_Reply *reply)
{
    auto lambda = [](std::shared_ptr<InputVStream> input_vstream) {
        return input_vstream->is_multi_planar();
    };
    auto &manager = ServiceResourceManager<InputVStream>::get_instance();
    auto multi_planar = manager.execute<Expected<bool>>(request->identifier().vstream_handle(), lambda);
    CHECK_EXPECTED_AS_RPC_STATUS(multi_planar, reply);

    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    reply->set_is_multi_planar(multi_planar.release());
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::InputVStream_write(grpc::ServerContext*, const InputVStream_write_Request *request,
        InputVStream_write_Reply *reply)
{
    MemoryView mem_view = MemoryView::create_const(reinterpret_cast<const uint8_t*>(request->data().c_str()),
        request->data().size());
    auto lambda = [](std::shared_ptr<InputVStream> input_vstream, const MemoryView &buffer) {
        return input_vstream->write(std::move(buffer));
    };
    auto &manager = ServiceResourceManager<InputVStream>::get_instance();
    auto status = manager.execute(request->identifier().vstream_handle(), lambda, mem_view);

    if (HAILO_STREAM_ABORT == status) {
        LOGGER__INFO("User aborted VStream write.");
        reply->set_status(static_cast<uint32_t>(HAILO_STREAM_ABORT));
        return grpc::Status::OK;
    }
    CHECK_SUCCESS_AS_RPC_STATUS(status,  reply, "VStream write failed");
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::InputVStream_write_pix(grpc::ServerContext*, const InputVStream_write_pix_Request *request,
    InputVStream_write_pix_Reply *reply)
{
    hailo_pix_buffer_t pix_buffer = {};
    pix_buffer.index = request->index();
    pix_buffer.number_of_planes = request->number_of_planes();
    pix_buffer.memory_type = HAILO_PIX_BUFFER_MEMORY_TYPE_USERPTR;  // Service does not support other memory types
    std::vector<std::vector<uint8_t>> data_arrays;
    data_arrays.reserve(pix_buffer.number_of_planes);
    for (uint32_t i =0; i < pix_buffer.number_of_planes; i++) {
        data_arrays.push_back(std::vector<uint8_t>(request->planes_data(i).begin(), request->planes_data(i).end()));
        pix_buffer.planes[i].user_ptr = data_arrays[i].data();
        pix_buffer.planes[i].bytes_used = static_cast<uint32_t>(data_arrays[i].size());
    }

    auto lambda = [](std::shared_ptr<InputVStream> input_vstream, const hailo_pix_buffer_t &buffer) {
        return input_vstream->write(std::move(buffer));
    };
    auto &manager = ServiceResourceManager<InputVStream>::get_instance();
    auto status = manager.execute(request->identifier().vstream_handle(), lambda, pix_buffer);

    if (HAILO_STREAM_ABORT == status) {
        LOGGER__INFO("User aborted VStream write.");
        reply->set_status(static_cast<uint32_t>(HAILO_STREAM_ABORT));
        return grpc::Status::OK;
    }
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
    auto expected_network_infos = manager.execute<Expected<std::vector<hailo_network_info_t>>>(request->identifier().network_group_handle(),
        lambda);
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
    auto ng_handle = request->identifier().network_group_handle();
    auto vstream_name = output_vstream_name(request->identifier().vstream_handle());
    CHECK_EXPECTED_AS_RPC_STATUS(vstream_name, reply);

    auto buffer_exp = acquire_buffer_from_cng_pool(ng_handle, vstream_name.value());
    CHECK_EXPECTED_AS_RPC_STATUS(buffer_exp, reply);
    auto buffer = buffer_exp.value();

    auto lambda = [](std::shared_ptr<OutputVStream> output_vstream, MemoryView &buffer) {
        return output_vstream->read(std::move(buffer));
    };
    auto &manager = ServiceResourceManager<OutputVStream>::get_instance();
    auto status = manager.execute(request->identifier().vstream_handle(), lambda, MemoryView(buffer->data(), buffer->size()));

    if (HAILO_STREAM_ABORT == status) {
        LOGGER__INFO("User aborted VStream read.");
        reply->set_status(static_cast<uint32_t>(HAILO_STREAM_ABORT));
        return grpc::Status::OK;
    }
    CHECK_SUCCESS_AS_RPC_STATUS(status,  reply, "VStream read failed");

    if (buffer->size() > MAX_GRPC_BUFFER_SIZE) {
        LOGGER__ERROR("Response buffer size is too big: {}. Max response size is: {}", buffer->size(), MAX_GRPC_BUFFER_SIZE);
        reply->set_status(static_cast<uint32_t>(HAILO_RPC_FAILED));
        return grpc::Status::OK;
    }

    reply->set_data(buffer->data(), buffer->size());

    status = return_buffer_to_cng_pool(ng_handle, vstream_name.value(), buffer);
    CHECK_SUCCESS_AS_RPC_STATUS(status, reply);

    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

Expected<std::vector<hailo_stream_info_t>> HailoRtRpcService::get_all_stream_infos(uint32_t ng_handle)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng) {
        return cng->get_all_stream_infos();
    };
    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    TRY(auto stream_infos,
        manager.execute<Expected<std::vector<hailo_stream_info_t>>>(ng_handle, lambda));

    return stream_infos;
}

Expected<std::vector<hailo_vstream_info_t>> HailoRtRpcService::get_all_vstream_infos(uint32_t ng_handle)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng) {
        return cng->get_all_vstream_infos();
    };
    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    TRY(auto vstream_infos,
        manager.execute<Expected<std::vector<hailo_vstream_info_t>>>(ng_handle, lambda));

    return vstream_infos;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_get_all_stream_infos(grpc::ServerContext*,
    const ConfiguredNetworkGroup_get_all_stream_infos_Request *request,
    ConfiguredNetworkGroup_get_all_stream_infos_Reply *reply)
{
    auto expected_stream_infos = get_all_stream_infos(request->identifier().network_group_handle());
    CHECK_EXPECTED_AS_RPC_STATUS(expected_stream_infos, reply);

    auto proto_stream_infos = reply->mutable_stream_infos();
    for (const auto &stream_info : expected_stream_infos.value()) {
        ProtoStreamInfo proto_stream_info;
        if (stream_info.format.order == HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP) {
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
    auto expected_latency_result = manager.execute<Expected<LatencyMeasurementResult>>(
        request->identifier().network_group_handle(), lambda, request->network_name());
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
    auto is_multi_context = manager.execute<Expected<bool>>(request->identifier().network_group_handle(), lambda);
    CHECK_EXPECTED_AS_RPC_STATUS(is_multi_context, reply);

    reply->set_is_multi_context(static_cast<bool>(is_multi_context.release()));
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
    auto sorted_output_names_expected = manager.execute<Expected<std::vector<std::string>>>(request->identifier().network_group_handle(),
        lambda);
    CHECK_EXPECTED_AS_RPC_STATUS(sorted_output_names_expected, reply);

    auto sorted_output_names_proto = reply->mutable_sorted_output_names();
    for (auto &name : sorted_output_names_expected.value()) {
        sorted_output_names_proto->Add(std::move(name));
    }
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

Expected<size_t> HailoRtRpcService::infer_queue_size(uint32_t ng_handle)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng) {
        return cng->infer_queue_size();
    };
    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    TRY(auto queue_size, manager.execute<Expected<size_t>>(ng_handle, lambda));

    return queue_size;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_infer_queue_size(grpc::ServerContext*,
    const ConfiguredNetworkGroup_infer_queue_size_Request *request,
    ConfiguredNetworkGroup_infer_queue_size_Reply *reply)
{
    auto queue_size_expected = infer_queue_size(request->identifier().network_group_handle());
    CHECK_EXPECTED_AS_RPC_STATUS(queue_size_expected, reply);

    reply->set_infer_queue_size(static_cast<uint32_t>(queue_size_expected.release()));
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_get_layer_info(grpc::ServerContext*,
    const ConfiguredNetworkGroup_get_layer_info_Request *request,
    ConfiguredNetworkGroup_get_layer_info_Reply *reply)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng, const std::string &stream_name) {
        return cng->get_layer_info(stream_name);
    };
    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto layer_info_expected = manager.execute<Expected<std::unique_ptr<LayerInfo>>>(
        request->identifier().network_group_handle(), lambda, request->stream_name());
    CHECK_EXPECTED_AS_RPC_STATUS(layer_info_expected, reply);

    auto layer_info = layer_info_expected.release();
    auto info_proto = reply->mutable_layer_info();
    serialize_layer_info(*layer_info, info_proto);
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_get_ops_metadata(grpc::ServerContext*,
    const ConfiguredNetworkGroup_get_ops_metadata_Request *request,
    ConfiguredNetworkGroup_get_ops_metadata_Reply *reply)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng) {
        return cng->get_ops_metadata();
    };
    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto ops_metadata_expected = manager.execute<Expected<std::vector<net_flow::PostProcessOpMetadataPtr>>>(
        request->identifier().network_group_handle(), lambda);
    CHECK_EXPECTED_AS_RPC_STATUS(ops_metadata_expected, reply);

    auto ops_metadata = ops_metadata_expected.release();
    auto ops_metadata_proto = reply->mutable_ops_metadata();
    serialize_ops_metadata(ops_metadata, ops_metadata_proto);
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_set_nms_score_threshold(grpc::ServerContext*,
    const ConfiguredNetworkGroup_set_nms_score_threshold_Request *request,
    ConfiguredNetworkGroup_set_nms_score_threshold_Reply *reply)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng, const std::string &edge_name, float32_t nms_score_threshold) {
        return cng->set_nms_score_threshold(edge_name, nms_score_threshold);
    };
    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto status = manager.execute(request->identifier().network_group_handle(), lambda,
                                    request->edge_name(), request->nms_score_th());
    CHECK_SUCCESS_AS_RPC_STATUS(status, reply);

    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_set_nms_iou_threshold(grpc::ServerContext*,
    const ConfiguredNetworkGroup_set_nms_iou_threshold_Request *request,
    ConfiguredNetworkGroup_set_nms_iou_threshold_Reply *reply)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng, const std::string &edge_name, float32_t iou_threshold) {
        return cng->set_nms_iou_threshold(edge_name, iou_threshold);
    };
    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto status = manager.execute(request->identifier().network_group_handle(), lambda,
                                    request->edge_name(), request->nms_iou_th());
    CHECK_SUCCESS_AS_RPC_STATUS(status, reply);

    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_set_nms_max_bboxes_per_class(grpc::ServerContext*,
    const ConfiguredNetworkGroup_set_nms_max_bboxes_per_class_Request *request,
    ConfiguredNetworkGroup_set_nms_max_bboxes_per_class_Reply *reply)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng, const std::string &edge_name, uint32_t max_bboxes) {
        return cng->set_nms_max_bboxes_per_class(edge_name, max_bboxes);
    };
    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto status = manager.execute(request->identifier().network_group_handle(), lambda,
                                    request->edge_name(), request->nms_max_bboxes_per_class());
    CHECK_SUCCESS_AS_RPC_STATUS(status, reply);

    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_set_nms_max_bboxes_total(grpc::ServerContext*,
    const ConfiguredNetworkGroup_set_nms_max_bboxes_total_Request *request,
    ConfiguredNetworkGroup_set_nms_max_bboxes_total_Reply *reply)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng, const std::string &edge_name, uint32_t max_bboxes) {
        return cng->set_nms_max_bboxes_total(edge_name, max_bboxes);
    };
    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto status = manager.execute(request->identifier().network_group_handle(), lambda,
                                    request->edge_name(), request->nms_max_bboxes_total());
    CHECK_SUCCESS_AS_RPC_STATUS(status, reply);
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::ConfiguredNetworkGroup_set_nms_max_accumulated_mask_size(grpc::ServerContext*,
    const ConfiguredNetworkGroup_set_nms_max_accumulated_mask_size_Request *request,
    ConfiguredNetworkGroup_set_nms_max_accumulated_mask_size_Reply *reply)
{
    auto lambda = [](std::shared_ptr<ConfiguredNetworkGroup> cng, const std::string &edge_name, uint32_t max_accumulated_mask_size) {
        return cng->set_nms_max_accumulated_mask_size(edge_name, max_accumulated_mask_size);
    };
    auto &manager = ServiceResourceManager<ConfiguredNetworkGroup>::get_instance();
    auto status = manager.execute(request->identifier().network_group_handle(), lambda,
        request->edge_name(), request->max_accumulated_mask_size());
    CHECK_SUCCESS_AS_RPC_STATUS(status, reply);

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
    auto streams_names_expected = manager.execute<Expected<std::vector<std::string>>>(
        request->identifier().network_group_handle(), lambda, request->vstream_name());
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
    auto vstreams_names_expected = manager.execute<Expected<std::vector<std::string>>>(
        request->identifier().network_group_handle(), lambda, request->stream_name());
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
    auto frame_size = manager.execute<Expected<size_t>>(request->identifier().vstream_handle(), lambda);
    CHECK_EXPECTED_AS_RPC_STATUS(frame_size, reply);

    reply->set_frame_size(static_cast<uint32_t>(frame_size.release()));
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::OutputVStream_get_frame_size(grpc::ServerContext*, const VStream_get_frame_size_Request *request,
    VStream_get_frame_size_Reply *reply)
{
    auto frame_size = output_vstream_frame_size(request->identifier().vstream_handle());
    CHECK_EXPECTED_AS_RPC_STATUS(frame_size, reply);

    reply->set_frame_size(static_cast<uint32_t>(frame_size.release()));
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::InputVStream_flush(grpc::ServerContext*, const InputVStream_flush_Request *request,
    InputVStream_flush_Reply *reply)
{
    auto status = flush_input_vstream(request->identifier().vstream_handle());
    reply->set_status(status);
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::InputVStream_name(grpc::ServerContext*, const VStream_name_Request *request,
    VStream_name_Reply *reply)
{
    auto lambda = [](std::shared_ptr<InputVStream> input_vstream) {
        return input_vstream->name();
    };
    auto &manager = ServiceResourceManager<InputVStream>::get_instance();
    auto name = manager.execute<Expected<std::string>>(request->identifier().vstream_handle(), lambda);
    CHECK_EXPECTED_AS_RPC_STATUS(name, reply);


    reply->set_name(name.release());
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

Expected<std::string> HailoRtRpcService::output_vstream_name(uint32_t vstream_handle)
{
    auto lambda = [](std::shared_ptr<OutputVStream> output_vstream) {
        return output_vstream->name();
    };
    auto &manager = ServiceResourceManager<OutputVStream>::get_instance();
    TRY(auto name, manager.execute<Expected<std::string>>(vstream_handle, lambda));

    return name;
}

Expected<size_t> HailoRtRpcService::output_vstream_frame_size(uint32_t vstream_handle)
{
    auto lambda = [](std::shared_ptr<OutputVStream> output_vstream) {
        return output_vstream->get_frame_size();
    };
    auto &manager = ServiceResourceManager<OutputVStream>::get_instance();
    TRY(auto frame_size, manager.execute<Expected<size_t>>(vstream_handle, lambda));

    return frame_size;
}

grpc::Status HailoRtRpcService::OutputVStream_name(grpc::ServerContext*, const VStream_name_Request *request,
    VStream_name_Reply *reply)
{
    auto name = output_vstream_name(request->identifier().vstream_handle());
    CHECK_EXPECTED_AS_RPC_STATUS(name, reply);

    reply->set_name(name.release());
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
    auto name = manager.execute<Expected<std::string>>(request->identifier().vstream_handle(), lambda);
    CHECK_EXPECTED_AS_RPC_STATUS(name, reply);

    reply->set_network_name(name.release());
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
    auto name = manager.execute<Expected<std::string>>(request->identifier().vstream_handle(), lambda);
    CHECK_EXPECTED_AS_RPC_STATUS(name, reply);

    reply->set_network_name(name.release());
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::InputVStream_abort(grpc::ServerContext*, const VStream_abort_Request *request,
    VStream_abort_Reply *reply)
{
    auto status = abort_input_vstream(request->identifier().vstream_handle());
    reply->set_status(status);
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::OutputVStream_abort(grpc::ServerContext*, const VStream_abort_Request *request,
    VStream_abort_Reply *reply)
{
    auto status = abort_output_vstream(request->identifier().vstream_handle());
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
    auto status = manager.execute(request->identifier().vstream_handle(), lambda);
    CHECK_SUCCESS_AS_RPC_STATUS(status, reply);

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
    auto status = manager.execute(request->identifier().vstream_handle(), lambda);
    CHECK_SUCCESS_AS_RPC_STATUS(status, reply);

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
    auto status = manager.execute(request->identifier().vstream_handle(), lambda);
    CHECK_SUCCESS_AS_RPC_STATUS(status, reply);

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
    auto status = manager.execute(request->identifier().vstream_handle(), lambda);
    CHECK_SUCCESS_AS_RPC_STATUS(status, reply);

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
    auto status = manager.execute(request->identifier().vstream_handle(), lambda);
    CHECK_SUCCESS_AS_RPC_STATUS(status, reply);

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
    auto status = manager.execute(request->identifier().vstream_handle(), lambda);
    CHECK_SUCCESS_AS_RPC_STATUS(status, reply);

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
    auto format_exp = manager.execute<Expected<hailo_format_t>>(request->identifier().vstream_handle(), lambda);
    CHECK_EXPECTED_AS_RPC_STATUS(format_exp, reply);

    auto format = format_exp.release();
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
    auto format_exp = manager.execute<Expected<hailo_format_t>>(request->identifier().vstream_handle(), lambda);
    CHECK_EXPECTED_AS_RPC_STATUS(format_exp, reply);

    auto format = format_exp.release();
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
    auto info_exp = manager.execute<Expected<hailo_vstream_info_t>>(request->identifier().vstream_handle(), lambda);
    CHECK_EXPECTED_AS_RPC_STATUS(info_exp, reply);

    auto info = info_exp.release();
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
    auto info_exp = manager.execute<Expected<hailo_vstream_info_t>>(request->identifier().vstream_handle(), lambda);
    CHECK_EXPECTED_AS_RPC_STATUS(info_exp, reply);

    auto info = info_exp.release();
    auto info_proto = reply->mutable_vstream_info();
    serialize_vstream_info(info, info_proto);
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::OutputVStream_is_aborted(grpc::ServerContext*, const VStream_is_aborted_Request *request,
    VStream_is_aborted_Reply *reply)
{
    auto lambda = [](std::shared_ptr<OutputVStream> output_vstream) {
        return output_vstream->is_aborted();
    };
    auto &manager = ServiceResourceManager<OutputVStream>::get_instance();
    auto is_aborted = manager.execute<Expected<bool>>(request->identifier().vstream_handle(), lambda);
    CHECK_EXPECTED_AS_RPC_STATUS(is_aborted, reply);

    reply->set_is_aborted(is_aborted.release());
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::InputVStream_is_aborted(grpc::ServerContext*, const VStream_is_aborted_Request *request,
    VStream_is_aborted_Reply *reply)
{
    auto lambda = [](std::shared_ptr<InputVStream> input_vstream) {
        return input_vstream->is_aborted();
    };
    auto &manager = ServiceResourceManager<InputVStream>::get_instance();
    auto is_aborted = manager.execute<Expected<bool>>(request->identifier().vstream_handle(), lambda);
    CHECK_EXPECTED_AS_RPC_STATUS(is_aborted, reply);

    reply->set_is_aborted(is_aborted.release());
    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::OutputVStream_set_nms_score_threshold(grpc::ServerContext*, const VStream_set_nms_score_threshold_Request *request,
    VStream_set_nms_score_threshold_Reply *reply)
{
    auto lambda = [](std::shared_ptr<OutputVStream> output_vstream, float32_t threshold) {
        return output_vstream->set_nms_score_threshold(threshold);
    };
    auto &manager = ServiceResourceManager<OutputVStream>::get_instance();
    auto status = manager.execute(request->identifier().vstream_handle(), lambda, static_cast<float32_t>(request->threshold()));
    CHECK_SUCCESS_AS_RPC_STATUS(status, reply, "set_nms_score_threshold failed");

    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::OutputVStream_set_nms_iou_threshold(grpc::ServerContext*, const VStream_set_nms_iou_threshold_Request *request,
    VStream_set_nms_iou_threshold_Reply *reply)
{
    auto lambda = [](std::shared_ptr<OutputVStream> output_vstream, float32_t threshold) {
        return output_vstream->set_nms_iou_threshold(threshold);
    };
    auto &manager = ServiceResourceManager<OutputVStream>::get_instance();
    auto status = manager.execute(request->identifier().vstream_handle(), lambda, static_cast<float32_t>(request->threshold()));
    CHECK_SUCCESS_AS_RPC_STATUS(status, reply, "set_nms_iou_threshold failed");

    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

hailo_status HailoRtRpcService::update_buffer_size_in_pool(uint32_t vstream_handle, uint32_t network_group_handle)
{
    TRY(const auto vstream_name, output_vstream_name(vstream_handle));
    TRY(const auto frame_size, output_vstream_frame_size(vstream_handle));

    auto &cng_buffer_pool_manager = ServiceResourceManager<ServiceNetworkGroupBufferPool>::get_instance();
    auto allocate_lambda = [&](std::shared_ptr<ServiceNetworkGroupBufferPool> cng_buffer_pool) {
        return cng_buffer_pool->reallocate_pool(vstream_name, HAILO_DMA_BUFFER_DIRECTION_D2H, frame_size);
    };
    CHECK_SUCCESS(cng_buffer_pool_manager.execute(network_group_handle, allocate_lambda));

    return HAILO_SUCCESS;
}

grpc::Status HailoRtRpcService::OutputVStream_set_nms_max_proposals_per_class(grpc::ServerContext*, const VStream_set_nms_max_proposals_per_class_Request *request,
    VStream_set_nms_max_proposals_per_class_Reply *reply)
{
    auto lambda = [](std::shared_ptr<OutputVStream> output_vstream, uint32_t max_proposals_per_class) {
        return output_vstream->set_nms_max_proposals_per_class(max_proposals_per_class);
    };
    auto &manager = ServiceResourceManager<OutputVStream>::get_instance();
    auto status = manager.execute(request->identifier().vstream_handle(), lambda, static_cast<uint32_t>(request->max_proposals_per_class()));
    CHECK_SUCCESS_AS_RPC_STATUS(status,  reply, "set_nms_max_proposals_per_class failed");

    status = update_buffer_size_in_pool(request->identifier().vstream_handle(), request->identifier().network_group_handle());
    CHECK_SUCCESS_AS_RPC_STATUS(status, reply, "Updating buffer size in pool failed");

    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

grpc::Status HailoRtRpcService::OutputVStream_set_nms_max_accumulated_mask_size(grpc::ServerContext*,
    const VStream_set_nms_max_accumulated_mask_size_Request *request, VStream_set_nms_max_accumulated_mask_size_Reply *reply)
{
    auto lambda = [](std::shared_ptr<OutputVStream> output_vstream, uint32_t max_accumulated_mask_size) {
        return output_vstream->set_nms_max_accumulated_mask_size(max_accumulated_mask_size);
    };
    auto &manager = ServiceResourceManager<OutputVStream>::get_instance();
    auto status = manager.execute(request->identifier().vstream_handle(), lambda, static_cast<uint32_t>(request->max_accumulated_mask_size()));
    CHECK_SUCCESS_AS_RPC_STATUS(status,  reply, "set_nms_max_accumulated_mask_size failed");

    status = update_buffer_size_in_pool(request->identifier().vstream_handle(), request->identifier().network_group_handle());
    CHECK_SUCCESS_AS_RPC_STATUS(status, reply, "Updating buffer size in pool failed");

    reply->set_status(static_cast<uint32_t>(HAILO_SUCCESS));
    return grpc::Status::OK;
}

}

