/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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
#include "common/utils.hpp"

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
    DEVICE__GET_CHIP_TEMPERATURE,
    DEVICE__POWER_MEASUREMENT,
    DEVICE__SET_POWER_MEASUREMENT,
    DEVICE__GET_POWER_MEASUREMENT,
    DEVICE__START_POWER_MEASUREMENT,
    DEVICE__STOP_POWER_MEASUREMENT,
    DEVICE__QUERY_HEALTH_STATS,
    DEVICE__QUERY_PERFORMANCE_STATS,
    DEVICE__GET_ARCHITECTURE,
    DEVICE__SET_NOTIFICATION_CALLBACK,
    DEVICE__REMOVE_NOTIFICATION_CALLBACK,

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
    uint32_t nms_max_proposals_total;
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

struct RunAsyncRpcCallback {
    hailo_status status;
};

struct DeviceNotifcationRpcCallback {
    hailo_notification_t notification;
};

enum RpcCallbackType {
    INVALID = 0,
    RUN_ASYNC,
    DEVICE_NOTIFICATION
};

union RpcCallbackUnion {
    RunAsyncRpcCallback run_async;
    DeviceNotifcationRpcCallback device_notification;
};

struct RpcCallback {
    rpc_object_handle_t callback_id;
    rpc_object_handle_t dispatcher_id;
    RpcCallbackType type;
    RpcCallbackUnion data;
};

class SerializerVDeviceParamsWrapper
{
public:
    SerializerVDeviceParamsWrapper(hailo_scheduling_algorithm_t scheduling_algorithm, const std::string &group_id, bool is_device_id_user_specific)
        : m_group_id(group_id), m_is_device_id_user_specific(is_device_id_user_specific)
    {
        constexpr static bool DISABLE_MULTI_PROCESS_SERVICE = false;
        m_vdevice_params = {
            1,
            nullptr,
            scheduling_algorithm,
            m_group_id.c_str(),
            DISABLE_MULTI_PROCESS_SERVICE
        };
    }

    SerializerVDeviceParamsWrapper(SerializerVDeviceParamsWrapper &&other) noexcept
        : m_group_id(std::move(other.m_group_id)), m_vdevice_params(std::move(other.m_vdevice_params)),
            m_is_device_id_user_specific(other.m_is_device_id_user_specific)
    {
        m_vdevice_params.group_id = m_group_id.c_str();
    }

    SerializerVDeviceParamsWrapper& operator=(SerializerVDeviceParamsWrapper &&other) noexcept
    {
        if (this != &other) {
            m_group_id = std::move(other.m_group_id);
            m_vdevice_params = std::move(other.m_vdevice_params);
            m_vdevice_params.group_id = m_group_id.c_str();
            m_is_device_id_user_specific = other.m_is_device_id_user_specific;
        }
        return *this;
    }

    const hailo_vdevice_params_t &get() const { return m_vdevice_params; }
    bool is_device_id_user_specific() const { return m_is_device_id_user_specific; }

private:
    std::string m_group_id;
    hailo_vdevice_params_t m_vdevice_params;
    bool m_is_device_id_user_specific;
};

template <typename T>
Expected<size_t> get_serialized_request(T request, const std::string &name, MemoryView buffer)
{
    CHECK(buffer.size() >= request.ByteSizeLong(), HAILO_INTERNAL_FAILURE);

    CHECK(request.SerializeToArray(buffer.data(), static_cast<int>(request.ByteSizeLong())),
        HAILO_RPC_FAILED, "Failed to serialize '{}'", name);

    return request.ByteSizeLong();
}

template <typename T>
Expected<rpc_object_handle_t> get_deserialized_request(const MemoryView &serialized_request, const std::string &name)
{
    T request;
    CHECK(request.ParseFromArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize '{}'", name);
    return request.device_handle().id();
}

template <typename T>
Expected<Buffer> get_serialized_reply(T reply, const std::string &name)
{
    // TODO: serialize_reply should receive a buffer instead of creating one (HRT-16540)
    TRY(auto serialized_reply, Buffer::create(reply.ByteSizeLong(), BufferStorageParams::create_dma()));
    CHECK(reply.SerializeToArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())), \
        HAILO_RPC_FAILED, "Failed to serialize '{}'", name);
    return serialized_reply;
}

