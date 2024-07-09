/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file serializer.cpp
 * @brief HRPC Serialization implementation
 **/

#include "serializer.hpp"
#include "hailo/hailort.h"
#include "hailo/hailort_common.hpp"
#include "hailo/hailort_defaults.hpp"
#include "common/utils.hpp"

// https://github.com/protocolbuffers/protobuf/tree/master/cmake#notes-on-compiler-warnings
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4244 4267 4127)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "rpc.pb.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#endif

namespace hailort
{

Expected<Buffer> CreateVDeviceSerializer::serialize_request(const hailo_vdevice_params_t &params)
{
    VDevice_Create_Request request;

    auto proto_params = request.mutable_params();
    proto_params->set_scheduling_algorithm(params.scheduling_algorithm);
    proto_params->set_group_id(params.group_id == nullptr ? "" : std::string(params.group_id));

    // TODO (HRT-13983) - check if we can use GetCachedSize
    TRY(auto serialized_request, Buffer::create(request.ByteSizeLong(), BufferStorageParams::create_dma()));

    CHECK_AS_EXPECTED(request.SerializeToArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'CreateVDevice'");

    return serialized_request;
}

Expected<hailo_vdevice_params_t> CreateVDeviceSerializer::deserialize_request(const MemoryView &serialized_request)
{
    VDevice_Create_Request request;

    CHECK_AS_EXPECTED(request.ParseFromArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'CreateVDevice'");

    bool multi_process_service_flag = false;
    hailo_vdevice_params_t res = {
        1,
        nullptr,
        static_cast<hailo_scheduling_algorithm_e>(request.params().scheduling_algorithm()),
        request.params().group_id().c_str(),
        multi_process_service_flag
    };

    return res;
}

Expected<Buffer> CreateVDeviceSerializer::serialize_reply(hailo_status status, rpc_object_handle_t vdevice_handle)
{
    VDevice_Create_Reply reply;

    reply.set_status(status);
    auto proto_vdevice_handle = reply.mutable_vdevice_handle();
    proto_vdevice_handle->set_id(vdevice_handle);

    TRY(auto serialized_reply, Buffer::create(reply.ByteSizeLong(), BufferStorageParams::create_dma()));

    CHECK_AS_EXPECTED(reply.SerializeToArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'CreateVDevice'");

    return serialized_reply;
}

Expected<std::tuple<hailo_status, rpc_object_handle_t>> CreateVDeviceSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    VDevice_Create_Reply reply;

    CHECK_AS_EXPECTED(reply.ParseFromArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'CreateVDevice'");

    return std::make_tuple(static_cast<hailo_status>(reply.status()), reply.vdevice_handle().id());
}

Expected<Buffer> DestroyVDeviceSerializer::serialize_request(rpc_object_handle_t vdevice_handle)
{
    VDevice_Destroy_Request request;

    auto proto_vdevice_handle= request.mutable_vdevice_handle();
    proto_vdevice_handle->set_id(vdevice_handle);

    // TODO (HRT-13983) - check if we can use GetCachedSize
    TRY(auto serialized_request, Buffer::create(request.ByteSizeLong(), BufferStorageParams::create_dma()));
    CHECK_AS_EXPECTED(request.SerializeToArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'DestroyVDevice'");

    return serialized_request;
}

Expected<rpc_object_handle_t> DestroyVDeviceSerializer::deserialize_request(const MemoryView &serialized_request)
{
    VDevice_Destroy_Request request;

    CHECK_AS_EXPECTED(request.ParseFromArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'DestroyVDevice'");

    return request.vdevice_handle().id();
}

Expected<Buffer> DestroyVDeviceSerializer::serialize_reply(hailo_status status)
{
    VDevice_Destroy_Reply reply;
    reply.set_status(status);

    TRY(auto serialized_reply, Buffer::create(reply.ByteSizeLong(), BufferStorageParams::create_dma()));

    CHECK_AS_EXPECTED(reply.SerializeToArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'DestroyVDevice'");

    return serialized_reply;
}

hailo_status DestroyVDeviceSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    VDevice_Destroy_Reply reply;

    CHECK_AS_EXPECTED(reply.ParseFromArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'DestroyVDevice'");

    return static_cast<hailo_status>(reply.status());
}

Expected<Buffer> CreateInferModelSerializer::serialize_request(rpc_object_handle_t vdevice_handle, uint64_t hef_size)
{
    VDevice_CreateInferModel_Request request;

    auto proto_vdevice_handle = request.mutable_vdevice_handle();
    proto_vdevice_handle->set_id(vdevice_handle);

    request.set_hef_size(hef_size);

    // TODO (HRT-13983) - check if we can use GetCachedSize
    TRY(auto serialized_request, Buffer::create(request.ByteSizeLong(), BufferStorageParams::create_dma()));
    CHECK_AS_EXPECTED(request.SerializeToArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'CreateVInferModel'");

    return serialized_request;
}

Expected<std::tuple<rpc_object_handle_t, uint64_t>> CreateInferModelSerializer::deserialize_request(const MemoryView &serialized_request)
{
    VDevice_CreateInferModel_Request request;

    CHECK_AS_EXPECTED(request.ParseFromArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'CreateVInferModel'");

    return std::make_tuple(request.vdevice_handle().id(), request.hef_size());
}

Expected<Buffer> CreateInferModelSerializer::serialize_reply(hailo_status status, rpc_object_handle_t infer_model_handle)
{
    VDevice_CreateInferModel_Reply reply;

    reply.set_status(status);
    auto proto_infer_model_handle = reply.mutable_infer_model_handle();
    proto_infer_model_handle->set_id(infer_model_handle);

    TRY(auto serialized_reply, Buffer::create(reply.ByteSizeLong(), BufferStorageParams::create_dma()));

    CHECK_AS_EXPECTED(reply.SerializeToArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'CreateVInferModel'");

    return serialized_reply;
}

Expected<std::tuple<hailo_status, rpc_object_handle_t>> CreateInferModelSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    VDevice_CreateInferModel_Reply reply;

    CHECK_AS_EXPECTED(reply.ParseFromArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'CreateVInferModel'");

    return std::make_tuple(static_cast<hailo_status>(reply.status()), reply.infer_model_handle().id());
}

Expected<Buffer> DestroyInferModelSerializer::serialize_request(rpc_object_handle_t infer_model_handle)
{
    InferModel_Destroy_Request request;

    auto proto_infer_model_handle = request.mutable_infer_model_handle();
    proto_infer_model_handle->set_id(infer_model_handle);

    // TODO (HRT-13983) - check if we can use GetCachedSize
    TRY(auto serialized_request, Buffer::create(request.ByteSizeLong(), BufferStorageParams::create_dma()));
    CHECK_AS_EXPECTED(request.SerializeToArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'DestroyInferModel'");

    return serialized_request;
}

Expected<rpc_object_handle_t> DestroyInferModelSerializer::deserialize_request(const MemoryView &serialized_request)
{
    InferModel_Destroy_Request request;

    CHECK_AS_EXPECTED(request.ParseFromArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'DestroyInferModel'");

    return request.infer_model_handle().id();
}

Expected<Buffer> DestroyInferModelSerializer::serialize_reply(hailo_status status)
{
    InferModel_Destroy_Reply reply;
    reply.set_status(status);

    TRY(auto serialized_reply, Buffer::create(reply.ByteSizeLong(), BufferStorageParams::create_dma()));

    CHECK_AS_EXPECTED(reply.SerializeToArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'DestroyInferModel'");

    return serialized_reply;
}

hailo_status DestroyInferModelSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    InferModel_Destroy_Reply reply;

    CHECK_AS_EXPECTED(reply.ParseFromArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'DestroyInferModel'");

    return static_cast<hailo_status>(reply.status());
}

Expected<Buffer> CreateConfiguredInferModelSerializer::serialize_request(rpc_create_configured_infer_model_request_params_t params)
{
    InferModel_CreateConfiguredInferModel_Request request;

    auto proto_infer_model_handle = request.mutable_infer_model_handle();
    proto_infer_model_handle->set_id(params.infer_model_handle);

    auto proto_vdevide_handle = request.mutable_vdevice_handle();
    proto_vdevide_handle->set_id(params.vdevice_handle);

    for (auto &input_stream_params : params.input_streams_params) {
        auto proto_input_stream = request.add_input_infer_streams();
        proto_input_stream->set_name(input_stream_params.first);
        proto_input_stream->set_format_order(input_stream_params.second.format_order);
        proto_input_stream->set_format_type(input_stream_params.second.format_type);
        proto_input_stream->set_nms_score_threshold(input_stream_params.second.nms_score_threshold);
        proto_input_stream->set_nms_iou_threshold(input_stream_params.second.nms_iou_threshold);
        proto_input_stream->set_nms_max_proposals_per_class(input_stream_params.second.nms_max_proposals_per_class);
        proto_input_stream->set_nms_max_accumulated_mask_size(input_stream_params.second.nms_max_accumulated_mask_size);
    }

    for (auto &output_stream_params : params.output_streams_params) {
        auto proto_output_stream = request.add_output_infer_streams();
        proto_output_stream->set_name(output_stream_params.first);
        proto_output_stream->set_format_order(output_stream_params.second.format_order);
        proto_output_stream->set_format_type(output_stream_params.second.format_type);
        proto_output_stream->set_nms_score_threshold(output_stream_params.second.nms_score_threshold);
        proto_output_stream->set_nms_iou_threshold(output_stream_params.second.nms_iou_threshold);
        proto_output_stream->set_nms_max_proposals_per_class(output_stream_params.second.nms_max_proposals_per_class);
        proto_output_stream->set_nms_max_accumulated_mask_size(output_stream_params.second.nms_max_accumulated_mask_size);
    }

    request.set_batch_size(static_cast<uint32_t>(params.batch_size));
    request.set_power_mode(static_cast<uint32_t>(params.power_mode));
    request.set_latency_flag(static_cast<uint32_t>(params.latency_flag));

    // TODO (HRT-13983) - check if we can use GetCachedSize
    TRY(auto serialized_request, Buffer::create(request.ByteSizeLong(), BufferStorageParams::create_dma()));
    CHECK_AS_EXPECTED(request.SerializeToArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'CreateConfiguredInferModel'");

    return serialized_request;
}

Expected<rpc_create_configured_infer_model_request_params_t> CreateConfiguredInferModelSerializer::deserialize_request(const MemoryView &serialized_request)
{
    rpc_create_configured_infer_model_request_params_t request_params;
    InferModel_CreateConfiguredInferModel_Request request;

    CHECK_AS_EXPECTED(request.ParseFromArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'CreateConfiguredInferModel'");

    request_params.infer_model_handle = request.infer_model_handle().id();
    request_params.vdevice_handle = request.vdevice_handle().id();

    for (auto input_stream: request.input_infer_streams()) {
        rpc_stream_params_t current_stream_params;
        current_stream_params.format_order = input_stream.format_order();
        current_stream_params.format_type = input_stream.format_type();
        current_stream_params.nms_score_threshold = input_stream.nms_score_threshold();
        current_stream_params.nms_iou_threshold = input_stream.nms_iou_threshold();
        current_stream_params.nms_max_proposals_per_class = input_stream.nms_max_proposals_per_class();
        current_stream_params.nms_max_accumulated_mask_size = input_stream.nms_max_accumulated_mask_size();
        request_params.input_streams_params.emplace(input_stream.name(), current_stream_params);
    }

    for (auto output_stream: request.output_infer_streams()) {
        rpc_stream_params_t current_stream_params;
        current_stream_params.format_order = output_stream.format_order();
        current_stream_params.format_type = output_stream.format_type();
        current_stream_params.nms_score_threshold = output_stream.nms_score_threshold();
        current_stream_params.nms_iou_threshold = output_stream.nms_iou_threshold();
        current_stream_params.nms_max_proposals_per_class = output_stream.nms_max_proposals_per_class();
        current_stream_params.nms_max_accumulated_mask_size = output_stream.nms_max_accumulated_mask_size();
        request_params.output_streams_params.emplace(output_stream.name(), current_stream_params);
    }

    request_params.batch_size = static_cast<uint16_t>(request.batch_size());
    request_params.power_mode = static_cast<hailo_power_mode_t>(request.power_mode());
    request_params.latency_flag = static_cast<hailo_latency_measurement_flags_t>(request.latency_flag());

    return request_params;
}

Expected<Buffer> CreateConfiguredInferModelSerializer::serialize_reply(hailo_status status, rpc_object_handle_t configured_infer_handle,
    uint32_t async_queue_size)
{
    InferModel_CreateConfiguredInferModel_Reply reply;

    reply.set_status(status);
    auto proto_configured_infer_model_handle = reply.mutable_configured_infer_model_handle();
    proto_configured_infer_model_handle->set_id(configured_infer_handle);
    reply.set_async_queue_size(async_queue_size);

    TRY(auto serialized_reply, Buffer::create(reply.ByteSizeLong(), BufferStorageParams::create_dma()));

    CHECK_AS_EXPECTED(reply.SerializeToArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'CreateConfiguredInferModel'");

    return serialized_reply;
}

Expected<std::tuple<hailo_status, rpc_object_handle_t, uint32_t>> CreateConfiguredInferModelSerializer::deserialize_reply(
    const MemoryView &serialized_reply)
{
    InferModel_CreateConfiguredInferModel_Reply reply;

    CHECK_AS_EXPECTED(reply.ParseFromArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'CreateConfiguredInferModel'");

    return std::make_tuple(static_cast<hailo_status>(reply.status()), reply.configured_infer_model_handle().id(), reply.async_queue_size());
}

Expected<Buffer> DestroyConfiguredInferModelSerializer::serialize_request(rpc_object_handle_t configured_infer_model_handle)
{
    ConfiguredInferModel_Destroy_Request request;

    auto proto_infer_model_handle = request.mutable_configured_infer_model_handle();
    proto_infer_model_handle->set_id(configured_infer_model_handle);

    // TODO (HRT-13983) - check if we can use GetCachedSize
    TRY(auto serialized_request, Buffer::create(request.ByteSizeLong(), BufferStorageParams::create_dma()));
    CHECK_AS_EXPECTED(request.SerializeToArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'DestroyConfiguredInferModel'");

    return serialized_request;
}

Expected<rpc_object_handle_t> DestroyConfiguredInferModelSerializer::deserialize_request(const MemoryView &serialized_request)
{
    ConfiguredInferModel_Destroy_Request request;

    CHECK_AS_EXPECTED(request.ParseFromArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'DestroyConfiguredInferModel'");

    return request.configured_infer_model_handle().id();
}

Expected<Buffer> DestroyConfiguredInferModelSerializer::serialize_reply(hailo_status status)
{
    ConfiguredInferModel_Destroy_Reply reply;
    reply.set_status(status);

    TRY(auto serialized_reply, Buffer::create(reply.ByteSizeLong(), BufferStorageParams::create_dma()));

    CHECK_AS_EXPECTED(reply.SerializeToArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'DestroyConfiguredInferModel'");

    return serialized_reply;
}

hailo_status DestroyConfiguredInferModelSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    ConfiguredInferModel_Destroy_Reply reply;

    CHECK_AS_EXPECTED(reply.ParseFromArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'CreateConfiguredInferModel'");
    CHECK_SUCCESS(static_cast<hailo_status>(reply.status()));

    return HAILO_SUCCESS;
}

Expected<Buffer> SetSchedulerTimeoutSerializer::serialize_request(rpc_object_handle_t configured_infer_model_handle, const std::chrono::milliseconds &timeout)
{
    ConfiguredInferModel_SetSchedulerTimeout_Request request;

    auto proto_configured_infer_model_handle = request.mutable_configured_infer_model_handle();
    proto_configured_infer_model_handle->set_id(configured_infer_model_handle);
    request.set_timeout(static_cast<uint32_t>(timeout.count()));

    // TODO (HRT-13983) - check if we can use GetCachedSize
    TRY(auto serialized_request, Buffer::create(request.ByteSizeLong(), BufferStorageParams::create_dma()));
    CHECK_AS_EXPECTED(request.SerializeToArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'SetSchedulerTimeout'");

    return serialized_request;
}

Expected<std::tuple<rpc_object_handle_t, std::chrono::milliseconds>> SetSchedulerTimeoutSerializer::deserialize_request(
    const MemoryView &serialized_request)
{
    ConfiguredInferModel_SetSchedulerTimeout_Request request;

    CHECK_AS_EXPECTED(request.ParseFromArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'SetSchedulerTimeout'");

    return std::make_tuple(request.configured_infer_model_handle().id(), std::chrono::milliseconds(request.timeout()));
}

Expected<Buffer> SetSchedulerTimeoutSerializer::serialize_reply(hailo_status status)
{
    ConfiguredInferModel_SetSchedulerTimeout_Reply reply;
    reply.set_status(status);

    TRY(auto serialized_reply, Buffer::create(reply.ByteSizeLong(), BufferStorageParams::create_dma()));

    CHECK_AS_EXPECTED(reply.SerializeToArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'SetSchedulerTimeout'");

    return serialized_reply;
}

hailo_status SetSchedulerTimeoutSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    ConfiguredInferModel_SetSchedulerTimeout_Reply reply;

    CHECK_AS_EXPECTED(reply.ParseFromArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'SetSchedulerTimeout'");

    return static_cast<hailo_status>(reply.status());
}

Expected<Buffer> SetSchedulerThresholdSerializer::serialize_request(rpc_object_handle_t configured_infer_model_handle, uint32_t threshold)
{
    ConfiguredInferModel_SetSchedulerThreshold_Request request;

    auto proto_configured_infer_model_handle = request.mutable_configured_infer_model_handle();
    proto_configured_infer_model_handle->set_id(configured_infer_model_handle);
    request.set_threshold(threshold);

    // TODO (HRT-13983) - check if we can use GetCachedSize
    TRY(auto serialized_request, Buffer::create(request.ByteSizeLong(), BufferStorageParams::create_dma()));
    CHECK_AS_EXPECTED(request.SerializeToArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'SetSchedulerThreshold'");

    return serialized_request;
}

Expected<std::tuple<rpc_object_handle_t, uint32_t>> SetSchedulerThresholdSerializer::deserialize_request(
    const MemoryView &serialized_request)
{
    ConfiguredInferModel_SetSchedulerThreshold_Request request;

    CHECK_AS_EXPECTED(request.ParseFromArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'SetSchedulerThreshold'");

    return std::make_tuple(request.configured_infer_model_handle().id(), request.threshold());
}

Expected<Buffer> SetSchedulerThresholdSerializer::serialize_reply(hailo_status status)
{
    ConfiguredInferModel_SetSchedulerThreshold_Reply reply;
    reply.set_status(status);

    TRY(auto serialized_reply, Buffer::create(reply.ByteSizeLong(), BufferStorageParams::create_dma()));

    CHECK_AS_EXPECTED(reply.SerializeToArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'SetSchedulerThreshold'");

    return serialized_reply;
}

hailo_status SetSchedulerThresholdSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    ConfiguredInferModel_SetSchedulerThreshold_Reply reply;

    CHECK_AS_EXPECTED(reply.ParseFromArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'SetSchedulerThreshold'");

    return static_cast<hailo_status>(reply.status());
}

Expected<Buffer> SetSchedulerPrioritySerializer::serialize_request(rpc_object_handle_t configured_infer_model_handle, uint32_t priority)
{
    ConfiguredInferModel_SetSchedulerPriority_Request request;

    auto proto_configured_infer_model_handle = request.mutable_configured_infer_model_handle();
    proto_configured_infer_model_handle->set_id(configured_infer_model_handle);
    request.set_priority(priority);

    // TODO (HRT-13983) - check if we can use GetCachedSize
    TRY(auto serialized_request, Buffer::create(request.ByteSizeLong(), BufferStorageParams::create_dma()));
    CHECK_AS_EXPECTED(request.SerializeToArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'SetSchedulerPriority'");

    return serialized_request;
}

Expected<std::tuple<rpc_object_handle_t, uint32_t>> SetSchedulerPrioritySerializer::deserialize_request(
    const MemoryView &serialized_request)
{
    ConfiguredInferModel_SetSchedulerPriority_Request request;

    CHECK_AS_EXPECTED(request.ParseFromArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'SetSchedulerPriority'");

    return std::make_tuple(request.configured_infer_model_handle().id(), request.priority());
}

Expected<Buffer> SetSchedulerPrioritySerializer::serialize_reply(hailo_status status)
{
    ConfiguredInferModel_SetSchedulerPriority_Reply reply;
    reply.set_status(status);

    TRY(auto serialized_reply, Buffer::create(reply.ByteSizeLong(), BufferStorageParams::create_dma()));

    CHECK_AS_EXPECTED(reply.SerializeToArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'SetSchedulerPriority'");

    return serialized_reply;
}

hailo_status SetSchedulerPrioritySerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    ConfiguredInferModel_SetSchedulerPriority_Reply reply;

    CHECK_AS_EXPECTED(reply.ParseFromArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'SetSchedulerPriority'");

    return static_cast<hailo_status>(reply.status());
}

Expected<Buffer> GetHwLatencyMeasurementSerializer::serialize_request(rpc_object_handle_t configured_infer_model_handle)
{
    ConfiguredInferModel_GetHwLatencyMeasurement_Request request;

    auto proto_configured_infer_model_handle = request.mutable_configured_infer_model_handle();
    proto_configured_infer_model_handle->set_id(configured_infer_model_handle);

    // TODO (HRT-13983) - check if we can use GetCachedSize
    TRY(auto serialized_request, Buffer::create(request.ByteSizeLong(), BufferStorageParams::create_dma()));
    CHECK_AS_EXPECTED(request.SerializeToArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'GetHwLatencyMeasurement'");

    return serialized_request;
}

Expected<rpc_object_handle_t> GetHwLatencyMeasurementSerializer::deserialize_request(const MemoryView &serialized_request)
{
    ConfiguredInferModel_GetHwLatencyMeasurement_Request request;

    CHECK_AS_EXPECTED(request.ParseFromArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'GetHwLatencyMeasurement'");

    return request.configured_infer_model_handle().id();
}

Expected<Buffer> GetHwLatencyMeasurementSerializer::serialize_reply(hailo_status status, uint32_t avg_hw_latency)
{
    ConfiguredInferModel_GetHwLatencyMeasurement_Reply reply;
    reply.set_status(status);
    reply.set_avg_hw_latency(avg_hw_latency);

    TRY(auto serialized_reply, Buffer::create(reply.ByteSizeLong(), BufferStorageParams::create_dma()));

    CHECK_AS_EXPECTED(reply.SerializeToArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'GetHwLatencyMeasurement'");

    return serialized_reply;
}

Expected<std::tuple<hailo_status, std::chrono::nanoseconds>> GetHwLatencyMeasurementSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    ConfiguredInferModel_GetHwLatencyMeasurement_Reply reply;

    CHECK_AS_EXPECTED(reply.ParseFromArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'GetHwLatencyMeasurement'");

    return std::make_tuple(static_cast<hailo_status>(reply.status()), std::chrono::nanoseconds(reply.avg_hw_latency()));
}

Expected<Buffer> ActivateSerializer::serialize_request(rpc_object_handle_t configured_infer_model_handle)
{
    ConfiguredInferModel_Activate_Request request;

    auto proto_configured_infer_model_handle = request.mutable_configured_infer_model_handle();
    proto_configured_infer_model_handle->set_id(configured_infer_model_handle);

    // TODO (HRT-13983) - check if we can use GetCachedSize
    TRY(auto serialized_request, Buffer::create(request.ByteSizeLong(), BufferStorageParams::create_dma()));
    CHECK_AS_EXPECTED(request.SerializeToArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'Activate'");

    return serialized_request;
}

Expected<rpc_object_handle_t> ActivateSerializer::deserialize_request(const MemoryView &serialized_request)
{
    ConfiguredInferModel_Activate_Request request;

    CHECK_AS_EXPECTED(request.ParseFromArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'Activate'");

    return request.configured_infer_model_handle().id();
}

Expected<Buffer> ActivateSerializer::serialize_reply(hailo_status status)
{
    ConfiguredInferModel_Activate_Reply reply;
    reply.set_status(status);

    TRY(auto serialized_reply, Buffer::create(reply.ByteSizeLong(), BufferStorageParams::create_dma()));

    CHECK_AS_EXPECTED(reply.SerializeToArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'Activate'");

    return serialized_reply;
}

hailo_status ActivateSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    ConfiguredInferModel_Activate_Reply reply;

    CHECK_AS_EXPECTED(reply.ParseFromArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'Activate'");

    return static_cast<hailo_status>(reply.status());
}

Expected<Buffer> DeactivateSerializer::serialize_request(rpc_object_handle_t configured_infer_model_handle)
{
    ConfiguredInferModel_Deactivate_Request request;

    auto proto_configured_infer_model_handle = request.mutable_configured_infer_model_handle();
    proto_configured_infer_model_handle->set_id(configured_infer_model_handle);

    // TODO (HRT-13983) - check if we can use GetCachedSize
    TRY(auto serialized_request, Buffer::create(request.ByteSizeLong(), BufferStorageParams::create_dma()));
    CHECK_AS_EXPECTED(request.SerializeToArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'Deactivate'");

    return serialized_request;
}

Expected<rpc_object_handle_t> DeactivateSerializer::deserialize_request(const MemoryView &serialized_request)
{
    ConfiguredInferModel_Deactivate_Request request;

    CHECK_AS_EXPECTED(request.ParseFromArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'Deactivate'");

    return request.configured_infer_model_handle().id();
}

Expected<Buffer> DeactivateSerializer::serialize_reply(hailo_status status)
{
    ConfiguredInferModel_Deactivate_Reply reply;
    reply.set_status(status);

    TRY(auto serialized_reply, Buffer::create(reply.ByteSizeLong(), BufferStorageParams::create_dma()));

    CHECK_AS_EXPECTED(reply.SerializeToArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'Deactivate'");

    return serialized_reply;
}

hailo_status DeactivateSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    ConfiguredInferModel_Deactivate_Reply reply;

    CHECK_AS_EXPECTED(reply.ParseFromArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'Deactivate'");

    return static_cast<hailo_status>(reply.status());
}

Expected<Buffer> ShutdownSerializer::serialize_request(rpc_object_handle_t configured_infer_model_handle)
{
    ConfiguredInferModel_Shutdown_Request request;

    auto proto_configured_infer_model_handle = request.mutable_configured_infer_model_handle();
    proto_configured_infer_model_handle->set_id(configured_infer_model_handle);

    // TODO (HRT-13983) - check if we can use GetCachedSize
    TRY(auto serialized_request, Buffer::create(request.ByteSizeLong(), BufferStorageParams::create_dma()));
    CHECK_AS_EXPECTED(request.SerializeToArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'Shutdown'");

    return serialized_request;
}

Expected<rpc_object_handle_t> ShutdownSerializer::deserialize_request(const MemoryView &serialized_request)
{
    ConfiguredInferModel_Shutdown_Request request;

    CHECK_AS_EXPECTED(request.ParseFromArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'Shutdown'");

    return request.configured_infer_model_handle().id();
}

Expected<Buffer> ShutdownSerializer::serialize_reply(hailo_status status)
{
    ConfiguredInferModel_Shutdown_Reply reply;
    reply.set_status(status);

    TRY(auto serialized_reply, Buffer::create(reply.ByteSizeLong(), BufferStorageParams::create_dma()));

    CHECK_AS_EXPECTED(reply.SerializeToArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'Shutdown'");

    return serialized_reply;
}

hailo_status ShutdownSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    ConfiguredInferModel_Shutdown_Reply reply;

    CHECK_AS_EXPECTED(reply.ParseFromArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'Shutdown'");

    return static_cast<hailo_status>(reply.status());
}

Expected<Buffer> RunAsyncSerializer::serialize_request(rpc_object_handle_t configured_infer_model_handle, rpc_object_handle_t infer_model_handle,
    rpc_object_handle_t callback_handle)
{
    ConfiguredInferModel_AsyncInfer_Request request;

    auto proto_configured_infer_model_handle = request.mutable_configured_infer_model_handle();
    proto_configured_infer_model_handle->set_id(configured_infer_model_handle);

    auto proto_infer_model_handle = request.mutable_infer_model_handle();
    proto_infer_model_handle->set_id(infer_model_handle);

    auto proto_cb_handle = request.mutable_callback_handle();
    proto_cb_handle->set_id(callback_handle);

    // TODO (HRT-13983) - check if we can use GetCachedSize
    TRY(auto serialized_request, Buffer::create(request.ByteSizeLong(), BufferStorageParams::create_dma()));
    CHECK_AS_EXPECTED(request.SerializeToArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'RunAsync'");

    return serialized_request;
}

Expected<std::tuple<rpc_object_handle_t, rpc_object_handle_t, rpc_object_handle_t>> RunAsyncSerializer::deserialize_request(
    const MemoryView &serialized_request)
{
    ConfiguredInferModel_AsyncInfer_Request request;

    CHECK_AS_EXPECTED(request.ParseFromArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'RunAsync'");

    return std::make_tuple(request.configured_infer_model_handle().id(), request.infer_model_handle().id(),
        request.callback_handle().id());
}

Expected<Buffer> RunAsyncSerializer::serialize_reply(hailo_status status)
{
    ConfiguredInferModel_AsyncInfer_Reply reply;
    reply.set_status(status);

    TRY(auto serialized_reply, Buffer::create(reply.ByteSizeLong(), BufferStorageParams::create_dma()));

    CHECK_AS_EXPECTED(reply.SerializeToArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'RunAsync'");

    return serialized_reply;
}

hailo_status RunAsyncSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    ConfiguredInferModel_AsyncInfer_Reply reply;

    CHECK(reply.ParseFromArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'RunAsync'");

    return static_cast<hailo_status>(reply.status());
}

Expected<Buffer> CallbackCalledSerializer::serialize_reply(hailo_status status, rpc_object_handle_t callback_handle)
{
    CallbackCalled_Reply reply;

    reply.set_status(status);
    auto proto_callback_handle = reply.mutable_callback_handle();
    proto_callback_handle->set_id(callback_handle);

    TRY(auto serialized_reply, Buffer::create(reply.ByteSizeLong(), BufferStorageParams::create_dma()));

    CHECK_AS_EXPECTED(reply.SerializeToArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'CallbackCalled'");

    return serialized_reply;
}

Expected<std::tuple<hailo_status, rpc_object_handle_t>> CallbackCalledSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    CallbackCalled_Reply reply;

    CHECK_AS_EXPECTED(reply.ParseFromArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'CallbackCalled'");

    return std::make_tuple(static_cast<hailo_status>(reply.status()), reply.callback_handle().id());
}

} /* namespace hailort */
