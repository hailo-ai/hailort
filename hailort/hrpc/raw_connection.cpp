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

#ifdef _WIN32
#include "hrpc/os/windows/raw_connection_internal.hpp"
#else
#include "hrpc/os/posix/raw_connection_internal.hpp"
#endif

#define HAILO_FORCE_SOCKET_COM_ENV_VAR "HAILO_FORCE_SOCKET_COM"

using namespace hrpc;


Expected<std::shared_ptr<ConnectionContext>> ConnectionContext::create_shared(bool is_accepting)
{
    // The env var HAILO_FORCE_HRPC_CLIENT_ENV_VAR is supported for debug purposes
    char *socket_com = std::getenv(HAILO_FORCE_SOCKET_COM_ENV_VAR); // TODO: Remove duplication
    auto force_socket_com = (nullptr != socket_com) && ("1" == std::string(socket_com));

    if (force_socket_com || VDevice::force_hrpc_client()) {// If forcing hrpc service, its because we work without EP driver -> use sockets
        return OsConnectionContext::create_shared(is_accepting);
    } else {
        return PcieConnectionContext::create_shared(is_accepting);
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