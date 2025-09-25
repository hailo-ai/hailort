/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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
#include "common/genai/session_wrapper/session_wrapper.hpp"


namespace hailort
{
namespace genai
{

const std::string FILE_NOT_FOUND = "<file_not_found>";

Expected<std::shared_ptr<VDeviceGenAI>> VDeviceGenAI::create_shared()
{
    hailo_vdevice_params_t params {};
    CHECK_SUCCESS_AS_EXPECTED(hailo_init_vdevice_params(&params));
    return create_shared(params);
}

hailo_status VDeviceGenAI::validate_params(const hailo_vdevice_params_t &params)
{
    CHECK_AS_EXPECTED(params.device_count == 1, HAILO_OUT_OF_PHYSICAL_DEVICES, "Only single device is supported!");
    CHECK_AS_EXPECTED(params.multi_process_service == false, HAILO_NOT_SUPPORTED, "Working with multi-process service is not supported for GenAI");
    CHECK_AS_EXPECTED(params.scheduling_algorithm != HAILO_SCHEDULING_ALGORITHM_NONE, HAILO_NOT_SUPPORTED, "Working without schecduler is not supported for GenAI");

    return HAILO_SUCCESS;
}

Expected<hailo_device_id_t> get_device_id(const hailo_vdevice_params_t &params)
{
    hailo_device_id_t device_id = {};

    TRY(auto device_ids, VDeviceHrpcClient::get_device_ids(params));
    std::strncpy(device_id.id, device_ids[0].c_str(), (device_ids[0].length() + 1));

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
    auto vdevice_genai = make_shared_nothrow<VDeviceGenAI>(device_id, params);
    CHECK_NOT_NULL(vdevice_genai, HAILO_OUT_OF_HOST_MEMORY);

    return vdevice_genai;
}

VDeviceGenAI::VDeviceGenAI(hailo_device_id_t device_id, const hailo_vdevice_params_t &params) :
    m_device_id(device_id), m_vdevice_params(params)
{}

Expected<std::shared_ptr<GenAISession>> VDeviceGenAI::create_session(uint16_t port)
{
    return GenAISession::create_shared(port, m_device_id.id);
}

Expected<std::shared_ptr<GenAISession>> GenAISession::create_shared(uint16_t port, const std::string &device_id)
{
    TRY(auto session, Session::connect(port, device_id));
    auto session_wrapper_ptr = make_shared_nothrow<SessionWrapper>(session);
    CHECK_NOT_NULL_AS_EXPECTED(session_wrapper_ptr, HAILO_OUT_OF_HOST_MEMORY);

    auto ptr = make_shared_nothrow<GenAISession>(session_wrapper_ptr);
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY);
    return ptr;
}

GenAISession::GenAISession(std::shared_ptr<SessionWrapper> session_wrapper) :
    m_session_wrapper(session_wrapper)
{}

hailo_status GenAISession::write(MemoryView buffer, std::chrono::milliseconds timeout)
{
    return m_session_wrapper->write(buffer, timeout);
}

Expected<size_t> GenAISession::read(MemoryView buffer, std::chrono::milliseconds timeout)
{
    return m_session_wrapper->read(buffer, timeout);
}

Expected<std::shared_ptr<Buffer>> GenAISession::read(std::chrono::milliseconds timeout)
{
    return m_session_wrapper->read(timeout);
}

Expected<std::string> GenAISession::get_ack(std::chrono::milliseconds timeout)
{
    // TODO (HRT-15334): - adjusting all ack's once server is written in cpp, validate the ack
    std::string server_ack(SERVER_ACK_SIZE, '\0');
    TRY(auto size, read(server_ack, timeout));
    return (0 == size)? "" : server_ack;
}

hailo_status GenAISession::send_file(const std::string &path)
{
    if ((BUILTIN == path)) {
        // Write the `BUILTIN` indicator
        auto status = write(path);
        CHECK_SUCCESS(status);
    } else {
        // Send file bytes
        TRY(auto file_data, read_binary_file(path, BufferStorageParams::create_dma()));
        auto status = write(MemoryView(file_data));
        CHECK_SUCCESS(status);
    }

    // Ack from server - finished sending data file
    TRY(auto ack, get_ack());
    CHECK(ack != FILE_NOT_FOUND, HAILO_NOT_FOUND, "Builtin file does not exist");
    LOGGER__INFO("Sent file - '{}', Received ack from server - '{}'", path, ack);

    return HAILO_SUCCESS;
}

} /* namespace genai */
} /* namespace hailort */
