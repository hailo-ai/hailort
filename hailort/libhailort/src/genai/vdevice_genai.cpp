/**
 * Copyright (c) 2019-2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdevice_genai.cpp
 * @brief VDeviceGenAI implementation
 **/

#include "hailo/genai/vdevice_genai.hpp"
#include "hailo/hailort.h"
#include "common/utils.hpp"
#include "common/internal_env_vars.hpp"
#include "hrpc/connection_context.hpp"
#include "hrpc/rpc_connection.hpp"
#include "vdevice/vdevice_hrpc_client.hpp"

namespace hailort
{
namespace genai
{

Expected<std::shared_ptr<VDeviceGenAI>> VDeviceGenAI::create_shared()
{
    hailo_vdevice_params_t params {};
    CHECK_SUCCESS_AS_EXPECTED(hailo_init_vdevice_params(&params));
    return create_shared(params);
}

hailo_status VDeviceGenAI::validate_params(const hailo_vdevice_params_t &params)
{
    CHECK_AS_EXPECTED(params.device_count == 1, HAILO_OUT_OF_PHYSICAL_DEVICES, "Only single device is supported!");
    CHECK_AS_EXPECTED(params.multi_process_service == false, HAILO_NOT_SUPPORTED, "Multi proc service is not supported for GenAI");

    return HAILO_SUCCESS;
}

Expected<hailo_device_id_t> get_device_id(const hailo_vdevice_params_t &params)
{
    hailo_device_id_t device_id = {};

    TRY(auto device_id_str, VDeviceHrpcClient::get_device_id(params));
    std::strncpy(device_id.id, device_id_str.c_str(),
        (device_id_str.length() + 1));

    return device_id;
}

Expected<std::shared_ptr<VDeviceGenAI>> VDeviceGenAI::create_shared(const hailo_vdevice_params_t &params)
{
    CHECK_SUCCESS_AS_EXPECTED(validate_params(params));
    // When using socket-based hrpc-iface, there is no need to get device-id
    hailo_device_id_t device_id = {};
    if (!hailort::VDevice::should_force_hrpc_client()) {
        TRY(device_id, get_device_id(params));
    }
    auto vdevice_genai = make_shared_nothrow<VDeviceGenAI>(device_id);
    CHECK_NOT_NULL(vdevice_genai, HAILO_OUT_OF_HOST_MEMORY);

    return vdevice_genai;
}

VDeviceGenAI::VDeviceGenAI(hailo_device_id_t device_id) :
    m_device_id(device_id)
{}

Expected<std::shared_ptr<GenAISession>> VDeviceGenAI::create_session(uint16_t port)
{
    return GenAISession::create_shared(port, m_device_id.id);
}

Expected<std::shared_ptr<GenAISession>> GenAISession::create_shared(uint16_t port, const std::string &device_id)
{
    TRY(auto session, Session::connect(port, device_id));
    auto ptr = make_shared_nothrow<GenAISession>(session);
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY);
    return ptr;
}

GenAISession::GenAISession(std::shared_ptr<Session> session) :
    m_session(session)
{}

hailo_status GenAISession::write(const uint8_t *buffer, size_t size, std::chrono::milliseconds timeout)
{
    // First we send the buffer's size. Then the buffer itself.
    // TODO: Use hrpc protocol
    auto status = m_session->write(reinterpret_cast<const uint8_t*>(&size), sizeof(size), timeout);
    CHECK_SUCCESS(status);

    status = m_session->write(buffer, size, timeout);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

Expected<size_t> GenAISession::read(uint8_t *buffer, size_t size, std::chrono::milliseconds timeout)
{
    auto start_time = std::chrono::steady_clock::now();
	size_t size_to_read = 0;
	auto status = m_session->read(reinterpret_cast<uint8_t*>(&size_to_read), sizeof(size_to_read), timeout);
    CHECK_SUCCESS(status);

    CHECK(size_to_read <= size, HAILO_INVALID_OPERATION,
        "Read buffer is smaller then necessary. Buffer size = {}, generation size = {}", size, size_to_read);

    auto elapsed_time =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time);
    status = m_session->read(buffer, size_to_read, (timeout - elapsed_time));
    CHECK_SUCCESS(status);

    return size_to_read;
}


} /* namespace genai */
} /* namespace hailort */
