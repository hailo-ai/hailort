/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailo_server.cpp
 * @brief Hailo Server
 **/

#include "hailort_server.hpp"
#include "hailo/hailort.h"
#include "hrpc/server.hpp"
#include "hrpc/server_resource_manager.hpp"
#include "hailo/vdevice.hpp"
#include "hrpc_protocol/serializer.hpp"
#include "net_flow/ops/nms_post_process.hpp"
#include "common/thread_safe_queue.hpp"
#include "hrpc/connection_context.hpp"
#include "vdma/pcie_session.hpp"

using namespace hailort;

#define ASYNC_QUEUE_SIZE_FACTOR (2) // double buffer

#define REGISTER_ACTION(_dispatcher, action_id, handler) \
    _dispatcher.register_action(HailoRpcActionID::action_id, \
        [this] (const MemoryView &_1, ClientConnectionPtr _2, ResponseWriter _3) { return handler(_1, _2, _3); });

struct InferModelInfo
{
    std::unordered_map<std::string, size_t> input_streams_sizes;
    std::unordered_map<std::string, size_t> output_streams_sizes;
    std::vector<std::string> inputs_names;
    std::vector<std::string> outputs_names;
};

void HailoRTServer::cleanup_infer_model_infos(const std::vector<uint32_t> &infer_model_handles)
{
    for (const auto &infer_model_handle : infer_model_handles) {
        auto info_id_iter = m_infer_model_to_info_id.find(infer_model_handle);
        if (m_infer_model_to_info_id.end() != info_id_iter) {
            m_infer_model_to_info_id.erase(infer_model_handle);
        }
    }
}

void HailoRTServer::cleanup_cim_buffer_pools(const std::vector<uint32_t> &cim_handles)
{
    for (const auto &cim_handle : cim_handles) {
        m_buffer_pool_per_cim.erase(cim_handle);
        m_run_async_info_per_cim.erase(cim_handle);
    }
}

hailo_status HailoRTServer::cleanup_client_resources(ClientConnectionPtr client_connection)
{
    std::set<uint32_t> ids = { client_connection->client_id() };
    auto cim_handles = ServerResourceManager<ConfiguredInferModel>::get_instance().resources_handles_by_ids(ids);
    (void)ServerResourceManager<ConfiguredInferModel>::get_instance().release_by_id(client_connection->client_id());
    cleanup_cim_buffer_pools(cim_handles);

    auto infer_model_handles = ServerResourceManager<InferModel>::get_instance().resources_handles_by_ids(ids);
    (void)ServerResourceManager<InferModelInfo>::get_instance().release_by_id(client_connection->client_id());
    (void)ServerResourceManager<InferModel>::get_instance().release_by_id(client_connection->client_id());
    cleanup_infer_model_infos(infer_model_handles);

    (void)ServerResourceManager<VDevice>::get_instance().release_by_id(client_connection->client_id());

    return HAILO_SUCCESS;
}

Expected<std::unique_ptr<HailoRTServer>> HailoRTServer::create_unique(const std::string& ip)
{
    TRY(auto connection_context, ConnectionContext::create_server_shared(ip));

    auto write_mutex = make_shared_nothrow<std::mutex>();
    CHECK_NOT_NULL(write_mutex, HAILO_OUT_OF_HOST_MEMORY);

    bool is_unix_socket = (ip == SERVER_ADDR_USE_UNIX_SOCKET);
    auto res = make_unique_nothrow<HailoRTServer>(connection_context, write_mutex, is_unix_socket);
    CHECK_NOT_NULL(res, HAILO_OUT_OF_HOST_MEMORY);
    return res;
}

hailo_status HailoRTServer::handle_vdevice_create(const MemoryView &request, ClientConnectionPtr client_connection, ResponseWriter response_writer)
{
    TRY(auto tuple, CreateVDeviceSerializer::deserialize_request(request));
    auto &vdevice_params = std::get<0>(tuple);
    bool should_disable_pp_ops = std::get<1>(tuple);

    auto res = OsUtils::set_environment_variable(HAILO_DISABLE_PP_ENV_VAR, should_disable_pp_ops ? "1" : "0");
    CHECK(0 == res, HAILO_INTERNAL_FAILURE, "Failed to set env var {} to {}",
        HAILO_DISABLE_PP_ENV_VAR, should_disable_pp_ops);

    auto vdevice_expected = VDevice::create(vdevice_params.get());
    auto vdevice_status = vdevice_expected.status();
    if (HAILO_OUT_OF_PHYSICAL_DEVICES == vdevice_status) {
        // This is a small hack to have the same behavior and return code as the standard VDevice
        vdevice_status = HAILO_DEVICE_IN_USE;
    }
    CHECK_SUCCESS(vdevice_status);

    auto &manager = ServerResourceManager<VDevice>::get_instance();
    auto id = manager.register_resource(client_connection->client_id(), vdevice_expected.release());

    TRY(auto reply, CreateVDeviceSerializer::serialize_reply(id));
    return response_writer.write(HAILO_SUCCESS, std::move(reply));
}

hailo_status HailoRTServer::handle_vdevice_destroy(const MemoryView &request, ClientConnectionPtr client_connection, ResponseWriter response_writer)
{
    auto &manager = ServerResourceManager<VDevice>::get_instance();
    TRY(auto vdevice_handle, DestroyVDeviceSerializer::deserialize_request(request));
    (void)manager.release_resource(vdevice_handle, client_connection->client_id());

    TRY(auto reply, DestroyVDeviceSerializer::serialize_reply());
    return response_writer.write(HAILO_SUCCESS, std::move(reply));
}

