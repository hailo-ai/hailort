/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pcie_device_hrpc_client.cpp
 * @brief Pcie Device HRPC client implementation
 **/

#include "pcie_device_hrpc_client.hpp"
#include "device_common/device_internal.hpp"
#include "hailo/hailort.h"
#include "serializer.hpp"
#include "vdma/vdma_device.hpp"


namespace hailort
{

Expected<std::shared_ptr<Client>> PcieDeviceHrpcClient::get_connected_client(const std::string &device_id)
{
    auto client = make_shared_nothrow<Client>(device_id);
    CHECK_NOT_NULL(client, HAILO_OUT_OF_HOST_MEMORY);

    auto status = client->connect();
    CHECK_SUCCESS(status, "Failed to connect to server");

    client->register_custom_reply(HailoRpcActionID::CALLBACK_CALLED,
    [callback_dispatcher_manager = client->callback_dispatcher_manager()] (const MemoryView &serialized_reply, RpcConnection connection) -> hailo_status {
        TRY(auto rpc_callback, CallbackCalledSerializer::deserialize_reply(serialized_reply));
        auto status = callback_dispatcher_manager->at(rpc_callback.dispatcher_id)->trigger_callback(rpc_callback, connection);
        CHECK_SUCCESS(status);

        return HAILO_SUCCESS;
    });

    return client;
}

Expected<std::unique_ptr<PcieDeviceHrpcClient>> PcieDeviceHrpcClient::create(const std::string &device_id)
{
    auto client = get_connected_client(device_id);
    return PcieDeviceHrpcClient::create(device_id, client ? client.release() : nullptr);
}

Expected<rpc_object_handle_t> PcieDeviceHrpcClient::create_remote_device(std::shared_ptr<Client> client)
{
    TRY(auto request_buffer, client->allocate_request_buffer(), "Failed to allocate request buffer");
    TRY(auto request_size, CreateDeviceSerializer::serialize_request(MemoryView(*request_buffer)));
    TRY(auto result, client->execute_request(HailoRpcActionID::DEVICE__CREATE, MemoryView(request_buffer->data(), request_size)));
    TRY(auto tuple, CreateDeviceSerializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size)));
    auto status = std::get<0>(tuple);
    CHECK_SUCCESS(status);

    auto device_handle = std::get<1>(tuple);
    return device_handle;
}

Expected<std::unique_ptr<PcieDeviceHrpcClient>> PcieDeviceHrpcClient::create(const std::string &device_id,
    std::shared_ptr<Client> client)
{
    auto device_handle = INVALID_HANDLE_ID;
    std::shared_ptr<ClientCallbackDispatcher> callback_dispatcher = nullptr;
    if (client) {
        TRY(device_handle, create_remote_device(client), "Failed to create device");
        TRY(callback_dispatcher, client->callback_dispatcher_manager()->new_dispatcher(RpcCallbackType::DEVICE_NOTIFICATION, false));
    }

    auto device = make_unique_nothrow<PcieDeviceHrpcClient>(device_id, client, device_handle, callback_dispatcher);
    CHECK_NOT_NULL(device, HAILO_OUT_OF_HOST_MEMORY);

    return std::unique_ptr<PcieDeviceHrpcClient>(std::move(device));
}

PcieDeviceHrpcClient::~PcieDeviceHrpcClient()
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

    auto result_expected = m_client->execute_request(HailoRpcActionID::DEVICE__DESTROY, MemoryView(request_buffer.value()->data(), *request_size));
    if (!result_expected) {
        LOGGER__CRITICAL("Failed to destroy Device! status = {}", result_expected.status());
        return;
    }
    auto result = result_expected.release();

    auto status = DestroyDeviceSerializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size));
    if (HAILO_SUCCESS != status) {
        LOGGER__CRITICAL("Failed to destroy Device! status = {}", status);
    }

    status = m_client->callback_dispatcher_manager()->remove_dispatcher(m_callback_dispatcher->id());
    if (HAILO_SUCCESS != status) {
        LOGGER__CRITICAL("Failed to remove callback dispatcher! status = {}", status);
    }
}

