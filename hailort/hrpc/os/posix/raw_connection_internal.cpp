/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file raw_connection_internal.cpp
 * @brief Linux Sockets Raw Connection
 **/

#include "hrpc/os/posix/raw_connection_internal.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <string>
#include <unistd.h>

#include "common/logger_macros.hpp"
#include "common/utils.hpp"
#include "common/internal_env_vars.hpp"
#include "hailo/hailort.h"

using namespace hrpc;

Expected<std::shared_ptr<ConnectionContext>> OsConnectionContext::create_shared(bool is_accepting)
{
    auto ptr = make_shared_nothrow<OsConnectionContext>(is_accepting);
    CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::dynamic_pointer_cast<ConnectionContext>(ptr);
}

Expected<std::shared_ptr<OsRawConnection>> OsRawConnection::create_localhost_server(std::shared_ptr<OsConnectionContext> context)
{
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK_AS_EXPECTED(fd >= 0, HAILO_OPEN_FILE_FAILURE, "Socket creation error, errno = {}", errno);

    struct sockaddr_un server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    std::string addr = "/tmp/unix_socket";
    strncpy(server_addr.sun_path, addr.c_str(), addr.size());

    unlink(addr.c_str());
    int result = ::bind(fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    CHECK_AS_EXPECTED(result >= 0, HAILO_FILE_OPERATION_FAILURE, "Bind error, errno = {}", errno);

    result = ::listen(fd, 5);
    CHECK_AS_EXPECTED(result >= 0, HAILO_FILE_OPERATION_FAILURE, "Listen error, errno = {}", errno);

    auto ptr = make_shared_nothrow<OsRawConnection>(fd, context);
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY);
    return ptr;
}

Expected<std::shared_ptr<OsRawConnection>> OsRawConnection::create_localhost_client(std::shared_ptr<OsConnectionContext> context)
{
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK_AS_EXPECTED(fd >= 0, HAILO_OPEN_FILE_FAILURE, "Socket creation error, errno = {}", errno);
    
    auto ptr = make_shared_nothrow<OsRawConnection>(fd, context);
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY);
    return ptr;
}

Expected<std::shared_ptr<OsRawConnection>> OsRawConnection::create_by_addr_server(std::shared_ptr<OsConnectionContext> context,
    const std::string &ip, uint16_t port)
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    CHECK_AS_EXPECTED(fd >= 0, HAILO_OPEN_FILE_FAILURE, "Socket creation error, errno = {}", errno);

    sockaddr_in server_addr = {};
    socklen_t addr_len = sizeof(server_addr);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    auto inet_rc = inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr);
    CHECK_AS_EXPECTED(1 == inet_rc, HAILO_ETH_FAILURE,
        "Failed to run 'inet_pton', errno = {}. make sure 'HAILO_SOCKET_COM_ADDR_SERVER' is set correctly (ip:port)", errno);

    int result = ::bind(fd, (struct sockaddr*)&server_addr, addr_len);
    CHECK_AS_EXPECTED(result >= 0, HAILO_FILE_OPERATION_FAILURE, "Bind error, errno = {}", errno);

    result = ::listen(fd, 5);
    CHECK_AS_EXPECTED(result >= 0, HAILO_FILE_OPERATION_FAILURE, "Listen error, errno = {}", errno);

    auto res = make_shared_nothrow<OsRawConnection>(fd, context);
    CHECK_NOT_NULL_AS_EXPECTED(res, HAILO_OUT_OF_HOST_MEMORY);
    return res;
}

Expected<std::shared_ptr<OsRawConnection>> OsRawConnection::create_by_addr_client(std::shared_ptr<OsConnectionContext> context,
    const std::string &ip, uint16_t port)
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    CHECK_AS_EXPECTED(fd >= 0, HAILO_OPEN_FILE_FAILURE, "Socket creation error, errno = {}", errno);

    sockaddr_in server_addr = {};
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    auto inet_rc = inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr);
    CHECK_AS_EXPECTED(1 == inet_rc, HAILO_ETH_FAILURE,
        "Failed to run 'inet_pton', errno = {}. make sure 'HAILO_SOCKET_COM_ADDR_CLIENT' is set correctly (ip:port)", errno);

    auto res = make_shared_nothrow<OsRawConnection>(fd, context);
    CHECK_NOT_NULL_AS_EXPECTED(res, HAILO_OUT_OF_HOST_MEMORY);
    return res;
}

Expected<std::pair<std::string, uint16_t>> OsRawConnection::parse_ip_port(const std::string &ip_port)
{
    std::istringstream ss(ip_port);
    std::string ip;
    uint16_t port;

    if (std::getline(ss, ip, ':') && (ss >> port)) {
        return std::make_pair(ip, port);
    }
    CHECK_AS_EXPECTED(false, HAILO_INVALID_ARGUMENT ,"Failed to parse ip and port. Format should be as follows: 'X.X.X.X:PP' (e.g. 127.0.0.1:2000)");
}

