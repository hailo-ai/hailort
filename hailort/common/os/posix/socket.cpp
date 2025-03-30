/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file socket.cpp
 * @brief Socket wrapper for Unix
 **/

#include "common/socket.hpp"

#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <array>

namespace hailort
{

#define LINUX_RMEM_MAX_PATH "/proc/sys/net/core/rmem_max"

hailo_status Socket::SocketModuleWrapper::init_module()
{
    return HAILO_SUCCESS;
}

hailo_status Socket::SocketModuleWrapper::free_module()
{
    return HAILO_SUCCESS;
}

Expected<Socket> Socket::create(int af, int type, int protocol)
{
    TRY(auto module_wrapper_ptr, SocketModuleWrapper::create_shared());
    TRY(const auto socket_fd, create_socket_fd(af, type, protocol));

    auto obj = Socket(module_wrapper_ptr, socket_fd);
    return obj;
}

Socket::Socket(std::shared_ptr<SocketModuleWrapper> module_wrapper, const socket_t socket_fd) :
  m_module_wrapper(std::move(module_wrapper)), m_socket_fd(socket_fd)
{
}

Socket::~Socket()
{
    auto status = close_socket_fd();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to free socket fd with status {}", status);
    }
}

Expected<socket_t> Socket::create_socket_fd(int af, int type, int protocol)
{
    socket_t local_socket = INVALID_SOCKET;

    local_socket = socket(af, type, protocol);
    CHECK_VALID_SOCKET_AS_EXPECTED(local_socket);

    return local_socket;
}

hailo_status Socket::close_socket_fd()
{
    if (INVALID_SOCKET != m_socket_fd) {
        int socket_rc = close(m_socket_fd);
        CHECK(0 == socket_rc, HAILO_ETH_FAILURE, "Failed to close socket. errno={}", errno);
        m_socket_fd = INVALID_SOCKET;
    }

    return HAILO_SUCCESS;
}

hailo_status Socket::abort()
{
    int socket_rc = shutdown(m_socket_fd, SHUT_RDWR);
    CHECK((0 == socket_rc) || ((-1 == socket_rc) && (ENOTCONN == errno)), HAILO_ETH_FAILURE, "Failed to shutdown (abort) socket. errno={}", errno);

    return HAILO_SUCCESS;
}

hailo_status Socket::socket_bind(const sockaddr *addr, socklen_t len)
{
    int socket_rc = SOCKET_ERROR;

    CHECK_ARG_NOT_NULL(addr);

    socket_rc = bind(m_socket_fd, addr, len);
    CHECK(0 == socket_rc, HAILO_ETH_FAILURE, "Failed to bind socket. errno={}", errno);

    return HAILO_SUCCESS;
}

hailo_status Socket::get_sock_name(sockaddr *addr, socklen_t *len)
{
    int socket_rc = SOCKET_ERROR;

    CHECK_ARG_NOT_NULL(addr);
    CHECK_ARG_NOT_NULL(len);

    socket_rc = getsockname(m_socket_fd, addr, len);
    CHECK(0 == socket_rc, HAILO_ETH_FAILURE, "Failed getsockname. errno={}", errno);

    return HAILO_SUCCESS;
}

hailo_status Socket::listen(int backlog)
{
    auto res = ::listen(m_socket_fd, backlog);
    CHECK(0 == res, HAILO_ETH_FAILURE, "Failed to listen on socket. errno={}", errno);
    return HAILO_SUCCESS;
}

Expected<Socket> Socket::accept()
{
    auto client_socket = ::accept(m_socket_fd, nullptr, nullptr);
    CHECK(client_socket != INVALID_SOCKET, make_unexpected(HAILO_ETH_FAILURE), "Failed to accept connection {}", errno);

    return Socket(m_module_wrapper, client_socket);
}

hailo_status Socket::connect(const sockaddr *addr, socklen_t len)
{
    int ret = ::connect(m_socket_fd, addr, len);
    if (0 != ret) {
        switch (errno) {
        case ECONNREFUSED:
            return HAILO_CONNECTION_REFUSED;
        default:
            LOGGER__ERROR("Failed to connect to socket {}", errno);
            return HAILO_ETH_FAILURE;
        }
    }
    return HAILO_SUCCESS;
}

Expected<size_t> Socket::recv(uint8_t *buffer, size_t size, int flags)
{
    auto read_bytes = ::recv(m_socket_fd, buffer, size, flags);
    CHECK(read_bytes >= 0, make_unexpected(HAILO_ETH_FAILURE), "Failed to read from socket {}", errno);
    return Expected<size_t>(read_bytes);
}

Expected<size_t> Socket::send(const uint8_t *buffer, size_t size, int flags)
{
    auto bytes_written = ::send(m_socket_fd, buffer, size, flags);
    CHECK(bytes_written >= 0, make_unexpected(HAILO_ETH_FAILURE), "Failed to write to socket {}", errno);
    return Expected<size_t>(bytes_written);
}

