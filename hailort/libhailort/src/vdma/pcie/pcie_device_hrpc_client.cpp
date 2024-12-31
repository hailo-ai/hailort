/**
 * Copyright (c) 2019-2024 Hailo Technologies Ltd. All rights reserved.
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

Expected<std::unique_ptr<PcieDeviceHrpcClient>> PcieDeviceHrpcClient::create(const std::string &device_id)
{
    auto client = make_shared_nothrow<Client>(device_id);
    CHECK_NOT_NULL(client, HAILO_INTERNAL_FAILURE);

    auto status = client->connect();
    CHECK_SUCCESS_AS_EXPECTED(status, "Failed to connect to server");

    return PcieDeviceHrpcClient::create(device_id, client);
}

Expected<std::unique_ptr<PcieDeviceHrpcClient>> PcieDeviceHrpcClient::create(const std::string &device_id,
    std::shared_ptr<Client> client)
{
    TRY(auto request, CreateDeviceSerializer::serialize_request());
    TRY(auto result, client->execute_request(HailoRpcActionID::DEVICE__CREATE, MemoryView(request)));
    TRY(auto tuple, CreateDeviceSerializer::deserialize_reply(MemoryView(result)));
    auto status = std::get<0>(tuple);
    CHECK_SUCCESS_AS_EXPECTED(status);

    auto device_handle = std::get<1>(tuple);
    auto device = make_unique_nothrow<PcieDeviceHrpcClient>(device_id, client, device_handle);
    CHECK_NOT_NULL(device, HAILO_OUT_OF_HOST_MEMORY);

    return std::unique_ptr<PcieDeviceHrpcClient>(std::move(device));
}

PcieDeviceHrpcClient::~PcieDeviceHrpcClient()
{
    if (INVALID_HANDLE_ID == m_handle) {
        return;
    }

    auto request = DestroyDeviceSerializer::serialize_request(m_handle);
    if (!request) {
        LOGGER__CRITICAL("Failed to serialize Device_release request");
        return;
    }

    auto result = m_client->execute_request(HailoRpcActionID::DEVICE__DESTROY, MemoryView(*request));
    if (!result) {
        LOGGER__CRITICAL("Failed to destroy Device! status = {}", result.status());
        return;
    }

    if (HAILO_SUCCESS != DestroyDeviceSerializer::deserialize_reply(MemoryView(*result))) {
        LOGGER__CRITICAL("Failed to destroy Device! status = {}", result.status());
    }
}

Expected<hailo_device_identity_t> PcieDeviceHrpcClient::identify()
{
    TRY(auto request, IdentifyDeviceSerializer::serialize_request(m_handle));
    TRY(auto result, m_client->execute_request(HailoRpcActionID::DEVICE__IDENTIFY, MemoryView(request)));
    TRY(auto tuple, IdentifyDeviceSerializer::deserialize_reply(MemoryView(result)));

    CHECK_SUCCESS_AS_EXPECTED(std::get<0>(tuple));
    auto identity = std::get<1>(tuple);

    return identity;
}

Expected<hailo_extended_device_information_t> PcieDeviceHrpcClient::get_extended_device_information()
{
    TRY(auto request, ExtendedDeviceInfoSerializer::serialize_request(m_handle));
    TRY(auto result, m_client->execute_request(HailoRpcActionID::DEVICE__EXTENDED_INFO, MemoryView(request)));
    TRY(auto tuple, ExtendedDeviceInfoSerializer::deserialize_reply(MemoryView(result)));

    CHECK_SUCCESS_AS_EXPECTED(std::get<0>(tuple));
    auto extended_info = std::get<1>(tuple);

    return extended_info;
}

Expected<hailo_chip_temperature_info_t> PcieDeviceHrpcClient::get_chip_temperature()
{
    using Serializer = GetChipTemperatureSerializer;
    constexpr auto ActionID = HailoRpcActionID::DEVICE__GET_CHIP_TEMPERATURE;

    TRY(auto request, Serializer::serialize_request(m_handle));
    TRY(auto result, m_client->execute_request(ActionID, MemoryView(request)));
    TRY(auto tuple, Serializer::deserialize_reply(MemoryView(result)));

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

    TRY(auto request, Serializer::serialize_request(m_handle, dvm, measurement_type));
    TRY(auto result, m_client->execute_request(ActionID, MemoryView(request)));
    TRY(auto tuple, Serializer::deserialize_reply(MemoryView(result)));

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

    TRY(auto request, Serializer::serialize_request(m_handle, averaging_factor, sampling_period));
    TRY(auto result, m_client->execute_request(ActionID, MemoryView(request)));
    return Serializer::deserialize_reply(MemoryView(result));
}

Expected<hailo_power_measurement_data_t> PcieDeviceHrpcClient::get_power_measurement(
    hailo_measurement_buffer_index_t buffer_index,
    bool should_clear)
{
    (void)buffer_index;

    using Serializer = GetPowerMeasurementSerializer;
    constexpr auto ActionID = HailoRpcActionID::DEVICE__GET_POWER_MEASUREMENT;

    TRY(auto request, Serializer::serialize_request(m_handle, should_clear));
    TRY(auto result, m_client->execute_request(ActionID, MemoryView(request)));
    TRY(auto tuple, Serializer::deserialize_reply(MemoryView(result)));

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

    TRY(auto request, Serializer::serialize_request(m_handle, dvm, measurement_type));
    TRY(auto result, m_client->execute_request(ActionID, MemoryView(request)));

    return Serializer::deserialize_reply(MemoryView(result));
}

hailo_status PcieDeviceHrpcClient::stop_power_measurement()
{
    using Serializer = StopPowerMeasurementSerializer;
    constexpr auto ActionID = HailoRpcActionID::DEVICE__STOP_POWER_MEASUREMENT;

    TRY(auto request, Serializer::serialize_request(m_handle));
    TRY(auto result, m_client->execute_request(ActionID, MemoryView(request)));

    return Serializer::deserialize_reply(MemoryView(result));
}

hailo_status PcieDeviceHrpcClient::dma_map(void *address, size_t size, hailo_dma_buffer_direction_t data_direction)
{
    auto driver = m_client->get_driver();
    if (nullptr == driver) {
        return HAILO_SUCCESS;
    }
    return VdmaDevice::dma_map_impl(*driver.get(), address, size, data_direction);
}

hailo_status PcieDeviceHrpcClient::dma_unmap(void *address, size_t size, hailo_dma_buffer_direction_t data_direction)
{
    auto driver = m_client->get_driver();
    if (nullptr == driver) {
        return HAILO_SUCCESS;
    }
    return VdmaDevice::dma_unmap_impl(*driver.get(), address, size, data_direction);
}

hailo_status PcieDeviceHrpcClient::dma_map_dmabuf(int dmabuf_fd, size_t size, hailo_dma_buffer_direction_t data_direction)
{
    auto driver = m_client->get_driver();
    if (nullptr == driver) {
        return HAILO_SUCCESS;
    }
    return VdmaDevice::dma_map_dmabuf_impl(*driver.get(), dmabuf_fd, size, data_direction);
}

hailo_status PcieDeviceHrpcClient::dma_unmap_dmabuf(int dmabuf_fd, size_t size, hailo_dma_buffer_direction_t data_direction)
{
    auto driver = m_client->get_driver();
    if (nullptr == driver) {
        return HAILO_SUCCESS;
    }
    return VdmaDevice::dma_unmap_dmabuf_impl(*driver.get(), dmabuf_fd, size, data_direction);
}

} /* namespace hailort */