hailo_status HailoRTServer::handle_vdevice_create_infer_model(const MemoryView &request, ClientConnectionPtr client_connection, ResponseWriter response_writer)
{
    TRY(auto tuple, CreateInferModelSerializer::deserialize_request(request));
    auto vdevice_handle = std::get<0>(tuple);
    uint64_t hef_size = std::get<1>(tuple);
    auto name = std::get<2>(tuple);

    assert(hef_size <= SIZE_MAX);
    TRY(auto hef_buffer, Buffer::create_shared(static_cast<size_t>(hef_size), BufferStorageParams::create_dma()));

    auto status = client_connection->read_buffer(hef_buffer->as_view());
    CHECK_SUCCESS(status);

    auto &vdevice_manager = ServerResourceManager<VDevice>::get_instance();
    auto lambda = [hef_buffer, &name] (std::shared_ptr<VDevice> vdevice) {
        return vdevice->create_infer_model(hef_buffer, name);
    };
    TRY(auto infer_model,
        vdevice_manager.execute<Expected<std::shared_ptr<InferModel>>>(vdevice_handle, lambda));

    auto &infer_model_manager = ServerResourceManager<InferModel>::get_instance();
    auto infer_model_id = infer_model_manager.register_resource(client_connection->client_id(), std::move(infer_model));

    TRY(auto reply, CreateInferModelSerializer::serialize_reply(infer_model_id));
    return response_writer.write(HAILO_SUCCESS, std::move(reply));
}

hailo_status HailoRTServer::handle_infer_model_destroy(const MemoryView &request, ClientConnectionPtr client_connection, ResponseWriter response_writer)
{
    auto &manager = ServerResourceManager<InferModel>::get_instance();
    TRY(auto infer_model_handle, DestroyInferModelSerializer::deserialize_request(request));
    (void)manager.release_resource(infer_model_handle, client_connection->client_id());

    TRY(auto reply, DestroyInferModelSerializer::serialize_reply());
    return response_writer.write(HAILO_SUCCESS, std::move(reply));
}

hailo_status HailoRTServer::handle_infer_model_create_configured_infer_model(const MemoryView &request, ClientConnectionPtr client_connection, ResponseWriter response_writer)
{
    auto &infer_model_manager = ServerResourceManager<InferModel>::get_instance();

    TRY(auto request_params, CreateConfiguredInferModelSerializer::deserialize_request(request));
    const auto &infer_model_handle = request_params.infer_model_handle;
    const auto &vdevice_handle = request_params.vdevice_handle;

    auto lambda = [&request_params] (std::shared_ptr<InferModel> infer_model) -> Expected<ConfiguredInferModel> {
        const auto &input_streams_formats = request_params.input_streams_params;
        const auto &output_streams_formats = request_params.output_streams_params;
        for (const auto &input_stream_format : input_streams_formats) {
            TRY(auto input, infer_model->input(input_stream_format.first));

            input.set_format_order(static_cast<hailo_format_order_t>(input_stream_format.second.format_order));
            input.set_format_type(static_cast<hailo_format_type_t>(input_stream_format.second.format_type));
            if (INVALID_NMS_CONFIG != input_stream_format.second.nms_score_threshold) {
                input.set_nms_score_threshold(input_stream_format.second.nms_score_threshold);
            }
            if (INVALID_NMS_CONFIG != input_stream_format.second.nms_iou_threshold) {
                input.set_nms_iou_threshold(input_stream_format.second.nms_iou_threshold);
            }
            if (static_cast<uint32_t>(INVALID_NMS_CONFIG) != input_stream_format.second.nms_max_proposals_per_class) {
                input.set_nms_max_proposals_per_class(input_stream_format.second.nms_max_proposals_per_class);
            }
            if (static_cast<uint32_t>(INVALID_NMS_CONFIG) != input_stream_format.second.nms_max_proposals_total) {
                input.set_nms_max_proposals_total(input_stream_format.second.nms_max_proposals_total);
            }
            if (static_cast<uint32_t>(INVALID_NMS_CONFIG) != input_stream_format.second.nms_max_accumulated_mask_size) {
                input.set_nms_max_accumulated_mask_size(input_stream_format.second.nms_max_accumulated_mask_size);
            }
        }

        for (const auto &output_stream_format : output_streams_formats) {
            TRY(auto output, infer_model->output(output_stream_format.first));
            output.set_format_order(static_cast<hailo_format_order_t>(output_stream_format.second.format_order));
            output.set_format_type(static_cast<hailo_format_type_t>(output_stream_format.second.format_type));
            if (INVALID_NMS_CONFIG != output_stream_format.second.nms_score_threshold) {
                output.set_nms_score_threshold(output_stream_format.second.nms_score_threshold);
            }
            if (INVALID_NMS_CONFIG != output_stream_format.second.nms_iou_threshold) {
                output.set_nms_iou_threshold(output_stream_format.second.nms_iou_threshold);
            }
            if (static_cast<uint32_t>(INVALID_NMS_CONFIG) != output_stream_format.second.nms_max_proposals_per_class) {
                output.set_nms_max_proposals_per_class(output_stream_format.second.nms_max_proposals_per_class);
            }
            if (static_cast<uint32_t>(INVALID_NMS_CONFIG) != output_stream_format.second.nms_max_proposals_total) {
                output.set_nms_max_proposals_total(output_stream_format.second.nms_max_proposals_total);
            }
            if (static_cast<uint32_t>(INVALID_NMS_CONFIG) != output_stream_format.second.nms_max_accumulated_mask_size) {
                output.set_nms_max_accumulated_mask_size(output_stream_format.second.nms_max_accumulated_mask_size);
            }
        }

        infer_model->set_batch_size(request_params.batch_size);
        infer_model->set_power_mode(request_params.power_mode);
        infer_model->set_hw_latency_measurement_flags(request_params.latency_flag);

        return infer_model->configure();
    };

    TRY(auto configured_infer_model,
        infer_model_manager.execute<Expected<ConfiguredInferModel>>(infer_model_handle, lambda));

    TRY(auto async_queue_size, configured_infer_model.get_async_queue_size());
    auto set_model_info_lambda = [] (std::shared_ptr<InferModel> infer_model) -> Expected<std::shared_ptr<InferModelInfo>> {
        auto infer_model_info = make_shared_nothrow<InferModelInfo>();
        CHECK_NOT_NULL_AS_EXPECTED(infer_model_info, HAILO_OUT_OF_HOST_MEMORY);

        for (const auto &input : infer_model->inputs()) {
            infer_model_info->input_streams_sizes.emplace(input.name(), input.get_frame_size());
            infer_model_info->inputs_names.push_back(input.name());
        }
        for (const auto &output : infer_model->outputs()) {
            infer_model_info->output_streams_sizes.emplace(output.name(), output.get_frame_size());
            infer_model_info->outputs_names.push_back(output.name());
        }
        return infer_model_info;
    };
    TRY(auto model_info,
        infer_model_manager.execute<Expected<std::shared_ptr<InferModelInfo>>>(infer_model_handle, set_model_info_lambda));

    auto &infer_model_infos_manager = ServerResourceManager<InferModelInfo>::get_instance();
    auto infer_model_info_id = infer_model_infos_manager.register_resource(client_connection->client_id(), std::move(model_info));

    auto &cim_manager = ServerResourceManager<ConfiguredInferModel>::get_instance();
    auto cim_id = cim_manager.register_resource(client_connection->client_id(),
        std::move(make_shared_nothrow<ConfiguredInferModel>(configured_infer_model)));

    TRY(auto buffer_pool, ServerNetworkGroupBufferPool::create(vdevice_handle));

    auto get_infer_model_info_lambda = [] (std::shared_ptr<InferModelInfo> infer_model_info) {
        return *infer_model_info;
    };
    TRY(auto infer_model_info,
        infer_model_infos_manager.execute<Expected<InferModelInfo>>(infer_model_info_id, get_infer_model_info_lambda));

    for (const auto &input_name : infer_model_info.inputs_names) {
        auto status = buffer_pool->allocate_pool(input_name, HAILO_DMA_BUFFER_DIRECTION_D2H,
            infer_model_info.input_streams_sizes[input_name], async_queue_size * ASYNC_QUEUE_SIZE_FACTOR);
        CHECK_SUCCESS(status);
    }
    for (const auto &output_name : infer_model_info.outputs_names) {
        auto status = buffer_pool->allocate_pool(output_name, HAILO_DMA_BUFFER_DIRECTION_H2D,
            infer_model_info.output_streams_sizes[output_name], async_queue_size * ASYNC_QUEUE_SIZE_FACTOR);
        CHECK_SUCCESS(status);
    }
    m_buffer_pool_per_cim.emplace(cim_id, buffer_pool);

    auto bindings_lambda = [] (std::shared_ptr<ConfiguredInferModel> configured_infer_model) {
        return configured_infer_model->create_bindings();
    };
    TRY(auto run_async_info_pool, ObjectPool<RunAsyncInfo>::create_shared(
        async_queue_size * ASYNC_QUEUE_SIZE_FACTOR, [&cim_manager, cim_id, &bindings_lambda, &infer_model_info] () -> Expected<RunAsyncInfo> {
            RunAsyncInfo run_async_info;
            TRY(run_async_info.bindings,
                cim_manager.execute<Expected<ConfiguredInferModel::Bindings>>(cim_id, bindings_lambda));
            run_async_info.buffer_inputs.reserve(infer_model_info.inputs_names.size());
            run_async_info.fd_inputs.reserve(infer_model_info.inputs_names.size());
            run_async_info.buffer_outputs.reserve(infer_model_info.outputs_names.size());
            run_async_info.fd_outputs.reserve(infer_model_info.outputs_names.size());
            return run_async_info;
        }));
    m_run_async_info_per_cim.emplace(cim_id, run_async_info_pool);

    m_infer_model_to_info_id[infer_model_handle] = infer_model_info_id;

    TRY(auto reply, CreateConfiguredInferModelSerializer::serialize_reply(cim_id, static_cast<uint32_t>(async_queue_size)));
    return response_writer.write(HAILO_SUCCESS, std::move(reply));
}

