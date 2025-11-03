/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file device_hrpc_client.cpp
 * @brief Device HRPC client implementation
 **/

#include "device_hrpc_client.hpp"
#include "device_common/device_internal.hpp"
#include "hailo/hailort.h"
#include "serializer.hpp"
#include "vdma/vdma_device.hpp"


namespace hailort
{

Expected<std::shared_ptr<Client>> DeviceHrpcClient::get_connected_client(const std::string &device_id)
{
    auto client = make_shared_nothrow<Client>(device_id);
    CHECK_NOT_NULL(client, HAILO_OUT_OF_HOST_MEMORY);

    auto status = client->connect();
    CHECK_SUCCESS(status, "Failed to connect to server");

    client->set_notification_callback(
    [callback_dispatcher_manager = client->callback_dispatcher_manager()]
    (const MemoryView &serialized_reply) -> hailo_status {
        TRY(auto rpc_callback, CallbackCalledSerializer::deserialize_reply(serialized_reply));
        auto status = callback_dispatcher_manager->at(rpc_callback.dispatcher_id)->trigger_callback(rpc_callback);
        CHECK_SUCCESS(status);

        return HAILO_SUCCESS;
    });

    return client;
}

Expected<std::unique_ptr<Device>> DeviceHrpcClient::create(const std::string &device_id)
{
    TRY(auto client, get_connected_client(device_id));
    return DeviceHrpcClient::create(device_id, client);
}

Expected<rpc_object_handle_t> DeviceHrpcClient::create_remote_device(std::shared_ptr<Client> client)
{
    TRY(auto request_buffer, client->allocate_request_buffer(), "Failed to allocate request buffer");
    TRY(auto request_size, CreateDeviceSerializer::serialize_request(MemoryView(*request_buffer)));
    TRY(auto result, client->execute_request(static_cast<uint32_t>(HailoRpcActionID::DEVICE__CREATE), MemoryView(request_buffer->data(), request_size)));

    return CreateDeviceSerializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size));
}

Expected<std::unique_ptr<Device>> DeviceHrpcClient::create(const std::string &device_id,
    std::shared_ptr<Client> client)
{
    auto device_handle = INVALID_HANDLE_ID;
    std::shared_ptr<ClientCallbackDispatcher> callback_dispatcher = nullptr;
    if (client) {
        TRY(device_handle, create_remote_device(client), "Failed to create device");
        TRY(callback_dispatcher, client->callback_dispatcher_manager()->new_dispatcher(RpcCallbackType::DEVICE_NOTIFICATION, false));
    }

    auto device = make_unique_nothrow<DeviceHrpcClient>(device_id, client, device_handle, callback_dispatcher);
    CHECK_NOT_NULL(device, HAILO_OUT_OF_HOST_MEMORY);

    return std::unique_ptr<Device>(std::move(device));
}

DeviceHrpcClient::~DeviceHrpcClient()
{
    if ((INVALID_HANDLE_ID == m_handle) || !m_client) {
        return;
    }

    auto request_buffer = m_client->allocate_request_buffer();
    if (!request_buffer) {
        LOGGER__CRITICAL("Failed to create buffer for Device_release request");
        return;
    }

    auto request_size = DestroyDeviceSerializer::serialize_request(m_handle, MemoryView(**request_buffer));
    if (!request_size) {
        LOGGER__CRITICAL("Failed to serialize Device_release request");
        return;
    }

    auto result_expected = m_client->execute_request(static_cast<uint32_t>(HailoRpcActionID::DEVICE__DESTROY),
        MemoryView(request_buffer.value()->data(), *request_size));
    if (!result_expected) {
        LOGGER__CRITICAL("Failed to destroy Device! status = {}", result_expected.status());
        return;
    }

    auto status = m_client->callback_dispatcher_manager()->remove_dispatcher(m_callback_dispatcher->id());
    if (HAILO_SUCCESS != status) {
        LOGGER__CRITICAL("Failed to remove callback dispatcher! status = {}", status);
    }
}

