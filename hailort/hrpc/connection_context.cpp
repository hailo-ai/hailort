/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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
#include "hrpc/raw_connection_internal/pcie/hailo_session_internal.hpp"
#include "hrpc/raw_connection_internal/socket/hailo_session_internal.hpp"
#include "vdma/driver/hailort_driver.hpp"

namespace hailort
{

Expected<std::shared_ptr<ConnectionContext>> ConnectionContext::create_client_shared(const std::string &device_id)
{
    if (device_id.empty()) {
        return PcieConnectionContext::create_client_shared(device_id);
    }

    if (SERVER_ADDR_USE_UNIX_SOCKET == device_id) {
        return OsConnectionContext::create_client_shared(device_id);
    }

    TRY(auto device_type, Device::get_device_type(device_id));
    if (Device::Type::ETH == device_type) {
        return OsConnectionContext::create_client_shared(device_id);
    } else {
        return PcieConnectionContext::create_client_shared(device_id);
    }
}

Expected<std::shared_ptr<ConnectionContext>> ConnectionContext::create_server_shared(const std::string& ip)
{
    if (!ip.empty()) {
        return OsConnectionContext::create_server_shared(ip);
    } else {
        return PcieConnectionContext::create_server_shared();
    }
}

} // namespace hailort