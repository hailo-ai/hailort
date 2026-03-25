/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file connection_context.cpp
 * @brief Connection Context
 **/

#include "connection_context.hpp"
#include "hailo/hailo_session.hpp"
#include "common/internal_env_vars.hpp"
#include "hailo/device.hpp"
#include "hrpc/session_internal/pcie_session_internal.hpp"
#include "hrpc/session_internal/eth_session.hpp"
#include "hrpc/session_internal/usb/usb_session.hpp"
#include "vdma/driver/hailort_driver.hpp"

namespace hailort
{

Expected<std::shared_ptr<ConnectionContext>> ConnectionContext::create_client_shared(const std::string &device_id)
{
    if (device_id.empty()) {
        return PcieConnectionContext::create_client_shared(device_id);
    }

    if (SERVER_ADDR_USE_UNIX_SOCKET == device_id) {
        return OsConnectionContext::create_client_shared(device_id, true);
    }

    TRY(auto device_type, Device::get_device_type(device_id));
    switch (device_type) {
    case Device::Type::ETH:
        return OsConnectionContext::create_client_shared(device_id, false);
    case Device::Type::PCIE:
        return PcieConnectionContext::create_client_shared(device_id);
    case Device::Type::USB:
        return UsbConnectionContext::create_client_shared(device_id);
    default:
        LOGGER__ERROR("Invalid device type {}", static_cast<uint32_t>(device_type));
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }
}

Expected<std::shared_ptr<ConnectionContext>> ConnectionContext::create_server_shared(const std::string &device_id)
{
    if (device_id.empty() || (device_id.find("pcie") != std::string::npos)) {
        return PcieConnectionContext::create_server_shared();
    }
    if (device_id.find("usb") != std::string::npos) {
        return UsbConnectionContext::create_server_shared();
    }
    return OsConnectionContext::create_server_shared(device_id);
}

} // namespace hailort
