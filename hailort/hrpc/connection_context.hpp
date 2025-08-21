/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file connection_context.hpp
 * @brief Connection Context - holds the driver instance
 **/

#ifndef _HAILO_CONNECTION_CONTEXT_HPP_
#define _HAILO_CONNECTION_CONTEXT_HPP_

#include "hailo/expected.hpp"

#include <memory>
#include <string>

static const std::string SERVER_ADDR_USE_UNIX_SOCKET = "unix-socket";

namespace hailort
{

class HailoRTDriver;

class ConnectionContext
{
public:
    static Expected<std::shared_ptr<ConnectionContext>> create_client_shared(const std::string &device_id = "");
    static Expected<std::shared_ptr<ConnectionContext>> create_server_shared(const std::string &ip = "");

    bool is_accepting() const { return m_is_accepting; }
    virtual std::shared_ptr<HailoRTDriver> get_driver() { return nullptr; };

    ConnectionContext(bool is_accepting) : m_is_accepting(is_accepting) {}
    virtual ~ConnectionContext() = default;

protected:
    bool m_is_accepting;
};

} // namespace hailort

#endif // _HAILO_CONNECTION_CONTEXT_HPP_