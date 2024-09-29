/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file raw_connection.cpp
 * @brief Raw Connection
 **/

#include "hailo/vdevice.hpp"
#include "hrpc/raw_connection.hpp"
#include "hrpc/os/pcie/raw_connection_internal.hpp"
#include "common/internal_env_vars.hpp"

#ifdef _WIN32
#include "hrpc/os/windows/raw_connection_internal.hpp"
#else
#include "hrpc/os/posix/raw_connection_internal.hpp"
#endif


using namespace hrpc;


Expected<std::shared_ptr<ConnectionContext>> ConnectionContext::create_client_shared(const std::string &device_id)
{
    auto should_force_socket_com = get_env_variable(HAILO_SOCKET_COM_ADDR_CLIENT_ENV_VAR);

    // If forcing hrpc service, its because we work without EP driver -> use sockets
    if (should_force_socket_com.has_value() || VDevice::should_force_hrpc_client()) {
        return OsConnectionContext::create_shared(false);
    } else {
        return PcieConnectionContext::create_client_shared(device_id);
    }
}

Expected<std::shared_ptr<ConnectionContext>> ConnectionContext::create_server_shared()
{
    auto should_force_socket_com = get_env_variable(HAILO_SOCKET_COM_ADDR_SERVER_ENV_VAR);

    // If forcing hrpc service, its because we work without EP driver -> use sockets
    if (should_force_socket_com.has_value() || VDevice::should_force_hrpc_client()) {
        return OsConnectionContext::create_shared(true);
    } else {
        return PcieConnectionContext::create_server_shared();
    }
}

Expected<std::shared_ptr<RawConnection>> RawConnection::create_shared(std::shared_ptr<ConnectionContext> context)
{
    // Create according to ConnectionContext type
    auto os_connection_context = std::dynamic_pointer_cast<OsConnectionContext>(context);
    if (os_connection_context != nullptr) {
        return OsRawConnection::create_shared(os_connection_context);
    } else {
        return PcieRawConnection::create_shared(std::dynamic_pointer_cast<PcieConnectionContext>(context));
    }
}