/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file udp.hpp
 * @brief Defines udp transport method.
 *
 * DEPRECATED: ethernet/UDP firmware control is no longer supported. This class
 * is retained as a header-only stub so that legacy call sites continue to
 * compile; every method now returns HAILO_NOT_SUPPORTED. The class will be
 * removed in a future release.
 **/

#ifndef __OS_UDP_H__
#define __OS_UDP_H__

#include "hailo/hailort.h"
#include "hailo/expected.hpp"

#include "common/socket.hpp"


namespace hailort
{

typedef struct sockaddr_in UDP__sockaddr_in_t;
typedef struct timeval UDP__timeout_t;

// DEPRECATED: ethernet/UDP fw-control is no longer supported; all methods are stubs.
class Udp final {
public:
    static Expected<Udp> create(struct in_addr device_ip, uint16_t device_port, struct in_addr host_ip,
        uint16_t host_port)
    {
        (void)device_ip;
        (void)device_port;
        (void)host_ip;
        (void)host_port;
        return make_unexpected(HAILO_NOT_SUPPORTED);
    }

    hailo_status set_timeout(const std::chrono::milliseconds timeout_ms)
    {
        (void)timeout_ms;
        return HAILO_NOT_SUPPORTED;
    }

    hailo_status send(uint8_t *buffer, size_t *size, bool use_padding, size_t max_payload_size)
    {
        (void)buffer;
        (void)size;
        (void)use_padding;
        (void)max_payload_size;
        return HAILO_NOT_SUPPORTED;
    }

    hailo_status recv(uint8_t *buffer, size_t *size)
    {
        (void)buffer;
        (void)size;
        return HAILO_NOT_SUPPORTED;
    }

    hailo_status abort()
    {
        return HAILO_NOT_SUPPORTED;
    }

    hailo_status has_data(bool log_timeouts_in_debug = false)
    {
        (void)log_timeouts_in_debug;
        return HAILO_NOT_SUPPORTED;
    }

    hailo_status fw_interact(uint8_t *request_buffer, size_t request_size, uint8_t *response_buffer,
        size_t *response_size, uint32_t expected_sequence)
    {
        (void)request_buffer;
        (void)request_size;
        (void)response_buffer;
        (void)response_size;
        (void)expected_sequence;
        return HAILO_NOT_SUPPORTED;
    }

    hailo_status set_max_number_of_attempts(uint8_t max_number_of_attempts)
    {
        (void)max_number_of_attempts;
        return HAILO_NOT_SUPPORTED;
    }

    UDP__sockaddr_in_t m_host_address;
    socklen_t m_host_address_length;
    UDP__sockaddr_in_t m_device_address;
    socklen_t m_device_address_length;
    UDP__timeout_t m_timeout;

private:
    Udp(struct in_addr device_ip, uint16_t device_port, struct in_addr host_ip, uint16_t host_port,
        Socket &&socket, hailo_status &status) :
        m_host_address(),
        m_host_address_length(0),
        m_device_address(),
        m_device_address_length(0),
        m_timeout()
    {
        (void)device_ip;
        (void)device_port;
        (void)host_ip;
        (void)host_port;
        (void)socket;
        status = HAILO_NOT_SUPPORTED;
    }
};

} /* namespace hailort */

#endif /* __OS_UDP_H__ */