template <typename T>
hailo_status get_deserialized_status_only_reply(const MemoryView &serialized_reply, const std::string &name)
{
    T reply;
    CHECK(reply.ParseFromArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize '{}'", name);
    return static_cast<hailo_status>(reply.status());
}

struct CreateVDeviceSerializer
{
    CreateVDeviceSerializer() = delete;

    static Expected<size_t> serialize_request(const hailo_vdevice_params_t &params, MemoryView buffer);
    static Expected<SerializerVDeviceParamsWrapper> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, rpc_object_handle_t vdevice_handle = INVALID_HANDLE_ID);
    static Expected<std::tuple<hailo_status, rpc_object_handle_t>> deserialize_reply(const MemoryView &serialized_reply);
};

struct DestroyVDeviceSerializer
{
    DestroyVDeviceSerializer() = delete;

    static Expected<size_t> serialize_request(rpc_object_handle_t vdevice_handle, MemoryView buffer);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct CreateInferModelSerializer
{
    CreateInferModelSerializer() = delete;

    static Expected<size_t> serialize_request(rpc_object_handle_t vdevice_handle, uint64_t hef_size, const std::string &name, MemoryView buffer);
    static Expected<std::tuple<rpc_object_handle_t, uint64_t, std::string>> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, rpc_object_handle_t infer_model_handle = INVALID_HANDLE_ID);
    static Expected<std::tuple<hailo_status, rpc_object_handle_t>> deserialize_reply(const MemoryView &serialized_reply);
};

struct DestroyInferModelSerializer
{
    DestroyInferModelSerializer() = delete;

    static Expected<size_t> serialize_request(rpc_object_handle_t infer_model_handle, MemoryView buffer);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct CreateConfiguredInferModelSerializer
{
    CreateConfiguredInferModelSerializer() = delete;

    static Expected<size_t> serialize_request(rpc_create_configured_infer_model_request_params_t params, MemoryView buffer);
    static Expected<rpc_create_configured_infer_model_request_params_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, rpc_object_handle_t configured_infer_handle = INVALID_HANDLE_ID,
        uint32_t async_queue_size = 0);
    static Expected<std::tuple<hailo_status, rpc_object_handle_t, uint32_t>> deserialize_reply(const MemoryView &serialized_reply);
};

struct DestroyConfiguredInferModelSerializer
{
    DestroyConfiguredInferModelSerializer() = delete;