hailo_status HailoRTServer::handle_configured_infer_model_destroy(const MemoryView &request, ClientConnectionPtr client_connection, ResponseWriter response_writer)
{
    auto &manager = ServerResourceManager<ConfiguredInferModel>::get_instance();
    TRY(auto configured_infer_model_handle, DestroyConfiguredInferModelSerializer::deserialize_request(request));

    auto shutdown_lambda = [] (std::shared_ptr<ConfiguredInferModel> configured_infer_model) {
        configured_infer_model->shutdown();
        return HAILO_SUCCESS;
    };
    manager.execute<hailo_status>(configured_infer_model_handle, shutdown_lambda);
    cleanup_cim_buffer_pools({ configured_infer_model_handle });
    (void)manager.release_resource(configured_infer_model_handle, client_connection->client_id());

    TRY(auto reply, DestroyConfiguredInferModelSerializer::serialize_reply());
    return response_writer.write(HAILO_SUCCESS, std::move(reply));
}

hailo_status HailoRTServer::handle_configured_infer_model_set_scheduler_timeout(const MemoryView &request, ClientConnectionPtr, ResponseWriter response_writer)
{
    auto &cim_manager = ServerResourceManager<ConfiguredInferModel>::get_instance();
    TRY(auto tuple, SetSchedulerTimeoutSerializer::deserialize_request(request));
    const auto &configured_infer_model_handle = std::get<0>(tuple);
    const auto &timeout = std::get<1>(tuple);
    auto lambda = [timeout] (std::shared_ptr<ConfiguredInferModel> configured_infer_model) {
        return configured_infer_model->set_scheduler_timeout(timeout);
    };
    auto status = cim_manager.execute<hailo_status>(configured_infer_model_handle, lambda);
    CHECK_SUCCESS(status);

    TRY(auto reply, SetSchedulerTimeoutSerializer::serialize_reply());
    return response_writer.write(HAILO_SUCCESS, std::move(reply));
}

