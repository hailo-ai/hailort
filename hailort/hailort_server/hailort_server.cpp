/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file hailo_server.cpp
 * @brief Hailo Server
 **/

#include "hailort_server.hpp"
#include "hrpc/server.hpp"
#include "hailo/vdevice.hpp"
#include "hailo/infer_model.hpp"
#include "hrpc_protocol/serializer.hpp"
#include "net_flow/ops/nms_post_process.hpp"
#include "hailort_service/service_resource_manager.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

using namespace hailort;

// TODO: These macros should be merged with the grpc macros, also change them to TRY
#define CHECK_EXPECTED_AS_HRPC_STATUS(_exepcted, T) \
    do { \
        if (!_exepcted) { \
            LOGGER__ERROR("CHECK_EXPECTED_AS_HRPC_STATUS failed, status: {}", _exepcted.status()); \
            auto reply = T::serialize_reply(_exepcted.status()); \
            if (reply) return reply; \
            LOGGER__CRITICAL("Failed to create reply with status: {}", reply.status()); \
            return make_unexpected(HAILO_INTERNAL_FAILURE); \
        } \
    } while (0)
#define CHECK_SUCCESS_AS_HRPC_STATUS(_status, T) \
    do { \
        if (_status != HAILO_SUCCESS) { \
            LOGGER__ERROR("CHECK_SUCCESS_AS_HRPC_STATUS failed, status: {}", _status); \
            auto reply = T::serialize_reply(_status); \
            if (reply) return reply; \
            LOGGER__CRITICAL("Failed to create reply with status: {}", reply.status()); \
            return make_unexpected(HAILO_INTERNAL_FAILURE); \
        } \
    } while (0)

#define __HAILO_CONCAT(x, y) x ## y
#define _HAILO_CONCAT(x, y) __HAILO_CONCAT(x, y)

#define _TRY_AS_HRPC_STATUS(expected_var_name, var_decl, expr, ...) \
    auto expected_var_name = (expr); \
    CHECK_EXPECTED_AS_HRPC_STATUS(expected_var_name, __VA_ARGS__); \
    var_decl = expected_var_name.release()

#define TRY_AS_HRPC_STATUS(var_decl, expr, ...) _TRY_AS_HRPC_STATUS(_HAILO_CONCAT(__expected, __COUNTER__), var_decl, expr, __VA_ARGS__)

#ifdef NDEBUG
#define LOGGER_PATTERN ("[%n] [%^%l%$] %v")
#else
#define LOGGER_PATTERN ("[%Y-%m-%d %X.%e] [%P] [%t] [%n] [%^%l%$] [%s:%#] [%!] %v")
#endif
#define BUFFER_POOL_SIZE (10) // TODO: this may hurt performance, should be configurable

struct InferModelInfo
{
    std::unordered_map<std::string, size_t> input_streams_sizes;
    std::unordered_map<std::string, size_t> output_streams_sizes;
    std::vector<std::string> inputs_names;
    std::vector<std::string> outputs_names;
};

void init_logger(const std::string &name)
{
    auto console_sink = make_shared_nothrow<spdlog::sinks::stderr_color_sink_mt>();
    console_sink->set_level(spdlog::level::info);
    console_sink->set_pattern(LOGGER_PATTERN);
    spdlog::set_default_logger(make_shared_nothrow<spdlog::logger>(name, console_sink));
}

void hrpc::HailoRTServer::cleanup_infer_model_hef_buffers(const std::vector<uint32_t> &infer_model_handles)
{
    for (const auto &infer_model_handle : infer_model_handles) {
        auto hef_buffers_iter = m_hef_buffers_per_infer_model.find(infer_model_handle);
        if (m_hef_buffers_per_infer_model.end() != hef_buffers_iter) {
            m_hef_buffers_per_infer_model.erase(infer_model_handle);
        }
    }
}

void hrpc::HailoRTServer::cleanup_cim_buffer_pools(const std::vector<uint32_t> &cim_handles)
{
    for (const auto &cim_handle : cim_handles) {
        auto buffer_pool_iter = m_buffer_pool_per_cim.find(cim_handle);
        if (m_buffer_pool_per_cim.end() != buffer_pool_iter) {
            m_buffer_pool_per_cim.erase(cim_handle);
        }
    }
}

