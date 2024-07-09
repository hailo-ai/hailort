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
#include <common/logger_macros.hpp>
#include <common/utils.hpp>
#include <hailo/hailort.h>

using namespace hrpc;

Expected<std::shared_ptr<ConnectionContext>> OsConnectionContext::create_shared(bool is_accepting)
{
    auto ptr = make_shared_nothrow<OsConnectionContext>(is_accepting);
    CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::dynamic_pointer_cast<ConnectionContext>(ptr);
}

Expected<std::shared_ptr<RawConnection>> OsRawConnection::create_shared(std::shared_ptr<OsConnectionContext> context)
{
    std::shared_ptr<RawConnection> ptr;
    if (context->is_accepting()) {
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

        ptr = make_shared_nothrow<OsRawConnection>(fd, context);
    } else {

        int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        CHECK_AS_EXPECTED(fd >= 0, HAILO_OPEN_FILE_FAILURE, "Socket creation error, errno = {}", errno);
        ptr = make_shared_nothrow<OsRawConnection>(fd, context);
    }
    
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY);
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

hailo_status OsRawConnection::connect()
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

hailo_status OsRawConnection::write(const uint8_t *buffer, size_t size)
{
    size_t bytes_written = 0;
    while (bytes_written < size) {
        ssize_t result = ::send(m_fd, buffer + bytes_written, size - bytes_written, MSG_NOSIGNAL);
        CHECK(result >= 0, HAILO_FILE_OPERATION_FAILURE, "Write error, errno = {}", errno);
        bytes_written += result;
    }
    return HAILO_SUCCESS;
}

hailo_status OsRawConnection::read(uint8_t *buffer, size_t size)
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