hailo_status HailoRTServer::handle_configured_infer_model_set_scheduler_threshold(const MemoryView &request, ClientConnectionPtr, ResponseWriter response_writer)
{
    auto &cim_manager = ServerResourceManager<ConfiguredInferModel>::get_instance();
    TRY(auto tuple, SetSchedulerThresholdSerializer::deserialize_request(request));
    const auto &configured_infer_model_handle = std::get<0>(tuple);
    const auto &threshold = std::get<1>(tuple);
    auto lambda = [threshold] (std::shared_ptr<ConfiguredInferModel> configured_infer_model) {
        return configured_infer_model->set_scheduler_threshold(threshold);
    };
    auto status = cim_manager.execute<hailo_status>(configured_infer_model_handle, lambda);
    CHECK_SUCCESS(status);

    TRY(auto reply, SetSchedulerThresholdSerializer::serialize_reply());
    return response_writer.write(HAILO_SUCCESS, std::move(reply));
}

hailo_status HailoRTServer::handle_configured_infer_model_set_scheduler_priority(const MemoryView &request, ClientConnectionPtr, ResponseWriter response_writer)
{
    auto &cim_manager = ServerResourceManager<ConfiguredInferModel>::get_instance();
    TRY(auto tuple, SetSchedulerPrioritySerializer::deserialize_request(request));
    const auto &configured_infer_model_handle = std::get<0>(tuple);
    const auto &priority = std::get<1>(tuple);
    auto lambda = [priority] (std::shared_ptr<ConfiguredInferModel> configured_infer_model) {
        return configured_infer_model->set_scheduler_priority(static_cast<uint8_t>(priority));
    };
    auto status = cim_manager.execute<hailo_status>(configured_infer_model_handle, lambda);
    CHECK_SUCCESS(status);

    TRY(auto reply, SetSchedulerPrioritySerializer::serialize_reply());
    return response_writer.write(HAILO_SUCCESS, std::move(reply));
}

hailo_status HailoRTServer::handle_configured_infer_model_get_hw_latency_measurement(const MemoryView &request, ClientConnectionPtr, ResponseWriter response_writer)
{
    auto &cim_manager = ServerResourceManager<ConfiguredInferModel>::get_instance();
    TRY(auto configured_infer_model_handle, GetHwLatencyMeasurementSerializer::deserialize_request(request));
    auto lambda = [] (std::shared_ptr<ConfiguredInferModel> configured_infer_model) {
        return configured_infer_model->get_hw_latency_measurement();
    };
    TRY_WITH_ACCEPTABLE_STATUS(HAILO_NOT_AVAILABLE, auto latency_measurement,
        cim_manager.execute<Expected<LatencyMeasurementResult>>(configured_infer_model_handle, lambda));

    uint32_t avg_hw_latency = static_cast<uint32_t>(latency_measurement.avg_hw_latency.count());

    TRY(auto reply, GetHwLatencyMeasurementSerializer::serialize_reply(avg_hw_latency));
    return response_writer.write(HAILO_SUCCESS, std::move(reply));

}

hailo_status HailoRTServer::handle_configured_infer_model_activate(const MemoryView &request, ClientConnectionPtr, ResponseWriter response_writer)
{
    auto &cim_manager = ServerResourceManager<ConfiguredInferModel>::get_instance();
    TRY(auto configured_infer_model_handle, ActivateSerializer::deserialize_request(request));
    auto lambda = [] (std::shared_ptr<ConfiguredInferModel> configured_infer_model) {
        return configured_infer_model->activate();
    };
    auto status = cim_manager.execute<hailo_status>(configured_infer_model_handle, lambda);
    CHECK_SUCCESS(status);

    TRY(auto reply, ActivateSerializer::serialize_reply());
    return response_writer.write(HAILO_SUCCESS, std::move(reply));
}

hailo_status HailoRTServer::handle_configured_infer_model_deactivate(const MemoryView &request, ClientConnectionPtr, ResponseWriter response_writer)
{
    auto &cim_manager = ServerResourceManager<ConfiguredInferModel>::get_instance();
    TRY(auto configured_infer_model_handle, DeactivateSerializer::deserialize_request(request));
    auto lambda = [] (std::shared_ptr<ConfiguredInferModel> configured_infer_model) {
        return configured_infer_model->deactivate();
    };
    auto status = cim_manager.execute<hailo_status>(configured_infer_model_handle, lambda);
    CHECK_SUCCESS(status);

    TRY(auto reply, DeactivateSerializer::serialize_reply());
    return response_writer.write(HAILO_SUCCESS, std::move(reply));
}

hailo_status HailoRTServer::handle_configured_infer_model_shutdown(const MemoryView &request, ClientConnectionPtr, ResponseWriter response_writer)
{
    auto &cim_manager = ServerResourceManager<ConfiguredInferModel>::get_instance();
    TRY(auto configured_infer_model_handle, ShutdownSerializer::deserialize_request(request));
    auto lambda = [] (std::shared_ptr<ConfiguredInferModel> configured_infer_model) {
        return configured_infer_model->shutdown();
    };
    auto status = cim_manager.execute<hailo_status>(configured_infer_model_handle, lambda);
    CHECK_SUCCESS(status);

    TRY(auto reply, ShutdownSerializer::serialize_reply());
    return response_writer.write(HAILO_SUCCESS, std::move(reply));
}

