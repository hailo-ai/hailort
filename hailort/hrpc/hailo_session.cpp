/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailo_session.cpp
 * @brief Hailo Session
 **/

#include "hailo/hailo_session.hpp"
#include "hrpc/raw_connection_internal/pcie/hailo_session_internal.hpp"
#include "hrpc/raw_connection_internal/socket/hailo_session_internal.hpp"
#include "connection_context.hpp"
#include "vdma/channel/transfer_common.hpp"

namespace hailort
{

Expected<std::shared_ptr<SessionListener>> SessionListener::create_shared(uint16_t port, const std::string &ip)
{
    TRY(auto context, ConnectionContext::create_server_shared(ip));
    return SessionListener::create_shared(context, port);
}

Expected<std::shared_ptr<SessionListener>> SessionListener::create_shared(std::shared_ptr<ConnectionContext> context, uint16_t port)
{
    // Create according to ConnectionContext type
    auto os_connection_context = std::dynamic_pointer_cast<OsConnectionContext>(context);
    // using BACKLOG_SIZE
    if (os_connection_context != nullptr) {
        return OsListener::create_shared(os_connection_context, port);
    } else {
        return RawPcieListener::create_shared(std::dynamic_pointer_cast<PcieConnectionContext>(context), port);
    }
}

Expected<std::shared_ptr<Session>> Session::connect(uint16_t port, const std::string &device_id)
{
    TRY(auto context, ConnectionContext::create_client_shared(device_id));
    auto os_connection_context = std::dynamic_pointer_cast<OsConnectionContext>(context);
    if (os_connection_context != nullptr) {
        return OsSession::connect(os_connection_context, port);
    } else {
        return RawPcieSession::connect(std::dynamic_pointer_cast<PcieConnectionContext>(context), port);
    }
}

hailo_status Session::write_async(const uint8_t *buffer, size_t size, std::function<void(hailo_status)> &&callback)
{
    return write_async(to_request(const_cast<uint8_t *>(buffer), size, std::move(callback)));
}

hailo_status Session::read_async(uint8_t *buffer, size_t size, std::function<void(hailo_status)> &&callback)
{
    return read_async(to_request(buffer, size, std::move(callback)));
}

Expected<std::shared_ptr<Session>> Session::connect(std::shared_ptr<ConnectionContext> context, uint16_t port)
{
    // Create according to ConnectionContext type
    auto os_connection_context = std::dynamic_pointer_cast<OsConnectionContext>(context);
    if (os_connection_context != nullptr) {
        return OsSession::connect(os_connection_context, port);
    } else {
        return RawPcieSession::connect(std::dynamic_pointer_cast<PcieConnectionContext>(context), port);
    }
}

constexpr std::chrono::milliseconds Session::DEFAULT_WRITE_TIMEOUT;
constexpr std::chrono::milliseconds Session::DEFAULT_READ_TIMEOUT;

} // namespace hailort