hailo_status Socket::ntop(int af, const void *src, char *dst, socklen_t size)
{
    CHECK_ARG_NOT_NULL(src);
    CHECK_ARG_NOT_NULL(dst);

    CHECK(NULL != inet_ntop(af, src, dst, size), HAILO_ETH_FAILURE,
        "Could not convert sockaddr struct to string ip address");

    return HAILO_SUCCESS;
}

hailo_status Socket::pton(int af, const char *src, void *dst)
{
    int inet_rc = 0;

    CHECK_ARG_NOT_NULL(src);
    CHECK_ARG_NOT_NULL(dst);

    inet_rc = inet_pton(af, reinterpret_cast<const char*>(src), dst);
    CHECK(0 != inet_rc, HAILO_ETH_FAILURE,
        "Failed to run 'inet_pton'. src is not a valid network address in the specified address family");
    CHECK(1 == inet_rc, HAILO_ETH_FAILURE, "Failed to run 'inet_pton', errno = {}.", errno);

    return HAILO_SUCCESS;
}

hailo_status Socket::set_recv_buffer_size_max()
{
    int socket_rc = SOCKET_ERROR;
    FILE *rmem_max_file = NULL;
    uint8_t rmem_max_buffer[20] = {};
    uint64_t rmem_max = 0;
    int file_status = 0;
    size_t bytes_read = 0;

    rmem_max_file = fopen(LINUX_RMEM_MAX_PATH, "r");
    if (NULL != rmem_max_file) {
        bytes_read = fread(rmem_max_buffer, sizeof(rmem_max_buffer), sizeof(*rmem_max_buffer), rmem_max_file);
        if ((0 != bytes_read) || (feof(rmem_max_file))) {
            rmem_max = strtoul((char *)rmem_max_buffer, NULL, 10);
        }

        if (0 == rmem_max) {
            LOGGER__WARN("Could not read rmem_max value from file '{}'", LINUX_RMEM_MAX_PATH);
            rmem_max = UINT64_MAX;
        }

        file_status = fclose(rmem_max_file);
        if (0 != file_status) {
            LOGGER__WARN("Could not close file '{}' errno - {}.", LINUX_RMEM_MAX_PATH, errno);
        }
    } else {
        LOGGER__WARN("Could not open file '{}' to read rmem_max value.", LINUX_RMEM_MAX_PATH);
    }
    socket_rc = setsockopt(m_socket_fd, SOL_SOCKET, SO_RCVBUF, &rmem_max, sizeof(rmem_max));
    CHECK(0 == socket_rc, HAILO_ETH_FAILURE,  "Cannot set the rcv socket buffer to {}", rmem_max);

    return HAILO_SUCCESS;
}

hailo_status Socket::set_timeout(std::chrono::milliseconds timeout_ms, timeval_t *timeout)
{
    int socket_rc = SOCKET_ERROR;
    time_t seconds = 0;
    suseconds_t microseconds = 0;
    auto timeout_value = static_cast<uint32_t>(timeout_ms.count());

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(timeout);

    seconds = (timeout_value / MILLISECONDS_IN_SECOND);
    microseconds = (timeout_value % MILLISECONDS_IN_SECOND) * MICROSECONDS_IN_MILLISECOND;

    timeout->tv_sec = seconds;
    timeout->tv_usec = microseconds;

    socket_rc = setsockopt(m_socket_fd, SOL_SOCKET, SO_RCVTIMEO, timeout, sizeof(*timeout));
    CHECK(0 == socket_rc, HAILO_ETH_FAILURE, "Cannot set receive timeout. Seconds: {}, microseconds {}", seconds,
        microseconds);

    socket_rc = setsockopt(m_socket_fd, SOL_SOCKET, SO_SNDTIMEO, timeout, sizeof(*timeout));
    CHECK(0 == socket_rc, HAILO_ETH_FAILURE, "Cannot set send timeout. Seconds: {}, microseconds {}", seconds,
        microseconds);

    return HAILO_SUCCESS;
}

hailo_status Socket::enable_broadcast()
{
    int socket_rc = SOCKET_ERROR;
    int enable_broadcast = 1;

    socket_rc = setsockopt(m_socket_fd, SOL_SOCKET, SO_BROADCAST, &enable_broadcast, sizeof(enable_broadcast));
    CHECK(0 == socket_rc, HAILO_ETH_FAILURE, "Cannot set socket to be broadcast");

    return HAILO_SUCCESS;
}

