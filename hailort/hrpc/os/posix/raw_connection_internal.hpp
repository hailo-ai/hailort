/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file raw_connection_internal.hpp
 * @brief Raw Connection Header for sockets based comunication
 **/

#ifndef _POSIX_RAW_CONNECTION_INTERNAL_HPP_
#define _POSIX_RAW_CONNECTION_INTERNAL_HPP_

#include "hailo/expected.hpp"
#include "hrpc/raw_connection.hpp"

#include <memory>

using namespace hailort;

namespace hrpc
{

class OsConnectionContext : public ConnectionContext
{
public:
    static Expected<std::shared_ptr<ConnectionContext>> create_shared(bool is_accepting);

    OsConnectionContext(bool is_accepting) : ConnectionContext(is_accepting) {}

    virtual ~OsConnectionContext() = default;
};

class OsRawConnection : public RawConnection
{
public:
    static Expected<std::shared_ptr<RawConnection>> create_shared(std::shared_ptr<OsConnectionContext> context);

    virtual ~OsRawConnection() = default;

    virtual Expected<std::shared_ptr<RawConnection>> accept() override;
    virtual hailo_status connect() override;
    virtual hailo_status write(const uint8_t *buffer, size_t size,
        std::chrono::milliseconds timeout = DEFAULT_WRITE_TIMEOUT) override;
    virtual hailo_status read(uint8_t *buffer, size_t size,
        std::chrono::milliseconds timeout = DEFAULT_READ_TIMEOUT) override;
    virtual hailo_status close() override;

    OsRawConnection(int fd, std::shared_ptr<OsConnectionContext> context) : m_fd(fd), m_context(context) {}
private:
    static Expected<std::shared_ptr<OsRawConnection>> create_by_addr_server(std::shared_ptr<OsConnectionContext> context,
        const std::string &ip, uint16_t port);
    static Expected<std::shared_ptr<OsRawConnection>> create_by_addr_client(std::shared_ptr<OsConnectionContext> context,
        const std::string &ip, uint16_t port);
    static Expected<std::shared_ptr<OsRawConnection>> create_localhost_server(std::shared_ptr<OsConnectionContext> context);
    static Expected<std::shared_ptr<OsRawConnection>> create_localhost_client(std::shared_ptr<OsConnectionContext> context);

    hailo_status connect_by_addr(const std::string &ip, uint16_t port);
    hailo_status connect_localhost();

    static Expected<std::pair<std::string, uint16_t>> parse_ip_port(const std::string &ip_port);

    int m_fd;
    std::shared_ptr<OsConnectionContext> m_context;
};

} // namespace hrpc

#endif // _POSIX_RAW_CONNECTION_INTERNAL_HPP_