Expected<hailo_device_identity_t> PcieDeviceHrpcClient::identify()
{
    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);
    TRY(auto request_buffer, m_client->allocate_request_buffer(), "Failed to allocate request buffer");
    TRY(auto request_size, IdentifyDeviceSerializer::serialize_request(m_handle, MemoryView(*request_buffer)));
    TRY(auto result, m_client->execute_request(HailoRpcActionID::DEVICE__IDENTIFY, MemoryView(request_buffer->data(), request_size)));
    TRY(auto tuple, IdentifyDeviceSerializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size)));

    CHECK_SUCCESS_AS_EXPECTED(std::get<0>(tuple));
    auto identity = std::get<1>(tuple);

    return identity;
}

Expected<hailo_extended_device_information_t> PcieDeviceHrpcClient::get_extended_device_information()
{
    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);
    TRY(auto request_buffer, m_client->allocate_request_buffer(), "Failed to allocate request buffer");
    TRY(auto request_size, ExtendedDeviceInfoSerializer::serialize_request(m_handle, MemoryView(*request_buffer)));
    TRY(auto result, m_client->execute_request(HailoRpcActionID::DEVICE__EXTENDED_INFO, MemoryView(request_buffer->data(), request_size)));
    TRY(auto tuple, ExtendedDeviceInfoSerializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size)));

    CHECK_SUCCESS_AS_EXPECTED(std::get<0>(tuple));
    auto extended_info = std::get<1>(tuple);

    return extended_info;
}

Expected<hailo_chip_temperature_info_t> PcieDeviceHrpcClient::get_chip_temperature()
{
    using Serializer = GetChipTemperatureSerializer;
    constexpr auto ActionID = HailoRpcActionID::DEVICE__GET_CHIP_TEMPERATURE;

    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);
    TRY(auto request_buffer, m_client->allocate_request_buffer(), "Failed to allocate request buffer");
    TRY(auto request_size, Serializer::serialize_request(m_handle, MemoryView(*request_buffer)));
    TRY(auto result, m_client->execute_request(ActionID, MemoryView(request_buffer->data(), request_size)));
    TRY(auto tuple, Serializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size)));

    CHECK_SUCCESS_AS_EXPECTED(std::get<0>(tuple));
    auto info = std::get<1>(tuple);

    return info;
}

Expected<hailo_health_stats_t> PcieDeviceHrpcClient::query_health_stats()
{
    using Serializer = QueryHealthStatsSerializer;
    constexpr auto ActionID = HailoRpcActionID::DEVICE__QUERY_HEALTH_STATS;

    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);
    TRY(auto request_buffer, m_client->allocate_request_buffer(), "Failed to allocate request buffer");
    TRY(auto request_size, Serializer::serialize_request(m_handle, MemoryView(*request_buffer)));
    TRY(auto result, m_client->execute_request(ActionID, MemoryView(request_buffer->data(), request_size)));
    TRY(auto tuple, Serializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size)));

    CHECK_SUCCESS_AS_EXPECTED(std::get<0>(tuple));
    auto info = std::get<1>(tuple);

    return info;
}

Expected<hailo_performance_stats_t> PcieDeviceHrpcClient::query_performance_stats()
{
    using Serializer = QueryPerformanceStatsSerializer;
    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);
    TRY(auto request_buffer, m_client->allocate_request_buffer(), "Failed to allocate request buffer");
    TRY(auto request_size, Serializer::serialize_request(m_handle, MemoryView(*request_buffer)));
    TRY(auto result, m_client->execute_request(HailoRpcActionID::DEVICE__QUERY_PERFORMANCE_STATS, MemoryView(request_buffer->data(), request_size)));
    TRY(auto tuple, Serializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size)));

    CHECK_SUCCESS_AS_EXPECTED(std::get<0>(tuple));
    auto info = std::get<1>(tuple);

    return info;
}

