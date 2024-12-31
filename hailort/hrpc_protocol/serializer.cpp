/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file serializer.cpp
 * @brief HRPC Serialization implementation
 **/

#include "serializer.hpp"
#include "hailo/buffer.hpp"
#include "hailo/hailort.h"
#include "hailo/hailort_common.hpp"
#include "common/utils.hpp"
#include <cstdint>
#include <tuple>

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

    return get_serialized_request<VDevice_Create_Request>(request, "CreateVDevice");
}

Expected<SerializerVDeviceParamsWrapper> CreateVDeviceSerializer::deserialize_request(const MemoryView &serialized_request)
{
    VDevice_Create_Request request;

    CHECK_AS_EXPECTED(request.ParseFromArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'CreateVDevice'");

    bool multi_process_service_flag = false;
    SerializerVDeviceParamsWrapper params( 
        1,
        nullptr,
        static_cast<hailo_scheduling_algorithm_e>(request.params().scheduling_algorithm()),
        request.params().group_id(),
        multi_process_service_flag);

    return params;
}

Expected<Buffer> CreateVDeviceSerializer::serialize_reply(hailo_status status, rpc_object_handle_t vdevice_handle)
{
    VDevice_Create_Reply reply;

    reply.set_status(status);
    auto proto_vdevice_handle = reply.mutable_vdevice_handle();
    proto_vdevice_handle->set_id(vdevice_handle);

    return get_serialized_reply<VDevice_Create_Reply>(reply, "CreateVDevice");

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

    // TODO (HRT-14732) - check if we can use GetCachedSize
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
    return get_deserialized_status_only_reply<VDevice_Destroy_Reply>(
        serialized_reply, "DestroyVDevice");
}

Expected<Buffer> CreateInferModelSerializer::serialize_request(rpc_object_handle_t vdevice_handle, uint64_t hef_size, const std::string &name)
{
    VDevice_CreateInferModel_Request request;

    auto proto_vdevice_handle = request.mutable_vdevice_handle();
    proto_vdevice_handle->set_id(vdevice_handle);
    request.set_hef_size(hef_size);
    request.set_name(name);

    // TODO (HRT-14732) - check if we can use GetCachedSize
    TRY(auto serialized_request, Buffer::create(request.ByteSizeLong(), BufferStorageParams::create_dma()));
    CHECK_AS_EXPECTED(request.SerializeToArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'CreateVInferModel'");

    return serialized_request;
}

Expected<std::tuple<rpc_object_handle_t, uint64_t, std::string>> CreateInferModelSerializer::deserialize_request(const MemoryView &serialized_request)
{
    VDevice_CreateInferModel_Request request;

    CHECK_AS_EXPECTED(request.ParseFromArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'CreateVInferModel'");

    return std::make_tuple(request.vdevice_handle().id(), request.hef_size(), request.name());
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

    // TODO (HRT-14732) - check if we can use GetCachedSize
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
    return get_deserialized_status_only_reply<InferModel_Destroy_Reply>(
        serialized_reply, "DestroyInferModel");
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
        proto_input_stream->set_nms_max_proposals_total(input_stream_params.second.nms_max_proposals_total);
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
        proto_output_stream->set_nms_max_proposals_total(output_stream_params.second.nms_max_proposals_total);
    }

    request.set_batch_size(static_cast<uint32_t>(params.batch_size));
    request.set_power_mode(static_cast<uint32_t>(params.power_mode));
    request.set_latency_flag(static_cast<uint32_t>(params.latency_flag));

    // TODO (HRT-14732) - check if we can use GetCachedSize
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
        current_stream_params.nms_max_proposals_total = input_stream.nms_max_proposals_total();
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
        current_stream_params.nms_max_proposals_total = output_stream.nms_max_proposals_total();
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

    // TODO (HRT-14732) - check if we can use GetCachedSize
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
    return get_deserialized_status_only_reply<ConfiguredInferModel_Destroy_Reply>(
        serialized_reply, "DestroyConfiguredInferModel");
}

Expected<Buffer> SetSchedulerTimeoutSerializer::serialize_request(rpc_object_handle_t configured_infer_model_handle, const std::chrono::milliseconds &timeout)
{
    ConfiguredInferModel_SetSchedulerTimeout_Request request;

    auto proto_configured_infer_model_handle = request.mutable_configured_infer_model_handle();
    proto_configured_infer_model_handle->set_id(configured_infer_model_handle);
    request.set_timeout(static_cast<uint32_t>(timeout.count()));

    // TODO (HRT-14732) - check if we can use GetCachedSize
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
    return get_deserialized_status_only_reply<ConfiguredInferModel_SetSchedulerTimeout_Reply>(
        serialized_reply, "SetSchedulerTimeout");
}

Expected<Buffer> SetSchedulerThresholdSerializer::serialize_request(rpc_object_handle_t configured_infer_model_handle, uint32_t threshold)
{
    ConfiguredInferModel_SetSchedulerThreshold_Request request;

    auto proto_configured_infer_model_handle = request.mutable_configured_infer_model_handle();
    proto_configured_infer_model_handle->set_id(configured_infer_model_handle);
    request.set_threshold(threshold);

    // TODO (HRT-14732) - check if we can use GetCachedSize
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
    return get_deserialized_status_only_reply<ConfiguredInferModel_SetSchedulerThreshold_Reply>(
        serialized_reply, "SetSchedulerThreshold");
}

Expected<Buffer> SetSchedulerPrioritySerializer::serialize_request(rpc_object_handle_t configured_infer_model_handle, uint32_t priority)
{
    ConfiguredInferModel_SetSchedulerPriority_Request request;

    auto proto_configured_infer_model_handle = request.mutable_configured_infer_model_handle();
    proto_configured_infer_model_handle->set_id(configured_infer_model_handle);
    request.set_priority(priority);

    // TODO (HRT-14732) - check if we can use GetCachedSize
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
    return get_deserialized_status_only_reply<ConfiguredInferModel_SetSchedulerPriority_Reply>(
        serialized_reply, "SetSchedulerPriority");
}

Expected<Buffer> GetHwLatencyMeasurementSerializer::serialize_request(rpc_object_handle_t configured_infer_model_handle)
{
    ConfiguredInferModel_GetHwLatencyMeasurement_Request request;

    auto proto_configured_infer_model_handle = request.mutable_configured_infer_model_handle();
    proto_configured_infer_model_handle->set_id(configured_infer_model_handle);

    // TODO (HRT-14732) - check if we can use GetCachedSize
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

    // TODO (HRT-14732) - check if we can use GetCachedSize
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
    return get_deserialized_status_only_reply<ConfiguredInferModel_Activate_Reply>(
        serialized_reply, "Activate");
}

Expected<Buffer> DeactivateSerializer::serialize_request(rpc_object_handle_t configured_infer_model_handle)
{
    ConfiguredInferModel_Deactivate_Request request;

    auto proto_configured_infer_model_handle = request.mutable_configured_infer_model_handle();
    proto_configured_infer_model_handle->set_id(configured_infer_model_handle);

    // TODO (HRT-14732) - check if we can use GetCachedSize
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
    return get_deserialized_status_only_reply<ConfiguredInferModel_Deactivate_Reply>(
        serialized_reply, "Deactivate");
}

Expected<Buffer> ShutdownSerializer::serialize_request(rpc_object_handle_t configured_infer_model_handle)
{
    ConfiguredInferModel_Shutdown_Request request;

    auto proto_configured_infer_model_handle = request.mutable_configured_infer_model_handle();
    proto_configured_infer_model_handle->set_id(configured_infer_model_handle);

    // TODO (HRT-14732) - check if we can use GetCachedSize
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
    return get_deserialized_status_only_reply<ConfiguredInferModel_Shutdown_Reply>(
        serialized_reply, "Shutdown");
}

Expected<Buffer> RunAsyncSerializer::serialize_request(const RunAsyncSerializer::Request &request_struct)
{
    ConfiguredInferModel_AsyncInfer_Request request;

    auto proto_configured_infer_model_handle = request.mutable_configured_infer_model_handle();
    proto_configured_infer_model_handle->set_id(request_struct.configured_infer_model_handle);

    auto proto_infer_model_handle = request.mutable_infer_model_handle();
    proto_infer_model_handle->set_id(request_struct.infer_model_handle);

    auto proto_cb_handle = request.mutable_callback_handle();
    proto_cb_handle->set_id(request_struct.callback_handle);

    *request.mutable_input_buffer_sizes() = {request_struct.input_buffer_sizes.begin(), request_struct.input_buffer_sizes.end()};

    // TODO (HRT-14732) - check if we can use GetCachedSize
    TRY(auto serialized_request, Buffer::create(request.ByteSizeLong(), BufferStorageParams::create_dma()));
    CHECK_AS_EXPECTED(request.SerializeToArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'RunAsync'");

    return serialized_request;
}

Expected<RunAsyncSerializer::Request> RunAsyncSerializer::deserialize_request(
    const MemoryView &serialized_request)
{
    ConfiguredInferModel_AsyncInfer_Request request;

    CHECK_AS_EXPECTED(request.ParseFromArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'RunAsync'");

    std::vector<uint32_t> input_buffer_sizes(request.input_buffer_sizes().begin(), request.input_buffer_sizes().end());

    RunAsyncSerializer::Request request_struct;
    request_struct.configured_infer_model_handle = request.configured_infer_model_handle().id();
    request_struct.infer_model_handle = request.infer_model_handle().id();
    request_struct.callback_handle = request.callback_handle().id();
    request_struct.input_buffer_sizes = input_buffer_sizes;
    return request_struct;
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
    return get_deserialized_status_only_reply<ConfiguredInferModel_AsyncInfer_Reply>(
        serialized_reply, "RunAsync");
}

Expected<Buffer> CallbackCalledSerializer::serialize_reply(hailo_status status, rpc_object_handle_t callback_handle,
    rpc_object_handle_t configured_infer_model_handle)
{
    CallbackCalled_Reply reply;

    reply.set_status(status);
    auto proto_callback_handle = reply.mutable_callback_handle();
    proto_callback_handle->set_id(callback_handle);

    auto proto_cim_handle = reply.mutable_configured_infer_model_handle();
    proto_cim_handle->set_id(configured_infer_model_handle);

    TRY(auto serialized_reply, Buffer::create(reply.ByteSizeLong(), BufferStorageParams::create_dma()));

    CHECK_AS_EXPECTED(reply.SerializeToArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'CallbackCalled'");

    return serialized_reply;
}

Expected<std::tuple<hailo_status, rpc_object_handle_t, rpc_object_handle_t>>
CallbackCalledSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    CallbackCalled_Reply reply;

    CHECK_AS_EXPECTED(reply.ParseFromArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'CallbackCalled'");

    return std::make_tuple(static_cast<hailo_status>(reply.status()), reply.callback_handle().id(),
        reply.configured_infer_model_handle().id());
}

Expected<Buffer> CreateDeviceSerializer::serialize_request()
{
    Device_Create_Request request;

    // TODO (HRT-14732) - check if we can use GetCachedSize
    TRY(auto serialized_request, Buffer::create(request.ByteSizeLong(), BufferStorageParams::create_dma()));

    CHECK_AS_EXPECTED(request.SerializeToArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'CreateDevice'");

    return serialized_request;
}

hailo_status CreateDeviceSerializer::deserialize_request(const MemoryView &serialized_request)
{
    Device_Create_Request request;

    CHECK_AS_EXPECTED(request.ParseFromArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'CreateDevice'");

    return HAILO_SUCCESS;
}

Expected<Buffer> CreateDeviceSerializer::serialize_reply(hailo_status status, rpc_object_handle_t device_handle)
{
    Device_Create_Reply reply;

    reply.set_status(status);
    auto proto_device_handle = reply.mutable_device_handle();
    proto_device_handle->set_id(device_handle);

    TRY(auto serialized_reply, Buffer::create(reply.ByteSizeLong(), BufferStorageParams::create_dma()));

    CHECK_AS_EXPECTED(reply.SerializeToArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'CreateDevice'");

    return serialized_reply;
}

Expected<std::tuple<hailo_status, rpc_object_handle_t>> CreateDeviceSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    Device_Create_Reply reply;

    CHECK_AS_EXPECTED(reply.ParseFromArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'CreateDevice'");

    return std::make_tuple(static_cast<hailo_status>(reply.status()), reply.device_handle().id());
}

Expected<Buffer> DestroyDeviceSerializer::serialize_request(rpc_object_handle_t device_handle)
{
    Device_Destroy_Request request;

    auto proto_device_handle= request.mutable_device_handle();
    proto_device_handle->set_id(device_handle);

    // TODO (HRT-14732) - check if we can use GetCachedSize
    TRY(auto serialized_request, Buffer::create(request.ByteSizeLong(), BufferStorageParams::create_dma()));
    CHECK_AS_EXPECTED(request.SerializeToArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'DestroyDevice'");

    return serialized_request;
}

Expected<rpc_object_handle_t> DestroyDeviceSerializer::deserialize_request(const MemoryView &serialized_request)
{
    Device_Destroy_Request request;

    CHECK_AS_EXPECTED(request.ParseFromArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'DestroyDevice'");

    return request.device_handle().id();
}

Expected<Buffer> DestroyDeviceSerializer::serialize_reply(hailo_status status)
{
    Device_Destroy_Reply reply;
    reply.set_status(status);

    TRY(auto serialized_reply, Buffer::create(reply.ByteSizeLong(), BufferStorageParams::create_dma()));

    CHECK_AS_EXPECTED(reply.SerializeToArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'DestroyDevice'");

    return serialized_reply;
}

hailo_status DestroyDeviceSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    return get_deserialized_status_only_reply<Device_Destroy_Reply>(
        serialized_reply, "DestroyDevice");
}

Expected<Buffer> IdentifyDeviceSerializer::serialize_request(rpc_object_handle_t device_handle)
{
    Device_Identify_Request request;

    auto proto_device_handle = request.mutable_device_handle();
    proto_device_handle->set_id(device_handle);

    // TODO (HRT-14732) - check if we can use GetCachedSize
    TRY(auto serialized_request, Buffer::create(request.ByteSizeLong(), BufferStorageParams::create_dma()));
    CHECK_AS_EXPECTED(request.SerializeToArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'IdentifyDevice'");

    return serialized_request;
}

Expected<rpc_object_handle_t> IdentifyDeviceSerializer::deserialize_request(const MemoryView &serialized_request)
{
    Device_Identify_Request request;

    CHECK_AS_EXPECTED(request.ParseFromArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'IdentifyDevice'");

    return request.device_handle().id();
}

Expected<Buffer> IdentifyDeviceSerializer::serialize_reply(hailo_status status, const hailo_device_identity_t &identity)
{
    Device_Identify_Reply reply;

    reply.set_status(status);
    auto proto_identity = reply.mutable_identity();
    proto_identity->set_protocol_version(identity.protocol_version);
    proto_identity->set_logger_version(identity.logger_version);
    proto_identity->set_board_name(identity.board_name);
    proto_identity->set_is_release(identity.is_release);
    proto_identity->set_extended_context_switch_buffer(identity.extended_context_switch_buffer);
    proto_identity->set_device_architecture(static_cast<DeviceArchitectureProto>(identity.device_architecture));

    auto mut_serial_number = proto_identity->mutable_serial_number();
    for (uint8_t i = 0; i < identity.serial_number_length; i++) {
        mut_serial_number->Add(identity.serial_number[i]);
    }
    auto mut_part_number = proto_identity->mutable_part_number();
    for (uint8_t i = 0; i < identity.part_number_length; i++) {
        mut_part_number->Add(identity.part_number[i]);
    }
    proto_identity->set_product_name(identity.product_name);

    auto fw_version = proto_identity->mutable_fw_version();
    fw_version->set_major_value(identity.fw_version.major);
    fw_version->set_minor_value(identity.fw_version.minor);
    fw_version->set_revision_value(identity.fw_version.revision);

    TRY(auto serialized_reply, Buffer::create(reply.ByteSizeLong(), BufferStorageParams::create_dma()));

    CHECK_AS_EXPECTED(reply.SerializeToArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to serialize 'IdentifyDevice'");

    return serialized_reply;
}

Expected<std::tuple<hailo_status, hailo_device_identity_t>> IdentifyDeviceSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    Device_Identify_Reply reply;

    CHECK_AS_EXPECTED(reply.ParseFromArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'IdentifyDevice'");

    hailo_device_identity_t identity = {};

    identity.protocol_version = reply.identity().protocol_version();
    identity.logger_version = reply.identity().logger_version();
    identity.is_release = reply.identity().is_release();
    identity.extended_context_switch_buffer = reply.identity().extended_context_switch_buffer();
    identity.device_architecture = static_cast<hailo_device_architecture_t>(reply.identity().device_architecture());

    std::memcpy(identity.board_name, reply.identity().board_name().c_str(), reply.identity().board_name().size());
    identity.board_name_length = static_cast<uint8_t>(reply.identity().board_name().size());

    std::transform(reply.identity().serial_number().begin(), reply.identity().serial_number().end(), identity.serial_number, [](uint32_t val) {
        return static_cast<uint8_t>(val);
    });
    identity.part_number_length = static_cast<uint8_t>(reply.identity().part_number().size());
    std::transform(reply.identity().part_number().begin(), reply.identity().part_number().end(), identity.part_number, [](uint32_t val) {
        return static_cast<uint8_t>(val);
    });
    identity.part_number_length = static_cast<uint8_t>(reply.identity().serial_number().size());

    std::memcpy(identity.product_name, reply.identity().product_name().c_str(), reply.identity().product_name().size());
    identity.product_name_length = static_cast<uint8_t>(reply.identity().product_name().size());

    identity.fw_version.major = reply.identity().fw_version().major_value();
    identity.fw_version.minor = reply.identity().fw_version().minor_value();
    identity.fw_version.revision = reply.identity().fw_version().revision_value();

    return std::make_tuple(static_cast<hailo_status>(reply.status()), identity);
}

Expected<Buffer> ExtendedDeviceInfoSerializer::serialize_request(rpc_object_handle_t device_handle)
{
    Device_ExtendedInfo_Request request;

    auto proto_device_handle = request.mutable_device_handle();
    proto_device_handle->set_id(device_handle);

    return get_serialized_request<Device_ExtendedInfo_Request>(request, "ExtendedDeviceInfo");
}

Expected<rpc_object_handle_t> ExtendedDeviceInfoSerializer::deserialize_request(const MemoryView &serialized_request)
{
    return get_deserialized_request<Device_ExtendedInfo_Request>(serialized_request, "ExtendedDeviceInfo");
}

Expected<Buffer> ExtendedDeviceInfoSerializer::serialize_reply(hailo_status status, const hailo_extended_device_information_t &extended_info)
{
    Device_ExtendedInfo_Reply reply;

    reply.set_status(status);
    reply.set_neural_network_core_clock_rate(extended_info.neural_network_core_clock_rate);

    auto supported_features = reply.mutable_supported_features();
    supported_features->set_ethernet(extended_info.supported_features.ethernet);
    supported_features->set_mipi(extended_info.supported_features.mipi);
    supported_features->set_pcie(extended_info.supported_features.pcie);
    supported_features->set_current_monitoring(extended_info.supported_features.current_monitoring);
    supported_features->set_mdio(extended_info.supported_features.mdio);

    reply.set_boot_source(static_cast<DeviceBootSourceProto>(extended_info.boot_source));

    auto soc_id = reply.mutable_soc_id();
    for (auto i = 0; i < HAILO_SOC_ID_LENGTH; i++) {
        soc_id->Add(extended_info.soc_id[i]);
    }

    reply.set_lcs(extended_info.lcs);

    auto eth_mac_address = reply.mutable_eth_mac_address();
    for (auto i = 0; i < HAILO_ETH_MAC_LENGTH; i++) {
        eth_mac_address->Add(extended_info.eth_mac_address[i]);
    }

    auto unit_level_tracking_id = reply.mutable_unit_level_tracking_id();
    for (auto i = 0; i < HAILO_UNIT_LEVEL_TRACKING_BYTES_LENGTH; i++) {
        unit_level_tracking_id->Add(extended_info.unit_level_tracking_id[i]);
    }

    auto soc_pm_values = reply.mutable_soc_pm_values();
    for (auto i = 0; i < HAILO_SOC_PM_VALUES_BYTES_LENGTH; i++) {
        soc_pm_values->Add(extended_info.soc_pm_values[i]);
    }

    return get_serialized_reply<Device_ExtendedInfo_Reply>(reply, "ExtendedDeviceInfo");
}

Expected<std::tuple<hailo_status, hailo_extended_device_information_t>> ExtendedDeviceInfoSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    Device_ExtendedInfo_Reply reply;

    CHECK_AS_EXPECTED(reply.ParseFromArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'ExtendedDeviceInfo'");

    hailo_extended_device_information_t extended_info = {};

    extended_info.neural_network_core_clock_rate = reply.neural_network_core_clock_rate();
    extended_info.supported_features.ethernet = reply.supported_features().ethernet();
    extended_info.supported_features.mipi = reply.supported_features().mipi();
    extended_info.supported_features.pcie = reply.supported_features().pcie();
    extended_info.supported_features.current_monitoring = reply.supported_features().current_monitoring();
    extended_info.supported_features.mdio = reply.supported_features().mdio();
    extended_info.boot_source = static_cast<hailo_device_boot_source_t>(reply.boot_source());
    std::transform(reply.soc_id().begin(), reply.soc_id().end(), extended_info.soc_id, [](uint32_t val) {
        return static_cast<uint8_t>(val);
    });
    extended_info.lcs = static_cast<uint8_t>(reply.lcs());

    // Ensure that the sizes of the input and output arrays match before transformation
    assert(reply.eth_mac_address().size() == HAILO_ETH_MAC_LENGTH);
    std::transform(reply.eth_mac_address().begin(), reply.eth_mac_address().begin() + HAILO_ETH_MAC_LENGTH,
        extended_info.eth_mac_address, [](uint32_t val) { return static_cast<uint8_t>(val); });

    assert(reply.unit_level_tracking_id().size() == HAILO_UNIT_LEVEL_TRACKING_BYTES_LENGTH);
    std::transform(reply.unit_level_tracking_id().begin(), reply.unit_level_tracking_id().begin() + HAILO_UNIT_LEVEL_TRACKING_BYTES_LENGTH,
        extended_info.unit_level_tracking_id, [](uint32_t val) { return static_cast<uint8_t>(val); });

    assert(reply.soc_pm_values().size() == HAILO_SOC_PM_VALUES_BYTES_LENGTH);
    std::transform(reply.soc_pm_values().begin(), reply.soc_pm_values().begin() + HAILO_SOC_PM_VALUES_BYTES_LENGTH, 
        extended_info.soc_pm_values, [](uint32_t val) { return static_cast<uint8_t>(val); });

    return std::make_tuple(static_cast<hailo_status>(reply.status()), extended_info);
}


Expected<Buffer> GetChipTemperatureSerializer::serialize_request(rpc_object_handle_t device_handle)
{
    Device_GetChipTemperature_Request request;

    auto proto_device_handle = request.mutable_device_handle();
    proto_device_handle->set_id(device_handle);

    return get_serialized_request<Device_GetChipTemperature_Request>(request, "GetChipTemperature");
}

Expected<rpc_object_handle_t> GetChipTemperatureSerializer::deserialize_request(const MemoryView &serialized_request)
{
    return get_deserialized_request<Device_GetChipTemperature_Request>(serialized_request, "GetChipTemperature");
}

Expected<Buffer> GetChipTemperatureSerializer::serialize_reply(hailo_status status, const hailo_chip_temperature_info_t &info)
{
    Device_GetChipTemperature_Reply reply;

    reply.set_status(status);
    reply.set_ts0_temperature(info.ts0_temperature);
    reply.set_ts1_temperature(info.ts1_temperature);
    reply.set_sample_count(info.sample_count);

    return get_serialized_reply<Device_GetChipTemperature_Reply>(reply, "GetChipTemperature");
}

Expected<std::tuple<hailo_status, hailo_chip_temperature_info_t>> GetChipTemperatureSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    Device_GetChipTemperature_Reply reply;
    CHECK_AS_EXPECTED(reply.ParseFromArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'GetChipTemperature'");
    hailo_chip_temperature_info_t info = {};
    info.ts0_temperature = reply.ts0_temperature();
    info.ts1_temperature = reply.ts1_temperature();
    info.sample_count = static_cast<uint16_t>(reply.sample_count());
    return std::make_tuple(static_cast<hailo_status>(reply.status()), info);
}

Expected<Buffer> PowerMeasurementSerializer::serialize_request(rpc_object_handle_t device_handle, uint32_t hailo_dvm_options, uint32_t hailo_power_measurement_type)
{
    Device_PowerMeasurement_Request request;

    auto proto_device_handle = request.mutable_device_handle();
    proto_device_handle->set_id(device_handle);
    request.set_hailo_dvm_options(hailo_dvm_options);
    request.set_hailo_power_measurement_type(hailo_power_measurement_type);

    return get_serialized_request<Device_PowerMeasurement_Request>(request, "PowerMeasurement");
}

Expected<std::tuple<rpc_object_handle_t, uint32_t, uint32_t>> PowerMeasurementSerializer::deserialize_request(const MemoryView &serialized_request)
{
    Device_PowerMeasurement_Request request;
    CHECK_AS_EXPECTED(request.ParseFromArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize '{}'", "PowerMeasurement");

    auto device_handle_id = request.device_handle().id();
    auto hailo_dvm_options = request.hailo_dvm_options();
    auto hailo_power_measurement_type = request.hailo_power_measurement_type();
    return std::make_tuple(device_handle_id, hailo_dvm_options, hailo_power_measurement_type);
}

Expected<Buffer> PowerMeasurementSerializer::serialize_reply(hailo_status status, const float32_t &power)
{
    Device_PowerMeasurement_Reply reply;

    reply.set_status(status);
    reply.set_power(power);

    return get_serialized_reply<Device_PowerMeasurement_Reply>(reply, "PowerMeasurement");
}

Expected<std::tuple<hailo_status, float32_t>> PowerMeasurementSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    Device_PowerMeasurement_Reply reply;
    CHECK_AS_EXPECTED(reply.ParseFromArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'PowerMeasurement'");
    auto power = reply.power();
    return std::make_tuple(static_cast<hailo_status>(reply.status()), power);
}

Expected<Buffer> SetPowerMeasurementSerializer::serialize_request(rpc_object_handle_t device_handle, uint32_t hailo_dvm_options, uint32_t hailo_power_measurement_type)
{
    Device_SetPowerMeasurement_Request request;

    auto proto_device_handle = request.mutable_device_handle();
    proto_device_handle->set_id(device_handle);
    request.set_hailo_dvm_options(hailo_dvm_options);
    request.set_hailo_power_measurement_type(hailo_power_measurement_type);

    return get_serialized_request<Device_SetPowerMeasurement_Request>(request, "SetPowerMeasurement");
}

Expected<std::tuple<rpc_object_handle_t, uint32_t, uint32_t>> SetPowerMeasurementSerializer::deserialize_request(const MemoryView &serialized_request)
{
    Device_SetPowerMeasurement_Request request;
    CHECK_AS_EXPECTED(request.ParseFromArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize '{}'", "SetPowerMeasurement");

    auto device_handle_id = request.device_handle().id();
    auto hailo_dvm_options = request.hailo_dvm_options();
    auto hailo_power_measurement_type = request.hailo_power_measurement_type();
    return std::make_tuple(device_handle_id, hailo_dvm_options, hailo_power_measurement_type);
}

Expected<Buffer> SetPowerMeasurementSerializer::serialize_reply(hailo_status status)
{
    Device_SetPowerMeasurement_Reply reply;
    reply.set_status(status);
    return get_serialized_reply<Device_SetPowerMeasurement_Reply>(reply, "SetPowerMeasurement");
}

hailo_status SetPowerMeasurementSerializer::deserialize_reply(const MemoryView &serialized_reply)
{
    return get_deserialized_status_only_reply<Device_SetPowerMeasurement_Reply>(serialized_reply, "SetPowerMeasurement");
}

Expected<Buffer> StartPowerMeasurementSerializer::serialize_request(
    rpc_object_handle_t device_handle, uint32_t averaging_factor,
    uint32_t sampling_period)
{
    Device_StartPowerMeasurement_Request request;

    auto proto_device_handle = request.mutable_device_handle();
    proto_device_handle->set_id(device_handle);
    request.set_averaging_factor(averaging_factor);
    request.set_sampling_period(sampling_period);

    return get_serialized_request<Device_StartPowerMeasurement_Request>(
        request, "StartPowerMeasurement");
}

Expected<std::tuple<rpc_object_handle_t, uint32_t, uint32_t>>
StartPowerMeasurementSerializer::deserialize_request(
    const MemoryView &serialized_request)
{
    Device_StartPowerMeasurement_Request request;
    auto parsed_request = request.ParseFromArray(serialized_request.data(), static_cast<int>(serialized_request.size()));
    CHECK_AS_EXPECTED(parsed_request, HAILO_RPC_FAILED, "Failed to de-serialize '{}'", "StartPowerMeasurement");

    auto device_handle_id = request.device_handle().id();
    auto averaging_factor = request.averaging_factor();
    auto sampling_period = request.sampling_period();
    return std::make_tuple(device_handle_id, averaging_factor, sampling_period);
}

Expected<Buffer> StartPowerMeasurementSerializer::serialize_reply(
    hailo_status status)
{
    Device_StartPowerMeasurement_Reply reply;
    reply.set_status(status);
    auto serialized_reply = get_serialized_reply<Device_StartPowerMeasurement_Reply>(reply, "StartPowerMeasurement");
    return serialized_reply;
}

hailo_status StartPowerMeasurementSerializer::deserialize_reply(
    const MemoryView &serialized_reply)
{
    return get_deserialized_status_only_reply<Device_StartPowerMeasurement_Reply>(
        serialized_reply, "StartPowerMeasurement");
}

Expected<Buffer> GetPowerMeasurementSerializer::serialize_request(
    rpc_object_handle_t device_handle, bool should_clear)
{
    Device_GetPowerMeasurement_Request request;

    auto proto_device_handle = request.mutable_device_handle();
    proto_device_handle->set_id(device_handle);
    request.set_should_clear(should_clear);

    return get_serialized_request<Device_GetPowerMeasurement_Request>(request, "GetPowerMeasurement");
}

Expected<std::tuple<rpc_object_handle_t, bool>> GetPowerMeasurementSerializer::deserialize_request(const MemoryView &serialized_request)
{
    Device_GetPowerMeasurement_Request request;
    CHECK_AS_EXPECTED(
        request.ParseFromArray(serialized_request.data(),
                               static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize '{}'", "GetPowerMeasurement");

    auto device_handle_id = request.device_handle().id();
    auto should_clear = request.should_clear();
    return std::make_tuple(device_handle_id, should_clear);
}

Expected<Buffer> GetPowerMeasurementSerializer::serialize_reply(
    hailo_status status, const hailo_power_measurement_data_t &data)
{
    Device_GetPowerMeasurement_Reply reply;

    reply.set_status(status);
    auto proto_data = reply.mutable_data();

    proto_data->set_average_value(data.average_value);
    proto_data->set_average_time_value_milliseconds(data.average_time_value_milliseconds);
    proto_data->set_min_value(data.min_value);
    proto_data->set_max_value(data.max_value);
    proto_data->set_total_number_of_samples(data.total_number_of_samples);

    return get_serialized_reply<Device_GetPowerMeasurement_Reply>(reply, "GetPowerMeasurement");
}

Expected<std::tuple<hailo_status, hailo_power_measurement_data_t>>
GetPowerMeasurementSerializer::deserialize_reply(
    const MemoryView &serialized_reply)
{
    Device_GetPowerMeasurement_Reply reply;
    CHECK_AS_EXPECTED(
        reply.ParseFromArray(serialized_reply.data(),
                             static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize 'GetPowerMeasurement'");

    auto data_serialized = reply.data();
    hailo_power_measurement_data_t data = {};
    data.average_value = data_serialized.average_value();
    data.average_time_value_milliseconds = data_serialized.average_time_value_milliseconds();
    data.min_value = data_serialized.min_value();
    data.max_value = data_serialized.max_value();
    data.total_number_of_samples = data_serialized.total_number_of_samples();

    return std::make_tuple(
        static_cast<hailo_status>(reply.status()), (data));
}

Expected<Buffer> StopPowerMeasurementSerializer::serialize_request(
    rpc_object_handle_t device_handle)
{
    Device_StopPowerMeasurement_Request request;

    auto proto_device_handle = request.mutable_device_handle();
    proto_device_handle->set_id(device_handle);

    return get_serialized_request<Device_StopPowerMeasurement_Request>(
        request, "StopPowerMeasurement");
}

Expected<rpc_object_handle_t> StopPowerMeasurementSerializer::deserialize_request(
    const MemoryView &serialized_request)
{
    return get_deserialized_request<Device_StopPowerMeasurement_Request>(
        serialized_request, "StopPowerMeasurement");
}

Expected<Buffer> StopPowerMeasurementSerializer::serialize_reply(
    hailo_status status)
{
    Device_StopPowerMeasurement_Reply reply;
    reply.set_status(status);
    return get_serialized_reply<Device_StopPowerMeasurement_Reply>(
        reply, "StopPowerMeasurement");
}

hailo_status StopPowerMeasurementSerializer::deserialize_reply(
    const MemoryView &serialized_reply)
{
    return get_deserialized_status_only_reply<Device_StopPowerMeasurement_Reply>(
        serialized_reply, "StopPowerMeasurement");
}

} /* namespace hailort */