Expected<std::shared_ptr<RawConnection>> OsRawConnection::create_shared(std::shared_ptr<OsConnectionContext> context)
{
    std::shared_ptr<RawConnection> ptr;
    if (context->is_accepting()) {
        auto force_socket_com_value = get_env_variable(HAILO_SOCKET_COM_ADDR_SERVER_ENV_VAR);
        CHECK_EXPECTED(force_socket_com_value); // We know its set, otherwise we'll be working with PCIeRawCon
        if (HAILO_SOCKET_COM_ADDR_UNIX_SOCKET == force_socket_com_value.value()) {
            TRY(ptr, create_localhost_server(context));
        } else {
            TRY(auto ip_port_pair, parse_ip_port(force_socket_com_value.value()));
            TRY(ptr, create_by_addr_server(context, std::get<0>(ip_port_pair), std::get<1>(ip_port_pair)));
        }
    } else {
        auto force_socket_com_value = get_env_variable(HAILO_SOCKET_COM_ADDR_CLIENT_ENV_VAR);
        CHECK_EXPECTED(force_socket_com_value); // We know its set, otherwise we'll be working with PCIeRawCon
        if (HAILO_SOCKET_COM_ADDR_UNIX_SOCKET == force_socket_com_value.value()) {
            TRY(ptr, create_localhost_client(context));
        } else {
            TRY(auto ip_port_pair, parse_ip_port(force_socket_com_value.value()));
            TRY(ptr, create_by_addr_client(context, std::get<0>(ip_port_pair), std::get<1>(ip_port_pair)));
        }
    }

    return ptr;
}

Expected<std::shared_ptr<RawConnection>> OsRawConnection::accept()
{
    int fd = ::accept(m_fd, nullptr, nullptr);
    CHECK_AS_EXPECTED(fd >= 0, HAILO_FILE_OPERATION_FAILURE, "Accept error, errno = {}", errno);

    std::shared_ptr<RawConnection> ptr = make_shared_nothrow<OsRawConnection>(fd, m_context);
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY);

    return ptr;
}

hailo_status OsRawConnection::connect_localhost()
{
    struct sockaddr_un server_addr;
    std::string addr = "/tmp/unix_socket";

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, addr.c_str(), addr.size());

    int result = ::connect(m_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    CHECK(result >= 0, HAILO_FILE_OPERATION_FAILURE, "Connect error, errno = {}", errno);

    return HAILO_SUCCESS;
}

hailo_status OsRawConnection::connect_by_addr(const std::string &ip, uint16_t port)
{
    sockaddr_in server_addr = {};
    socklen_t addr_len = sizeof(server_addr);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    auto inet_rc = inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr);
    CHECK(1 == inet_rc, HAILO_ETH_FAILURE,
        "Failed to run 'inet_pton', errno = {}. make sure 'HAILO_SOCKET_COM_ADDR_XX' is set correctly (ip:port)", errno);
    auto result = ::connect(m_fd, (struct sockaddr*)&server_addr, addr_len);
    CHECK(result >= 0, HAILO_FILE_OPERATION_FAILURE, "Connect error, errno = {}. "
        "make sure 'HAILO_SOCKET_COM_ADDR_XX' is set correctly (ip:port)", errno);

    return HAILO_SUCCESS;
}

hailo_status OsRawConnection::connect()
{
    if (m_context->is_accepting()) {
        auto force_socket_com_value = get_env_variable(HAILO_SOCKET_COM_ADDR_SERVER_ENV_VAR);
        CHECK_EXPECTED(force_socket_com_value); // We know its set, otherwise we'll be working with PCIeRawCon
        if (HAILO_SOCKET_COM_ADDR_UNIX_SOCKET == force_socket_com_value.value()) {
            return connect_localhost();
        } else {
            TRY(auto ip_port_pair, parse_ip_port(force_socket_com_value.value()));
            return connect_by_addr(std::get<0>(ip_port_pair), std::get<1>(ip_port_pair));
        }
    } else {
        auto force_socket_com_value = get_env_variable(HAILO_SOCKET_COM_ADDR_CLIENT_ENV_VAR);
        CHECK_EXPECTED(force_socket_com_value); // We know its set, otherwise we'll be working with PCIeRawCon
        if (HAILO_SOCKET_COM_ADDR_UNIX_SOCKET == force_socket_com_value.value()) {
            return connect_localhost();
        } else {
            TRY(auto ip_port_pair, parse_ip_port(force_socket_com_value.value()));
            return connect_by_addr(std::get<0>(ip_port_pair), std::get<1>(ip_port_pair));
        }
    }
}

hailo_status OsRawConnection::write(const uint8_t *buffer, size_t size, std::chrono::milliseconds /*timeout*/)
{
    size_t bytes_written = 0;
    while (bytes_written < size) {
        ssize_t result = ::send(m_fd, buffer + bytes_written, size - bytes_written, MSG_NOSIGNAL);
        CHECK(result >= 0, HAILO_FILE_OPERATION_FAILURE, "Write error, errno = {}", errno);
        bytes_written += result;
    }
    return HAILO_SUCCESS;
}

hailo_status OsRawConnection::read(uint8_t *buffer, size_t size, std::chrono::milliseconds /*timeout*/)
{
    size_t bytes_read = 0;
    while (bytes_read < size) {
        ssize_t result = ::read(m_fd, buffer + bytes_read, size - bytes_read);
        if (0 == result) {
            return HAILO_COMMUNICATION_CLOSED; // 0 means the communication is closed
        }
        CHECK(result >= 0, HAILO_FILE_OPERATION_FAILURE, "Read error, errno = {}", errno);
        bytes_read += result;
    }
    return HAILO_SUCCESS;
}

hailo_status OsRawConnection::close()
{
    int result = ::shutdown(m_fd, SHUT_RDWR);
    CHECK(0 == result, HAILO_CLOSE_FAILURE, "Socket shutdown failed, errno = {}", errno);

    result = ::close(m_fd);
    CHECK(0 == result, HAILO_CLOSE_FAILURE, "Socket close failed, errno = {}", errno);

    return HAILO_SUCCESS;
}