Expected<float32_t> PcieDeviceHrpcClient::power_measurement(
    hailo_dvm_options_t dvm,
    hailo_power_measurement_types_t measurement_type)
{
    using Serializer = PowerMeasurementSerializer;
    constexpr auto ActionID = HailoRpcActionID::DEVICE__POWER_MEASUREMENT;

    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);
    TRY(auto request_buffer, m_client->allocate_request_buffer(), "Failed to allocate request buffer");
    TRY(auto request_size, Serializer::serialize_request(m_handle, dvm, measurement_type, MemoryView(*request_buffer)));
    TRY(auto result, m_client->execute_request(ActionID, MemoryView(request_buffer->data(), request_size)));
    TRY(auto tuple, Serializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size)));

    CHECK_SUCCESS_AS_EXPECTED(std::get<0>(tuple));
    auto power = std::get<1>(tuple);

    return power;
}

hailo_status PcieDeviceHrpcClient::start_power_measurement(
    hailo_averaging_factor_t averaging_factor,
    hailo_sampling_period_t sampling_period)
{
    using Serializer = StartPowerMeasurementSerializer;
    constexpr auto ActionID = HailoRpcActionID::DEVICE__START_POWER_MEASUREMENT;

    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);
    TRY(auto request_buffer, m_client->allocate_request_buffer(), "Failed to allocate request buffer");
    TRY(auto request_size, Serializer::serialize_request(m_handle, averaging_factor, sampling_period, MemoryView(*request_buffer)));
    TRY(auto result, m_client->execute_request(ActionID, MemoryView(request_buffer->data(), request_size)));
    return Serializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size));
}

Expected<hailo_power_measurement_data_t> PcieDeviceHrpcClient::get_power_measurement(
    hailo_measurement_buffer_index_t buffer_index,
    bool should_clear)
{
    (void)buffer_index;

    using Serializer = GetPowerMeasurementSerializer;
    constexpr auto ActionID = HailoRpcActionID::DEVICE__GET_POWER_MEASUREMENT;

    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);
    TRY(auto request_buffer, m_client->allocate_request_buffer(), "Failed to allocate request buffer");
    TRY(auto request_size, Serializer::serialize_request(m_handle, should_clear, MemoryView(*request_buffer)));
    TRY(auto result, m_client->execute_request(ActionID, MemoryView(request_buffer->data(), request_size)));
    TRY(auto tuple, Serializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size)));

    CHECK_SUCCESS_AS_EXPECTED(std::get<0>(tuple));
    auto data = std::get<1>(tuple);
    return data;
}

hailo_status PcieDeviceHrpcClient::set_power_measurement(
    hailo_measurement_buffer_index_t buffer_index,
    hailo_dvm_options_t dvm,
    hailo_power_measurement_types_t measurement_type)
{
    (void)buffer_index;

    using Serializer = SetPowerMeasurementSerializer;
    constexpr auto ActionID = HailoRpcActionID::DEVICE__SET_POWER_MEASUREMENT;

    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);
    TRY(auto request_buffer, m_client->allocate_request_buffer(), "Failed to allocate request buffer");
    TRY(auto request_size, Serializer::serialize_request(m_handle, dvm, measurement_type, MemoryView(*request_buffer)));
    TRY(auto result, m_client->execute_request(ActionID, MemoryView(request_buffer->data(), request_size)));

    return Serializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size));
}

hailo_status PcieDeviceHrpcClient::stop_power_measurement()
{
    using Serializer = StopPowerMeasurementSerializer;
    constexpr auto ActionID = HailoRpcActionID::DEVICE__STOP_POWER_MEASUREMENT;

    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);
    TRY(auto request_buffer, m_client->allocate_request_buffer(), "Failed to allocate request buffer");
    TRY(auto request_size, Serializer::serialize_request(m_handle, MemoryView(*request_buffer)));
    TRY(auto result, m_client->execute_request(ActionID, MemoryView(request_buffer->data(), request_size)));

    return Serializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size));
}

Expected<hailo_device_architecture_t> PcieDeviceHrpcClient::get_architecture() const
{
    using Serializer = GetArchitectureSerializer;
    constexpr auto ActionID = HailoRpcActionID::DEVICE__GET_ARCHITECTURE;

    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);
    TRY(auto request_buffer, m_client->allocate_request_buffer(), "Failed to allocate request buffer");
    TRY(auto request_size, Serializer::serialize_request(m_handle, MemoryView(*request_buffer)));
    TRY(auto result, m_client->execute_request(ActionID, MemoryView(request_buffer->data(), request_size)));
    TRY(auto tuple, Serializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size)));

    CHECK_SUCCESS_AS_EXPECTED(std::get<0>(tuple));
    auto device_arch = std::get<1>(tuple);
    return device_arch;
}

