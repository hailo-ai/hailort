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

class SerializerVDeviceParamsWrapper
{
public:
    SerializerVDeviceParamsWrapper(uint32_t device_count,
                                  hailo_device_id_t *device_ids,
                                  hailo_scheduling_algorithm_t scheduling_algorithm,
                                  const std::string &group_id,
                                  bool multi_process_service) : m_group_id(group_id)
    {
        m_vdevice_params = {
            device_count,
            device_ids,
            scheduling_algorithm,
            m_group_id.c_str(),
            multi_process_service
        };
    }
    const hailo_vdevice_params_t &get() const { return m_vdevice_params; }
private:
    std::string m_group_id;
    hailo_vdevice_params_t m_vdevice_params;

};

template <typename T>
Expected<Buffer> get_serialized_request(T request, const std::string &name)
{
    // TODO (HRT-14732) - check if we can use GetCachedSize
    TRY(auto serialized_request, Buffer::create(request.ByteSizeLong(), BufferStorageParams::create_dma()));
    CHECK_AS_EXPECTED(request.SerializeToArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to serialize '{}'", name);

    return serialized_request;
}

template <typename T>
Expected<rpc_object_handle_t> get_deserialized_request(const MemoryView &serialized_request, const std::string &name)
{
    T request;
    CHECK_AS_EXPECTED(request.ParseFromArray(serialized_request.data(), static_cast<int>(serialized_request.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize '{}'", name);
    return request.device_handle().id();
}

template <typename T>
Expected<Buffer> get_serialized_reply(T reply, const std::string &name)
{
    TRY(auto serialized_reply, Buffer::create(reply.ByteSizeLong(), BufferStorageParams::create_dma()));
    CHECK_AS_EXPECTED(reply.SerializeToArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())), \
        HAILO_RPC_FAILED, "Failed to serialize '{}'", name);
    return serialized_reply;
}

template <typename T>
hailo_status get_deserialized_status_only_reply(const MemoryView &serialized_reply, const std::string &name)
{
    T reply;
    CHECK_AS_EXPECTED(reply.ParseFromArray(serialized_reply.data(), static_cast<int>(serialized_reply.size())),
        HAILO_RPC_FAILED, "Failed to de-serialize '{}'", name);
    return static_cast<hailo_status>(reply.status());
}

struct CreateVDeviceSerializer
{
    CreateVDeviceSerializer() = delete;

    static Expected<Buffer> serialize_request(const hailo_vdevice_params_t &params);
    static Expected<SerializerVDeviceParamsWrapper> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, rpc_object_handle_t vdevice_handle = INVALID_HANDLE_ID);
    static Expected<std::tuple<hailo_status, rpc_object_handle_t>> deserialize_reply(const MemoryView &serialized_reply);
};

struct DestroyVDeviceSerializer
{
    DestroyVDeviceSerializer() = delete;

    static Expected<Buffer> serialize_request(rpc_object_handle_t vdevice_handle);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct CreateInferModelSerializer
{
    CreateInferModelSerializer() = delete;

    static Expected<Buffer> serialize_request(rpc_object_handle_t vdevice_handle, uint64_t hef_size, const std::string &name);
    static Expected<std::tuple<rpc_object_handle_t, uint64_t, std::string>> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, rpc_object_handle_t infer_model_handle = INVALID_HANDLE_ID);
    static Expected<std::tuple<hailo_status, rpc_object_handle_t>> deserialize_reply(const MemoryView &serialized_reply);
};

struct DestroyInferModelSerializer
{
    DestroyInferModelSerializer() = delete;

    static Expected<Buffer> serialize_request(rpc_object_handle_t infer_model_handle);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct CreateConfiguredInferModelSerializer
{
    CreateConfiguredInferModelSerializer() = delete;

    static Expected<Buffer> serialize_request(rpc_create_configured_infer_model_request_params_t params);
    static Expected<rpc_create_configured_infer_model_request_params_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, rpc_object_handle_t configured_infer_handle = INVALID_HANDLE_ID,
        uint32_t async_queue_size = 0);
    static Expected<std::tuple<hailo_status, rpc_object_handle_t, uint32_t>> deserialize_reply(const MemoryView &serialized_reply);
};

struct DestroyConfiguredInferModelSerializer
{
    DestroyConfiguredInferModelSerializer() = delete;

    static Expected<Buffer> serialize_request(rpc_object_handle_t configured_infer_model_handle);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct SetSchedulerTimeoutSerializer
{
    SetSchedulerTimeoutSerializer() = delete;

    static Expected<Buffer> serialize_request(rpc_object_handle_t configured_infer_model_handle, const std::chrono::milliseconds &timeout);
    static Expected<std::tuple<rpc_object_handle_t, std::chrono::milliseconds>> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct SetSchedulerThresholdSerializer
{
    SetSchedulerThresholdSerializer() = delete;

    static Expected<Buffer> serialize_request(rpc_object_handle_t configured_infer_model_handle, uint32_t threshold);
    static Expected<std::tuple<rpc_object_handle_t, uint32_t>> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct SetSchedulerPrioritySerializer
{
    SetSchedulerPrioritySerializer() = delete;

    static Expected<Buffer> serialize_request(rpc_object_handle_t configured_infer_model_handle, uint32_t priority);
    static Expected<std::tuple<rpc_object_handle_t, uint32_t>> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct GetHwLatencyMeasurementSerializer
{
    GetHwLatencyMeasurementSerializer() = delete;

    static Expected<Buffer> serialize_request(rpc_object_handle_t configured_infer_model_handle);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, uint32_t avg_hw_latency = INVALID_LATENCY_MEASUREMENT);
    static Expected<std::tuple<hailo_status, std::chrono::nanoseconds>> deserialize_reply(const MemoryView &serialized_reply);
};

struct ActivateSerializer
{
    ActivateSerializer() = delete;

    static Expected<Buffer> serialize_request(rpc_object_handle_t configured_infer_model_handle);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct DeactivateSerializer
{
    DeactivateSerializer() = delete;

    static Expected<Buffer> serialize_request(rpc_object_handle_t configured_infer_model_handle);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct ShutdownSerializer
{
    ShutdownSerializer() = delete;

    static Expected<Buffer> serialize_request(rpc_object_handle_t configured_infer_model_handle);
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
        std::vector<uint32_t> input_buffer_sizes;
    };

    static Expected<Buffer> serialize_request(const Request &request_struct);
    static Expected<Request> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct CallbackCalledSerializer
{
    CallbackCalledSerializer() = delete;

    static Expected<Buffer> serialize_reply(hailo_status status, rpc_object_handle_t callback_handle = INVALID_HANDLE_ID,
        rpc_object_handle_t configured_infer_model_handle = INVALID_HANDLE_ID);
    static Expected<std::tuple<hailo_status, rpc_object_handle_t, rpc_object_handle_t>> deserialize_reply(const MemoryView &serialized_reply);
};

struct CreateDeviceSerializer
{
    CreateDeviceSerializer() = delete;

    static Expected<Buffer> serialize_request();
    static hailo_status deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, rpc_object_handle_t device_handle = INVALID_HANDLE_ID);
    static Expected<std::tuple<hailo_status, rpc_object_handle_t>> deserialize_reply(const MemoryView &serialized_reply);
};

struct DestroyDeviceSerializer
{
    DestroyDeviceSerializer() = delete;

    static Expected<Buffer> serialize_request(rpc_object_handle_t device_handle);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct IdentifyDeviceSerializer
{
    IdentifyDeviceSerializer() = delete;

    static Expected<Buffer> serialize_request(rpc_object_handle_t device_handle);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, const hailo_device_identity_t &identity = {});
    static Expected<std::tuple<hailo_status, hailo_device_identity_t>> deserialize_reply(const MemoryView &serialized_reply);
};

struct ExtendedDeviceInfoSerializer
{
    ExtendedDeviceInfoSerializer() = delete;

    static Expected<Buffer> serialize_request(rpc_object_handle_t device_handle);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);

    static Expected<Buffer> serialize_reply(hailo_status status, const hailo_extended_device_information_t &extended_info = {});
    static Expected<std::tuple<hailo_status, hailo_extended_device_information_t>> deserialize_reply(const MemoryView &serialized_reply);
};

struct GetChipTemperatureSerializer
{
    GetChipTemperatureSerializer() = delete;
    static Expected<Buffer> serialize_request(rpc_object_handle_t device_handle);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);
    static Expected<Buffer> serialize_reply(hailo_status status, const hailo_chip_temperature_info_t &info = {});
    static Expected<std::tuple<hailo_status, hailo_chip_temperature_info_t>> deserialize_reply(const MemoryView &serialized_reply);
};

struct PowerMeasurementSerializer
{
    PowerMeasurementSerializer() = delete;
    static Expected<Buffer> serialize_request(rpc_object_handle_t device_handle, uint32_t hailo_dvm_options, uint32_t hailo_power_measurement_type);
    static Expected<std::tuple<rpc_object_handle_t, uint32_t, uint32_t>> deserialize_request(const MemoryView &serialized_request);
    static Expected<Buffer> serialize_reply(hailo_status status, const float32_t &power = 0.0f);
    static Expected<std::tuple<hailo_status, float32_t>> deserialize_reply(const MemoryView &serialized_reply);
};

struct SetPowerMeasurementSerializer
{
    SetPowerMeasurementSerializer() = delete;
    static Expected<Buffer> serialize_request(rpc_object_handle_t device_handle, uint32_t hailo_dvm_options, uint32_t hailo_power_measurement_type);
    static Expected<std::tuple<rpc_object_handle_t, uint32_t, uint32_t>> deserialize_request(const MemoryView &serialized_request);
    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct StartPowerMeasurementSerializer
{
    StartPowerMeasurementSerializer() = delete;
    static Expected<Buffer> serialize_request(rpc_object_handle_t device_handle, uint32_t averaging_factor, uint32_t sampling_period);
    static Expected<std::tuple<rpc_object_handle_t, uint32_t, uint32_t>> deserialize_request(const MemoryView &serialized_request);
    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

struct GetPowerMeasurementSerializer
{
    GetPowerMeasurementSerializer() = delete;
    static Expected<Buffer> serialize_request(rpc_object_handle_t device_handle, bool should_clear);
    static Expected<std::tuple<rpc_object_handle_t, bool>> deserialize_request(const MemoryView &serialized_request);
    static Expected<Buffer> serialize_reply(hailo_status status, const hailo_power_measurement_data_t &data = {});
    static Expected<std::tuple<hailo_status, hailo_power_measurement_data_t>> deserialize_reply(const MemoryView &serialized_reply);
};

struct StopPowerMeasurementSerializer
{
    StopPowerMeasurementSerializer() = delete;
    static Expected<Buffer> serialize_request(rpc_object_handle_t device_handle);
    static Expected<rpc_object_handle_t> deserialize_request(const MemoryView &serialized_request);
    static Expected<Buffer> serialize_reply(hailo_status status);
    static hailo_status deserialize_reply(const MemoryView &serialized_reply);
};

} /* namespace hailort */

#endif /* _HAILO_SERIALIZER_HPP_ */
