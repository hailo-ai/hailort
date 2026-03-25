/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailo_session.cpp
 * @brief Hailo Session
 **/

#include "hailo/hailo_session.hpp"
#include "hrpc/session_internal/pcie_session_internal.hpp"
#include "hrpc/session_internal/eth_session.hpp"
#include "hrpc/session_internal/usb/usb_session.hpp"
#include "connection_context.hpp"
#include "vdma/transfer_common.hpp"

namespace hailort
{

Expected<std::shared_ptr<SessionListener>> SessionListener::create_shared(uint16_t port, const std::string &device_id)
{
    TRY(auto context, ConnectionContext::create_server_shared(device_id));
    return SessionListener::create_shared(context, port);
}

Expected<std::shared_ptr<SessionListener>> SessionListener::create_shared(std::shared_ptr<ConnectionContext> context, uint16_t port)
{
    // Create according to ConnectionContext device_type
    switch (context->device_type()) {
    case Device::Type::ETH:
    case Device::Type::INTEGRATED:
        return OsListener::create_shared(std::static_pointer_cast<OsConnectionContext>(context), port);
    case Device::Type::USB:
#ifdef __linux__
        return UsbListener::create_shared(std::static_pointer_cast<UsbConnectionContext>(context), port);
#else
        return make_unexpected(HAILO_NOT_SUPPORTED);
#endif
    case Device::Type::PCIE:
        return RawPcieListener::create_shared(std::static_pointer_cast<PcieConnectionContext>(context), port);
    default:
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }
}

Expected<std::shared_ptr<Session>> Session::connect(uint16_t port, const std::string &device_id)
{
    TRY(auto context, ConnectionContext::create_client_shared(device_id));
    return Session::connect(context, port);
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
    // Create according to ConnectionContext device_type
    switch (context->device_type()) {
    case Device::Type::ETH:
    case Device::Type::INTEGRATED:
        return OsSession::connect(std::static_pointer_cast<OsConnectionContext>(context), port);
    case Device::Type::USB:
        return UsbSession::connect(std::static_pointer_cast<UsbConnectionContext>(context), port);
    case Device::Type::PCIE:
        return RawPcieSession::connect(std::static_pointer_cast<PcieConnectionContext>(context), port);
    default:
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }
}

constexpr std::chrono::milliseconds Session::DEFAULT_WRITE_TIMEOUT;
constexpr std::chrono::milliseconds Session::DEFAULT_READ_TIMEOUT;

} // namespace hailort