Expected<hailo_device_identity_t> DeviceHrpcClient::identify()
{
    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);
    TRY(auto request_buffer, m_client->allocate_request_buffer(), "Failed to allocate request buffer");
    TRY(auto request_size, IdentifyDeviceSerializer::serialize_request(m_handle, MemoryView(*request_buffer)));
    TRY(auto result, m_client->execute_request(static_cast<uint32_t>(HailoRpcActionID::DEVICE__IDENTIFY),
        MemoryView(request_buffer->data(), request_size)));

    return IdentifyDeviceSerializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size));
}

Expected<hailo_extended_device_information_t> DeviceHrpcClient::get_extended_device_information()
{
    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);
    TRY(auto request_buffer, m_client->allocate_request_buffer(), "Failed to allocate request buffer");
    TRY(auto request_size, ExtendedDeviceInfoSerializer::serialize_request(m_handle, MemoryView(*request_buffer)));
    TRY(auto result, m_client->execute_request(static_cast<uint32_t>(HailoRpcActionID::DEVICE__EXTENDED_INFO),
        MemoryView(request_buffer->data(), request_size)));

    return ExtendedDeviceInfoSerializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size));
}

Expected<hailo_chip_temperature_info_t> DeviceHrpcClient::get_chip_temperature()
{

    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);

    using Serializer = GetChipTemperatureSerializer;
    TRY(auto request_buffer, m_client->allocate_request_buffer(), "Failed to allocate request buffer");
    TRY(auto request_size, Serializer::serialize_request(m_handle, MemoryView(*request_buffer)));
    TRY(auto result, m_client->execute_request(static_cast<uint32_t>(HailoRpcActionID::DEVICE__GET_CHIP_TEMPERATURE),
        MemoryView(request_buffer->data(), request_size)));

    return Serializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size));
}

Expected<hailo_health_stats_t> DeviceHrpcClient::query_health_stats()
{
    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);

    using Serializer = QueryHealthStatsSerializer;
    TRY(auto request_buffer, m_client->allocate_request_buffer(), "Failed to allocate request buffer");
    TRY(auto request_size, Serializer::serialize_request(m_handle, MemoryView(*request_buffer)));
    TRY(auto result, m_client->execute_request(static_cast<uint32_t>(HailoRpcActionID::DEVICE__QUERY_HEALTH_STATS),
        MemoryView(request_buffer->data(), request_size)));

    return Serializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size));
}

Expected<hailo_performance_stats_t> DeviceHrpcClient::query_performance_stats()
{
    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);

    using Serializer = QueryPerformanceStatsSerializer;
    TRY(auto request_buffer, m_client->allocate_request_buffer(), "Failed to allocate request buffer");
    TRY(auto request_size, Serializer::serialize_request(m_handle, MemoryView(*request_buffer)));
    TRY(auto result, m_client->execute_request(static_cast<uint32_t>(HailoRpcActionID::DEVICE__QUERY_PERFORMANCE_STATS),
        MemoryView(request_buffer->data(), request_size)));

    return Serializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size));
}

Expected<float32_t> DeviceHrpcClient::power_measurement(
    hailo_dvm_options_t dvm,
    hailo_power_measurement_types_t measurement_type)
{
    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);

    using Serializer = PowerMeasurementSerializer;
    TRY(auto request_buffer, m_client->allocate_request_buffer(), "Failed to allocate request buffer");
    TRY(auto request_size, Serializer::serialize_request(m_handle, dvm, measurement_type, MemoryView(*request_buffer)));
    TRY_WITH_ACCEPTABLE_STATUS(HAILO_OPEN_FILE_FAILURE, auto result,
                               m_client->execute_request(static_cast<uint32_t>(HailoRpcActionID::DEVICE__POWER_MEASUREMENT),
                               MemoryView(request_buffer->data(), request_size)));
    return Serializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size));
}