hailo_status HailoRTServer::handle_configured_infer_model_run_async(const MemoryView &request, ClientConnectionPtr client_connection, ResponseWriter response_writer)
{
    TRY(auto request_struct, RunAsyncSerializer::deserialize_request(request));
    auto configured_infer_model_handle = request_struct.configured_infer_model_handle;
    auto infer_model_handle = request_struct.infer_model_handle;

    TRY(auto run_async_info, m_run_async_info_per_cim.at(configured_infer_model_handle)->acquire());

    auto infer_model_info_lambda = [] (std::shared_ptr<InferModelInfo> infer_model_info) {
        return *infer_model_info;
    };
    auto &infer_model_infos_manager = ServerResourceManager<InferModelInfo>::get_instance();
    TRY(auto infer_model_info,
        infer_model_infos_manager.execute<Expected<InferModelInfo>>(m_infer_model_to_info_id[infer_model_handle], infer_model_info_lambda));

    uint32_t buffer_index = 0;

    for (const auto &input_name : infer_model_info.inputs_names) {
        TRY(auto input, run_async_info->bindings.input(input_name));

        if (BufferType::DMA_BUFFER == static_cast<BufferType>(request_struct.buffer_infos[buffer_index].type) && m_is_unix_socket) {
#ifdef __linux__
            auto stream_size = infer_model_info.input_streams_sizes[input_name];

            if (stream_size == request_struct.buffer_infos[buffer_index].size) {
                TRY(auto fd, client_connection->read_dmabuf_fd());
                input.set_dma_buffer({*fd, request_struct.buffer_infos[buffer_index++].size});
                run_async_info->fd_inputs.emplace_back(fd);
            } else {
                hailo_pix_buffer_t pix_buffer;
                pix_buffer.index = 0;
                pix_buffer.memory_type = HAILO_PIX_BUFFER_MEMORY_TYPE_DMABUF;

                uint32_t read_size = 0;
                uint32_t plane_index = 0;
                while (read_size < stream_size) {
                    uint32_t current_size = request_struct.buffer_infos[buffer_index++].size;
                    CHECK(read_size + current_size <= stream_size, HAILO_INTERNAL_FAILURE);

                    TRY(auto fd, client_connection->read_dmabuf_fd());
                    pix_buffer.planes[plane_index].fd = *fd;
                    pix_buffer.planes[plane_index].bytes_used = current_size;
                    pix_buffer.planes[plane_index].plane_size = current_size;
                    
                    run_async_info->fd_inputs.emplace_back(fd);

                    read_size += current_size;
                    plane_index++;
                }

                pix_buffer.number_of_planes = plane_index;
                input.set_pix_buffer(pix_buffer);
            }
#else
            LOGGER__ERROR("DMA buffer is not supported on this platform");
            return make_unexpected(HAILO_NOT_SUPPORTED);
#endif
        } else {
            TRY(auto buffer, m_buffer_pool_per_cim.at(configured_infer_model_handle)->acquire_buffer(input_name));

            uint32_t read_size = 0;
            while (read_size < buffer->size()) {
                uint32_t current_size = request_struct.buffer_infos[buffer_index++].size;
                CHECK(read_size + current_size <= buffer->size(), HAILO_INTERNAL_FAILURE);

                auto status = client_connection->read_buffer(MemoryView(buffer->data() + read_size, current_size));
                CHECK_SUCCESS(status);

                read_size += current_size;
            }

            auto status = input.set_buffer(MemoryView(*buffer));
            run_async_info->buffer_inputs.push_back(std::move(buffer));
            CHECK_SUCCESS(status);
        }
    }

    for (const auto &output_name : infer_model_info.outputs_names) {
        TRY(auto output, run_async_info->bindings.output(output_name));

        if ((BufferType::DMA_BUFFER == static_cast<BufferType>(request_struct.buffer_infos[buffer_index].type)) && m_is_unix_socket) {
            TRY(auto fd, client_connection->read_dmabuf_fd());
            auto status = output.set_dma_buffer({*fd, request_struct.buffer_infos[buffer_index++].size});
            CHECK_SUCCESS(status);
            run_async_info->fd_outputs.emplace_back(fd);
        } else {
            TRY(auto buffer, m_buffer_pool_per_cim.at(configured_infer_model_handle)->acquire_buffer(output_name));

            auto status = output.set_buffer(MemoryView(buffer->data(), buffer->size()));
            CHECK_SUCCESS(status);

            run_async_info->buffer_outputs.push_back(std::move(buffer));
            buffer_index++;
        }
    }

    auto infer_done_callback = [run_async_info, response_writer]
        (const AsyncInferCompletionInfo &completion_info) mutable {
        // Need to clear buffers so they aren't retained when
        // `run_asyc_info` is returned to the pool.
        auto outputs = std::move(run_async_info->buffer_outputs);
        run_async_info->buffer_inputs.clear();
        run_async_info->buffer_outputs.clear();

        auto status = response_writer.write(completion_info.status, {}, std::move(outputs));
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to write async response to client with status: {}", status);
        }
    };

    auto infer_lambda = [run_async_info, infer_done_callback, response_writer]
        (std::shared_ptr<ConfiguredInferModel> configured_infer_model) {
        return configured_infer_model->run_async(run_async_info->bindings, infer_done_callback);
    };

    auto &cim_manager = ServerResourceManager<ConfiguredInferModel>::get_instance();
    TRY(auto job, cim_manager.execute<Expected<AsyncInferJob>>(configured_infer_model_handle, infer_lambda));
    job.detach();

    return HAILO_SUCCESS;
}

hailo_status HailoRTServer::handle_device_create(const MemoryView &request, ClientConnectionPtr client_connection, ResponseWriter response_writer)
{
    auto status = CreateDeviceSerializer::deserialize_request(request);
    CHECK_SUCCESS(status);

    TRY(auto device, Device::create());

    auto &manager = ServerResourceManager<Device>::get_instance();
    auto id = manager.register_resource(client_connection->client_id(), std::move(device));

    TRY(auto reply, CreateDeviceSerializer::serialize_reply(id));
    return response_writer.write(HAILO_SUCCESS, std::move(reply));
}

