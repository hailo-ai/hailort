/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file serializer.hpp
 * @brief HRPC protocol serialization
 **/

#ifndef _HAILO_SERIALIZER_HPP_
#define _HAILO_SERIALIZER_HPP_

#include "hailo/hailort.h"
#include "hailo/buffer.hpp"
#include "hailo/expected.hpp"

#include <chrono>
#include <unordered_map>
#include <vector>

namespace hailort
{

#define INVALID_HANDLE_ID (UINT32_MAX)
#define INVALID_LATENCY_MEASUREMENT (UINT32_MAX)

enum class HailoRpcActionID {
    VDEVICE__CREATE,
    VDEVICE__DESTROY,
    VDEVICE__CREATE_INFER_MODEL,

    INFER_MODEL__DESTROY,
    INFER_MODEL__CREATE_CONFIGURED_INFER_MODEL,

    CONFIGURED_INFER_MODEL__DESTROY,
    CONFIGURED_INFER_MODEL__SET_SCHEDULER_TIMEOUT,
    CONFIGURED_INFER_MODEL__SET_SCHEDULER_THRESHOLD,
    CONFIGURED_INFER_MODEL__SET_SCHEDULER_PRIORITY,
    CONFIGURED_INFER_MODEL__GET_HW_LATENCY_MEASUREMENT,
    CONFIGURED_INFER_MODEL__ACTIVATE,
    CONFIGURED_INFER_MODEL__DEACTIVATE,
    CONFIGURED_INFER_MODEL__SHUTDOWN,
    CONFIGURED_INFER_MODEL__RUN_ASYNC,

    DEVICE__CREATE,
    DEVICE__DESTROY,
    DEVICE__IDENTIFY,
    DEVICE__EXTENDED_INFO,

    CALLBACK_CALLED,

    MAX_VALUE,
};

using rpc_object_handle_t = uint32_t;
struct rpc_stream_params_t
{
    uint32_t format_order;
    uint32_t format_type;
    float32_t nms_score_threshold;
    float32_t nms_iou_threshold;
    uint32_t nms_max_proposals_per_class;
    uint32_t nms_max_accumulated_mask_size;
};
using rpc_stream_params_map_t = std::unordered_map<std::string, rpc_stream_params_t>;
struct rpc_create_configured_infer_model_request_params_t
{
    rpc_object_handle_t infer_model_handle;
    rpc_object_handle_t vdevice_handle;
    rpc_stream_params_map_t input_streams_params;
    rpc_stream_params_map_t output_streams_params;
    uint16_t batch_size;
    hailo_power_mode_t power_mode;
    hailo_latency_measurement_flags_t latency_flag;
};

class CreateVDeviceSerializer
{
public:
    CreateVDeviceSerializer() = delete;

    static Expected<Buffer> serialize_request(const hailo_vdevice_params_t &params);
    static Expected<hailo_vdevice_params_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, rpc_object_handle_t vdevice_handle = INVALID_HANDLE_ID);
    static Expected<std::tuple<hailo_status, rpc_object_handle_t>> deserialize_reply(const MemoryView &serialized_reply);
};

class DestroyVDeviceSerializer
{
public:
    DestroyVDeviceSerializer() = delete;

    static Expected<Buffer> serialize_request(rpc_object_handle_t vdevice_handle);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

class CreateInferModelSerializer
{
public:
    CreateInferModelSerializer() = delete;

    static Expected<Buffer> serialize_request(rpc_object_handle_t vdevice_handle, uint64_t hef_size, const std::string &name);
    static Expected<std::tuple<rpc_object_handle_t, uint64_t, std::string>> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, rpc_object_handle_t infer_model_handle = INVALID_HANDLE_ID);
    static Expected<std::tuple<hailo_status, rpc_object_handle_t>> deserialize_reply(const MemoryView &serialized_reply);
};

class DestroyInferModelSerializer
{
public:
    DestroyInferModelSerializer() = delete;

    static Expected<Buffer> serialize_request(rpc_object_handle_t infer_model_handle);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

class CreateConfiguredInferModelSerializer
{
public:
    CreateConfiguredInferModelSerializer() = delete;

    static Expected<Buffer> serialize_request(rpc_create_configured_infer_model_request_params_t params);
    static Expected<rpc_create_configured_infer_model_request_params_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, rpc_object_handle_t configured_infer_handle = INVALID_HANDLE_ID,
        uint32_t async_queue_size = 0);
    static Expected<std::tuple<hailo_status, rpc_object_handle_t, uint32_t>> deserialize_reply(const MemoryView &serialized_reply);
};

