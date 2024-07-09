/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file vdevice_hrpc_client.cpp
 * @brief VDevice HRPC client implementation
 **/

#include "vdevice_hrpc_client.hpp"
#include "hailo/hailort.h"
#include "hrpc_protocol/serializer.hpp"
#include "net_flow/pipeline/infer_model_hrpc_client.hpp"

namespace hailort
{

Expected<std::unique_ptr<VDevice>> VDeviceHrpcClient::create(const hailo_vdevice_params_t &params)
{
    CHECK_AS_EXPECTED(params.device_count == 1, HAILO_OUT_OF_PHYSICAL_DEVICES, "Only single device is supported!");

    auto client = make_shared_nothrow<hrpc::Client>();
    CHECK_NOT_NULL(client, HAILO_INTERNAL_FAILURE);

    auto status = client->connect();
    CHECK_SUCCESS_AS_EXPECTED(status, "Failed to connect to server");

    TRY(auto request, CreateVDeviceSerializer::serialize_request(params));
    TRY(auto result, client->execute_request(HailoRpcActionID::VDEVICE__CREATE, MemoryView(request)));
    TRY(auto tuple, CreateVDeviceSerializer::deserialize_reply(MemoryView(result)));
    status = std::get<0>(tuple);
    CHECK_SUCCESS_AS_EXPECTED(status);

    auto vdevice_handle = std::get<1>(tuple);
    auto vdevice_client = make_unique_nothrow<VDeviceHrpcClient>(std::move(client), vdevice_handle);
    CHECK_NOT_NULL(vdevice_client, HAILO_OUT_OF_HOST_MEMORY);

    return std::unique_ptr<VDevice>(std::move(vdevice_client));
}

VDeviceHrpcClient::~VDeviceHrpcClient()
{
    if (INVALID_HANDLE_ID == m_handle) {
        return;
    }

    auto request = DestroyVDeviceSerializer::serialize_request(m_handle);
    if (!request) {
        LOGGER__CRITICAL("Failed to serialize VDevice_release request");
        return;
    }

    auto result = m_client->execute_request(HailoRpcActionID::VDEVICE__DESTROY, MemoryView(*request));
    if (!result) {
        LOGGER__CRITICAL("Failed to destroy VDevice! status = {}", result.status());
    }

    if (HAILO_SUCCESS != DestroyVDeviceSerializer::deserialize_reply(MemoryView(*result))) {
        LOGGER__CRITICAL("Failed to destroy VDevice! status = {}", result.status());
    }
}

Expected<std::shared_ptr<InferModel>> VDeviceHrpcClient::create_infer_model(const std::string &hef_path, const std::string &network_name)
{
    CHECK_AS_EXPECTED(network_name.empty(), HAILO_NOT_IMPLEMENTED, "Passing network name is not supported yet!");

    FileReader hef_reader(hef_path);
    auto status = hef_reader.open();
    CHECK_SUCCESS(status);

    TRY(auto hef_size, hef_reader.get_size());
    TRY(auto hef_buffer, Buffer::create(hef_size));
    status = hef_reader.read(hef_buffer.data(), hef_size);
    CHECK_SUCCESS(status);

    status = hef_reader.close();
    CHECK_SUCCESS(status);

    TRY(auto request, CreateInferModelSerializer::serialize_request(m_handle, hef_size));
    TRY(auto result, m_client->execute_request(HailoRpcActionID::VDEVICE__CREATE_INFER_MODEL,
        MemoryView(request), [&hef_buffer] (hrpc::RpcConnection connection) -> hailo_status {
        // TODO: change write to accept uint64_t, or accept file stream instead or write in chunks
        auto status = connection.write_buffer(MemoryView(hef_buffer));
        CHECK_SUCCESS(status);

        return HAILO_SUCCESS;
    }));
    TRY(auto tuple, CreateInferModelSerializer::deserialize_reply(MemoryView(result)));

    CHECK_SUCCESS_AS_EXPECTED(std::get<0>(tuple));
    auto infer_model_handle = std::get<1>(tuple);

    TRY(auto hef, Hef::create(MemoryView(hef_buffer)));
    TRY(auto infer_model, InferModelHrpcClient::create(std::move(hef), m_client, infer_model_handle, m_handle, *this));

    return std::shared_ptr<InferModel>(std::move(infer_model));
}

Expected<ConfiguredNetworkGroupVector> VDeviceHrpcClient::configure(Hef &hef, const NetworkGroupsParamsMap &configure_params)
{
    (void)m_handle;
    (void)hef;
    (void)configure_params;
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

Expected<std::vector<std::reference_wrapper<Device>>> VDeviceHrpcClient::get_physical_devices() const
{
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

Expected<std::vector<std::string>> VDeviceHrpcClient::get_physical_devices_ids() const
{
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

// Currently only homogeneous vDevice is allow (= all devices are from the same type)
Expected<hailo_stream_interface_t> VDeviceHrpcClient::get_default_streams_interface() const
{
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

hailo_status VDeviceHrpcClient::dma_map(void *address, size_t size, hailo_dma_buffer_direction_t direction)
{
    (void)address;
    (void)size;
    (void)direction;
    return HAILO_SUCCESS; // TODO: implement this (HRT-13689)
}

hailo_status VDeviceHrpcClient::dma_unmap(void *address, size_t size, hailo_dma_buffer_direction_t direction)
{
    (void)address;
    (void)size;
    (void)direction;
    return HAILO_SUCCESS; // TODO: implement this (HRT-13689)
}

hailo_status VDeviceHrpcClient::dma_map_dmabuf(int dmabuf_fd, size_t size, hailo_dma_buffer_direction_t direction)
{
    (void)dmabuf_fd;
    (void)size;
    (void)direction;
    return HAILO_SUCCESS; // TODO: implement this (HRT-13689)
}

hailo_status VDeviceHrpcClient::dma_unmap_dmabuf(int dmabuf_fd, size_t size, hailo_dma_buffer_direction_t direction)
{
    (void)dmabuf_fd;
    (void)size;
    (void)direction;
    return HAILO_SUCCESS; // TODO: implement this (HRT-13689)
}

} /* namespace hailort */
