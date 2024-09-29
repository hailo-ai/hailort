/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pcie_device_hrpc_client.cpp
 * @brief Pcie Device HRPC client implementation
 **/

#include "pcie_device_hrpc_client.hpp"
#include "vdma/driver/hailort_driver.hpp"


namespace hailort
{

Expected<std::unique_ptr<PcieDeviceHrpcClient>> PcieDeviceHrpcClient::create(const std::string &device_id)
{
    auto client = make_shared_nothrow<hrpc::Client>(device_id);
    CHECK_NOT_NULL(client, HAILO_INTERNAL_FAILURE);

    auto status = client->connect();
    CHECK_SUCCESS_AS_EXPECTED(status, "Failed to connect to server");

    return PcieDeviceHrpcClient::create(device_id, client);
}

Expected<std::unique_ptr<PcieDeviceHrpcClient>> PcieDeviceHrpcClient::create(const std::string &device_id,
    std::shared_ptr<hrpc::Client> client)
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


} /* namespace hailort */