hailo_status DeviceHrpcClient::start_power_measurement(
    hailo_averaging_factor_t averaging_factor,
    hailo_sampling_period_t sampling_period)
{
    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);

    using Serializer = StartPowerMeasurementSerializer;
    TRY(auto request_buffer, m_client->allocate_request_buffer(), "Failed to allocate request buffer");
    TRY(auto request_size, Serializer::serialize_request(m_handle, averaging_factor, sampling_period, MemoryView(*request_buffer)));
    TRY(auto result, m_client->execute_request(static_cast<uint32_t>(HailoRpcActionID::DEVICE__START_POWER_MEASUREMENT),
        MemoryView(request_buffer->data(), request_size)));

    return HAILO_SUCCESS;
}

Expected<hailo_power_measurement_data_t> DeviceHrpcClient::get_power_measurement(
    hailo_measurement_buffer_index_t buffer_index,
    bool should_clear)
{
    (void)buffer_index;
    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);

    using Serializer = GetPowerMeasurementSerializer;
    TRY(auto request_buffer, m_client->allocate_request_buffer(), "Failed to allocate request buffer");
    TRY(auto request_size, Serializer::serialize_request(m_handle, should_clear, MemoryView(*request_buffer)));
    TRY(auto result, m_client->execute_request(static_cast<uint32_t>(HailoRpcActionID::DEVICE__GET_POWER_MEASUREMENT),
        MemoryView(request_buffer->data(), request_size)));

    return Serializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size));
}

hailo_status DeviceHrpcClient::set_power_measurement(
    hailo_measurement_buffer_index_t buffer_index,
    hailo_dvm_options_t dvm,
    hailo_power_measurement_types_t measurement_type)
{
    (void)buffer_index;
    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);

    using Serializer = SetPowerMeasurementSerializer;
    TRY(auto request_buffer, m_client->allocate_request_buffer(), "Failed to allocate request buffer");
    TRY(auto request_size, Serializer::serialize_request(m_handle, dvm, measurement_type, MemoryView(*request_buffer)));
    TRY(auto result, m_client->execute_request(static_cast<uint32_t>(HailoRpcActionID::DEVICE__SET_POWER_MEASUREMENT),
        MemoryView(request_buffer->data(), request_size)));

    return HAILO_SUCCESS;
}

hailo_status DeviceHrpcClient::stop_power_measurement()
{
    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);

    using Serializer = StopPowerMeasurementSerializer;
    TRY(auto request_buffer, m_client->allocate_request_buffer(), "Failed to allocate request buffer");
    TRY(auto request_size, Serializer::serialize_request(m_handle, MemoryView(*request_buffer)));
    TRY(auto result, m_client->execute_request(static_cast<uint32_t>(HailoRpcActionID::DEVICE__STOP_POWER_MEASUREMENT),
        MemoryView(request_buffer->data(), request_size)));

    return HAILO_SUCCESS;
}

Expected<hailo_device_architecture_t> DeviceHrpcClient::get_architecture() const
{
    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);

    using Serializer = GetArchitectureSerializer;
    TRY(auto request_buffer, m_client->allocate_request_buffer(), "Failed to allocate request buffer");
    TRY(auto request_size, Serializer::serialize_request(m_handle, MemoryView(*request_buffer)));
    TRY(auto result, m_client->execute_request(static_cast<uint32_t>(HailoRpcActionID::DEVICE__GET_ARCHITECTURE), MemoryView(request_buffer->data(), request_size)));

    return Serializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size));
}

hailo_status DeviceHrpcClient::dma_map(void *address, size_t size, hailo_dma_buffer_direction_t data_direction)
{
    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);
    auto driver = m_client->get_driver();
    if (nullptr == driver) {
        return HAILO_SUCCESS;
    }
    return VdmaDevice::dma_map_impl(*driver.get(), address, size, data_direction);
}