    static Expected<size_t> serialize_request(rpc_object_handle_t configured_infer_model_handle, MemoryView buffer);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct SetSchedulerTimeoutSerializer
{
    SetSchedulerTimeoutSerializer() = delete;

    static Expected<size_t> serialize_request(rpc_object_handle_t configured_infer_model_handle, const std::chrono::milliseconds &timeout, MemoryView buffer);
    static Expected<std::tuple<rpc_object_handle_t, std::chrono::milliseconds>> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct SetSchedulerThresholdSerializer
{
    SetSchedulerThresholdSerializer() = delete;

    static Expected<size_t> serialize_request(rpc_object_handle_t configured_infer_model_handle, uint32_t threshold, MemoryView buffer);
    static Expected<std::tuple<rpc_object_handle_t, uint32_t>> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct SetSchedulerPrioritySerializer
{
    SetSchedulerPrioritySerializer() = delete;

    static Expected<size_t> serialize_request(rpc_object_handle_t configured_infer_model_handle, uint32_t priority, MemoryView buffer);
    static Expected<std::tuple<rpc_object_handle_t, uint32_t>> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct GetHwLatencyMeasurementSerializer
{
    GetHwLatencyMeasurementSerializer() = delete;

    static Expected<size_t> serialize_request(rpc_object_handle_t configured_infer_model_handle, MemoryView buffer);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, uint32_t avg_hw_latency = INVALID_LATENCY_MEASUREMENT);
    static Expected<std::tuple<hailo_status, std::chrono::nanoseconds>> deserialize_reply(const MemoryView &serialized_reply);
};

struct ActivateSerializer
{
    ActivateSerializer() = delete;

    static Expected<size_t> serialize_request(rpc_object_handle_t configured_infer_model_handle, MemoryView buffer);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct DeactivateSerializer
{
    DeactivateSerializer() = delete;

    static Expected<size_t> serialize_request(rpc_object_handle_t configured_infer_model_handle, MemoryView buffer);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct ShutdownSerializer
{
    ShutdownSerializer() = delete;

    static Expected<size_t> serialize_request(rpc_object_handle_t configured_infer_model_handle, MemoryView buffer);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct RunAsyncSerializer
{
    RunAsyncSerializer() = delete;

    struct Request
    {
        rpc_object_handle_t configured_infer_model_handle;
        rpc_object_handle_t infer_model_handle;
        rpc_object_handle_t callback_handle;
        rpc_object_handle_t dispatcher_id;
        std::vector<uint32_t> input_buffer_sizes;
    };

    static Expected<size_t> serialize_request(const Request &request_struct, MemoryView buffer);
    static Expected<Request> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct CallbackCalledSerializer
{
    CallbackCalledSerializer() = delete;

    static Expected<Buffer> serialize_reply(const RpcCallback &callback);
    static Expected<RpcCallback> deserialize_reply(const MemoryView &serialized_reply);
};

struct CreateDeviceSerializer
{
    CreateDeviceSerializer() = delete;

    static Expected<size_t> serialize_request(MemoryView buffer);
    static hailo_status deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, rpc_object_handle_t device_handle = INVALID_HANDLE_ID);
    static Expected<std::tuple<hailo_status, rpc_object_handle_t>> deserialize_reply(const MemoryView &serialized_reply);
};

struct DestroyDeviceSerializer
{
    DestroyDeviceSerializer() = delete;

    static Expected<size_t> serialize_request(rpc_object_handle_t device_handle, MemoryView buffer);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct IdentifyDeviceSerializer
{
    IdentifyDeviceSerializer() = delete;

    static Expected<size_t> serialize_request(rpc_object_handle_t device_handle, MemoryView buffer);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, const hailo_device_identity_t &identity = {});
    static Expected<std::tuple<hailo_status, hailo_device_identity_t>> deserialize_reply(const MemoryView &serialized_reply);
};

struct ExtendedDeviceInfoSerializer
{
    ExtendedDeviceInfoSerializer() = delete;

    static Expected<size_t> serialize_request(rpc_object_handle_t device_handle, MemoryView buffer);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, const hailo_extended_device_information_t &extended_info = {});
    static Expected<std::tuple<hailo_status, hailo_extended_device_information_t>> deserialize_reply(const MemoryView &serialized_reply);
};

struct GetChipTemperatureSerializer
{
    GetChipTemperatureSerializer() = delete;
    static Expected<size_t> serialize_request(rpc_object_handle_t device_handle, MemoryView buffer);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);
    static Expected<Buffer> serialize_reply(hailo_status status, const hailo_chip_temperature_info_t &info = {});
    static Expected<std::tuple<hailo_status, hailo_chip_temperature_info_t>> deserialize_reply(const MemoryView &serialized_reply);
};

struct QueryHealthStatsSerializer
{
    QueryHealthStatsSerializer() = delete;
    static Expected<size_t> serialize_request(rpc_object_handle_t device_handle, MemoryView buffer);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);
    static Expected<Buffer> serialize_reply(hailo_status status, const hailo_health_stats_t &info = {});
    static Expected<std::tuple<hailo_status, hailo_health_stats_t>> deserialize_reply(const MemoryView &serialized_reply);
};

struct QueryPerformanceStatsSerializer
{
    QueryPerformanceStatsSerializer() = delete;
    static Expected<size_t> serialize_request(rpc_object_handle_t device_handle, MemoryView buffer);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);
    static Expected<Buffer> serialize_reply(hailo_status status, const hailo_performance_stats_t &info = {});
    static Expected<std::tuple<hailo_status, hailo_performance_stats_t>> deserialize_reply(const MemoryView &serialized_reply);
};

struct PowerMeasurementSerializer
{
    PowerMeasurementSerializer() = delete;
    static Expected<size_t> serialize_request(rpc_object_handle_t device_handle, uint32_t hailo_dvm_options, uint32_t hailo_power_measurement_type, MemoryView buffer);
    static Expected<std::tuple<rpc_object_handle_t, uint32_t, uint32_t>> deserialize_request(const MemoryView &serialized_request);
    static Expected<Buffer> serialize_reply(hailo_status status, const float32_t &power = 0.0f);
    static Expected<std::tuple<hailo_status, float32_t>> deserialize_reply(const MemoryView &serialized_reply);
};

struct SetPowerMeasurementSerializer
{
    SetPowerMeasurementSerializer() = delete;
    static Expected<size_t> serialize_request(rpc_object_handle_t device_handle, uint32_t hailo_dvm_options, uint32_t hailo_power_measurement_type, MemoryView buffer);
    static Expected<std::tuple<rpc_object_handle_t, uint32_t, uint32_t>> deserialize_request(const MemoryView &serialized_request);
    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct StartPowerMeasurementSerializer
{
    StartPowerMeasurementSerializer() = delete;
    static Expected<size_t> serialize_request(rpc_object_handle_t device_handle, uint32_t averaging_factor, uint32_t sampling_period, MemoryView buffer);
    static Expected<std::tuple<rpc_object_handle_t, uint32_t, uint32_t>> deserialize_request(const MemoryView &serialized_request);
    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct GetPowerMeasurementSerializer
{
    GetPowerMeasurementSerializer() = delete;
    static Expected<size_t> serialize_request(rpc_object_handle_t device_handle, bool should_clear, MemoryView buffer);
    static Expected<std::tuple<rpc_object_handle_t, bool>> deserialize_request(const MemoryView &serialized_request);
    static Expected<Buffer> serialize_reply(hailo_status status, const hailo_power_measurement_data_t &data = {});
    static Expected<std::tuple<hailo_status, hailo_power_measurement_data_t>> deserialize_reply(const MemoryView &serialized_reply);
};

struct StopPowerMeasurementSerializer
{
    StopPowerMeasurementSerializer() = delete;
    static Expected<size_t> serialize_request(rpc_object_handle_t device_handle, MemoryView buffer);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);
    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct GetArchitectureSerializer
{
    GetArchitectureSerializer() = delete;
    static Expected<size_t> serialize_request(rpc_object_handle_t device_handle, MemoryView buffer);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);
    static Expected<Buffer> serialize_reply(hailo_status status, const hailo_device_architecture_t &device_architecture = HAILO_ARCH_MAX_ENUM);
    static Expected<std::tuple<hailo_status, hailo_device_architecture_t>> deserialize_reply(const MemoryView &serialized_reply);
};

struct SetNotificationCallbackSerializer
{
    struct Request {
        rpc_object_handle_t device_handle;
        hailo_notification_id_t notification_id;
        rpc_object_handle_t callback;
        rpc_object_handle_t dispatcher_id;
    };

    SetNotificationCallbackSerializer() = delete;
    static Expected<size_t> serialize_request(const Request &request, MemoryView buffer);
    static Expected<Request> deserialize_request(const MemoryView &serialized_request);
    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct RemoveNotificationCallbackSerializer
{
    RemoveNotificationCallbackSerializer() = delete;
    static Expected<size_t> serialize_request(rpc_object_handle_t device_handle, hailo_notification_id_t notification_id,
        MemoryView buffer);
    static Expected<std::tuple<rpc_object_handle_t, hailo_notification_id_t>> deserialize_request(const MemoryView &serialized_request);
    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

} /* namespace hailort */

#endif /* _HAILO_SERIALIZER_HPP_ */
