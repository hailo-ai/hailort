/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file socket.cpp
 * @brief Socket wrapper for Windows
 **/

#include "common/socket.hpp"

#include <array>

namespace hailort
{

#define WSA_VERSION MAKEWORD(2, 2)

hailo_status Socket::SocketModuleWrapper::init_module()
{
    uint16_t wsa_version = WSA_VERSION;
    WSADATA wsa_data{};
    int wsa_rt = SOCKET_ERROR;

    wsa_rt = WSAStartup(wsa_version, &wsa_data);
    CHECK(0 == wsa_rt, HAILO_ETH_FAILURE, "WSAStartup failed. rt={}", wsa_rt);

    return HAILO_SUCCESS;
}

hailo_status Socket::SocketModuleWrapper::free_module()
{
    int wsa_rt = SOCKET_ERROR;

    wsa_rt = WSACleanup();
    CHECK(0 == wsa_rt, HAILO_ETH_FAILURE, "WSACleanup failed. LE={}", WSAGetLastError());

    return HAILO_SUCCESS;
}

Expected<Socket> Socket::create(int af, int type, int protocol)
{
    TRY(auto module_wrapper_ptr, SocketModuleWrapper::create_shared());
    TRY(const auto socket_fd, create_socket_fd(af, type, protocol));

    auto obj = Socket(std::move(module_wrapper_ptr), socket_fd);
    return std::move(obj);
}

Socket::Socket(std::shared_ptr<SocketModuleWrapper> module_wrapper, const socket_t socket_fd) :
  m_module_wrapper(std::move(module_wrapper)), m_socket_fd(socket_fd)
{
}

Socket::~Socket()
{
    auto status = close_socket_fd();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to free socket fd with status {:X}");
    }
}

Expected<socket_t> Socket::create_socket_fd(int af, int type, int protocol)
{
    socket_t local_socket = INVALID_SOCKET;

    local_socket = socket(af, type, protocol);
    CHECK_VALID_SOCKET_AS_EXPECTED(local_socket);
  
    return local_socket;
}

hailo_status Socket::abort()
{
    int socket_rc = shutdown(m_socket_fd, SD_BOTH);
    CHECK((0 == socket_rc) || ((-1 == socket_rc) && (WSAENOTCONN == WSAGetLastError())), HAILO_ETH_FAILURE,
        "Failed to shutdown (abort) socket. WSALE={}", WSAGetLastError());

    return HAILO_SUCCESS;
}

hailo_status Socket::close_socket_fd()
{
    if (INVALID_SOCKET != m_socket_fd) {
        int socket_rc = closesocket(m_socket_fd);
        CHECK(0 == socket_rc, HAILO_ETH_FAILURE, "Failed to close socket. WSALE={}", WSAGetLastError());
        m_socket_fd = INVALID_SOCKET;
    }

    return HAILO_SUCCESS;
}

hailo_status Socket::socket_bind(const sockaddr *addr, socklen_t len)
{
    int socket_rc = SOCKET_ERROR;

    CHECK_ARG_NOT_NULL(addr);

    socket_rc = bind(m_socket_fd, addr, len);
    CHECK(0 == socket_rc, HAILO_ETH_FAILURE, "Failed to bind socket. WSALE={}", WSAGetLastError());
    
    return HAILO_SUCCESS;
}

hailo_status Socket::get_sock_name(sockaddr *addr, socklen_t *len)
{
    int socket_rc = SOCKET_ERROR;
    
    CHECK_ARG_NOT_NULL(addr);
    CHECK_ARG_NOT_NULL(len);

    socket_rc = getsockname(m_socket_fd, addr, len);
    CHECK(0 == socket_rc, HAILO_ETH_FAILURE, "Failed getsockname. WSALE={}", WSAGetLastError());
    
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
    CHECK(0 == ret, HAILO_ETH_FAILURE, "Failed to connect to socket {}", errno);
    return HAILO_SUCCESS;
}

Expected<size_t> Socket::recv(uint8_t *buffer, size_t size, int flags)
{
    auto read_bytes = ::recv(m_socket_fd, reinterpret_cast<char*>(buffer), static_cast<int>(size), flags);
    CHECK(read_bytes >= 0, make_unexpected(HAILO_ETH_FAILURE), "Failed to read from socket {}", errno);
    return Expected<size_t>(read_bytes);
}

Expected<size_t> Socket::send(const uint8_t *buffer, size_t size, int flags)
{
    auto bytes_written = ::send(m_socket_fd, reinterpret_cast<const char*>(buffer), static_cast<int>(size), flags);
    CHECK(bytes_written >= 0, make_unexpected(HAILO_ETH_FAILURE), "Failed to write to socket {}", errno);
    return Expected<size_t>(bytes_written);
}

