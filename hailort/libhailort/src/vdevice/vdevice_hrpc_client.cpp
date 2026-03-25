/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdevice_hrpc_client.cpp
 * @brief VDevice HRPC client implementation
 **/

#include "vdevice_hrpc_client.hpp"
#include "common/logger_macros.hpp"
#include "hailo/hailort.h"
#include "hrpc_protocol/serializer.hpp"
#include "net_flow/pipeline/infer_model_hrpc_client.hpp"
#include "utils/buffer_storage.hpp"
#include "vdma/driver/hailort_driver.hpp"
#include "device_common/usb/usb_utils.hpp"

namespace hailort
{

Expected<std::vector<std::string>> VDeviceHrpcClient::get_device_ids(const hailo_vdevice_params_t &params)
{
    std::vector<std::string> device_ids;

    if (nullptr == params.device_ids) {
        auto acc_type = HailoRTDriver::DeviceType::DEV_TYPE_MAX_VALUE;
        TRY(acc_type, VDeviceBase::get_device_type(params.device_ids, params.device_count));

        TRY(auto device_infos, HailoRTDriver::scan_devices(acc_type));
        device_ids.reserve(device_infos.size());
        for (const auto &device_info : device_infos) {
            device_ids.push_back(device_info.device_id);
        }

        if (device_ids.size() == 0) {
            TRY(auto usb_device_infos, UsbUtils::scan());
            if (usb_device_infos.size() > 0) {
                TRY(auto device_id, UsbUtils::usb_device_info_to_string(usb_device_infos.at(0)));
                device_ids.push_back(device_id);
            }
        }

        return device_ids;
    } else {
        device_ids.reserve(params.device_count);
        for (uint32_t i = 0; i < params.device_count; i++) {
            device_ids.push_back(std::string(params.device_ids[i].id));
        }
        return device_ids;
    }
}

Expected<std::tuple<std::shared_ptr<Client>, rpc_object_handle_t>>
VDeviceHrpcClient::create_available_vdevice(const std::vector<std::string> &device_ids, const hailo_vdevice_params_t &params)
{
    for (const auto &device_id : device_ids) {
        const bool is_h15_device = params.multi_process_service;
        TRY(auto client, Client::created_connected(is_h15_device ? SERVER_ADDR_USE_UNIX_SOCKET : device_id));
        TRY(auto request_buffer, client->allocate_request_buffer(), "Failed to allocate request buffer");
        TRY(auto request_size, CreateVDeviceSerializer::serialize_request(params, IS_PP_DISABLED(), MemoryView(*request_buffer)));
        auto expected_result = client->execute_request(static_cast<uint32_t>(HailoRpcActionID::VDEVICE__CREATE),
            MemoryView(request_buffer->data(), request_size));
        const auto status = expected_result.status();

        const bool device_in_use = ((HAILO_DEVICE_IN_USE == status) || (HAILO_OUT_OF_PHYSICAL_DEVICES == status));
        const bool is_any_device = (params.device_ids == nullptr);
        if (is_any_device && device_in_use) {
            continue;
        }

        TRY(auto result, expected_result);
        TRY(auto handle, CreateVDeviceSerializer::deserialize_reply(MemoryView(result.body.data(), result.header.size)));

        return std::make_tuple(client, handle); // Only single device is supported
    }

    LOGGER__ERROR("Failed to create vdevice. there are not enough free devices. requested: 1, found: 0");
    return make_unexpected(HAILO_OUT_OF_PHYSICAL_DEVICES);
}

Expected<std::unique_ptr<VDevice>> VDeviceSocketBasedClient::create(const hailo_vdevice_params_t &params)
{
    auto vdevice = make_unique_nothrow<VDeviceSocketBasedClient>(params);
    CHECK_NOT_NULL(vdevice, HAILO_OUT_OF_HOST_MEMORY);

    return std::unique_ptr<VDevice>(std::move(vdevice));
}

Expected<std::unique_ptr<VDevice>> VDeviceHrpcClient::create(const hailo_vdevice_params_t &params)
{
    CHECK(params.device_count == 1, HAILO_OUT_OF_PHYSICAL_DEVICES, "Only single device is supported!");

    TRY(auto device_ids, get_device_ids(params));
    TRY(auto tuple, create_available_vdevice(device_ids, params));
    auto client = std::get<0>(tuple);

    client->set_notification_callback(
    [callback_dispatcher_manager = client->callback_dispatcher_manager()]
    (const MemoryView &serialized_reply) -> hailo_status {
        TRY(auto rpc_callback, CallbackCalledSerializer::deserialize_reply(serialized_reply));
        auto status = callback_dispatcher_manager->at(rpc_callback.dispatcher_id)->trigger_callback(rpc_callback);
        CHECK_SUCCESS(status);

        return HAILO_SUCCESS;
    });


    auto vdevice_handle = std::get<1>(tuple);
    TRY(auto device, DeviceHrpcClient::create(client));
    auto vdevice_client = make_unique_nothrow<VDeviceHrpcClient>(params, std::move(client), vdevice_handle,
        client->callback_dispatcher_manager(), std::move(device), client->device_id());
    CHECK_NOT_NULL(vdevice_client, HAILO_OUT_OF_HOST_MEMORY);

    return std::unique_ptr<VDevice>(std::move(vdevice_client));
}

VDeviceHrpcClient::~VDeviceHrpcClient()
{
    if (INVALID_HANDLE_ID == m_handle) {
        return;
    }

    auto request_buffer = m_client->allocate_request_buffer();
    if (!request_buffer) {
        LOGGER__CRITICAL("Failed to create buffer for VDevice_release request");
        return;
    }

    auto request_size = DestroyVDeviceSerializer::serialize_request(m_handle, MemoryView(**request_buffer));
    if (!request_size) {
        LOGGER__CRITICAL("Failed to serialize VDevice_release request");
        return;
    }

    auto result_expected = m_client->execute_request(static_cast<uint32_t>(HailoRpcActionID::VDEVICE__DESTROY),
        MemoryView(request_buffer.value()->data(), *request_size));
    DTOR_LOG_ON_FAILURE(result_expected, "Failed to destroy VDevice! status = {}");
}

Expected<std::shared_ptr<InferModel>> VDeviceHrpcClient::create_infer_model(const MemoryView hef_buffer, const std::string &name)
{
    TRY(auto request_buffer, m_client->allocate_request_buffer(), "Failed to allocate request buffer");

    TRY(auto request_size, CreateInferModelSerializer::serialize_request(m_handle, hef_buffer.size(), name, MemoryView(*request_buffer)));
    TRY(auto result, m_client->execute_request(static_cast<uint32_t>(HailoRpcActionID::VDEVICE__CREATE_INFER_MODEL),
        MemoryView(request_buffer->data(), request_size), std::vector<TransferBuffer>{hef_buffer}, {}, LONG_RPC_ACTION_TIMEOUT));
    TRY(auto infer_model_handle, CreateInferModelSerializer::deserialize_reply(MemoryView(result.body.data(), result.header.size)));

    TRY(auto hef, Hef::create(hef_buffer));
    TRY(auto infer_model, InferModelHrpcClient::create(std::move(hef), name, m_client, infer_model_handle, m_handle,
        *this, m_callback_dispatcher_manager));

    return std::shared_ptr<InferModel>(std::move(infer_model));
}

Expected<std::shared_ptr<InferModel>> VDeviceHrpcClient::create_infer_model(const std::string &hef_path, const std::string &name)
{
    FileReader hef_reader(hef_path);
    auto status = hef_reader.open();
    CHECK_SUCCESS(status);

    TRY(auto hef_size, hef_reader.get_size());
    TRY(auto hef_buffer, Buffer::create(hef_size, BufferStorageParams::create_dma()));
    status = hef_reader.read(hef_buffer.data(), hef_size);
    CHECK_SUCCESS(status);

    status = hef_reader.close();
    CHECK_SUCCESS(status);

    return create_infer_model(MemoryView(hef_buffer), name);
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
    std::vector<std::reference_wrapper<Device>> result;
    result.reserve(1);
    result.push_back(*m_device);
    return result;
}

Expected<std::vector<std::string>> VDeviceHrpcClient::get_physical_devices_ids() const
{
    std::vector<std::string> result;
    result.reserve(1);
    result.push_back(m_device_id);
    return result;
}

Expected<hailo_stream_interface_t> VDeviceHrpcClient::get_default_streams_interface() const
{
    LOGGER__ERROR("Not supported. Did you try calling `create_configure_params` on H10? If so, use InferModel instead");
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

hailo_status VDeviceHrpcClient::dma_map(void *address, size_t size, hailo_dma_buffer_direction_t data_direction)
{
    return m_device->dma_map(address, size, data_direction);
}

hailo_status VDeviceHrpcClient::dma_unmap(void *address, size_t size, hailo_dma_buffer_direction_t data_direction)
{
    return m_device->dma_unmap(address, size, data_direction);
}

hailo_status VDeviceHrpcClient::dma_map_dmabuf(int dmabuf_fd, size_t size, hailo_dma_buffer_direction_t data_direction)
{
    return m_device->dma_map_dmabuf(dmabuf_fd, size, data_direction);
}

hailo_status VDeviceHrpcClient::dma_unmap_dmabuf(int dmabuf_fd, size_t size, hailo_dma_buffer_direction_t data_direction)
{
    return m_device->dma_unmap_dmabuf(dmabuf_fd, size, data_direction);
}

Expected<std::shared_ptr<Session>> VDeviceHrpcClient::create_session(uint16_t connection_port) const
{
    auto device_id = m_client->device_id();
    TRY(auto session, Session::connect(connection_port, device_id));
    return session;
}

} /* namespace hailort */