hailo_status DeviceHrpcClient::dma_unmap(void *address, size_t size, hailo_dma_buffer_direction_t data_direction)
{
    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);
    auto driver = m_client->get_driver();
    if (nullptr == driver) {
        return HAILO_SUCCESS;
    }
    return VdmaDevice::dma_unmap_impl(*driver.get(), address, size, data_direction);
}

hailo_status DeviceHrpcClient::dma_map_dmabuf(int dmabuf_fd, size_t size, hailo_dma_buffer_direction_t data_direction)
{
    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);
    auto driver = m_client->get_driver();
    if (nullptr == driver) {
        return HAILO_SUCCESS;
    }
    return VdmaDevice::dma_map_dmabuf_impl(*driver.get(), dmabuf_fd, size, data_direction);
}

hailo_status DeviceHrpcClient::dma_unmap_dmabuf(int dmabuf_fd, size_t size, hailo_dma_buffer_direction_t data_direction)
{
    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);
    auto driver = m_client->get_driver();
    if (nullptr == driver) {
        return HAILO_SUCCESS;
    }
    return VdmaDevice::dma_unmap_dmabuf_impl(*driver.get(), dmabuf_fd, size, data_direction);
}

hailo_status DeviceHrpcClient::reset(hailo_reset_device_mode_t mode)
{
    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);
    auto driver = m_client->get_driver();
    CHECK_NOT_NULL(driver, HAILO_NOT_IMPLEMENTED);

    if (mode != HAILO_RESET_DEVICE_MODE_CHIP) {
        return HAILO_NOT_IMPLEMENTED;
    }

    // Disconnect client before reset
    m_client = nullptr;
    return driver->reset_chip();
}

hailo_status DeviceHrpcClient::set_notification_callback(const NotificationCallback &func, hailo_notification_id_t notification_id,
    void *opaque)
{
    switch (notification_id) {
    case HAILO_NOTIFICATION_ID_HEALTH_MONITOR_TEMPERATURE_ALARM:
    case HAILO_NOTIFICATION_ID_HEALTH_MONITOR_OVERCURRENT_ALARM:
    case HAILO_NOTIFICATION_ID_NN_CORE_CRC_ERROR_EVENT:
        break;
    default:
        LOGGER__ERROR("Unsupported notification id = {}", static_cast<uint32_t>(notification_id));
        return HAILO_NOT_IMPLEMENTED;
    }

    m_callback_dispatcher->register_callback(notification_id,
        [this, func, opaque = opaque]
        (const RpcCallback &rpc_callback, hailo_status shutdown_status) {
            if (shutdown_status != HAILO_UNINITIALIZED) {
                return;
            }
            func(*this, rpc_callback.data.device_notification.notification, opaque);
        });
    using Serializer = SetNotificationCallbackSerializer;
    TRY(auto serialized_request, m_client->allocate_request_buffer());
    TRY(auto request_size, Serializer::serialize_request({m_handle, notification_id, static_cast<rpc_object_handle_t>(notification_id),
        m_callback_dispatcher->id()}, MemoryView(*serialized_request)));
    TRY(auto result, m_client->execute_request(static_cast<uint32_t>(HailoRpcActionID::DEVICE__SET_NOTIFICATION_CALLBACK),
        MemoryView(serialized_request->data(), request_size)));

    return HAILO_SUCCESS;
}

hailo_status DeviceHrpcClient::remove_notification_callback(hailo_notification_id_t notification_id)
{
    auto status = m_callback_dispatcher->remove_callback(notification_id);
    CHECK_SUCCESS(status);

    using Serializer = RemoveNotificationCallbackSerializer;
    TRY(auto serialized_request, m_client->allocate_request_buffer());
    TRY(auto request_size, Serializer::serialize_request(m_handle, notification_id, MemoryView(*serialized_request)));
    TRY(auto result, m_client->execute_request(static_cast<uint32_t>(HailoRpcActionID::DEVICE__REMOVE_NOTIFICATION_CALLBACK),
        MemoryView(serialized_request->data(), request_size)));

    return HAILO_SUCCESS;
}