hailo_status Socket::ntop(int af, const void *src, char *dst, socklen_t size)
{
    CHECK_ARG_NOT_NULL(src);
    CHECK_ARG_NOT_NULL(dst);

    TRY(const auto module_wrapper_ptr, SocketModuleWrapper::create_shared());

    const char *inet_result = inet_ntop(af, src, dst, size);
    CHECK(nullptr != inet_result, HAILO_ETH_FAILURE, "Failed inet_ntop. WSALE={}", WSAGetLastError());

    return HAILO_SUCCESS;
}

hailo_status Socket::pton(int af, const char *src, void *dst)
{
    int inet_result = SOCKET_ERROR;

    CHECK_ARG_NOT_NULL(src);
    CHECK_ARG_NOT_NULL(dst);

    TRY(const auto module_wrapper_ptr, SocketModuleWrapper::create_shared());

    inet_result = inet_pton(af, src, dst);
    CHECK(0 != inet_result, HAILO_ETH_FAILURE,
        "Failed to run 'inet_pton'. src is not a valid network address in the specified address family");
    CHECK(1 == inet_result, HAILO_ETH_FAILURE, "Failed to run 'inet_pton', WSALE = {}.", WSAGetLastError());

    return HAILO_SUCCESS;
}

hailo_status Socket::set_recv_buffer_size_max()
{
    int socket_rc = SOCKET_ERROR;
 
    // TOOD: MAX_SIZE?? https://docs.microsoft.com/en-us/windows/win32/winsock/sol-socket-socket-options
    const int MAX_RECV_BUFFER_SIZE = 52428800;
    socket_rc = setsockopt(m_socket_fd, SOL_SOCKET, SO_RCVBUF,
        reinterpret_cast<const char*>(&MAX_RECV_BUFFER_SIZE), sizeof(MAX_RECV_BUFFER_SIZE));
    CHECK(0 == socket_rc, HAILO_ETH_FAILURE, "Failed setsockopt(SOL_SOCKET, SO_RCVBUF). WSALE={}", WSAGetLastError());

    return HAILO_SUCCESS;
}

hailo_status Socket::set_timeout(const std::chrono::milliseconds timeout_ms, timeval_t *timeout)
{
    int socket_rc = SOCKET_ERROR;
    auto timeout_value = static_cast<uint32_t>(timeout_ms.count());
    
    /* Validate arguments */
    CHECK_ARG_NOT_NULL(timeout);
    
    // From https://docs.microsoft.com/en-us/windows/win32/winsock/sol-socket-socket-options (SO_RCVTIMEO):
    // If the socket is created using the WSASocket function, then the dwFlags parameter must have the
    // WSA_FLAG_OVERLAPPED attribute set for the timeout to function properly. Otherwise the timeout never takes effect.
    socket_rc = setsockopt(m_socket_fd, SOL_SOCKET, SO_RCVTIMEO,
        reinterpret_cast<const char*>(&timeout_value), sizeof(timeout_value));
    CHECK(0 == socket_rc, HAILO_ETH_FAILURE, "Failed setsockopt(SOL_SOCKET, SO_RCVTIMEO). WSALE={}", WSAGetLastError());

    socket_rc = setsockopt(m_socket_fd, SOL_SOCKET, SO_SNDTIMEO,
        reinterpret_cast<const char*>(&timeout_value), sizeof(timeout_value));
    CHECK(0 == socket_rc, HAILO_ETH_FAILURE, "Failed setsockopt(SOL_SOCKET, SO_SNDTIMEO). WSALE={}", WSAGetLastError());

    timeout->tv_sec = timeout_value / MILLISECONDS_IN_SECOND;
    timeout->tv_usec = (timeout_value % MILLISECONDS_IN_SECOND) * MICROSECONDS_IN_MILLISECOND;

    return HAILO_SUCCESS;
}

hailo_status Socket::enable_broadcast()
{
    int socket_rc = SOCKET_ERROR;
    int enable_broadcast = 1;

    socket_rc = setsockopt(m_socket_fd, SOL_SOCKET, SO_BROADCAST, 
        reinterpret_cast<const char*>(&enable_broadcast), sizeof(enable_broadcast));
    CHECK(0 == socket_rc, HAILO_ETH_FAILURE, "Failed setsockopt(SOL_SOCKET, SO_BROADCAST). WSALE={}", WSAGetLastError());

    return HAILO_SUCCESS;
}

