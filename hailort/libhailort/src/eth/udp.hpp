/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file udp.hpp
 * @brief Defines udp transport method.
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

class Udp final {
public:
    static Expected<Udp> create(struct in_addr device_ip, uint16_t device_port, struct in_addr host_ip,
        uint16_t host_port);

    hailo_status set_timeout(const std::chrono::milliseconds timeout_ms);
    hailo_status send(uint8_t *buffer, size_t *size, bool use_padding, size_t max_payload_size);
    hailo_status recv(uint8_t *buffer, size_t *size);
    hailo_status abort();
    hailo_status has_data(bool log_timeouts_in_debug = false);
    hailo_status fw_interact(uint8_t *request_buffer, size_t request_size, uint8_t *response_buffer,
        size_t *response_size, uint32_t expected_sequence);
    hailo_status set_max_number_of_attempts(uint8_t max_number_of_attempts);

    UDP__sockaddr_in_t m_host_address;
    socklen_t m_host_address_length;
    UDP__sockaddr_in_t m_device_address;
    socklen_t m_device_address_length;
    UDP__timeout_t m_timeout;

private:
    Udp(struct in_addr device_ip, uint16_t device_port, struct in_addr host_ip, uint16_t host_port,
        Socket &&socket, hailo_status &status);

    hailo_status bind(struct in_addr host_ip, uint16_t host_port);
    hailo_status receive_fw_response(uint8_t *buffer, size_t *size, uint32_t expected_sequence);
    hailo_status fw_interact_impl(uint8_t *request_buffer, size_t request_size, uint8_t *response_buffer,
        size_t *response_size, uint32_t expected_sequence);

    uint8_t m_max_number_of_attempts;
    Socket m_socket;
};

} /* namespace hailort */

#endif /* __OS_UDP_H__ */