hailo_status HailoRTServer::handle_device_destroy(const MemoryView &request, ClientConnectionPtr client_connection, ResponseWriter response_writer)
{
    auto &manager = ServerResourceManager<Device>::get_instance();
    TRY(auto device_handle, DestroyDeviceSerializer::deserialize_request(request));
    (void)manager.release_resource(device_handle, client_connection->client_id());
    
    TRY(auto reply, DestroyDeviceSerializer::serialize_reply());
    return response_writer.write(HAILO_SUCCESS, std::move(reply));
}

hailo_status HailoRTServer::handle_device_identify(const MemoryView &request, ClientConnectionPtr, ResponseWriter response_writer)
{
    TRY(auto device_handle, IdentifyDeviceSerializer::deserialize_request(request));

    auto &manager = ServerResourceManager<Device>::get_instance();
    auto device_lambda = [] (std::shared_ptr<Device> device) {
        return device->identify();
    };
    TRY(auto identity, manager.execute<Expected<hailo_device_identity_t>>(device_handle, device_lambda));
    
    TRY(auto reply, IdentifyDeviceSerializer::serialize_reply(identity));
    return response_writer.write(HAILO_SUCCESS, std::move(reply));
}

hailo_status HailoRTServer::handle_device_extended_info(const MemoryView &request, ClientConnectionPtr, ResponseWriter response_writer)
{
    using Serializer = ExtendedDeviceInfoSerializer;

    TRY(auto device_handle, Serializer::deserialize_request(request));

    auto &manager = ServerResourceManager<Device>::get_instance();
    auto device_lambda = [] (std::shared_ptr<Device> device) {
        return device->get_extended_device_information();
    };
    TRY(auto extended_info, manager.execute<Expected<hailo_extended_device_information_t>>(device_handle, device_lambda));
    
    TRY(auto reply, Serializer::serialize_reply(extended_info));
    return response_writer.write(HAILO_SUCCESS, std::move(reply));
}

hailo_status HailoRTServer::handle_device_get_chip_temperature(const MemoryView &request, ClientConnectionPtr, ResponseWriter response_writer)
{
    using Serializer = GetChipTemperatureSerializer;

    TRY(auto device_handle, Serializer::deserialize_request(request));

    auto &manager = ServerResourceManager<Device>::get_instance();
    auto device_lambda = [] (std::shared_ptr<Device> device) {
        return device->get_chip_temperature();
    };

    TRY(auto info, manager.execute<Expected<hailo_chip_temperature_info_t>>(device_handle, device_lambda));
    
    TRY(auto reply, Serializer::serialize_reply(info));
    return response_writer.write(HAILO_SUCCESS, std::move(reply));
}

hailo_status HailoRTServer::handle_device_query_health_stats(const MemoryView &request, ClientConnectionPtr, ResponseWriter response_writer)
{
    using Serializer = QueryHealthStatsSerializer;

    TRY(auto device_handle, Serializer::deserialize_request(request));

    auto &manager = ServerResourceManager<Device>::get_instance();
    auto device_lambda = [] (std::shared_ptr<Device> device) {
        return device->query_health_stats();
    };

    TRY(auto info, manager.execute<Expected<hailo_health_stats_t>>(device_handle, device_lambda));
    
    TRY(auto reply, Serializer::serialize_reply(info));
    return response_writer.write(HAILO_SUCCESS, std::move(reply));
}

hailo_status HailoRTServer::handle_device_query_performance_stats(const MemoryView &request, ClientConnectionPtr, ResponseWriter response_writer)
{
    using Serializer = QueryPerformanceStatsSerializer;

    TRY(auto device_handle, Serializer::deserialize_request(request));

    auto &manager = ServerResourceManager<Device>::get_instance();
    auto device_lambda = [] (std::shared_ptr<Device> device) {
        return device->query_performance_stats();
    };

    TRY(auto info, manager.execute<Expected<hailo_performance_stats_t>>(device_handle, device_lambda));
    
    TRY(auto reply, Serializer::serialize_reply(info));
    return response_writer.write(HAILO_SUCCESS, std::move(reply));
}

hailo_status HailoRTServer::handle_device_power_measurement(const MemoryView &request, ClientConnectionPtr, ResponseWriter response_writer)
{
    using Serializer = PowerMeasurementSerializer;

    TRY(auto tuple, Serializer::deserialize_request(request));

    auto device_handle = std::get<0>(tuple);
    auto dvm = std::get<1>(tuple);
    auto power_measurement_type = std::get<2>(tuple);

    auto &manager = ServerResourceManager<Device>::get_instance();
    auto device_lambda = [dvm, power_measurement_type] (std::shared_ptr<Device> device) {
        return device->power_measurement(
            static_cast<hailo_dvm_options_t>(dvm),
            static_cast<hailo_power_measurement_types_t>(power_measurement_type));
    };

    TRY(auto info, manager.execute<Expected<float32_t>>(device_handle, device_lambda));
    
    TRY(auto reply, Serializer::serialize_reply(info));
    return response_writer.write(HAILO_SUCCESS, std::move(reply));
}

hailo_status HailoRTServer::handle_device_set_power_measurement(const MemoryView &request, ClientConnectionPtr, ResponseWriter response_writer)
{
    using Serializer = SetPowerMeasurementSerializer;

    TRY(auto tuple, Serializer::deserialize_request(request));

    auto device_handle = std::get<0>(tuple);
    auto dvm = std::get<1>(tuple);
    auto power_measurement_type = std::get<2>(tuple);

    auto &manager = ServerResourceManager<Device>::get_instance();
    auto device_lambda = [dvm, power_measurement_type] (std::shared_ptr<Device> device) {
        constexpr hailo_measurement_buffer_index_t not_used_buffer_index = HAILO_MEASUREMENT_BUFFER_INDEX_MAX_ENUM;
        return device->set_power_measurement(
            not_used_buffer_index, /* Relevant only for H8. Not used in H10 */
            static_cast<hailo_dvm_options_t>(dvm),
            static_cast<hailo_power_measurement_types_t>(power_measurement_type));
    };

    auto status = manager.execute<hailo_status>(device_handle, device_lambda);
    CHECK_SUCCESS(status);
    
    TRY(auto reply, Serializer::serialize_reply());
    return response_writer.write(HAILO_SUCCESS, std::move(reply));
}