hailo_status hrpc::HailoRTServer::cleanup_client_resources(RpcConnection client_connection)
{
    std::set<uint32_t> pids = {SINGLE_CLIENT_PID};
    auto cim_handles = ServiceResourceManager<ConfiguredInferModel>::get_instance().resources_handles_by_pids(pids);
    (void)ServiceResourceManager<ConfiguredInferModel>::get_instance().release_by_pid(SINGLE_CLIENT_PID);
    cleanup_cim_buffer_pools(cim_handles);

    auto infer_model_handles = ServiceResourceManager<InferModel>::get_instance().resources_handles_by_pids(pids);
    (void)ServiceResourceManager<InferModelInfo>::get_instance().release_by_pid(SINGLE_CLIENT_PID);
    (void)ServiceResourceManager<InferModel>::get_instance().release_by_pid(SINGLE_CLIENT_PID);
    cleanup_infer_model_hef_buffers(infer_model_handles);
    m_infer_model_to_info_id.clear();

    (void)ServiceResourceManager<VDevice>::get_instance().release_by_pid(SINGLE_CLIENT_PID);
    CHECK_SUCCESS(client_connection.close());
    return HAILO_SUCCESS;
}

Expected<std::unique_ptr<hrpc::HailoRTServer>> hrpc::HailoRTServer::create_unique()
{
    TRY(auto connection_context, ConnectionContext::create_shared(true));
    auto res = make_unique_nothrow<HailoRTServer>(connection_context);
    CHECK_NOT_NULL(res, HAILO_OUT_OF_HOST_MEMORY);
    return res;
}