hailo_status PcieDeviceHrpcClient::dma_map(void *address, size_t size, hailo_dma_buffer_direction_t data_direction)
{
    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);
    auto driver = m_client->get_driver();
    if (nullptr == driver) {
        return HAILO_SUCCESS;
    }
    return VdmaDevice::dma_map_impl(*driver.get(), address, size, data_direction);
}

hailo_status PcieDeviceHrpcClient::dma_unmap(void *address, size_t size, hailo_dma_buffer_direction_t data_direction)
{
    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);
    auto driver = m_client->get_driver();
    if (nullptr == driver) {
        return HAILO_SUCCESS;
    }
    return VdmaDevice::dma_unmap_impl(*driver.get(), address, size, data_direction);
}

hailo_status PcieDeviceHrpcClient::dma_map_dmabuf(int dmabuf_fd, size_t size, hailo_dma_buffer_direction_t data_direction)
{
    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);
    auto driver = m_client->get_driver();
    if (nullptr == driver) {
        return HAILO_SUCCESS;
    }
    return VdmaDevice::dma_map_dmabuf_impl(*driver.get(), dmabuf_fd, size, data_direction);
}

hailo_status PcieDeviceHrpcClient::dma_unmap_dmabuf(int dmabuf_fd, size_t size, hailo_dma_buffer_direction_t data_direction)
{
    CHECK_NOT_NULL(m_client, HAILO_INVALID_OPERATION);
    auto driver = m_client->get_driver();
    if (nullptr == driver) {
        return HAILO_SUCCESS;
    }
    return VdmaDevice::dma_unmap_dmabuf_impl(*driver.get(), dmabuf_fd, size, data_direction);
}

hailo_status PcieDeviceHrpcClient::reset(hailo_reset_device_mode_t mode)
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

hailo_status PcieDeviceHrpcClient::set_notification_callback(const NotificationCallback &func, hailo_notification_id_t notification_id,
    void *opaque)
{
    switch (notification_id) {
    case HAILO_NOTIFICATION_ID_HEALTH_MONITOR_TEMPERATURE_ALARM:
    case HAILO_NOTIFICATION_ID_HEALTH_MONITOR_OVERCURRENT_ALARM:
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
    TRY(auto result, m_client->execute_request(HailoRpcActionID::DEVICE__SET_NOTIFICATION_CALLBACK, MemoryView(serialized_request->data(), request_size)));
    return Serializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size));
}

hailo_status PcieDeviceHrpcClient::remove_notification_callback(hailo_notification_id_t notification_id)
{
    auto status = m_callback_dispatcher->remove_callback(notification_id);
    CHECK_SUCCESS(status);

    using Serializer = RemoveNotificationCallbackSerializer;
    TRY(auto serialized_request, m_client->allocate_request_buffer());
    TRY(auto request_size, Serializer::serialize_request(m_handle, notification_id, MemoryView(*serialized_request)));
    TRY(auto result, m_client->execute_request(HailoRpcActionID::DEVICE__REMOVE_NOTIFICATION_CALLBACK, MemoryView(serialized_request->data(), request_size)));
    return Serializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size));
}

hailo_status PcieDeviceHrpcClient::before_fork()
{
    m_client.reset();
    return HAILO_SUCCESS;
}

hailo_status PcieDeviceHrpcClient::after_fork_in_parent()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // TODO: remove this after HRT-14998 is fixed
    TRY(m_client, get_connected_client(m_device_id), "Failed to create client");
    // Keeping the same device handle
    return HAILO_SUCCESS;
}

hailo_status PcieDeviceHrpcClient::after_fork_in_child()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // TODO: remove this after HRT-14998 is fixed
    TRY(m_client, get_connected_client(m_device_id), "Failed to create client");
    TRY(m_handle, create_remote_device(m_client), "Failed to create device");
    return HAILO_SUCCESS;
}

} /* namespace hailort */