hailo_status HailoRTServer::handle_device_start_power_measurement(const MemoryView &request, ClientConnectionPtr, ResponseWriter response_writer)
{
    using Serializer = SetPowerMeasurementSerializer;

    TRY(auto tuple, Serializer::deserialize_request(request));

    auto device_handle = std::get<0>(tuple);
    auto averaging_factor = std::get<1>(tuple);
    auto sampling_period = std::get<2>(tuple);

    auto &manager = ServerResourceManager<Device>::get_instance();
    auto device_lambda = [sampling_period, averaging_factor] (std::shared_ptr<Device> device) {
        return device->start_power_measurement(
            static_cast<hailo_averaging_factor_t>(averaging_factor),
            static_cast<hailo_sampling_period_t>(sampling_period));
    };

    auto status = manager.execute<hailo_status>(device_handle, device_lambda);
    CHECK_SUCCESS(status);
    
    TRY(auto reply, Serializer::serialize_reply());
    return response_writer.write(HAILO_SUCCESS, std::move(reply));
}

hailo_status HailoRTServer::handle_device_get_power_measurement(const MemoryView &request, ClientConnectionPtr, ResponseWriter response_writer)
{
    using Serializer = GetPowerMeasurementSerializer;

    TRY(auto tuple, Serializer::deserialize_request(request));

    auto device_handle = std::get<0>(tuple);
    auto should_clear = std::get<1>(tuple);

    auto &manager = ServerResourceManager<Device>::get_instance();
    auto device_lambda = [should_clear] (std::shared_ptr<Device> device) {
        constexpr hailo_measurement_buffer_index_t unused_buffer_index = HAILO_MEASUREMENT_BUFFER_INDEX_MAX_ENUM;
        return device->get_power_measurement(unused_buffer_index, should_clear);
    };

    TRY(auto info, manager.execute<Expected<hailo_power_measurement_data_t>>(device_handle, device_lambda));
    
    TRY(auto reply, Serializer::serialize_reply(info));
    return response_writer.write(HAILO_SUCCESS, std::move(reply));
}

hailo_status HailoRTServer::handle_device_stop_power_measurement(const MemoryView &request, ClientConnectionPtr, ResponseWriter response_writer)
{
    using Serializer = StopPowerMeasurementSerializer;

    TRY(auto device_handle, Serializer::deserialize_request(request));
    auto &manager = ServerResourceManager<Device>::get_instance();

    auto device_lambda = [] (std::shared_ptr<Device> device) {
        return device->stop_power_measurement();
    };

    auto status = manager.execute<hailo_status>(device_handle, device_lambda);
    CHECK_SUCCESS(status);
    
    TRY(auto reply, Serializer::serialize_reply());
    return response_writer.write(HAILO_SUCCESS, std::move(reply));
}

hailo_status HailoRTServer::handle_device_get_architecture(const MemoryView &request, ClientConnectionPtr, ResponseWriter response_writer)
{
    using Serializer = GetArchitectureSerializer;

    TRY(auto device_handle, Serializer::deserialize_request(request));
    auto &manager = ServerResourceManager<Device>::get_instance();

    auto device_lambda = [] (std::shared_ptr<Device> device) {
        return device->get_architecture();
    };

    TRY(auto info, manager.execute<Expected<hailo_device_architecture_t>>(device_handle, device_lambda));
    
    TRY(auto reply, Serializer::serialize_reply(info));
    return response_writer.write(HAILO_SUCCESS, std::move(reply));
}

hailo_status HailoRTServer::handle_device_set_notification_callback(const MemoryView &request, ClientConnectionPtr, ResponseWriter response_writer)
{
    using Serializer = SetNotificationCallbackSerializer;
    TRY(auto request_struct, Serializer::deserialize_request(request));
    auto notification_id = request_struct.notification_id;

    RpcCallback rpc_callback;
    rpc_callback.callback_id = request_struct.callback;
    rpc_callback.dispatcher_id = request_struct.dispatcher_id;
    rpc_callback.type = RpcCallbackType::DEVICE_NOTIFICATION;

    NotificationCallback notification_callback = [rpc_callback, response_writer]
        (Device&, const hailo_notification_t &notification, void*) mutable {

        RpcCallback callback_cpy = rpc_callback;
        callback_cpy.data.device_notification.notification = notification;

        auto reply = CallbackCalledSerializer::serialize_reply(std::move(callback_cpy));
        if (!reply) {
            LOGGER__ERROR("Failed to serialize notification callback");
            return;
        }

        auto status = response_writer.write(HAILO_SUCCESS, reply.release());
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to write notification message");
        }
    };

    auto device_lambda = [notification_callback, notification_id] (std::shared_ptr<Device> device) {
        return device->set_notification_callback(
            notification_callback, notification_id, nullptr);
    };

    auto &manager = ServerResourceManager<Device>::get_instance();
    auto status = manager.execute<hailo_status>(request_struct.device_handle, device_lambda);
    CHECK_SUCCESS(status);
    
    TRY(auto reply, Serializer::serialize_reply());
    return response_writer.write(HAILO_SUCCESS, std::move(reply));
}

hailo_status HailoRTServer::handle_device_remove_notification_callback(const MemoryView &request, ClientConnectionPtr, ResponseWriter response_writer)
{
    using Serializer = RemoveNotificationCallbackSerializer;
    TRY(auto tuple, Serializer::deserialize_request(request));
    auto device_handle = std::get<0>(tuple);
    auto notification_id = std::get<1>(tuple);
    auto &manager = ServerResourceManager<Device>::get_instance();
    auto device_lambda = [notification_id] (std::shared_ptr<Device> device) {
        return device->remove_notification_callback(notification_id);
    };
    auto status = manager.execute<hailo_status>(device_handle, device_lambda);
    CHECK_SUCCESS(status);
    
    TRY(auto reply, Serializer::serialize_reply());
    return response_writer.write(HAILO_SUCCESS, std::move(reply));
}

