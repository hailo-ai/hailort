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
#include "hailo/vdevice.hpp"
#include "hrpc/raw_connection_internal/pcie/hailo_session_internal.hpp"
#include "hrpc/raw_connection_internal/socket/hailo_session_internal.hpp"

namespace hailort
{

Expected<std::shared_ptr<ConnectionContext>> ConnectionContext::create_client_shared(const std::string &device_id)
{
    auto should_force_socket_com = get_env_variable(HAILO_SOCKET_COM_ADDR_CLIENT_ENV_VAR);

    // If forcing hrpc service, its because we work without EP driver -> use sockets
    if (should_force_socket_com.has_value()) {
        return OsConnectionContext::create_shared(false);
    } else {
        return PcieConnectionContext::create_client_shared(device_id);
    }
}

Expected<std::shared_ptr<ConnectionContext>> ConnectionContext::create_server_shared()
{
    auto should_force_socket_com = get_env_variable(HAILO_SOCKET_COM_ADDR_SERVER_ENV_VAR);

    // If forcing hrpc service, its because we work without EP driver -> use sockets
    if (should_force_socket_com.has_value()) {
        return OsConnectionContext::create_shared(true);
    } else {
        return PcieConnectionContext::create_server_shared();
    }
}

} // namespace hailort