class DestroyConfiguredInferModelSerializer
{
public:
    DestroyConfiguredInferModelSerializer() = delete;

    static Expected<Buffer> serialize_request(rpc_object_handle_t configured_infer_model_handle);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

class SetSchedulerTimeoutSerializer
{
public:
    SetSchedulerTimeoutSerializer() = delete;

    static Expected<Buffer> serialize_request(rpc_object_handle_t configured_infer_model_handle, const std::chrono::milliseconds &timeout);
    static Expected<std::tuple<rpc_object_handle_t, std::chrono::milliseconds>> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

class SetSchedulerThresholdSerializer
{
public:
    SetSchedulerThresholdSerializer() = delete;

    static Expected<Buffer> serialize_request(rpc_object_handle_t configured_infer_model_handle, uint32_t threshold);
    static Expected<std::tuple<rpc_object_handle_t, uint32_t>> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

class SetSchedulerPrioritySerializer
{
public:
    SetSchedulerPrioritySerializer() = delete;

    static Expected<Buffer> serialize_request(rpc_object_handle_t configured_infer_model_handle, uint32_t priority);
    static Expected<std::tuple<rpc_object_handle_t, uint32_t>> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

class GetHwLatencyMeasurementSerializer
{
public:
    GetHwLatencyMeasurementSerializer() = delete;

    static Expected<Buffer> serialize_request(rpc_object_handle_t configured_infer_model_handle);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, uint32_t avg_hw_latency = INVALID_LATENCY_MEASUREMENT);
    static Expected<std::tuple<hailo_status, std::chrono::nanoseconds>> deserialize_reply(const MemoryView &serialized_reply);
};

class ActivateSerializer
{
public:
    ActivateSerializer() = delete;

    static Expected<Buffer> serialize_request(rpc_object_handle_t configured_infer_model_handle);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

class DeactivateSerializer
{
public:
    DeactivateSerializer() = delete;

    static Expected<Buffer> serialize_request(rpc_object_handle_t configured_infer_model_handle);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

class ShutdownSerializer
{
public:
    ShutdownSerializer() = delete;

    static Expected<Buffer> serialize_request(rpc_object_handle_t configured_infer_model_handle);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

class RunAsyncSerializer
{
public:
    RunAsyncSerializer() = delete;

    struct Request
    {
        rpc_object_handle_t configured_infer_model_handle;
        rpc_object_handle_t infer_model_handle;
        rpc_object_handle_t callback_handle;
        std::vector<uint32_t> input_buffer_sizes;
    };

    static Expected<Buffer> serialize_request(const Request &request_struct);
    static Expected<Request> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

class CallbackCalledSerializer
{
public:
    CallbackCalledSerializer() = delete;

    static Expected<Buffer> serialize_reply(hailo_status status, rpc_object_handle_t callback_handle = INVALID_HANDLE_ID,
        rpc_object_handle_t configured_infer_model_handle = INVALID_HANDLE_ID);
    static Expected<std::tuple<hailo_status, rpc_object_handle_t, rpc_object_handle_t>> deserialize_reply(const MemoryView &serialized_reply);
};

class CreateDeviceSerializer
{
public:
    CreateDeviceSerializer() = delete;

    static Expected<Buffer> serialize_request();
    static hailo_status deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, rpc_object_handle_t device_handle = INVALID_HANDLE_ID);
    static Expected<std::tuple<hailo_status, rpc_object_handle_t>> deserialize_reply(const MemoryView &serialized_reply);
};

class DestroyDeviceSerializer
{
public:
    DestroyDeviceSerializer() = delete;

    static Expected<Buffer> serialize_request(rpc_object_handle_t device_handle);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

class IdentifyDeviceSerializer
{
public:
    IdentifyDeviceSerializer() = delete;

    static Expected<Buffer> serialize_request(rpc_object_handle_t device_handle);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, const hailo_device_identity_t &identity = {});
    static Expected<std::tuple<hailo_status, hailo_device_identity_t>> deserialize_reply(const MemoryView &serialized_reply);
};

class ExtendedDeviceInfoSerializer
{
public:
    ExtendedDeviceInfoSerializer() = delete;

    static Expected<Buffer> serialize_request(rpc_object_handle_t device_handle);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, const hailo_extended_device_information_t &extended_info = {});
    static Expected<std::tuple<hailo_status, hailo_extended_device_information_t>> deserialize_reply(const MemoryView &serialized_reply);
};


} /* namespace hailort */

#endif /* _HAILO_SERIALIZER_HPP_ */