hailo_status HailoRTServer::handle_device_fetch_logs(const MemoryView &request, ClientConnectionPtr, ResponseWriter response_writer)
{
    using Serializer = FetchLogsSerializer;
    TRY(auto tuple, Serializer::deserialize_request(request));
    auto device_handle = std::get<0>(tuple);
    auto buffer_size = std::get<1>(tuple);
    auto log_type = std::get<2>(tuple);
    auto &manager = ServerResourceManager<Device>::get_instance();

    TRY(auto syslog_buffer, Buffer::create_shared(buffer_size));

    auto device_lambda = [mem_view = syslog_buffer->as_view(), log_type] (std::shared_ptr<Device> device) -> Expected<size_t> {
        return device->fetch_logs(mem_view, log_type);
    };

    TRY(auto log_size, manager.execute<Expected<size_t>>(device_handle, device_lambda));

    TRY(auto reply, Serializer::serialize_reply(static_cast<uint32_t>(log_size)));

    std::vector<BufferPtr> buffer_outputs = {};
    buffer_outputs.push_back(syslog_buffer);

    return response_writer.write(HAILO_SUCCESS, std::move(reply), std::move(buffer_outputs));
}

Expected<Dispatcher> HailoRTServer::create_dispatcher()
{
    Dispatcher dispatcher;
    REGISTER_ACTION(dispatcher, VDEVICE__CREATE, handle_vdevice_create);
    REGISTER_ACTION(dispatcher, VDEVICE__DESTROY, handle_vdevice_destroy);
    REGISTER_ACTION(dispatcher, VDEVICE__CREATE_INFER_MODEL, handle_vdevice_create_infer_model);
    REGISTER_ACTION(dispatcher, INFER_MODEL__DESTROY, handle_infer_model_destroy);
    REGISTER_ACTION(dispatcher, INFER_MODEL__CREATE_CONFIGURED_INFER_MODEL, handle_infer_model_create_configured_infer_model);
    REGISTER_ACTION(dispatcher, CONFIGURED_INFER_MODEL__DESTROY, handle_configured_infer_model_destroy);
    REGISTER_ACTION(dispatcher, CONFIGURED_INFER_MODEL__SET_SCHEDULER_TIMEOUT, handle_configured_infer_model_set_scheduler_timeout);
    REGISTER_ACTION(dispatcher, CONFIGURED_INFER_MODEL__SET_SCHEDULER_THRESHOLD, handle_configured_infer_model_set_scheduler_threshold);
    REGISTER_ACTION(dispatcher, CONFIGURED_INFER_MODEL__SET_SCHEDULER_PRIORITY, handle_configured_infer_model_set_scheduler_priority);
    REGISTER_ACTION(dispatcher, CONFIGURED_INFER_MODEL__GET_HW_LATENCY_MEASUREMENT, handle_configured_infer_model_get_hw_latency_measurement);
    REGISTER_ACTION(dispatcher, CONFIGURED_INFER_MODEL__ACTIVATE, handle_configured_infer_model_activate);
    REGISTER_ACTION(dispatcher, CONFIGURED_INFER_MODEL__DEACTIVATE, handle_configured_infer_model_deactivate);
    REGISTER_ACTION(dispatcher, CONFIGURED_INFER_MODEL__SHUTDOWN, handle_configured_infer_model_shutdown);
    REGISTER_ACTION(dispatcher, CONFIGURED_INFER_MODEL__RUN_ASYNC, handle_configured_infer_model_run_async);
    REGISTER_ACTION(dispatcher, DEVICE__CREATE, handle_device_create);
    REGISTER_ACTION(dispatcher, DEVICE__DESTROY, handle_device_destroy);
    REGISTER_ACTION(dispatcher, DEVICE__IDENTIFY, handle_device_identify);
    REGISTER_ACTION(dispatcher, DEVICE__EXTENDED_INFO, handle_device_extended_info);
    REGISTER_ACTION(dispatcher, DEVICE__GET_CHIP_TEMPERATURE, handle_device_get_chip_temperature);
    REGISTER_ACTION(dispatcher, DEVICE__POWER_MEASUREMENT, handle_device_power_measurement);
    REGISTER_ACTION(dispatcher, DEVICE__SET_POWER_MEASUREMENT, handle_device_set_power_measurement);
    REGISTER_ACTION(dispatcher, DEVICE__GET_POWER_MEASUREMENT, handle_device_get_power_measurement);
    REGISTER_ACTION(dispatcher, DEVICE__START_POWER_MEASUREMENT, handle_device_start_power_measurement);
    REGISTER_ACTION(dispatcher, DEVICE__STOP_POWER_MEASUREMENT, handle_device_stop_power_measurement);
    REGISTER_ACTION(dispatcher, DEVICE__QUERY_HEALTH_STATS, handle_device_query_health_stats);
    REGISTER_ACTION(dispatcher, DEVICE__QUERY_PERFORMANCE_STATS, handle_device_query_performance_stats);
    REGISTER_ACTION(dispatcher, DEVICE__GET_ARCHITECTURE, handle_device_get_architecture);
    REGISTER_ACTION(dispatcher, DEVICE__SET_NOTIFICATION_CALLBACK, handle_device_set_notification_callback);
    REGISTER_ACTION(dispatcher, DEVICE__REMOVE_NOTIFICATION_CALLBACK, handle_device_remove_notification_callback);
    REGISTER_ACTION(dispatcher, DEVICE__FETCH_LOGS, handle_device_fetch_logs);
    return dispatcher;
}
