/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file hailo_server.cpp
 * @brief Hailo Server
 **/

#include "hailort_server.hpp"
#include "hailo/hailort.h"
#include "hrpc/server.hpp"
#include "hailo/vdevice.hpp"
#include "hrpc_protocol/serializer.hpp"
#include "net_flow/ops/nms_post_process.hpp"
#include "hailort_service/service_resource_manager.hpp"
#include "common/thread_safe_queue.hpp"
#include "hrpc/connection_context.hpp"
#include "vdma/pcie_session.hpp"

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
#define CHECK_AS_HRPC_STATUS(_cond, _status, T) \
    do { \
        if (!(_cond)) { \
            LOGGER__ERROR("CHECK_AS_HRPC_STATUS failed, status: {}", _status); \
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

// TODO: Benchmark this factor (HRT-15727)
#define ASYNC_QUEUE_SIZE_FACTOR (2) // double buffer

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

void HailoRTServer::cleanup_infer_model_hef_buffers(const std::vector<uint32_t> &infer_model_handles)
{
    for (const auto &infer_model_handle : infer_model_handles) {
        auto hef_buffers_iter = m_hef_buffers_per_infer_model.find(infer_model_handle);
        if (m_hef_buffers_per_infer_model.end() != hef_buffers_iter) {
            m_hef_buffers_per_infer_model.erase(infer_model_handle);
        }
    }
}

void HailoRTServer::cleanup_cim_buffer_pools(const std::vector<uint32_t> &cim_handles)
{
    std::lock_guard<std::mutex> lock(m_buffer_pool_mutex);
    for (const auto &cim_handle : cim_handles) {
        m_buffer_pool_per_cim.erase(cim_handle);
    }
}

hailo_status HailoRTServer::cleanup_client_resources(RpcConnection client_connection)
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

Expected<std::unique_ptr<HailoRTServer>> HailoRTServer::create_unique()
{
    TRY(auto connection_context, ConnectionContext::create_server_shared());
    TRY(auto callbacks_queue_shutdown_event, Event::create_shared(Event::State::not_signalled));
    auto callbacks_done_queue = SpscQueue<FinishedInferRequest>::create_shared(PcieSession::MAX_ONGOING_TRANSFERS, callbacks_queue_shutdown_event);
    CHECK_NOT_NULL_AS_EXPECTED(callbacks_done_queue, HAILO_OUT_OF_HOST_MEMORY);

    auto res = make_unique_nothrow<HailoRTServer>(connection_context, callbacks_done_queue, callbacks_queue_shutdown_event);
    CHECK_NOT_NULL(res, HAILO_OUT_OF_HOST_MEMORY);
    return res;
}

HailoRTServer::HailoRTServer(std::shared_ptr<ConnectionContext> connection_context,
    std::shared_ptr<SpscQueue<FinishedInferRequest>> callbacks_done_queue,
    EventPtr callbacks_queue_shutdown_event) : Server(connection_context), m_callbacks_done_queue(callbacks_done_queue),
    m_callbacks_queue_shutdown_event(callbacks_queue_shutdown_event)
{
    m_callbacks_thread = std::thread([this] {
        auto status = callbacks_thread_loop();
        if (HAILO_SUCCESS != status) {
            LOGGER__CRITICAL("Callback thread has failed with status {}. Server should restart!", status);
        }
    });
}

hailo_status HailoRTServer::callbacks_thread_loop()
{
    while (true) {
        auto request = m_callbacks_done_queue->dequeue(std::chrono::milliseconds(HAILO_INFINITE));
        if (HAILO_SHUTDOWN_EVENT_SIGNALED == request.status()) {
            break;
        }
        CHECK_EXPECTED_AS_STATUS(request);

        auto status = trigger_callback(request->callback_id, request->completion_info.status, request->configured_infer_model_handle,
            request->connection, [this, &request] (RpcConnection connection) -> hailo_status {
            if (HAILO_SUCCESS == request->completion_info.status) {
                for (auto output : request->outputs) {
                    auto status = connection.wait_for_write_buffer_async_ready(output->size(), SERVER_TIMEOUT);
                    CHECK_SUCCESS(status);

                    status = connection.write_buffer_async(MemoryView(*output), [output] (hailo_status status) {
                        (void)output; // capturing output so it won't be freed before the callback is called
                        if (HAILO_SUCCESS != status) {
                            LOGGER__ERROR("Failed to write buffer, status = {}", status);
                        }
                    });
                    CHECK_SUCCESS(status);
                }

                std::lock_guard<std::mutex> lock(m_buffer_pool_mutex);
                for (uint32_t i = 0; i < request->outputs.size(); i++) {
                    if (m_buffer_pool_per_cim.contains(request->configured_infer_model_handle)) {
                        auto status = m_buffer_pool_per_cim.at(request->configured_infer_model_handle)->return_to_pool(request->outputs_names[i], request->outputs[i]);
                        CHECK_SUCCESS(status);
                    }
                }
            }
            return HAILO_SUCCESS;
        });
        // HAILO_COMMUNICATION_CLOSED means the client disconnected. Server doesn't need to restart in this case.
        if (status != HAILO_COMMUNICATION_CLOSED) {
            CHECK_SUCCESS(status);
        }
    }
    return HAILO_SUCCESS;
}

HailoRTServer::~HailoRTServer()
{
    auto status = m_callbacks_queue_shutdown_event->signal();
    if (HAILO_SUCCESS != status) {
        LOGGER__CRITICAL("Failed to signal shutdown event, status = {}", status);
    }

    if (m_callbacks_thread.joinable()) {
        m_callbacks_thread.join();
    }
}

int main()
{
    init_logger("HailoRT-Server");
    TRY(auto server, HailoRTServer::create_unique());
    Dispatcher dispatcher;

    // TODO: add a server implementation class, with resources heiracrhy and more
    auto &infer_model_to_info_id = server->infer_model_to_info_id();
    auto &buffer_pool_per_cim = server->buffer_pool_per_cim();

    // Because the infer model is created with a hef buffer, we need to keep the buffer until the configure stage.
    // Here I keep it until the infer model is destroyed
    auto &hef_buffers = server->hef_buffers();

    dispatcher.register_action(HailoRpcActionID::VDEVICE__CREATE,
    [] (const MemoryView &request, ServerContextPtr /*server_context*/) -> Expected<Buffer> {
        TRY_AS_HRPC_STATUS(auto vdevice_params, CreateVDeviceSerializer::deserialize_request(request), CreateVDeviceSerializer);
        TRY_AS_HRPC_STATUS(auto vdevice, VDevice::create(vdevice_params.get()), CreateVDeviceSerializer);

        auto &manager = ServiceResourceManager<VDevice>::get_instance();
        auto id = manager.register_resource(SINGLE_CLIENT_PID, std::move(vdevice));
        auto reply = CreateVDeviceSerializer::serialize_reply(HAILO_SUCCESS, id);
        return reply;
    });
    dispatcher.register_action(HailoRpcActionID::VDEVICE__DESTROY,
    [] (const MemoryView &request, ServerContextPtr /*server_context*/) -> Expected<Buffer> {
        auto &manager = ServiceResourceManager<VDevice>::get_instance();
        TRY_AS_HRPC_STATUS(auto vdevice_handle, DestroyVDeviceSerializer::deserialize_request(request), DestroyVDeviceSerializer);
        (void)manager.release_resource(vdevice_handle, SINGLE_CLIENT_PID);
        TRY_AS_HRPC_STATUS(auto reply, DestroyVDeviceSerializer::serialize_reply(HAILO_SUCCESS), DestroyVDeviceSerializer);
        return reply;
    });
    dispatcher.register_action(HailoRpcActionID::VDEVICE__CREATE_INFER_MODEL,
    [&hef_buffers] (const MemoryView &request, ServerContextPtr server_context) -> Expected<Buffer> {
        TRY_AS_HRPC_STATUS(auto tuple, CreateInferModelSerializer::deserialize_request(request), CreateInferModelSerializer);
        auto vdevice_handle = std::get<0>(tuple);
        uint64_t hef_size = std::get<1>(tuple);
        auto name = std::get<2>(tuple);

        assert(hef_size <= SIZE_MAX);
        TRY_AS_HRPC_STATUS(auto hef_buffer, Buffer::create(static_cast<size_t>(hef_size), BufferStorageParams::create_dma()),
            CreateInferModelSerializer);

        auto status = server_context->connection().read_buffer(MemoryView(hef_buffer));
        CHECK_SUCCESS_AS_HRPC_STATUS(status, CreateInferModelSerializer);

        auto &vdevice_manager = ServiceResourceManager<VDevice>::get_instance();
        auto lambda = [view = MemoryView(hef_buffer), &name] (std::shared_ptr<VDevice> vdevice) {
            return vdevice->create_infer_model(view, name);
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
    [&hef_buffers] (const MemoryView &request, ServerContextPtr /*server_context*/) -> Expected<Buffer> {
        auto &manager = ServiceResourceManager<InferModel>::get_instance();
        TRY_AS_HRPC_STATUS(auto infer_model_handle, DestroyInferModelSerializer::deserialize_request(request), DestroyInferModelSerializer);
        hef_buffers.erase(infer_model_handle);
        (void)manager.release_resource(infer_model_handle, SINGLE_CLIENT_PID);
        TRY_AS_HRPC_STATUS(auto reply, DestroyInferModelSerializer::serialize_reply(HAILO_SUCCESS), DestroyInferModelSerializer);
        return reply;
    });
    dispatcher.register_action(HailoRpcActionID::INFER_MODEL__CREATE_CONFIGURED_INFER_MODEL,
    [&buffer_pool_per_cim, &infer_model_to_info_id]
    (const MemoryView &request, ServerContextPtr /*server_context*/) -> Expected<Buffer> {
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
                infer_model_info->input_streams_sizes[input_name], async_queue_size * ASYNC_QUEUE_SIZE_FACTOR);
            CHECK_SUCCESS_AS_HRPC_STATUS(status, CreateConfiguredInferModelSerializer);
        }
        for (const auto &output_name : infer_model_info->outputs_names) {
            auto status = buffer_pool_ptr->allocate_pool(output_name, HAILO_DMA_BUFFER_DIRECTION_H2D,
                infer_model_info->output_streams_sizes[output_name], async_queue_size * ASYNC_QUEUE_SIZE_FACTOR);
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
    [&server] (const MemoryView &request, ServerContextPtr /*server_context*/) -> Expected<Buffer> {
        auto &manager = ServiceResourceManager<ConfiguredInferModel>::get_instance();
        TRY_AS_HRPC_STATUS(auto configured_infer_model_handle, DestroyConfiguredInferModelSerializer::deserialize_request(request), DestroyInferModelSerializer);

        auto shutdown_lambda = [] (std::shared_ptr<ConfiguredInferModel> configured_infer_model) {
            configured_infer_model->shutdown();
            return HAILO_SUCCESS;
        };
        manager.execute<hailo_status>(configured_infer_model_handle, shutdown_lambda);
        server->cleanup_cim_buffer_pools({ configured_infer_model_handle });
        (void)manager.release_resource(configured_infer_model_handle, SINGLE_CLIENT_PID);
        TRY_AS_HRPC_STATUS(auto reply, DestroyConfiguredInferModelSerializer::serialize_reply(HAILO_SUCCESS), DestroyInferModelSerializer);
        return reply;
    });
    dispatcher.register_action(HailoRpcActionID::CONFIGURED_INFER_MODEL__SET_SCHEDULER_TIMEOUT,
    [] (const MemoryView &request, ServerContextPtr /*server_context*/) -> Expected<Buffer> {
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
    [] (const MemoryView &request, ServerContextPtr /*server_context*/) -> Expected<Buffer> {
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
    [] (const MemoryView &request, ServerContextPtr /*server_context*/) -> Expected<Buffer> {
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
    [] (const MemoryView &request, ServerContextPtr /*server_context*/) -> Expected<Buffer> {
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
    [] (const MemoryView &request, ServerContextPtr /*server_context*/) -> Expected<Buffer> {
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
    [] (const MemoryView &request, ServerContextPtr /*server_context*/) -> Expected<Buffer> {
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
    [] (const MemoryView &request, ServerContextPtr /*server_context*/) -> Expected<Buffer> {
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
    [&infer_model_to_info_id, &buffer_pool_per_cim, callbacks_done_queue = server->callbacks_done_queue()]
    (const MemoryView &request, ServerContextPtr server_context) -> Expected<Buffer> {
        auto &cim_manager = ServiceResourceManager<ConfiguredInferModel>::get_instance();
        auto bindings_lambda = [] (std::shared_ptr<ConfiguredInferModel> configured_infer_model) {
            return configured_infer_model->create_bindings();
        };
        TRY_AS_HRPC_STATUS(auto request_struct, RunAsyncSerializer::deserialize_request(request), RunAsyncSerializer);
        auto configured_infer_model_handle = request_struct.configured_infer_model_handle;
        auto infer_model_handle = request_struct.infer_model_handle;
        auto callback_id = request_struct.callback_handle;

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
        uint32_t buffer_size_index = 0;

        for (const auto &input_name : infer_model_info->inputs_names) {
            TRY_AS_HRPC_STATUS(auto input, bindings->input(input_name), RunAsyncSerializer);

            TRY_AS_HRPC_STATUS(auto buffer_ptr, buffer_pool_per_cim.at(configured_infer_model_handle)->acquire_buffer(input_name),
                RunAsyncSerializer);

            uint32_t read_size = 0;
            while (read_size < buffer_ptr->size()) {
                uint32_t current_size = request_struct.input_buffer_sizes[buffer_size_index++];
                CHECK_AS_HRPC_STATUS(read_size + current_size <= buffer_ptr->size(), HAILO_INTERNAL_FAILURE,
                    RunAsyncSerializer);

                auto status = server_context->connection().read_buffer(MemoryView(buffer_ptr->data() + read_size, current_size));
                CHECK_SUCCESS_AS_HRPC_STATUS(status, RunAsyncSerializer);

                read_size += current_size;
            }

            inputs.emplace_back(buffer_ptr);
            auto status = input.set_buffer(MemoryView(*buffer_ptr));
            CHECK_SUCCESS_AS_HRPC_STATUS(status, RunAsyncSerializer);
        }

        std::vector<BufferPtr> outputs; // TODO: add infer vector pool
        outputs.reserve(infer_model_info->outputs_names.size());
        for (const auto &output_name : infer_model_info->outputs_names) {
            TRY_AS_HRPC_STATUS(auto buffer_ptr, buffer_pool_per_cim.at(configured_infer_model_handle)->acquire_buffer(output_name),
                RunAsyncSerializer);

            auto output = bindings->output(output_name);
            CHECK_EXPECTED_AS_HRPC_STATUS(output, RunAsyncSerializer);

            auto status = output->set_buffer(MemoryView(buffer_ptr->data(), buffer_ptr->size()));
            CHECK_SUCCESS_AS_HRPC_STATUS(status, RunAsyncSerializer);

            outputs.emplace_back(buffer_ptr);
        }

        auto infer_lambda =
            [bindings = bindings.release(), callback_id, server_context, inputs, outputs, &buffer_pool_per_cim, configured_infer_model_handle,
                infer_model_info, callbacks_done_queue]
            (std::shared_ptr<ConfiguredInferModel> configured_infer_model) {
                return configured_infer_model->run_async(bindings,
                    [callback_id, server_context, inputs, outputs, &buffer_pool_per_cim, configured_infer_model_handle, infer_model_info,
                    callbacks_done_queue]
                        (const AsyncInferCompletionInfo &completion_info) {
                    for (uint32_t i = 0; i < inputs.size(); i++) {
                        auto status = buffer_pool_per_cim.at(configured_infer_model_handle)->return_to_pool(infer_model_info->inputs_names[i], inputs[i]);
                        if (HAILO_SUCCESS != status) {
                            LOGGER__ERROR("Failed to return buffer to pool, status = {}", status);
                        }
                    }

                    FinishedInferRequest request;
                    request.connection = server_context->connection();
                    request.completion_info = completion_info;
                    request.callback_id = callback_id;
                    request.configured_infer_model_handle = configured_infer_model_handle;
                    request.outputs = std::move(outputs);
                    request.outputs_names = infer_model_info->outputs_names;
                    auto status = callbacks_done_queue->enqueue(std::move(request));
                    if (HAILO_SUCCESS != status) {
                        LOGGER__ERROR("Failed to enqueue to infer requests queue, status = {}", status);
                    }
                });
            };
        auto job = cim_manager.execute<Expected<AsyncInferJob>>(configured_infer_model_handle, infer_lambda);
        CHECK_EXPECTED_AS_HRPC_STATUS(job, RunAsyncSerializer);

        job->detach();

        TRY_AS_HRPC_STATUS(auto reply, RunAsyncSerializer::serialize_reply(HAILO_SUCCESS), RunAsyncSerializer);
        return reply;
    });
    dispatcher.register_action(HailoRpcActionID::DEVICE__CREATE,
    [] (const MemoryView &request, ServerContextPtr /*server_context*/) -> Expected<Buffer> {
        auto status = CreateDeviceSerializer::deserialize_request(request);
        CHECK_SUCCESS_AS_HRPC_STATUS(status, CreateDeviceSerializer);

        TRY_AS_HRPC_STATUS(auto device, Device::create(), CreateDeviceSerializer);

        auto &manager = ServiceResourceManager<Device>::get_instance();
        auto id = manager.register_resource(SINGLE_CLIENT_PID, std::move(device));
        auto reply = CreateDeviceSerializer::serialize_reply(HAILO_SUCCESS, id);
        return reply;
    });
    dispatcher.register_action(HailoRpcActionID::DEVICE__DESTROY,
    [] (const MemoryView &request, ServerContextPtr /*server_context*/) -> Expected<Buffer> {
        auto &manager = ServiceResourceManager<Device>::get_instance();
        TRY_AS_HRPC_STATUS(auto device_handle, DestroyDeviceSerializer::deserialize_request(request), DestroyDeviceSerializer);
        (void)manager.release_resource(device_handle, SINGLE_CLIENT_PID);
        TRY_AS_HRPC_STATUS(auto reply, DestroyDeviceSerializer::serialize_reply(HAILO_SUCCESS), DestroyDeviceSerializer);
        return reply;
    });
    dispatcher.register_action(HailoRpcActionID::DEVICE__IDENTIFY,
    [] (const MemoryView &request, ServerContextPtr /*server_context*/) -> Expected<Buffer> {
        TRY_AS_HRPC_STATUS(auto device_handle, IdentifyDeviceSerializer::deserialize_request(request), IdentifyDeviceSerializer);

        auto &manager = ServiceResourceManager<Device>::get_instance();
        auto device_lambda = [] (std::shared_ptr<Device> device) {
            return device->identify();
        };
        TRY_AS_HRPC_STATUS(auto identity,
            manager.execute<Expected<hailo_device_identity_t>>(device_handle, device_lambda), IdentifyDeviceSerializer);
        TRY_AS_HRPC_STATUS(auto reply, IdentifyDeviceSerializer::serialize_reply(HAILO_SUCCESS, identity), IdentifyDeviceSerializer);
        return reply;
    });
    dispatcher.register_action(HailoRpcActionID::DEVICE__EXTENDED_INFO,
    [] (const MemoryView &request, ServerContextPtr /*server_context*/) -> Expected<Buffer> {
        using Serializer = ExtendedDeviceInfoSerializer;
        using ActionReturnType = hailo_extended_device_information_t;

        TRY_AS_HRPC_STATUS(auto device_handle, Serializer::deserialize_request(request), Serializer);

        auto &manager = ServiceResourceManager<Device>::get_instance();
        auto device_lambda = [] (std::shared_ptr<Device> device) {
            return device->get_extended_device_information();
        };
        TRY_AS_HRPC_STATUS(auto extended_info,
            manager.execute<Expected<ActionReturnType>>(device_handle, device_lambda), Serializer);
        TRY_AS_HRPC_STATUS(auto reply, Serializer::serialize_reply(HAILO_SUCCESS, extended_info), Serializer);
        return reply;
    });
    dispatcher.register_action(HailoRpcActionID::DEVICE__GET_CHIP_TEMPERATURE,
    [] (const MemoryView &request, ServerContextPtr /*server_context*/) -> Expected<Buffer> {
        using Serializer = GetChipTemperatureSerializer;
        using ActionReturnType = hailo_chip_temperature_info_t;

        TRY_AS_HRPC_STATUS(auto device_handle, Serializer::deserialize_request(request), Serializer);

        auto &manager = ServiceResourceManager<Device>::get_instance();
        auto device_lambda = [] (std::shared_ptr<Device> device) {
            return device->get_chip_temperature();
        };

        TRY_AS_HRPC_STATUS(auto info, manager.execute<Expected<ActionReturnType>>(device_handle, device_lambda), Serializer);
        TRY_AS_HRPC_STATUS(auto reply, Serializer::serialize_reply(HAILO_SUCCESS, info), Serializer);

        return reply;
    });
    dispatcher.register_action(HailoRpcActionID::DEVICE__POWER_MEASUREMENT,
    [] (const MemoryView &request, ServerContextPtr /*server_context*/) -> Expected<Buffer> {
        using Serializer = PowerMeasurementSerializer;
        using ActionReturnType = float32_t;

        TRY_AS_HRPC_STATUS(auto tuple, Serializer::deserialize_request(request), Serializer);

        auto device_handle = std::get<0>(tuple);
        auto dvm = std::get<1>(tuple);
        auto power_measurement_type = std::get<2>(tuple);

        auto &manager = ServiceResourceManager<Device>::get_instance();
        auto device_lambda = [dvm, power_measurement_type] (std::shared_ptr<Device> device) {
            return device->power_measurement(
                static_cast<hailo_dvm_options_t>(dvm),
                static_cast<hailo_power_measurement_types_t>(power_measurement_type));
        };

        TRY_AS_HRPC_STATUS(auto info, manager.execute<Expected<ActionReturnType>>(device_handle, device_lambda), Serializer);
        TRY_AS_HRPC_STATUS(auto reply, Serializer::serialize_reply(HAILO_SUCCESS, info), Serializer);

        return reply;
    });
    dispatcher.register_action(HailoRpcActionID::DEVICE__SET_POWER_MEASUREMENT,
    [] (const MemoryView &request, ServerContextPtr /*server_context*/) -> Expected<Buffer> {
        using Serializer = SetPowerMeasurementSerializer;
        using ActionReturnType = hailo_status;

        TRY_AS_HRPC_STATUS(auto tuple, Serializer::deserialize_request(request), Serializer);

        auto device_handle = std::get<0>(tuple);
        auto dvm = std::get<1>(tuple);
        auto power_measurement_type = std::get<2>(tuple);

        auto &manager = ServiceResourceManager<Device>::get_instance();
        auto device_lambda = [dvm, power_measurement_type] (std::shared_ptr<Device> device) {
            constexpr hailo_measurement_buffer_index_t not_used_buffer_index = HAILO_MEASUREMENT_BUFFER_INDEX_MAX_ENUM;
            return device->set_power_measurement(
                not_used_buffer_index, /* Relevant only for H8. Not used in H10 */
                static_cast<hailo_dvm_options_t>(dvm),
                static_cast<hailo_power_measurement_types_t>(power_measurement_type));
        };

        CHECK_SUCCESS_AS_HRPC_STATUS(manager.execute<ActionReturnType>(device_handle, device_lambda), Serializer);
        TRY_AS_HRPC_STATUS(auto reply, Serializer::serialize_reply(HAILO_SUCCESS), Serializer);

        return reply;
    });
    dispatcher.register_action(HailoRpcActionID::DEVICE__START_POWER_MEASUREMENT,
    [] (const MemoryView &request, ServerContextPtr) -> Expected<Buffer> {
        using Serializer = SetPowerMeasurementSerializer;
        using ActionReturnType = hailo_status;

        TRY_AS_HRPC_STATUS(auto tuple, Serializer::deserialize_request(request), Serializer);

        auto device_handle = std::get<0>(tuple);
        auto averaging_factor = std::get<1>(tuple);
        auto sampling_period = std::get<2>(tuple);

        auto &manager = ServiceResourceManager<Device>::get_instance();
        auto device_lambda = [sampling_period, averaging_factor] (std::shared_ptr<Device> device) {
            return device->start_power_measurement(
                static_cast<hailo_averaging_factor_t>(averaging_factor),
                static_cast<hailo_sampling_period_t>(sampling_period));
        };

        CHECK_SUCCESS_AS_HRPC_STATUS(manager.execute<ActionReturnType>(device_handle, device_lambda), Serializer);
        TRY_AS_HRPC_STATUS(auto reply, Serializer::serialize_reply(HAILO_SUCCESS), Serializer);

        return reply;
    });
    dispatcher.register_action(HailoRpcActionID::DEVICE__GET_POWER_MEASUREMENT,
    [] (const MemoryView &request, ServerContextPtr) -> Expected<Buffer> {
        using Serializer = GetPowerMeasurementSerializer;
        using ActionReturnType = hailo_power_measurement_data_t;

        TRY_AS_HRPC_STATUS(auto tuple, Serializer::deserialize_request(request), Serializer);

        auto device_handle = std::get<0>(tuple);
        auto should_clear = std::get<1>(tuple);

        auto &manager = ServiceResourceManager<Device>::get_instance();
        auto device_lambda = [should_clear] (std::shared_ptr<Device> device) {
			constexpr hailo_measurement_buffer_index_t unused_buffer_index = HAILO_MEASUREMENT_BUFFER_INDEX_MAX_ENUM;
            return device->get_power_measurement(unused_buffer_index, should_clear);
        };

        TRY_AS_HRPC_STATUS(auto info, manager.execute<Expected<ActionReturnType>>(device_handle, device_lambda), Serializer);
        TRY_AS_HRPC_STATUS(auto reply, Serializer::serialize_reply(HAILO_SUCCESS, info), Serializer);

        return reply;
    });
    dispatcher.register_action(HailoRpcActionID::DEVICE__STOP_POWER_MEASUREMENT,
    [] (const MemoryView &request, ServerContextPtr) -> Expected<Buffer> {
        using Serializer = StopPowerMeasurementSerializer;
        using ActionReturnType = hailo_status;

        TRY_AS_HRPC_STATUS(auto device_handle, Serializer::deserialize_request(request), Serializer);
        auto &manager = ServiceResourceManager<Device>::get_instance();

        auto device_lambda = [] (std::shared_ptr<Device> device) {
            return device->stop_power_measurement();
        };

        CHECK_SUCCESS_AS_HRPC_STATUS(manager.execute<ActionReturnType>(device_handle, device_lambda), Serializer);
        TRY_AS_HRPC_STATUS(auto reply, Serializer::serialize_reply(HAILO_SUCCESS), Serializer);

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