hailo_status Socket::allow_reuse_address()
{
    int allow_reuse = 1;

    auto socket_rc = setsockopt(m_socket_fd, SOL_SOCKET, SO_REUSEADDR,
        reinterpret_cast<const char*>(&allow_reuse), sizeof(allow_reuse));
    CHECK(0 == socket_rc, HAILO_ETH_FAILURE, "Cannot set socket to be broadcast");

    return HAILO_SUCCESS;
}

hailo_status Socket::bind_to_device(const std::string &device_name)
{
    (void)device_name;
    LOGGER__ERROR("bind_to_device is not implemented for Windows");
    return HAILO_NOT_IMPLEMENTED;
}

hailo_status Socket::send_to(const uint8_t *src_buffer, size_t src_buffer_size, int flags,
    const sockaddr *dest_addr, socklen_t dest_addr_size, size_t *bytes_sent)
{
    int number_of_sent_bytes = SOCKET_ERROR;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(src_buffer);
    CHECK_ARG_NOT_NULL(dest_addr);
    CHECK_ARG_NOT_NULL(bytes_sent);
    
    number_of_sent_bytes = sendto(m_socket_fd, reinterpret_cast<const char*>(src_buffer),
        static_cast<int>(src_buffer_size), flags, dest_addr, dest_addr_size);
    if (SOCKET_ERROR == number_of_sent_bytes) {
        const int wsale = WSAGetLastError();
        if (WSAETIMEDOUT == errno) {
            LOGGER__ERROR("Udp send timeout");
            return HAILO_TIMEOUT;
        } else {
            LOGGER__ERROR("Udp failed to send data, WSALE={}.", wsale);
            return HAILO_ETH_SEND_FAILURE;
        }
    }

    *bytes_sent = (size_t)number_of_sent_bytes;
    return HAILO_SUCCESS;
}

hailo_status Socket::recv_from(uint8_t *dest_buffer, size_t dest_buffer_size, int flags,
    sockaddr *src_addr, socklen_t src_addr_size, size_t *bytes_recieved, bool log_timeouts_in_debug)
{
    int number_of_received_bytes = SOCKET_ERROR;
    socklen_t result_src_addr_size = src_addr_size;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(dest_buffer);
    CHECK_ARG_NOT_NULL(src_addr);
    CHECK_ARG_NOT_NULL(bytes_recieved);

    number_of_received_bytes = recvfrom(m_socket_fd, reinterpret_cast<char*>(dest_buffer),
        static_cast<int>(dest_buffer_size), flags, src_addr, &result_src_addr_size); 
    if (SOCKET_ERROR == number_of_received_bytes) {
        const int wsale = WSAGetLastError();
        if (WSAETIMEDOUT == wsale) {
            if (log_timeouts_in_debug) {
                LOGGER__DEBUG("Udp recvfrom failed with timeout");
            } else { 
                LOGGER__ERROR("Udp recvfrom failed with timeout");
            }
            return HAILO_TIMEOUT;
        } else {
            LOGGER__ERROR("Udp failed to recv data. WSALE={}.", wsale);
            return HAILO_ETH_RECV_FAILURE;
        }
    }

    *bytes_recieved = static_cast<size_t>(number_of_received_bytes);
    return HAILO_SUCCESS;
}

hailo_status Socket::has_data(sockaddr *src_addr, socklen_t src_addr_size, bool log_timeouts_in_debug)
{
    int number_of_received_bytes = SOCKET_ERROR;
    socklen_t result_src_addr_size = src_addr_size;
    static const size_t DEST_BUFFER_SIZE = 1;
    std::array<char, DEST_BUFFER_SIZE> dest_buffer{};

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(src_addr);

    static const int NO_FLAGS = 0;
    number_of_received_bytes = recvfrom(m_socket_fd, dest_buffer.data(), static_cast<int>(dest_buffer.size()), NO_FLAGS,
        src_addr, &result_src_addr_size);
    if (SOCKET_ERROR == number_of_received_bytes) {
        const int wsale = WSAGetLastError();
        if (WSAETIMEDOUT == wsale) {
            if (log_timeouts_in_debug) {
                LOGGER__DEBUG("Udp recvfrom failed with timeout");
            } else {
                LOGGER__ERROR("Udp recvfrom failed with timeout");
            }
            return HAILO_TIMEOUT;
        }
        // The message may be bigger than DEST_BUFFER_SIZE bytes, leading to WSAEMSGSIZE. This is ok
        if (WSAEMSGSIZE != wsale) {
            LOGGER__ERROR("Udp failed to recv data. WSALE={}.", wsale);
            return HAILO_ETH_RECV_FAILURE;
        }
    }

    return HAILO_SUCCESS;
}

} /* namespace hailort */
