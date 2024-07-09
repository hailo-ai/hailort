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
    virtual hailo_status write(const uint8_t *buffer, size_t size) override;
    virtual hailo_status read(uint8_t *buffer, size_t size) override;
    virtual hailo_status close() override;

    OsRawConnection(int fd, std::shared_ptr<OsConnectionContext> context) : m_fd(fd), m_context(context) {}
private:
    int m_fd;
    std::shared_ptr<OsConnectionContext> m_context;
};

} // namespace hrpc

#endif // _POSIX_RAW_CONNECTION_INTERNAL_HPP_