hailo_status DeviceHrpcClient::before_fork()
{
    // It's important to destroy here because we initialize them again after the fork
    m_callback_dispatcher = nullptr;
    m_client.reset();
    return HAILO_SUCCESS;
}

hailo_status DeviceHrpcClient::after_fork_in_parent()
{
    TRY(m_client, get_connected_client(m_device_id), "Failed to create client");
    TRY(m_callback_dispatcher, m_client->callback_dispatcher_manager()->new_dispatcher(RpcCallbackType::DEVICE_NOTIFICATION, false));
    // Keeping the same device handle
    return HAILO_SUCCESS;
}

hailo_status DeviceHrpcClient::after_fork_in_child()
{
    TRY(m_client, get_connected_client(m_device_id), "Failed to create client");
    TRY(m_callback_dispatcher, m_client->callback_dispatcher_manager()->new_dispatcher(RpcCallbackType::DEVICE_NOTIFICATION, false));
    TRY(m_handle, create_remote_device(m_client), "Failed to create device");
    return HAILO_SUCCESS;
}

hailo_status DeviceHrpcClient::echo_buffer_async(const MemoryView buffer)
{
    TRY(auto request_buffer, m_client->allocate_request_buffer(), "Failed to allocate request buffer");
    TRY(auto request_size, EchoBufferSerializer::serialize_request(static_cast<uint32_t>(buffer.size()), MemoryView(*request_buffer)));
    if (0 == buffer.size()) {
        auto status = m_client->execute_request(static_cast<uint32_t>(HailoRpcActionID::DEVICE__ECHO_BUFFER),
            MemoryView(request_buffer->data(), request_size));
        CHECK_SUCCESS(status);

        return HAILO_SUCCESS;
    }

    auto status = m_client->execute_request_async(static_cast<uint32_t>(HailoRpcActionID::DEVICE__ECHO_BUFFER),
        MemoryView(request_buffer->data(), request_size),
        [] (rpc_message_t reply) {
            hailo_status status = static_cast<hailo_status>(reply.header.status);
            if(status != HAILO_SUCCESS) {
                LOGGER__ERROR("Failed to echo buffer. status: {}", status);
            }
        }, {TransferBuffer(buffer)}, {TransferBuffer(buffer)});
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

// Try getting power measurement just to see if it's possible.
// Assuming: Power measurement succeeded -> sensor exists
// In case of a failure, we will assume that sensor is not installed, because we have no way of verifying it
Expected<bool> DeviceHrpcClient::has_power_sensor()
{
    auto power = power_measurement(HAILO_DVM_OPTIONS_AUTO, HAILO_POWER_MEASUREMENT_TYPES__POWER);
    return power.has_value();
}

Expected<size_t> DeviceHrpcClient::fetch_logs(MemoryView buffer, hailo_log_type_t log_type)
{
    using Serializer = FetchLogsSerializer;
    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);

    TRY(auto max_logs_size, get_max_logs_size(log_type));

    CHECK(buffer.size() == max_logs_size,
        HAILO_INSUFFICIENT_BUFFER, "Buffer size must be equal to the maximum log size: {} bytes, got: {} bytes",
        max_logs_size, buffer.size());

    TRY(auto request_buffer, m_client->allocate_request_buffer(), "Failed to allocate request buffer");

    std::vector<TransferBuffer> write_buffers = {};
    std::vector<TransferBuffer> log_transfer_buffers = {TransferBuffer(buffer)};

    TRY(auto request_size, Serializer::serialize_request(m_handle, MemoryView(*request_buffer), buffer.size(), log_type));

    TRY(auto result, m_client->execute_request(static_cast<uint32_t>(HailoRpcActionID::DEVICE__FETCH_LOGS),
        MemoryView(request_buffer->data(), request_size), std::move(write_buffers), std::move(log_transfer_buffers)));

    return Serializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size));
}

} /* namespace hailort */