int main()
{
    init_logger("HailoRT-Server");
    TRY(auto server, hrpc::HailoRTServer::create_unique());
    hrpc::Dispatcher dispatcher;

    // TODO: add a server implementation class, with resources heiracrhy and more
    auto &infer_model_to_info_id = server->get_infer_model_to_info_id();
    auto &buffer_pool_per_cim = server->get_buffer_pool_per_cim();

    // Because the infer model is created with a hef buffer, we need to keep the buffer until the configure stage.
    // Here I keep it until the infer model is destroyed
    auto &hef_buffers = server->get_hef_buffers();

    dispatcher.register_action(HailoRpcActionID::VDEVICE__CREATE,
    [] (const MemoryView &request, hrpc::ServerContextPtr /*server_context*/) -> Expected<Buffer> {
        TRY_AS_HRPC_STATUS(auto vdevice_params, CreateVDeviceSerializer::deserialize_request(request), CreateVDeviceSerializer);
        TRY_AS_HRPC_STATUS(auto vdevice, VDevice::create(vdevice_params), CreateVDeviceSerializer);

        auto &manager = ServiceResourceManager<VDevice>::get_instance();
        auto id = manager.register_resource(SINGLE_CLIENT_PID, std::move(vdevice));
        auto reply = CreateVDeviceSerializer::serialize_reply(HAILO_SUCCESS, id);
        return reply;
    });
    dispatcher.register_action(HailoRpcActionID::VDEVICE__DESTROY,
    [] (const MemoryView &request, hrpc::ServerContextPtr /*server_context*/) -> Expected<Buffer> {
        auto &manager = ServiceResourceManager<VDevice>::get_instance();
        TRY_AS_HRPC_STATUS(auto vdevice_handle, DestroyVDeviceSerializer::deserialize_request(request), DestroyVDeviceSerializer);
        (void)manager.release_resource(vdevice_handle, SINGLE_CLIENT_PID);
        TRY_AS_HRPC_STATUS(auto reply, DestroyVDeviceSerializer::serialize_reply(HAILO_SUCCESS), DestroyVDeviceSerializer);
        return reply;
    });
    dispatcher.register_action(HailoRpcActionID::VDEVICE__CREATE_INFER_MODEL,
    [&hef_buffers] (const MemoryView &request, hrpc::ServerContextPtr server_context) -> Expected<Buffer> {
        TRY_AS_HRPC_STATUS(auto tuple, CreateInferModelSerializer::deserialize_request(request), CreateInferModelSerializer);
        auto vdevice_handle = std::get<0>(tuple);
        uint64_t hef_size = std::get<1>(tuple);

        assert(hef_size <= SIZE_MAX);
        TRY_AS_HRPC_STATUS(auto hef_buffer, Buffer::create(static_cast<size_t>(hef_size), BufferStorageParams::create_dma()), CreateInferModelSerializer);

        auto status = server_context->connection().read_buffer(MemoryView(hef_buffer));
        CHECK_SUCCESS_AS_HRPC_STATUS(status, CreateInferModelSerializer);

        auto &vdevice_manager = ServiceResourceManager<VDevice>::get_instance();
        auto lambda = [view = MemoryView(hef_buffer)] (std::shared_ptr<VDevice> vdevice) {
            return vdevice->create_infer_model(view);
        };
        auto infer_model = vdevice_manager.execute<Expected<std::shared_ptr<InferModel>>>(vdevice_handle, lambda);
        CHECK_EXPECTED_AS_HRPC_STATUS(infer_model, CreateInferModelSerializer);

        auto &infer_model_manager = ServiceResourceManager<InferModel>::get_instance();
        auto infer_model_id = infer_model_manager.register_resource(SINGLE_CLIENT_PID, std::move(infer_model.release()));
        hef_buffers.emplace(infer_model_id, std::move(hef_buffer));

        TRY_AS_HRPC_STATUS(auto reply, CreateInferModelSerializer::serialize_reply(HAILO_SUCCESS, infer_model_id), CreateInferModelSerializer);
        return reply;
    });
    dispatcher.register_action(HailoRpcActionID::INFER_MODEL__DESTROY,
    [&hef_buffers] (const MemoryView &request, hrpc::ServerContextPtr /*server_context*/) -> Expected<Buffer> {
        auto &manager = ServiceResourceManager<InferModel>::get_instance();
        TRY_AS_HRPC_STATUS(auto infer_model_handle, DestroyInferModelSerializer::deserialize_request(request), DestroyInferModelSerializer);
        hef_buffers.erase(infer_model_handle);
        (void)manager.release_resource(infer_model_handle, SINGLE_CLIENT_PID);
        TRY_AS_HRPC_STATUS(auto reply, DestroyInferModelSerializer::serialize_reply(HAILO_SUCCESS), DestroyInferModelSerializer);
        return reply;
    });
    dispatcher.register_action(HailoRpcActionID::INFER_MODEL__CREATE_CONFIGURED_INFER_MODEL,
    [&buffer_pool_per_cim, &infer_model_to_info_id]
    (const MemoryView &request, hrpc::ServerContextPtr /*server_context*/) -> Expected<Buffer> {
        auto &infer_model_manager = ServiceResourceManager<InferModel>::get_instance();

        TRY_AS_HRPC_STATUS(auto request_params, CreateConfiguredInferModelSerializer::deserialize_request(request), CreateConfiguredInferModelSerializer);
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
                if (static_cast<uint32_t>(INVALID_NMS_CONFIG) != output_stream_format.second.nms_max_accumulated_mask_size) {
                    output.set_nms_max_accumulated_mask_size(output_stream_format.second.nms_max_accumulated_mask_size);
                }
            }

            infer_model->set_batch_size(request_params.batch_size);
            infer_model->set_power_mode(request_params.power_mode);
            infer_model->set_hw_latency_measurement_flags(request_params.latency_flag);

            return infer_model->configure();
        };

        auto configured_infer_model = infer_model_manager.execute<Expected<ConfiguredInferModel>>(infer_model_handle, lambda);
        CHECK_EXPECTED_AS_HRPC_STATUS(configured_infer_model, CreateConfiguredInferModelSerializer);

        TRY_AS_HRPC_STATUS(auto async_queue_size, configured_infer_model->get_async_queue_size(), CreateConfiguredInferModelSerializer);
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
        auto model_info = infer_model_manager.execute<Expected<std::shared_ptr<InferModelInfo>>>(infer_model_handle, set_model_info_lambda);
        CHECK_EXPECTED_AS_HRPC_STATUS(model_info, CreateConfiguredInferModelSerializer);

        auto &infer_model_infos_manager = ServiceResourceManager<InferModelInfo>::get_instance();
        auto infer_model_info_id = infer_model_infos_manager.register_resource(SINGLE_CLIENT_PID, std::move(model_info.release()));

        auto &cim_manager = ServiceResourceManager<ConfiguredInferModel>::get_instance();
        auto cim_id = cim_manager.register_resource(SINGLE_CLIENT_PID,
            std::move(make_shared_nothrow<ConfiguredInferModel>(configured_infer_model.release())));

        auto buffer_pool = ServiceNetworkGroupBufferPool::create(vdevice_handle);
        CHECK_EXPECTED_AS_HRPC_STATUS(buffer_pool, CreateConfiguredInferModelSerializer);

        auto buffer_pool_ptr = buffer_pool.release();
        auto get_infer_model_info_lambda = [] (std::shared_ptr<InferModelInfo> infer_model_info) {
            return *infer_model_info;
        };
        auto infer_model_info = infer_model_infos_manager.execute<Expected<InferModelInfo>>(infer_model_info_id, get_infer_model_info_lambda);
        CHECK_EXPECTED_AS_HRPC_STATUS(infer_model_info, CreateConfiguredInferModelSerializer);

        for (const auto &input_name : infer_model_info->inputs_names) {
            auto status = buffer_pool_ptr->allocate_pool(input_name, HAILO_DMA_BUFFER_DIRECTION_D2H,
                infer_model_info->input_streams_sizes[input_name], BUFFER_POOL_SIZE);
            CHECK_SUCCESS_AS_HRPC_STATUS(status, CreateConfiguredInferModelSerializer);
        }
        for (const auto &output_name : infer_model_info->outputs_names) {
            auto status = buffer_pool_ptr->allocate_pool(output_name, HAILO_DMA_BUFFER_DIRECTION_H2D,
                infer_model_info->output_streams_sizes[output_name], BUFFER_POOL_SIZE);
            CHECK_SUCCESS_AS_HRPC_STATUS(status, CreateConfiguredInferModelSerializer);
        }
        buffer_pool_per_cim.emplace(cim_id, buffer_pool_ptr);

        infer_model_to_info_id[infer_model_handle] = infer_model_info_id;
        TRY_AS_HRPC_STATUS(auto reply,
            CreateConfiguredInferModelSerializer::serialize_reply(HAILO_SUCCESS, cim_id, static_cast<uint32_t>(async_queue_size)),
            CreateConfiguredInferModelSerializer);
        return reply;
    });
    dispatcher.register_action(HailoRpcActionID::CONFIGURED_INFER_MODEL__DESTROY,
    [&buffer_pool_per_cim] (const MemoryView &request, hrpc::ServerContextPtr /*server_context*/) -> Expected<Buffer> {
        auto &manager = ServiceResourceManager<ConfiguredInferModel>::get_instance();
        TRY_AS_HRPC_STATUS(auto configured_infer_model_handle, DestroyConfiguredInferModelSerializer::deserialize_request(request), DestroyInferModelSerializer);

        auto shutdown_lambda = [] (std::shared_ptr<ConfiguredInferModel> configured_infer_model) {
            configured_infer_model->shutdown();
            return HAILO_SUCCESS;
        };
        manager.execute<hailo_status>(configured_infer_model_handle, shutdown_lambda);
        buffer_pool_per_cim.erase(configured_infer_model_handle);
        (void)manager.release_resource(configured_infer_model_handle, SINGLE_CLIENT_PID);
        TRY_AS_HRPC_STATUS(auto reply, DestroyConfiguredInferModelSerializer::serialize_reply(HAILO_SUCCESS), DestroyInferModelSerializer);
        return reply;
    });
    dispatcher.register_action(HailoRpcActionID::CONFIGURED_INFER_MODEL__SET_SCHEDULER_TIMEOUT,
    [] (const MemoryView &request, hrpc::ServerContextPtr /*server_context*/) -> Expected<Buffer> {
        auto &cim_manager = ServiceResourceManager<ConfiguredInferModel>::get_instance();
        TRY_AS_HRPC_STATUS(auto tuple, SetSchedulerTimeoutSerializer::deserialize_request(request), SetSchedulerTimeoutSerializer);
        const auto &configured_infer_model_handle = std::get<0>(tuple);
        const auto &timeout = std::get<1>(tuple);
        auto lambda = [timeout] (std::shared_ptr<ConfiguredInferModel> configured_infer_model) {
            return configured_infer_model->set_scheduler_timeout(timeout);
        };
        auto status = cim_manager.execute<hailo_status>(configured_infer_model_handle, lambda);
        TRY_AS_HRPC_STATUS(auto reply, SetSchedulerTimeoutSerializer::serialize_reply(status), SetSchedulerTimeoutSerializer);

        return reply;
    });
    dispatcher.register_action(HailoRpcActionID::CONFIGURED_INFER_MODEL__SET_SCHEDULER_THRESHOLD,
    [] (const MemoryView &request, hrpc::ServerContextPtr /*server_context*/) -> Expected<Buffer> {
        auto &cim_manager = ServiceResourceManager<ConfiguredInferModel>::get_instance();
        TRY_AS_HRPC_STATUS(auto tuple, SetSchedulerThresholdSerializer::deserialize_request(request), SetSchedulerThresholdSerializer);
        const auto &configured_infer_model_handle = std::get<0>(tuple);
        const auto &threshold = std::get<1>(tuple);
        auto lambda = [threshold] (std::shared_ptr<ConfiguredInferModel> configured_infer_model) {
            return configured_infer_model->set_scheduler_threshold(threshold);
        };
        auto status = cim_manager.execute<hailo_status>(configured_infer_model_handle, lambda);
        TRY_AS_HRPC_STATUS(auto reply, SetSchedulerThresholdSerializer::serialize_reply(status), SetSchedulerThresholdSerializer);

        return reply;
    });
    dispatcher.register_action(HailoRpcActionID::CONFIGURED_INFER_MODEL__SET_SCHEDULER_PRIORITY,
    [] (const MemoryView &request, hrpc::ServerContextPtr /*server_context*/) -> Expected<Buffer> {
        auto &cim_manager = ServiceResourceManager<ConfiguredInferModel>::get_instance();
        TRY_AS_HRPC_STATUS(auto tuple, SetSchedulerPrioritySerializer::deserialize_request(request), SetSchedulerPrioritySerializer);
        const auto &configured_infer_model_handle = std::get<0>(tuple);
        const auto &priority = std::get<1>(tuple);
        auto lambda = [priority] (std::shared_ptr<ConfiguredInferModel> configured_infer_model) {
            return configured_infer_model->set_scheduler_priority(static_cast<uint8_t>(priority));
        };
        auto status = cim_manager.execute<hailo_status>(configured_infer_model_handle, lambda);
        TRY_AS_HRPC_STATUS(auto reply, SetSchedulerPrioritySerializer::serialize_reply(status), SetSchedulerPrioritySerializer);

        return reply;
    });
    dispatcher.register_action(HailoRpcActionID::CONFIGURED_INFER_MODEL__GET_HW_LATENCY_MEASUREMENT,
    [] (const MemoryView &request, hrpc::ServerContextPtr /*server_context*/) -> Expected<Buffer> {
        auto &cim_manager = ServiceResourceManager<ConfiguredInferModel>::get_instance();

        auto configured_infer_model_handle = GetHwLatencyMeasurementSerializer::deserialize_request(request);
        CHECK_EXPECTED_AS_HRPC_STATUS(configured_infer_model_handle, GetHwLatencyMeasurementSerializer);

        auto lambda = [] (std::shared_ptr<ConfiguredInferModel> configured_infer_model) {
            return configured_infer_model->get_hw_latency_measurement();
        };

        auto latency_measurement_result = cim_manager.execute<Expected<LatencyMeasurementResult>>(configured_infer_model_handle.value(), lambda);
        if (HAILO_NOT_AVAILABLE ==  latency_measurement_result.status()) {
            return GetHwLatencyMeasurementSerializer::serialize_reply(HAILO_NOT_AVAILABLE);
        }
        CHECK_EXPECTED_AS_HRPC_STATUS(latency_measurement_result, GetHwLatencyMeasurementSerializer);

        uint32_t avg_hw_latency = static_cast<uint32_t>(latency_measurement_result.value().avg_hw_latency.count());
        TRY_AS_HRPC_STATUS(auto reply, GetHwLatencyMeasurementSerializer::serialize_reply(latency_measurement_result.status(), avg_hw_latency), GetHwLatencyMeasurementSerializer);

        return reply;
    });
    dispatcher.register_action(HailoRpcActionID::CONFIGURED_INFER_MODEL__ACTIVATE,
    [] (const MemoryView &request, hrpc::ServerContextPtr /*server_context*/) -> Expected<Buffer> {
        auto &cim_manager = ServiceResourceManager<ConfiguredInferModel>::get_instance();

        auto configured_infer_model_handle = ActivateSerializer::deserialize_request(request);
        CHECK_EXPECTED_AS_HRPC_STATUS(configured_infer_model_handle, ActivateSerializer);

        auto lambda = [] (std::shared_ptr<ConfiguredInferModel> configured_infer_model) {
            return configured_infer_model->activate();
        };

        auto status = cim_manager.execute<hailo_status>(configured_infer_model_handle.value(), lambda);
        TRY_AS_HRPC_STATUS(auto reply, ActivateSerializer::serialize_reply(status), ActivateSerializer);

        return reply;
    });
    dispatcher.register_action(HailoRpcActionID::CONFIGURED_INFER_MODEL__DEACTIVATE,
    [] (const MemoryView &request, hrpc::ServerContextPtr /*server_context*/) -> Expected<Buffer> {
        auto &cim_manager = ServiceResourceManager<ConfiguredInferModel>::get_instance();

        auto configured_infer_model_handle = DeactivateSerializer::deserialize_request(request);
        CHECK_EXPECTED_AS_HRPC_STATUS(configured_infer_model_handle, DeactivateSerializer);

        auto lambda = [] (std::shared_ptr<ConfiguredInferModel> configured_infer_model) {
            return configured_infer_model->deactivate();
        };

        auto status = cim_manager.execute<hailo_status>(configured_infer_model_handle.value(), lambda);
        TRY_AS_HRPC_STATUS(auto reply, DeactivateSerializer::serialize_reply(status), DeactivateSerializer);

        return reply;
    });
    dispatcher.register_action(HailoRpcActionID::CONFIGURED_INFER_MODEL__SHUTDOWN,
    [] (const MemoryView &request, hrpc::ServerContextPtr /*server_context*/) -> Expected<Buffer> {
        auto &cim_manager = ServiceResourceManager<ConfiguredInferModel>::get_instance();

        auto configured_infer_model_handle = ShutdownSerializer::deserialize_request(request);
        CHECK_EXPECTED_AS_HRPC_STATUS(configured_infer_model_handle, ShutdownSerializer);

        auto lambda = [] (std::shared_ptr<ConfiguredInferModel> configured_infer_model) {
            return configured_infer_model->shutdown();
        };

        auto status = cim_manager.execute<hailo_status>(configured_infer_model_handle.value(), lambda);
        TRY_AS_HRPC_STATUS(auto reply, ShutdownSerializer::serialize_reply(status), ShutdownSerializer);

        return reply;
    });
    dispatcher.register_action(HailoRpcActionID::CONFIGURED_INFER_MODEL__RUN_ASYNC,
    [&infer_model_to_info_id, &buffer_pool_per_cim]
    (const MemoryView &request, hrpc::ServerContextPtr server_context) -> Expected<Buffer> {
        auto &cim_manager = ServiceResourceManager<ConfiguredInferModel>::get_instance();
        auto bindings_lambda = [] (std::shared_ptr<ConfiguredInferModel> configured_infer_model) {
            return configured_infer_model->create_bindings();
        };
        TRY_AS_HRPC_STATUS(auto request_tuple, RunAsyncSerializer::deserialize_request(request), RunAsyncSerializer);
        auto configured_infer_model_handle = std::get<0>(request_tuple);
        auto infer_model_handle = std::get<1>(request_tuple);
        auto callback_id = std::get<2>(request_tuple);

        auto bindings = cim_manager.execute<Expected<ConfiguredInferModel::Bindings>>(configured_infer_model_handle, bindings_lambda);
        CHECK_EXPECTED_AS_HRPC_STATUS(bindings, RunAsyncSerializer);

        auto infer_model_info_lambda = [] (std::shared_ptr<InferModelInfo> infer_model_info) {
            return *infer_model_info;
        };
        auto &infer_model_infos_manager = ServiceResourceManager<InferModelInfo>::get_instance();
        auto infer_model_info = infer_model_infos_manager.execute<Expected<InferModelInfo>>(infer_model_to_info_id[infer_model_handle],
            infer_model_info_lambda);
        CHECK_EXPECTED_AS_HRPC_STATUS(infer_model_info, RunAsyncSerializer);

        std::vector<BufferPtr> inputs; // TODO: add infer vector pool
        inputs.reserve(infer_model_info->inputs_names.size());
        for (const auto &input_name : infer_model_info->inputs_names) {
            TRY_AS_HRPC_STATUS(auto input, bindings->input(input_name), RunAsyncSerializer);

            TRY_AS_HRPC_STATUS(auto buffer_ptr, buffer_pool_per_cim[configured_infer_model_handle]->acquire_buffer(input_name),
                RunAsyncSerializer);

            auto status = server_context->connection().read_buffer(MemoryView(*buffer_ptr));
            CHECK_SUCCESS_AS_HRPC_STATUS(status, RunAsyncSerializer);

            inputs.emplace_back(buffer_ptr);
            status = input.set_buffer(MemoryView(*buffer_ptr));
            CHECK_SUCCESS_AS_HRPC_STATUS(status, RunAsyncSerializer);
        }

        std::vector<BufferPtr> outputs; // TODO: add infer vector pool
        outputs.reserve(infer_model_info->outputs_names.size());
        for (const auto &output_name : infer_model_info->outputs_names) {
            TRY_AS_HRPC_STATUS(auto buffer_ptr, buffer_pool_per_cim[configured_infer_model_handle]->acquire_buffer(output_name),
                RunAsyncSerializer);

            auto output = bindings->output(output_name);
            CHECK_EXPECTED_AS_HRPC_STATUS(output, RunAsyncSerializer);

            auto status = output->set_buffer(MemoryView(buffer_ptr->data(), buffer_ptr->size()));
            CHECK_SUCCESS_AS_HRPC_STATUS(status, RunAsyncSerializer);

            outputs.emplace_back(buffer_ptr);
        }

        auto infer_lambda =
            [bindings = bindings.release(), callback_id, server_context, inputs, outputs, &buffer_pool_per_cim, configured_infer_model_handle,
                infer_model_info]
            (std::shared_ptr<ConfiguredInferModel> configured_infer_model) {
                return configured_infer_model->run_async(bindings,
                    [callback_id, server_context, inputs, outputs, &buffer_pool_per_cim, configured_infer_model_handle, infer_model_info]
                        (const AsyncInferCompletionInfo &completion_info) {
                    auto status = server_context->trigger_callback(callback_id, completion_info.status, [outputs, completion_info] (hrpc::RpcConnection connection) -> hailo_status {
                        if (HAILO_SUCCESS == completion_info.status) {
                            for (auto output : outputs) {
                                auto status = connection.write_buffer(MemoryView(*output));
                                CHECK_SUCCESS(status);
                            }
                        }
                        return HAILO_SUCCESS;
                    });

                    // HAILO_COMMUNICATION_CLOSED means the client disconnected. Server doesn't need to restart in this case.
                    if ((status != HAILO_SUCCESS) && (status != HAILO_COMMUNICATION_CLOSED)) {
                        LOGGER__CRITICAL("Error {} returned from connection.write(). Server Should restart!", status);
                    }

                    for (uint32_t i = 0; i < inputs.size(); i++) {
                        status = buffer_pool_per_cim[configured_infer_model_handle]->return_to_pool(infer_model_info->inputs_names[i], inputs[i]);
                        if (status != HAILO_SUCCESS) {
                            LOGGER__CRITICAL("return_to_pool failed for input {}, status = {}. Server should restart!", infer_model_info->inputs_names[i], status);
                            return;
                        }
                    }
                    for (uint32_t i = 0; i < outputs.size(); i++) {
                        status = buffer_pool_per_cim[configured_infer_model_handle]->return_to_pool(infer_model_info->outputs_names[i], outputs[i]);
                        if (status != HAILO_SUCCESS) {
                            LOGGER__CRITICAL("return_to_pool failed for output {}, status = {}. Server should restart!", infer_model_info->outputs_names[i], status);
                            return;
                        }
                    }
                });
            };
        auto job = cim_manager.execute<Expected<AsyncInferJob>>(configured_infer_model_handle, infer_lambda);
        CHECK_EXPECTED_AS_HRPC_STATUS(job, RunAsyncSerializer);

        job->detach();

        TRY_AS_HRPC_STATUS(auto reply, RunAsyncSerializer::serialize_reply(HAILO_SUCCESS), RunAsyncSerializer);
        return reply;
    });

    server->set_dispatcher(dispatcher);
    auto status = server->serve();
    if (status != HAILO_SUCCESS) {
        LOGGER__ERROR("Error in serve, status = {}", status);
        return status;
    }

    return 0;
}
