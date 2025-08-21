/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file socket.hpp
 * @brief TODO
 **/

#ifndef __OS_SOCKET_H__
#define __OS_SOCKET_H__

#include <hailo/platform.h>
#include <hailo/hailort.h>
#include "common/utils.hpp"
#include "hailo/expected.hpp"

namespace hailort
{

// 12 for the octets (3 * 4, each octet<=255)
// 3 for the dots (".")
// 1 for the terminating null
#define IPV4_STRING_MAX_LENGTH (16)

#define PADDING_BYTES_SIZE (6)
#define PADDING_ALIGN_BYTES (8 - PADDING_BYTES_SIZE)
#define MIN_UDP_PAYLOAD_SIZE (24)
#define MAX_UDP_PAYLOAD_SIZE (1456)
#define MAX_UDP_PADDED_PAYLOAD_SIZE (MAX_UDP_PAYLOAD_SIZE - PADDING_BYTES_SIZE - PADDING_ALIGN_BYTES)

#define CHECK_VALID_SOCKET_AS_EXPECTED(sock) CHECK((sock) != INVALID_SOCKET, make_unexpected(HAILO_ETH_FAILURE), "Invalid socket")

class Socket final {
public:
    static Expected<Socket> create(int af, int type, int protocol);
    ~Socket();
    Socket(const Socket &other) = delete;
    Socket &operator=(const Socket &other) = delete;
    Socket &operator=(Socket &&other) = delete;
    Socket(Socket &&other) noexcept :
      m_module_wrapper(std::move(other.m_module_wrapper)), m_socket_fd(std::exchange(other.m_socket_fd, INVALID_SOCKET))
        {};

    socket_t get_fd() const { return m_socket_fd; }

    static hailo_status ntop(int af, const void *src, char *dst, socklen_t size);
    static hailo_status pton(int af, const char *src, void *dst);

    hailo_status socket_bind(const sockaddr *addr, socklen_t len);
    hailo_status get_sock_name(sockaddr *addr, socklen_t *len);

    hailo_status listen(int backlog);
    Expected<Socket> accept();
    hailo_status connect(const sockaddr *addr, socklen_t len);

    Expected<size_t> recv(uint8_t *buffer, size_t size, int flags = 0);
    Expected<size_t> send(const uint8_t *buffer, size_t size, int flags = 0);
    Expected<size_t> recvmsg(struct msghdr *msg, int flags = 0) const;
    Expected<size_t> sendmsg(const struct msghdr *msg, int flags = 0) const;
    Expected<int> read_fd();
    hailo_status write_fd(int fd, size_t buffer_size);

    hailo_status set_timeout(const std::chrono::milliseconds timeout_ms, timeval_t *timeout);
    hailo_status enable_broadcast();
    hailo_status allow_reuse_address();
    hailo_status bind_to_device(const std::string &device_name);
    hailo_status abort();
    hailo_status close_socket_fd();

    // TODO: Should these be in udp.cpp?
    // TODO: Work with const Buffer& instead of uint8_t*
    hailo_status send_to(const uint8_t *src_buffer, size_t src_buffer_size, int flags,
        const sockaddr *dest_addr, socklen_t dest_addr_size, size_t *bytes_sent);
    hailo_status recv_from(uint8_t *dest_buffer, size_t dest_buffer_size, int flags,
        sockaddr *src_addr, socklen_t src_addr_size, size_t *bytes_received, bool log_timeouts_in_debug = false);
    hailo_status has_data(sockaddr *src_addr, socklen_t src_addr_size, bool log_timeouts_in_debug = false);

    hailo_status sendall(const uint8_t *buffer, size_t size, int flags = 0)
    {
        size_t offset = 0;
        while (offset < size) {
            TRY(auto bytes_written, send(buffer + offset, size - offset, flags));
            if (bytes_written == 0) {
                return HAILO_ETH_SEND_FAILURE;
            }
            offset += bytes_written;
        }
        return HAILO_SUCCESS;
    }

    hailo_status recvall(uint8_t* buffer, size_t size, int flags = 0)
    {
        size_t offset = 0;
        while (offset < size) {
            TRY(auto bytes_read, recv(buffer + offset, size - offset, flags));
            if (bytes_read == 0) {
                return HAILO_COMMUNICATION_CLOSED;
            }
            offset += bytes_read;
        }
        return HAILO_SUCCESS;
    }

private:
    class SocketModuleWrapper final {
    public:
        static Expected<SocketModuleWrapper> create()
        {
            auto status = HAILO_UNINITIALIZED;
            auto obj = SocketModuleWrapper(status);
            CHECK_SUCCESS_AS_EXPECTED(status);
            return obj;
        }

        static Expected<std::shared_ptr<SocketModuleWrapper>> create_shared()
        {
            auto status = HAILO_UNINITIALIZED;
            auto ptr = make_shared_nothrow<SocketModuleWrapper>(status);
            CHECK_SUCCESS_AS_EXPECTED(status);
            CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY);
            return ptr;
        }

        SocketModuleWrapper(hailo_status &status)
        {
            status = init_module();
        }

        SocketModuleWrapper(const SocketModuleWrapper &other) = delete;
        SocketModuleWrapper &operator=(const SocketModuleWrapper &other) = delete;
        SocketModuleWrapper &operator=(SocketModuleWrapper &&other) = delete;
        SocketModuleWrapper(SocketModuleWrapper &&other) noexcept = default;

        ~SocketModuleWrapper()
        {
            auto status = free_module();
            if (HAILO_SUCCESS != status) {
                LOGGER__ERROR("Failed to free socket module.");
            }
        }
    private:
        static hailo_status init_module();
        static hailo_status free_module();
    };

    Socket(std::shared_ptr<SocketModuleWrapper> module_wrapper, const socket_t socket_fd);
    static Expected<socket_t> create_socket_fd(int af, int type, int protocol);

    // Itialization dependency
    std::shared_ptr<SocketModuleWrapper> m_module_wrapper;
    socket_t m_socket_fd;
};

} /* namespace hailort */

#endif /* __OS_SOCKET_H__ */