hailo_status Socket::allow_reuse_address()
{
    int allow_reuse = 1;

    auto socket_rc = setsockopt(m_socket_fd, SOL_SOCKET, SO_REUSEADDR, &allow_reuse, sizeof(allow_reuse));
    CHECK(0 == socket_rc, HAILO_ETH_FAILURE, "Cannot set socket to be broadcast");

    return HAILO_SUCCESS;
}

hailo_status Socket::bind_to_device(const std::string &device_name)
{
    int socket_rc = setsockopt(m_socket_fd, SOL_SOCKET, SO_BINDTODEVICE, device_name.c_str(), static_cast<socklen_t>(device_name.size()));
    CHECK(0 == socket_rc, HAILO_ETH_FAILURE, "Cannot bind socket to device {}", device_name);
    return HAILO_SUCCESS;
}

hailo_status Socket::send_to(const uint8_t *src_buffer, size_t src_buffer_size, int flags,
    const sockaddr *dest_addr, socklen_t dest_addr_size, size_t *bytes_sent)
{
    ssize_t number_of_sent_bytes = 0;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(src_buffer);
    CHECK_ARG_NOT_NULL(dest_addr);
    CHECK_ARG_NOT_NULL(bytes_sent);

    number_of_sent_bytes = sendto(m_socket_fd, src_buffer, src_buffer_size, flags,
        dest_addr,  dest_addr_size);
    if (-1 == number_of_sent_bytes) {
        if ((EWOULDBLOCK == errno) || (EAGAIN == errno)) {
            LOGGER__ERROR("Udp send timeout");
            return HAILO_TIMEOUT;
        } else if (EINTR == errno) {
            LOGGER__ERROR("Udp send interrupted!");
            return HAILO_INTERRUPTED_BY_SIGNAL;
        } else if (EPIPE == errno) {
            // When socket is aborted from another thread sendto will return errno EPIPE
            LOGGER__INFO("Udp send aborted!");
            return HAILO_STREAM_ABORT;
        } else {
            LOGGER__ERROR("Udp failed to send data, errno:{}.", errno);
            return HAILO_ETH_SEND_FAILURE;
        }
    }

    *bytes_sent = (size_t)number_of_sent_bytes;
    return HAILO_SUCCESS;
}

hailo_status Socket::recv_from(uint8_t *dest_buffer, size_t dest_buffer_size, int flags,
    sockaddr *src_addr, socklen_t src_addr_size, size_t *bytes_received, bool log_timeouts_in_debug)
{
    ssize_t number_of_received_bytes = 0;
    socklen_t result_src_addr_size = src_addr_size;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(dest_buffer);
    CHECK_ARG_NOT_NULL(src_addr);
    CHECK_ARG_NOT_NULL(bytes_received);

    number_of_received_bytes = recvfrom(m_socket_fd, dest_buffer, dest_buffer_size, flags,
        src_addr, &result_src_addr_size);
    if (-1 == number_of_received_bytes) {
        if ((EWOULDBLOCK == errno) || (EAGAIN == errno)) {
            if (log_timeouts_in_debug) {
                LOGGER__DEBUG("Udp recvfrom failed with timeout");
            } else {
                LOGGER__ERROR("Udp recvfrom failed with timeout");
            }
            return HAILO_TIMEOUT;
        } else if (EINTR == errno) {
            LOGGER__ERROR("Udp recv interrupted!");
            return HAILO_INTERRUPTED_BY_SIGNAL;
        } else {
            LOGGER__ERROR("Udp failed to recv data");
            return HAILO_ETH_RECV_FAILURE;
        }
    }
    else if ((0 == number_of_received_bytes) && (0 != dest_buffer_size)) {
        LOGGER__INFO("Udp socket was aborted");
        return HAILO_STREAM_ABORT;
    }

    if (result_src_addr_size > src_addr_size) {
        LOGGER__ERROR("src_addr size invalid");
        return HAILO_ETH_RECV_FAILURE;
    }

    *bytes_received = (size_t)number_of_received_bytes;
    return HAILO_SUCCESS;
}

hailo_status Socket::has_data(sockaddr *src_addr, socklen_t src_addr_size, bool log_timeouts_in_debug)
{
    hailo_status status = HAILO_UNINITIALIZED;
    static const size_t DEST_BUFFER_SIZE = 1;
    std::array<uint8_t, DEST_BUFFER_SIZE> dest_buffer{};
    size_t number_of_received_bytes = 0;

    status = recv_from(dest_buffer.data(), dest_buffer.size(), 0, src_addr, src_addr_size, &number_of_received_bytes, log_timeouts_in_debug);
    if ((status == HAILO_TIMEOUT) && log_timeouts_in_debug) {
        LOGGER__DEBUG("recv_from failed with timeout");
        return HAILO_TIMEOUT;
    } else {
        CHECK_SUCCESS(status);
        assert(number_of_received_bytes > 0);
    }

    return HAILO_SUCCESS;
}

} /* namespace hailort */
