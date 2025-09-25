/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file udp.cpp
 * @brief Socket wrapper for Unix
 **/

#include "hailo/hailort.h"

#include "common/utils.hpp"
#include "common/logger_macros.hpp"
#include "common/socket.hpp"
#include "eth/udp.hpp"
#include "device_common/control_protocol.hpp"

#include <stdint.h>
#include <errno.h>
#include <string.h>


namespace hailort
{

#define MILLISECONDS_IN_SECOND (1000)
#define MICROSECONDS_IN_MILLISECOND (1000)

//initialize with padding
uint8_t g_padded_buffer[MAX_UDP_PAYLOAD_SIZE] = {0,};

hailo_status Udp::bind(struct in_addr host_ip, uint16_t host_port)
{
    m_host_address.sin_family = AF_INET;
    m_host_address.sin_port = htons(host_port);
    m_host_address.sin_addr = host_ip;
    m_host_address_length = sizeof(m_host_address);

    /* Bind the socket */
    auto status = m_socket.socket_bind((struct sockaddr*)&(m_host_address), m_host_address_length);
    CHECK_SUCCESS(status);

    /* Save binded host address information */
    return m_socket.get_sock_name((struct sockaddr*)&(m_host_address), &m_host_address_length);
}

Expected<Udp> Udp::create(struct in_addr device_ip, uint16_t device_port, struct in_addr host_ip,
    uint16_t host_port)
{
    auto status = HAILO_UNINITIALIZED;
    TRY(auto socket, Socket::create(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    auto object = Udp(device_ip, device_port, host_ip, host_port, std::move(socket), status);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return object;
}

Udp::Udp(struct in_addr device_ip, uint16_t device_port, struct in_addr host_ip, uint16_t host_port,
    Socket &&socket, hailo_status &status) : m_socket(std::move(socket))
{
    m_device_address.sin_family = AF_INET;
    m_device_address.sin_port = htons(device_port);
    m_device_address.sin_addr = device_ip;
    m_device_address_length = sizeof(m_device_address);

    /* Adjust socket rcv buff size */
    status = m_socket.set_recv_buffer_size_max();
    if (HAILO_SUCCESS != status) {
        return;
    }

    /* Set default value timeout */
    status = set_timeout(std::chrono::milliseconds(HAILO_DEFAULT_ETH_SCAN_TIMEOUT_MS));
    if (HAILO_SUCCESS != status) {
        return;
    }

    /* Set deafult max number of retries */
    status = set_max_number_of_attempts(HAILO_DEFAULT_ETH_MAX_NUMBER_OF_RETRIES);
    if (HAILO_SUCCESS != status) {
        return;
    }

    /* If device address is 255.255.255.255 (broadcast), enable broadcast */
    if (INADDR_BROADCAST == m_device_address.sin_addr.s_addr) {
        status = m_socket.enable_broadcast();
        if (HAILO_SUCCESS != status) {
            return;
        }
    }

    /* Bind socket at the host */
    status = bind(host_ip, host_port);
    if (HAILO_SUCCESS != status) {
        return;
    }

    status = HAILO_SUCCESS;
}

hailo_status Udp::set_timeout(const std::chrono::milliseconds timeout_ms)
{
    return m_socket.set_timeout(timeout_ms, &(m_timeout));
}

hailo_status Udp::send(uint8_t *buffer, size_t *size, bool use_padding, size_t max_payload_size)
{
    hailo_status status = HAILO_UNINITIALIZED;
    size_t number_of_sent_bytes = 0;
    uint8_t *send_ptr = buffer;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(buffer);
    CHECK_ARG_NOT_NULL(size);

    if (use_padding) {
        if (*size > (max_payload_size - PADDING_BYTES_SIZE - PADDING_ALIGN_BYTES)) {
            *size = (max_payload_size - PADDING_BYTES_SIZE - PADDING_ALIGN_BYTES);
        }
        /*copy the data to the padded buffer and adjust the size*/
        memcpy((g_padded_buffer + PADDING_BYTES_SIZE), buffer, *size);
        send_ptr = g_padded_buffer;    
        *size += PADDING_BYTES_SIZE;
    }
    else if (*size > max_payload_size) {
        *size = max_payload_size;
    }

    status = m_socket.send_to((const uint8_t*)send_ptr, *size, MSG_CONFIRM, (const struct sockaddr *) &m_device_address,
         m_device_address_length, &number_of_sent_bytes);
    if (HAILO_STREAM_ABORT == status) {
        LOGGER__INFO("Socket send_to was aborted!");
        return status;
    } 
    CHECK_SUCCESS(status);

    /*if we had to pad, omit the padding when returning the number of bytes*/
    if (use_padding) {
        number_of_sent_bytes -= PADDING_BYTES_SIZE;
    }

    /* number_of_sent_bytes will be positive because of the validation above */
    *size = (size_t)number_of_sent_bytes;

    return HAILO_SUCCESS;
}

hailo_status Udp::recv(uint8_t *buffer, size_t *size)
{
    hailo_status status = HAILO_UNINITIALIZED;
    size_t number_of_received_bytes = 0;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(buffer);
    CHECK_ARG_NOT_NULL(size);

    if (*size > MAX_UDP_PAYLOAD_SIZE) {
        *size = MAX_UDP_PAYLOAD_SIZE;
    }

    status = m_socket.recv_from(buffer, *size,  0, (struct sockaddr *) &m_device_address, m_device_address_length,
        &number_of_received_bytes); 
    if (HAILO_STREAM_ABORT == status) {
        LOGGER__INFO("Socket recv_from was aborted!");
        return status;
    }
    CHECK_SUCCESS(status);
    
    *size = number_of_received_bytes;
    return HAILO_SUCCESS;
}

hailo_status Udp::abort()
{
    return m_socket.abort();
}

hailo_status Udp::has_data(bool log_timeouts_in_debug)
{
    return m_socket.has_data((struct sockaddr *) &m_device_address, m_device_address_length, log_timeouts_in_debug); 
}

hailo_status Udp::receive_fw_response(uint8_t *buffer, size_t *size, uint32_t expected_sequence)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;

    size_t receive_attempts = 0;
    uint32_t received_sequence = 0;

    ASSERT(NULL != buffer);
    ASSERT(NULL != size);

    for (receive_attempts = 0; receive_attempts < m_max_number_of_attempts; receive_attempts++) {
        /* Receive a single packet */
        status = recv(buffer, size);
        CHECK_SUCCESS(status);

        /* Get the sequence from the buffer */
        common_status = CONTROL_PROTOCOL__get_sequence_from_response_buffer(buffer, *size, &received_sequence);
        status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
        CHECK_SUCCESS(status);

        if (received_sequence == expected_sequence) {
            /* Received the expected response */
            break;
        } else {
            /* Invalid response was received */
            LOGGER__WARNING("Invalid sequence received (received {}, expected {}). Discarding it.", received_sequence,
                expected_sequence);
            continue;
        }
    }
    CHECK((receive_attempts < m_max_number_of_attempts), HAILO_ETH_FAILURE,
        "Received a response with an invalid sequence for {} time.", receive_attempts);

    return HAILO_SUCCESS;
}


hailo_status Udp::fw_interact_impl(uint8_t *request_buffer, size_t request_size, uint8_t *response_buffer,
    size_t *response_size, uint32_t expected_sequence)
{
    hailo_status status = HAILO_UNINITIALIZED;
    size_t expected_request_size = request_size;
    /* If the response_size value is 0, we do not expect response from the fw */
    bool expecting_response = (0 != *response_size);

    ASSERT(NULL != request_buffer);
    ASSERT(NULL != response_buffer);
    ASSERT(NULL != response_size);

    status = send(request_buffer, &request_size, false, MAX_UDP_PAYLOAD_SIZE);
    CHECK_SUCCESS(status);

    /* Validate all bytes were actually sent */
    CHECK(expected_request_size == request_size, HAILO_ETH_FAILURE,
        "Did not send all data at UDP__fw_interact. Expected to send: {}, actually sent: {}", expected_request_size,
        request_size);

    status = receive_fw_response(response_buffer, response_size, expected_sequence);
    if ((HAILO_TIMEOUT == status) && !expecting_response) {
        // This timeout was predictable
        status = HAILO_SUCCESS;
    }
    return status;
}

hailo_status Udp::fw_interact(uint8_t *request_buffer, size_t request_size, uint8_t *response_buffer,
    size_t *response_size, uint32_t expected_sequence)
{
    hailo_status status = HAILO_UNINITIALIZED;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(request_buffer);
    CHECK_ARG_NOT_NULL(response_buffer);
    CHECK_ARG_NOT_NULL(response_size);

    /* Not clearing the read socket before, because the FW ignores duplicated controls,
    so a leftover control response in the read socket is not possible */

    for (size_t attempt_number = 0; attempt_number < m_max_number_of_attempts; ++attempt_number) {
        status = fw_interact_impl(request_buffer, request_size, response_buffer, response_size, expected_sequence);
        if ((HAILO_ETH_RECV_FAILURE == status) || (HAILO_ETH_SEND_FAILURE == status) || (HAILO_TIMEOUT == status)) {
            LOGGER__WARN("Control response was not received, sending it again. Attempt number: {} (zero indexed)",
                    attempt_number);
            continue;
        }
        CHECK_SUCCESS(status);
        /* Not validating amount of received bytes because we can not know how many bytes are expected */
        break;
    }

    return HAILO_SUCCESS;
}

hailo_status Udp::set_max_number_of_attempts(uint8_t max_number_of_attempts)
{
    /* Validate arguments */
    CHECK(0 < max_number_of_attempts, HAILO_INVALID_ARGUMENT,
        "Invalid max_number_of_attempts attempt to be set. max_number_of_attempts cannot be 0.");

    m_max_number_of_attempts = max_number_of_attempts;

    return HAILO_SUCCESS;

}

} /* namespace hailort */
