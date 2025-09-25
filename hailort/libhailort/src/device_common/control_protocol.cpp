/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/

#include "common/utils.hpp"

#include "device_common/control_protocol.hpp"

#include "control_protocol.h"
#include "byte_order.h"
#include "status.h"
#include <stdint.h>
#include <string.h>


using namespace hailort;

#ifndef FIRMWARE_ARCH /*this file should not be compiled for firmware*/

bool g_CONTROL_PROTOCOL__is_critical[HAILO_CONTROL_OPCODE_COUNT] = {
#define CONTROL_PROTOCOL__OPCODE_X(name, is_critical, cpu_id) is_critical,
    CONTROL_PROTOCOL__OPCODES_VARIABLES
#undef CONTROL_PROTOCOL__OPCODE_X
};

CPU_ID_t g_CONTROL_PROTOCOL__cpu_id[HAILO_CONTROL_OPCODE_COUNT] = {
#define CONTROL_PROTOCOL__OPCODE_X(name, is_critical, cpu_id) cpu_id,
    CONTROL_PROTOCOL__OPCODES_VARIABLES
#undef CONTROL_PROTOCOL__OPCODE_X
};

const char *CONTROL_PROTOCOL__textual_format[] =
{
#define STRINGIFY(name) #name
#define CONTROL_PROTOCOL__OPCODE_X(name, is_critical, cpu_id) STRINGIFY(name),
    CONTROL_PROTOCOL__OPCODES_VARIABLES
#undef CONTROL_PROTOCOL__OPCODE_X
};

const char *CONTROL_PROTOCOL__get_textual_opcode(CONTROL_PROTOCOL__OPCODE_t opcode)
{
    return CONTROL_PROTOCOL__textual_format[opcode];
}

#define CHECK_NOT_NULL_COMMON_STATUS(arg, status) _CHECK(nullptr != (arg), (status), "CHECK_NOT_NULL for {} failed", #arg)
#define CHECK_COMMON_STATUS(cond, ret_val, ...) \
    _CHECK((cond), (ret_val), CONSTRUCT_MSG("CHECK failed", ##__VA_ARGS__))

/* Functions declarations */
HAILO_COMMON_STATUS_t control_protocol__parse_message(uint8_t *message,
        uint32_t message_size,
        CONTROL_PROTOCOL__common_header_t **header,
        uint16_t full_header_size,
        CONTROL_PROTOCOL__payload_t **payload,
        uint8_t expected_ack_value);

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__parse_response(uint8_t *message,
        uint32_t message_size,
        CONTROL_PROTOCOL__response_header_t **header,
        CONTROL_PROTOCOL__payload_t **payload,
        CONTROL_PROTOCOL__status_t *fw_status)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    if ((NULL == message) || (NULL == header) || (NULL == payload) || (NULL == fw_status)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    status = control_protocol__parse_message(message,
            message_size,
            (CONTROL_PROTOCOL__common_header_t**)header,
            sizeof(**header),
            payload,
            CONTROL_PROTOCOL__ACK_SET);
    if (HAILO_COMMON_STATUS__SUCCESS != status) {
        goto exit;
    }

    /* Copy firmware status from header */
    fw_status->major_status = BYTE_ORDER__ntohl((*header)->status.major_status);
    fw_status->minor_status = BYTE_ORDER__ntohl((*header)->status.minor_status);

    status = HAILO_COMMON_STATUS__SUCCESS;

exit:
    return status;
}

HAILO_COMMON_STATUS_t control_protocol__parse_message(uint8_t *message,
        uint32_t message_size,
        CONTROL_PROTOCOL__common_header_t **header,
        uint16_t full_header_size,
        CONTROL_PROTOCOL__payload_t **payload,
        uint8_t expected_ack_value)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t current_offset = 0;
    CONTROL_PROTOCOL__parameter_t *current_parameter = NULL;
    uint32_t parameter_count = 0;
    CONTROL_PROTOCOL_flags_t control_flags = {};
    CONTROL_PROTOCOL__common_header_t *local_common_header = NULL;
    CONTROL_PROTOCOL__payload_t *local_payload = NULL;
    uint32_t protocol_version = 0;

    local_common_header = (CONTROL_PROTOCOL__common_header_t *)(message);
    protocol_version = BYTE_ORDER__ntohl(local_common_header->version);

    switch (protocol_version) {
        case CONTROL_PROTOCOL__PROTOCOL_VERSION_2:
            break;
        default:
            status = HAILO_STATUS__CONTROL_PROTOCOL__INVALID_VERSION;
            goto exit;
            break;
    }

    control_flags.integer = BYTE_ORDER__ntohl(local_common_header->flags.integer);
    if (expected_ack_value != control_flags.bitstruct.ack) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__UNEXPECTED_ACK_VALUE;
        goto exit;
    }

    current_offset = full_header_size;
    /* Check if there are any parameters to parse */
    if (current_offset < message_size) {
        local_payload = (CONTROL_PROTOCOL__payload_t *)(message + current_offset);
        current_offset += sizeof(*local_payload);

        /* If the are any parameters, start parsing them */
        if (0 < BYTE_ORDER__ntohl(local_payload->parameter_count)) {
            /* Check that the frame doesn't overrun after parameter count */
            if (current_offset > message_size) {
                status = HAILO_STATUS__CONTROL_PROTOCOL__OVERRUN_BEFORE_PARAMETER;
                goto exit;
            }
            /* Validate each parameter */
            for (parameter_count = 0;
                    parameter_count < BYTE_ORDER__ntohl(local_payload->parameter_count);
                    ++parameter_count) {
                current_parameter = (CONTROL_PROTOCOL__parameter_t *)(
                        (message) + current_offset);
                /* Check that the parameter donesn't overrun the packet */
                current_offset += sizeof(*current_parameter) + BYTE_ORDER__ntohl(current_parameter->length);
                if (current_offset > message_size) {
                    status = HAILO_STATUS__CONTROL_PROTOCOL__OVERRUN_AT_PARAMETER;
                    goto exit;
                }
            }
        }
    }

    /* Validate all of the message was parsed */
    if (current_offset != message_size) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__PART_OF_THE_MESSAGE_NOT_PARSED;
        goto exit;
    }

    /* Packet is valid, assign out parameters */
    *header = local_common_header;
    local_common_header = NULL;
    *payload = local_payload;
    local_payload = NULL;

    status = HAILO_COMMON_STATUS__SUCCESS;

exit:
    return status;
}


HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__get_sequence_from_response_buffer(uint8_t *response_buffer,
        size_t response_buffer_size, uint32_t *sequence)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    uint32_t local_sequence = 0;
    CONTROL_PROTOCOL__common_header_t *common_header = NULL;

    if ((NULL == response_buffer) || (NULL == sequence)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    if (sizeof(CONTROL_PROTOCOL__common_header_t) > response_buffer_size) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__INVALID_BUFFER_SIZE;
        goto exit;
    }

    /* Get the sequence from the common header */
    common_header = ((CONTROL_PROTOCOL__common_header_t*)(response_buffer));
    local_sequence = BYTE_ORDER__ntohl(common_header->sequence);

    *sequence = local_sequence;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

void control_protocol__pack_request_header(CONTROL_PROTOCOL__request_t *request, uint32_t sequence, CONTROL_PROTOCOL__OPCODE_t opcode, uint32_t parameter_count)
{
    request->header.common_header.opcode = BYTE_ORDER__htonl(opcode);
    request->header.common_header.sequence = BYTE_ORDER__htonl(sequence);
    request->header.common_header.version = BYTE_ORDER__htonl(CONTROL_PROTOCOL__PROTOCOL_VERSION);

    request->parameter_count = BYTE_ORDER__htonl(parameter_count);
}

HAILO_COMMON_STATUS_t control_protocol__pack_empty_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, CONTROL_PROTOCOL__OPCODE_t opcode)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE;
    control_protocol__pack_request_header(request, sequence, opcode, 0);

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_identify_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence)
{
    return control_protocol__pack_empty_request(request, request_size, sequence, HAILO_CONTROL_OPCODE_IDENTIFY);
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_core_identify_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence)
{
    return control_protocol__pack_empty_request(request, request_size, sequence, HAILO_CONTROL_OPCODE_CORE_IDENTIFY);
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_set_fw_logger_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence,
                                                                   hailo_fw_logger_level_t level, uint8_t interface_mask)
{
    size_t local_request_size = 0;

    CHECK_COMMON_STATUS(request != nullptr, HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED);
    CHECK_COMMON_STATUS(request_size != nullptr, HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED);

    CHECK_COMMON_STATUS(level <= (uint8_t) CONTROL_PROTOCOL__FW_MAX_LOGGER_LEVEL, HAILO_STATUS__CONTROL_PROTOCOL__INVALID_ARGUMENT);
    CHECK_COMMON_STATUS(interface_mask <= CONTROL_PROTOCOL__FW_MAX_LOGGER_INTERFACE, HAILO_STATUS__CONTROL_PROTOCOL__INVALID_ARGUMENT);

    static_assert((uint32_t) FW_LOGGER_LEVEL_TRACE == (uint32_t) HAILO_FW_LOGGER_LEVEL_TRACE,
        "mismatch in FW_LOGGER_LEVEL_TRACE and HAILO_FW_LOGGER_LEVEL_TRACE");
    static_assert((uint32_t) FW_LOGGER_LEVEL_DEBUG == (uint32_t) HAILO_FW_LOGGER_LEVEL_DEBUG,
        "mismatch in FW_LOGGER_LEVEL_DEBUG and HAILO_FW_LOGGER_LEVEL_DEBUG");
    static_assert((uint32_t) FW_LOGGER_LEVEL_INFO == (uint32_t) HAILO_FW_LOGGER_LEVEL_INFO,
        "mismatch in FW_LOGGER_LEVEL_INFO and HAILO_FW_LOGGER_LEVEL_INFO");
    static_assert((uint32_t) FW_LOGGER_LEVEL_WARN == (uint32_t) HAILO_FW_LOGGER_LEVEL_WARN,
        "mismatch in FW_LOGGER_LEVEL_WARN and HAILO_FW_LOGGER_LEVEL_WARN");
    static_assert((uint32_t) FW_LOGGER_LEVEL_ERROR == (uint32_t) HAILO_FW_LOGGER_LEVEL_ERROR,
        "mismatch in FW_LOGGER_LEVEL_ERROR and HAILO_FW_LOGGER_LEVEL_ERROR");
    static_assert((uint32_t) FW_LOGGER_LEVEL_FATAL == (uint32_t) HAILO_FW_LOGGER_LEVEL_FATAL,
        "mismatch in FW_LOGGER_LEVEL_FATAL and HAILO_FW_LOGGER_LEVEL_FATAL");
    static_assert((uint32_t)CONTROL_PROTOCOL__INTERFACE_PCIE == (uint32_t)HAILO_FW_LOGGER_INTERFACE_PCIE,
        "mismatch in CONTROL_PROTOCOL__INTERFACE_PCIE and HAILO_FW_LOGGER_INTERFACE_PCIE");
    static_assert((uint32_t)CONTROL_PROTOCOL__INTERFACE_UART == (uint32_t)HAILO_FW_LOGGER_INTERFACE_UART,
        "mismatch in CONTROL_PROTOCOL__INTERFACE_UART and HAILO_FW_LOGGER_INTERFACE_UART");

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__set_fw_logger_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_SET_FW_LOGGER, 2);

    request->parameters.set_fw_logger_request.level_length = BYTE_ORDER__htonl(sizeof(request->parameters.set_fw_logger_request.level));
    request->parameters.set_fw_logger_request.level = static_cast<uint8_t>(level);

    request->parameters.set_fw_logger_request.logger_interface_bit_mask_length = BYTE_ORDER__htonl(sizeof(request->parameters.set_fw_logger_request.logger_interface_bit_mask));
    request->parameters.set_fw_logger_request.logger_interface_bit_mask = interface_mask;
    
    *request_size = local_request_size;
    return HAILO_COMMON_STATUS__SUCCESS;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_set_throttling_state_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence,
                                                                          bool should_activate)
{
    size_t local_request_size = 0;

    CHECK_NOT_NULL_COMMON_STATUS(request, HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED);
    CHECK_NOT_NULL_COMMON_STATUS(request_size, HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED);

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__set_throttling_state_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_SET_THROTTLING_STATE, 1);

    request->parameters.set_throttling_state_request.should_activate_length = BYTE_ORDER__htonl(sizeof(request->parameters.set_throttling_state_request.should_activate));
    request->parameters.set_throttling_state_request.should_activate = should_activate;
    
    *request_size = local_request_size;
    return HAILO_COMMON_STATUS__SUCCESS;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_get_throttling_state_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence)
{
    return control_protocol__pack_empty_request(request, request_size, sequence, HAILO_CONTROL_OPCODE_GET_THROTTLING_STATE);
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_set_overcurrent_state_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence,
    bool should_activate)
{
    size_t local_request_size = 0;

    CHECK_NOT_NULL_COMMON_STATUS(request, HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED);
    CHECK_NOT_NULL_COMMON_STATUS(request_size, HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED);

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__set_overcurrent_state_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_SET_OVERCURRENT_STATE, 1);

    request->parameters.set_overcurrent_state_request.should_activate_length = BYTE_ORDER__htonl(sizeof(request->parameters.set_overcurrent_state_request.should_activate));
    request->parameters.set_overcurrent_state_request.should_activate = should_activate;
    
    *request_size = local_request_size;
    return HAILO_COMMON_STATUS__SUCCESS;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_get_overcurrent_state_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence)
{
    return control_protocol__pack_empty_request(request, request_size, sequence, HAILO_CONTROL_OPCODE_GET_OVERCURRENT_STATE);
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_get_hw_consts_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence)
{
    return control_protocol__pack_empty_request(request, request_size, sequence, HAILO_CONTROL_OPCODE_GET_HW_CONSTS);
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_set_clock_freq_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence,
                                                                     uint32_t clock_freq)
{
    size_t local_request_size = 0;

    CHECK_COMMON_STATUS(request != nullptr, HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED);
    CHECK_COMMON_STATUS(request_size != nullptr, HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED);

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__set_clock_freq_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_SET_CLOCK_FREQ, 1);

    request->parameters.set_clock_freq_request.clock_freq_length = BYTE_ORDER__htonl(sizeof(request->parameters.set_clock_freq_request.clock_freq));
    request->parameters.set_clock_freq_request.clock_freq = BYTE_ORDER__htonl(clock_freq);
    
    *request_size = local_request_size;
    return HAILO_COMMON_STATUS__SUCCESS;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_write_memory_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint32_t address, const uint8_t *data, uint32_t data_length)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size) || (NULL == data)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__write_memory_request_t) + data_length;
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_WRITE_MEMORY, 2);

    /* Address */
    request->parameters.write_memory_request.address_length = BYTE_ORDER__htonl(sizeof(request->parameters.write_memory_request.address));
    request->parameters.write_memory_request.address = BYTE_ORDER__htonl(address);

    /* Data */
    request->parameters.write_memory_request.data_length = BYTE_ORDER__htonl(data_length);
    memcpy(&(request->parameters.write_memory_request.data), data, data_length);


    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_read_memory_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint32_t address, uint32_t data_length)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__read_memory_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_READ_MEMORY, 2);

    /* Address */
    request->parameters.read_memory_request.address_length = BYTE_ORDER__htonl(sizeof(request->parameters.read_memory_request.address));
    request->parameters.read_memory_request.address = BYTE_ORDER__htonl(address);

    /* Data count */
    request->parameters.read_memory_request.data_count_length = BYTE_ORDER__htonl(sizeof(request->parameters.read_memory_request.data_count));
    request->parameters.read_memory_request.data_count = BYTE_ORDER__htonl(data_length);


    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_open_stream_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint8_t dataflow_manager_id, uint8_t is_input)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__open_stream_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_OPEN_STREAM, 2);

    /* dataflow_manager_id */
    request->parameters.open_stream_request.dataflow_manager_id_length = BYTE_ORDER__htonl(sizeof(request->parameters.open_stream_request.dataflow_manager_id));
    request->parameters.open_stream_request.dataflow_manager_id = dataflow_manager_id;

    /* is_input */
    request->parameters.open_stream_request.is_input_length = BYTE_ORDER__htonl(sizeof(request->parameters.open_stream_request.is_input));
    request->parameters.open_stream_request.is_input = is_input;


    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_close_stream_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint8_t dataflow_manager_id, uint8_t is_input)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__close_stream_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_CLOSE_STREAM, 2);

    /* dataflow_manager_id */
    request->parameters.close_stream_request.dataflow_manager_id_length = BYTE_ORDER__htonl(sizeof(request->parameters.close_stream_request.dataflow_manager_id));
    request->parameters.close_stream_request.dataflow_manager_id = dataflow_manager_id;

    /* is_input */
    request->parameters.close_stream_request.is_input_length = BYTE_ORDER__htonl(sizeof(request->parameters.close_stream_request.is_input));
    request->parameters.close_stream_request.is_input = is_input;


    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t control_protocol__pack_config_stream_base_request(CONTROL_PROTOCOL__request_t *request, CONTROL_PROTOCOL__config_stream_params_t *params)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;

    /* stream index */
    request->parameters.config_stream_request.stream_index_length = BYTE_ORDER__htonl(sizeof(request->parameters.config_stream_request.stream_index));
    request->parameters.config_stream_request.stream_index = params->stream_index;

    /* is_input */
    request->parameters.config_stream_request.is_input_length = BYTE_ORDER__htonl(sizeof(request->parameters.config_stream_request.is_input));
    request->parameters.config_stream_request.is_input = params->is_input;

    /* communication_type */
    request->parameters.config_stream_request.communication_type_length = BYTE_ORDER__htonl(sizeof(request->parameters.config_stream_request.communication_type));
    request->parameters.config_stream_request.communication_type = BYTE_ORDER__htonl(params->communication_type);

    /* skip_nn_stream_config */
    request->parameters.config_stream_request.skip_nn_stream_config_length = BYTE_ORDER__htonl(sizeof(request->parameters.config_stream_request.skip_nn_stream_config));
    request->parameters.config_stream_request.skip_nn_stream_config = params->skip_nn_stream_config;

    /* power_mode */
    request->parameters.config_stream_request.power_mode_length = BYTE_ORDER__htonl(sizeof(request->parameters.config_stream_request.power_mode));
    request->parameters.config_stream_request.power_mode = params->power_mode;

    /* nn_stream_config */
    request->parameters.config_stream_request.nn_stream_config_length = BYTE_ORDER__htonl(sizeof(request->parameters.config_stream_request.nn_stream_config));
    request->parameters.config_stream_request.nn_stream_config.core_bytes_per_buffer = BYTE_ORDER__htons(params->nn_stream_config.core_bytes_per_buffer);
    request->parameters.config_stream_request.nn_stream_config.core_buffers_per_frame = BYTE_ORDER__htons(params->nn_stream_config.core_buffers_per_frame);
    request->parameters.config_stream_request.nn_stream_config.periph_bytes_per_buffer = BYTE_ORDER__htons(params->nn_stream_config.periph_bytes_per_buffer);
    request->parameters.config_stream_request.nn_stream_config.periph_buffers_per_frame = BYTE_ORDER__htons(params->nn_stream_config.periph_buffers_per_frame);
    request->parameters.config_stream_request.nn_stream_config.feature_padding_payload = BYTE_ORDER__htons(params->nn_stream_config.feature_padding_payload);
    request->parameters.config_stream_request.nn_stream_config.buffer_padding_payload = BYTE_ORDER__htons(params->nn_stream_config.buffer_padding_payload);
    request->parameters.config_stream_request.nn_stream_config.buffer_padding = BYTE_ORDER__htons(params->nn_stream_config.buffer_padding);

    status = HAILO_COMMON_STATUS__SUCCESS;
    goto exit;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_config_stream_udp_input_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, CONTROL_PROTOCOL__config_stream_params_t *params)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size) || (NULL == params)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__config_stream_request_t) - sizeof(CONTROL_PROTOCOL__communication_config_prams_t) + sizeof(CONTROL_PROTOCOL__udp_input_config_params_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_CONFIG_STREAM, 7);

    status = control_protocol__pack_config_stream_base_request(request, params);
    if (HAILO_COMMON_STATUS__SUCCESS != status) {
        goto exit;
    }

    request->parameters.config_stream_request.communication_params_length = BYTE_ORDER__htonl(sizeof(params->communication_params.udp_input));
    request->parameters.config_stream_request.communication_params.udp_input.listening_port = BYTE_ORDER__htons(params->communication_params.udp_input.listening_port);

    request->parameters.config_stream_request.communication_params.udp_input.sync.should_sync = params->communication_params.udp_input.sync.should_sync;
    request->parameters.config_stream_request.communication_params.udp_input.sync.frames_per_sync = BYTE_ORDER__htonl(params->communication_params.udp_input.sync.frames_per_sync);
    request->parameters.config_stream_request.communication_params.udp_input.sync.packets_per_frame = BYTE_ORDER__htonl(params->communication_params.udp_input.sync.packets_per_frame);
    request->parameters.config_stream_request.communication_params.udp_input.sync.sync_size = BYTE_ORDER__htons(params->communication_params.udp_input.sync.sync_size);

    request->parameters.config_stream_request.communication_params.udp_input.buffers_threshold = BYTE_ORDER__htonl(params->communication_params.udp_input.buffers_threshold);
    request->parameters.config_stream_request.communication_params.udp_input.use_rtp = params->communication_params.udp_input.use_rtp;

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_config_stream_udp_output_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, CONTROL_PROTOCOL__config_stream_params_t *params)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size) || (NULL == params)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__config_stream_request_t) - sizeof(CONTROL_PROTOCOL__communication_config_prams_t) + sizeof(CONTROL_PROTOCOL__udp_output_config_params_t);

    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_CONFIG_STREAM, 7);

    status = control_protocol__pack_config_stream_base_request(request, params);
    if (HAILO_COMMON_STATUS__SUCCESS != status) {
        goto exit;
    }

    request->parameters.config_stream_request.communication_params_length = BYTE_ORDER__htonl(sizeof(params->communication_params.udp_output));
    request->parameters.config_stream_request.communication_params.udp_output.host_udp_port = BYTE_ORDER__htons(params->communication_params.udp_output.host_udp_port);
    request->parameters.config_stream_request.communication_params.udp_output.chip_udp_port = BYTE_ORDER__htons(params->communication_params.udp_output.chip_udp_port);
    request->parameters.config_stream_request.communication_params.udp_output.max_udp_payload_size = BYTE_ORDER__htons(params->communication_params.udp_output.max_udp_payload_size);
    request->parameters.config_stream_request.communication_params.udp_output.should_send_sync_packets = params->communication_params.udp_output.should_send_sync_packets;
    request->parameters.config_stream_request.communication_params.udp_output.buffers_threshold = BYTE_ORDER__htonl(params->communication_params.udp_output.buffers_threshold);
    request->parameters.config_stream_request.communication_params.udp_output.use_rtp = params->communication_params.udp_output.use_rtp;

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_config_stream_mipi_input_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, CONTROL_PROTOCOL__config_stream_params_t *params)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size) || (NULL == params)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    /* Calculate the size of the exact mipi_input configuration struct instead of the entire union */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__config_stream_request_t) - sizeof(CONTROL_PROTOCOL__communication_config_prams_t) + sizeof(CONTROL_PROTOCOL__mipi_input_config_params_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_CONFIG_STREAM, 7);

    status = control_protocol__pack_config_stream_base_request(request, params);
    if (HAILO_COMMON_STATUS__SUCCESS != status) {
        goto exit;
    }

    request->parameters.config_stream_request.communication_params_length = BYTE_ORDER__htonl(sizeof(params->communication_params.mipi_input));
    request->parameters.config_stream_request.communication_params.mipi_input.common_params.data_type = params->communication_params.mipi_input.common_params.data_type;
    request->parameters.config_stream_request.communication_params.mipi_input.common_params.pixels_per_clock = params->communication_params.mipi_input.common_params.pixels_per_clock;
    request->parameters.config_stream_request.communication_params.mipi_input.mipi_rx_id = params->communication_params.mipi_input.mipi_rx_id;
    request->parameters.config_stream_request.communication_params.mipi_input.common_params.number_of_lanes = params->communication_params.mipi_input.common_params.number_of_lanes;
    request->parameters.config_stream_request.communication_params.mipi_input.common_params.clock_selection = params->communication_params.mipi_input.common_params.clock_selection;
    request->parameters.config_stream_request.communication_params.mipi_input.common_params.data_rate = BYTE_ORDER__htonl(params->communication_params.mipi_input.common_params.data_rate);
    request->parameters.config_stream_request.communication_params.mipi_input.common_params.virtual_channel_index = params->communication_params.mipi_input.common_params.virtual_channel_index;
    request->parameters.config_stream_request.communication_params.mipi_input.common_params.img_width_pixels = params->communication_params.mipi_input.common_params.img_width_pixels;
    request->parameters.config_stream_request.communication_params.mipi_input.common_params.img_height_pixels = params->communication_params.mipi_input.common_params.img_height_pixels;
    request->parameters.config_stream_request.communication_params.mipi_input.isp_params.isp_enable = params->communication_params.mipi_input.isp_params.isp_enable;
    request->parameters.config_stream_request.communication_params.mipi_input.isp_params.isp_img_in_order = params->communication_params.mipi_input.isp_params.isp_img_in_order;
    request->parameters.config_stream_request.communication_params.mipi_input.isp_params.isp_img_out_data_type = params->communication_params.mipi_input.isp_params.isp_img_out_data_type;
    request->parameters.config_stream_request.communication_params.mipi_input.isp_params.isp_crop_enable = params->communication_params.mipi_input.isp_params.isp_crop_enable;
    request->parameters.config_stream_request.communication_params.mipi_input.isp_params.isp_crop_output_width_pixels = params->communication_params.mipi_input.isp_params.isp_crop_output_width_pixels;
    request->parameters.config_stream_request.communication_params.mipi_input.isp_params.isp_crop_output_height_pixels = params->communication_params.mipi_input.isp_params.isp_crop_output_height_pixels;
    request->parameters.config_stream_request.communication_params.mipi_input.isp_params.isp_crop_output_width_start_offset_pixels = params->communication_params.mipi_input.isp_params.isp_crop_output_width_start_offset_pixels;
    request->parameters.config_stream_request.communication_params.mipi_input.isp_params.isp_crop_output_height_start_offset_pixels = params->communication_params.mipi_input.isp_params.isp_crop_output_height_start_offset_pixels;
    request->parameters.config_stream_request.communication_params.mipi_input.isp_params.isp_test_pattern_enable = params->communication_params.mipi_input.isp_params.isp_test_pattern_enable;
    request->parameters.config_stream_request.communication_params.mipi_input.isp_params.isp_configuration_bypass = params->communication_params.mipi_input.isp_params.isp_configuration_bypass;
    request->parameters.config_stream_request.communication_params.mipi_input.isp_params.isp_run_time_ae_enable = params->communication_params.mipi_input.isp_params.isp_run_time_ae_enable;
    request->parameters.config_stream_request.communication_params.mipi_input.isp_params.isp_run_time_awb_enable = params->communication_params.mipi_input.isp_params.isp_run_time_awb_enable;
    request->parameters.config_stream_request.communication_params.mipi_input.isp_params.isp_run_time_adt_enable = params->communication_params.mipi_input.isp_params.isp_run_time_adt_enable;
    request->parameters.config_stream_request.communication_params.mipi_input.isp_params.isp_run_time_af_enable = params->communication_params.mipi_input.isp_params.isp_run_time_af_enable;
    request->parameters.config_stream_request.communication_params.mipi_input.isp_params.isp_run_time_calculations_interval_ms = params->communication_params.mipi_input.isp_params.isp_run_time_calculations_interval_ms;
    request->parameters.config_stream_request.communication_params.mipi_input.isp_params.isp_light_frequency = params->communication_params.mipi_input.isp_params.isp_light_frequency;

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_config_stream_mipi_output_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, CONTROL_PROTOCOL__config_stream_params_t *params)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size) || (NULL == params)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    /* Calculate the size of the exact mipi_output configuration struct instead of the entire union */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__config_stream_request_t) - sizeof(CONTROL_PROTOCOL__communication_config_prams_t) + sizeof(CONTROL_PROTOCOL__mipi_output_config_params_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_CONFIG_STREAM, 7);

    status = control_protocol__pack_config_stream_base_request(request, params);
    if (HAILO_COMMON_STATUS__SUCCESS != status) {
        goto exit;
    }

    request->parameters.config_stream_request.communication_params_length = BYTE_ORDER__htonl(sizeof(params->communication_params.mipi_output));
    request->parameters.config_stream_request.communication_params.mipi_output.fifo_threshold_percent = params->communication_params.mipi_output.fifo_threshold_percent;
    request->parameters.config_stream_request.communication_params.mipi_output.mipi_tx_id = params->communication_params.mipi_output.mipi_tx_id;
    request->parameters.config_stream_request.communication_params.mipi_output.deskew_enable = params->communication_params.mipi_output.deskew_enable;
    request->parameters.config_stream_request.communication_params.mipi_output.common_params.data_rate = BYTE_ORDER__htonl(params->communication_params.mipi_output.common_params.data_rate);
    request->parameters.config_stream_request.communication_params.mipi_output.common_params.clock_selection = params->communication_params.mipi_output.common_params.clock_selection;
    request->parameters.config_stream_request.communication_params.mipi_output.common_params.data_type = params->communication_params.mipi_output.common_params.data_type;
    request->parameters.config_stream_request.communication_params.mipi_output.common_params.number_of_lanes = params->communication_params.mipi_output.common_params.number_of_lanes;
    request->parameters.config_stream_request.communication_params.mipi_output.common_params.pixels_per_clock = params->communication_params.mipi_output.common_params.pixels_per_clock;
    request->parameters.config_stream_request.communication_params.mipi_output.common_params.virtual_channel_index = params->communication_params.mipi_output.common_params.virtual_channel_index;

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_config_stream_pcie_input_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, CONTROL_PROTOCOL__config_stream_params_t *params)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size) || (NULL == params)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__config_stream_request_t) 
        - sizeof(CONTROL_PROTOCOL__communication_config_prams_t) + sizeof(CONTROL_PROTOCOL__pcie_input_config_params_t);

    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_CONFIG_STREAM, 7);

    status = control_protocol__pack_config_stream_base_request(request, params);
    if (HAILO_COMMON_STATUS__SUCCESS != status) {
        goto exit;
    }

    request->parameters.config_stream_request.communication_params_length = 
        BYTE_ORDER__htonl(sizeof(params->communication_params.pcie_input));
    request->parameters.config_stream_request.communication_params.pcie_input.pcie_channel_index = 
        params->communication_params.pcie_input.pcie_channel_index;
    request->parameters.config_stream_request.communication_params.pcie_input.pcie_dataflow_type = 
        params->communication_params.pcie_input.pcie_dataflow_type;

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_config_stream_pcie_output_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, CONTROL_PROTOCOL__config_stream_params_t *params)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size) || (NULL == params)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__config_stream_request_t) 
        - sizeof(CONTROL_PROTOCOL__communication_config_prams_t) + sizeof(CONTROL_PROTOCOL__pcie_output_config_params_t);

    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_CONFIG_STREAM, 7);

    status = control_protocol__pack_config_stream_base_request(request, params);
    if (HAILO_COMMON_STATUS__SUCCESS != status) {
        goto exit;
    }

    request->parameters.config_stream_request.communication_params_length = 
        BYTE_ORDER__htonl(sizeof(params->communication_params.pcie_output));
    request->parameters.config_stream_request.communication_params.pcie_output.pcie_channel_index = 
        params->communication_params.pcie_output.pcie_channel_index;
    request->parameters.config_stream_request.communication_params.pcie_output.desc_page_size = 
        params->communication_params.pcie_output.desc_page_size;

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_reset_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, CONTROL_PROTOCOL__reset_type_t reset_type)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__reset_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_RESET, 1);

    /* reset_type */
    request->parameters.reset_resquest.reset_type_length = BYTE_ORDER__htonl(sizeof(request->parameters.reset_resquest.reset_type));
    request->parameters.reset_resquest.reset_type = BYTE_ORDER__htonl((uint32_t)reset_type);

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_power_measurement_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, CONTROL_PROTOCOL__dvm_options_t dvm, CONTROL_PROTOCOL__power_measurement_types_t measurement_type)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__power_measurement_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_POWER_MEASUEMENT, 2);

    /* dvm */
    request->parameters.measure_power_request.dvm_length = BYTE_ORDER__htonl(sizeof(request->parameters.measure_power_request.dvm_length));
    request->parameters.measure_power_request.dvm = BYTE_ORDER__htonl((uint32_t)dvm);


    /* measurement_type */
    request->parameters.measure_power_request.measurement_type_length = BYTE_ORDER__htonl(sizeof(request->parameters.measure_power_request.measurement_type));
    request->parameters.measure_power_request.measurement_type = BYTE_ORDER__htonl((uint32_t)measurement_type);

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_set_power_measurement_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint32_t index, CONTROL_PROTOCOL__dvm_options_t dvm, CONTROL_PROTOCOL__power_measurement_types_t measurement_type)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__set_power_measurement_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_SET_POWER_MEASUEMENT, 3);

    /* index */
    request->parameters.set_measure_power_request.index_length = BYTE_ORDER__htonl(
            sizeof(request->parameters.set_measure_power_request.index));
    request->parameters.set_measure_power_request.index = BYTE_ORDER__htonl(index);

    /* dvm */
    request->parameters.set_measure_power_request.dvm_length = BYTE_ORDER__htonl(
            sizeof(request->parameters.set_measure_power_request.dvm));
    request->parameters.set_measure_power_request.dvm = BYTE_ORDER__htonl((uint32_t)dvm);


    /* measurement_type */
    request->parameters.set_measure_power_request.measurement_type_length = BYTE_ORDER__htonl(
            sizeof(request->parameters.set_measure_power_request.measurement_type));
    request->parameters.set_measure_power_request.measurement_type = BYTE_ORDER__htonl((uint32_t)measurement_type);

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_get_power_measurement_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint32_t index, bool should_clear)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__get_power_measurement_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_GET_POWER_MEASUEMENT, 2);

    /* index */
    request->parameters.get_measure_power_request.index_length = BYTE_ORDER__htonl(
            sizeof(request->parameters.get_measure_power_request.index));
    request->parameters.get_measure_power_request.index = BYTE_ORDER__htonl(index);

    /* should_clear */
    request->parameters.get_measure_power_request.should_clear_length = BYTE_ORDER__htonl(
            sizeof(request->parameters.get_measure_power_request.should_clear));
    request->parameters.get_measure_power_request.should_clear = (uint8_t)should_clear;

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_start_power_measurement_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint32_t delay_milliseconds, CONTROL_PROTOCOL__averaging_factor_t averaging_factor , CONTROL_PROTOCOL__sampling_period_t sampling_period)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;
    uint16_t local_averaging_factor = 0;
    uint16_t local_sampling_period = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    local_averaging_factor = ((uint16_t)(averaging_factor));
    local_sampling_period = ((uint16_t)(sampling_period));

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__start_power_measurement_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_START_POWER_MEASUEMENT, 3);

    /* delay_milliseconds */
    request->parameters.start_measure_power_request.delay_milliseconds_length = BYTE_ORDER__htonl(
            sizeof(request->parameters.start_measure_power_request.delay_milliseconds));
    request->parameters.start_measure_power_request.delay_milliseconds = BYTE_ORDER__htonl(delay_milliseconds);

    /* averaging_factor */
    request->parameters.start_measure_power_request.averaging_factor_length = BYTE_ORDER__htonl(
            sizeof(request->parameters.start_measure_power_request.averaging_factor));
    request->parameters.start_measure_power_request.averaging_factor = BYTE_ORDER__htons(local_averaging_factor);

    /* sampling_period */
    request->parameters.start_measure_power_request.sampling_period_length = BYTE_ORDER__htonl(
            sizeof(request->parameters.start_measure_power_request.sampling_period));
    request->parameters.start_measure_power_request.sampling_period = BYTE_ORDER__htons(local_sampling_period);

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_i2c_write_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size,
        uint32_t sequence, uint32_t register_address, uint8_t endianness, uint16_t slave_address,
        uint8_t register_address_size, uint8_t bus_index, const uint8_t *data, uint32_t length)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size) || (NULL == data)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__i2c_write_request_t) + length;
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_I2C_WRITE, 7);

    /* register_address */
    request->parameters.i2c_write_request.register_address_size = BYTE_ORDER__htonl(
            sizeof(request->parameters.i2c_write_request.register_address));
    request->parameters.i2c_write_request.register_address = BYTE_ORDER__htonl(register_address);

    /* endianness */
    request->parameters.i2c_write_request.slave_config.endianness_length = BYTE_ORDER__htonl(
            sizeof(request->parameters.i2c_write_request.slave_config.endianness));
    request->parameters.i2c_write_request.slave_config.endianness = endianness;

    /* slave_address */
    request->parameters.i2c_write_request.slave_config.slave_address_length = BYTE_ORDER__htonl(
            sizeof(request->parameters.i2c_write_request.slave_config.slave_address));
    request->parameters.i2c_write_request.slave_config.slave_address = BYTE_ORDER__htons(slave_address);

    /* register_address_size */
    request->parameters.i2c_write_request.slave_config.register_address_size_length = BYTE_ORDER__htonl(
            sizeof(request->parameters.i2c_write_request.slave_config.register_address_size));
    request->parameters.i2c_write_request.slave_config.register_address_size = register_address_size;

    /* bus_index */
    request->parameters.i2c_write_request.slave_config.bus_index_length = BYTE_ORDER__htonl(
            sizeof(request->parameters.i2c_write_request.slave_config.bus_index));
    request->parameters.i2c_write_request.slave_config.bus_index = bus_index;

    /* Data */
    request->parameters.i2c_write_request.data_length = BYTE_ORDER__htonl(length);
    memcpy(&(request->parameters.i2c_write_request.data), data, length);

    /* should_hold_bus */
    request->parameters.i2c_write_request.slave_config.should_hold_bus_length = BYTE_ORDER__htonl(
            sizeof(request->parameters.i2c_write_request.slave_config.should_hold_bus));
    request->parameters.i2c_write_request.slave_config.should_hold_bus = false;

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}


HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_i2c_read_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size,
        uint32_t sequence, uint32_t register_address, uint8_t endianness,
        uint16_t slave_address, uint8_t register_address_size, uint8_t bus_index, uint32_t length, bool should_hold_bus)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__i2c_read_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_I2C_READ, 7);

    /* data_length */
    request->parameters.i2c_read_request.data_length_length = BYTE_ORDER__htonl(
            sizeof(request->parameters.i2c_read_request.data_length));
    request->parameters.i2c_read_request.data_length = BYTE_ORDER__htonl(length);
    
    /* register_address */
    request->parameters.i2c_read_request.register_address_size = BYTE_ORDER__htonl(
            sizeof(request->parameters.i2c_read_request.register_address));
    request->parameters.i2c_read_request.register_address = BYTE_ORDER__htonl(register_address);

    /* endianness */
    request->parameters.i2c_read_request.slave_config.endianness_length = BYTE_ORDER__htonl(
            sizeof(request->parameters.i2c_read_request.slave_config.endianness));
    request->parameters.i2c_read_request.slave_config.endianness = endianness;

    /* slave_address */
    request->parameters.i2c_read_request.slave_config.slave_address_length = BYTE_ORDER__htonl(
            sizeof(request->parameters.i2c_read_request.slave_config.slave_address));
    request->parameters.i2c_read_request.slave_config.slave_address = BYTE_ORDER__htons(slave_address);

    /* register_address_size */
    request->parameters.i2c_read_request.slave_config.register_address_size_length = BYTE_ORDER__htonl(
            sizeof(request->parameters.i2c_read_request.slave_config.register_address_size));
    request->parameters.i2c_read_request.slave_config.register_address_size = register_address_size;

    /* bus_index */
    request->parameters.i2c_read_request.slave_config.bus_index_length = BYTE_ORDER__htonl(
            sizeof(request->parameters.i2c_read_request.slave_config.bus_index));
    request->parameters.i2c_read_request.slave_config.bus_index = bus_index;

    /* should_hold_bus */
    request->parameters.i2c_read_request.slave_config.should_hold_bus_length = BYTE_ORDER__htonl(
            sizeof(request->parameters.i2c_read_request.slave_config.should_hold_bus));
    request->parameters.i2c_read_request.slave_config.should_hold_bus = should_hold_bus;

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_stop_power_measurement_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence)
{
    return control_protocol__pack_empty_request(request, request_size, sequence, HAILO_CONTROL_OPCODE_STOP_POWER_MEASUEMENT);
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_config_core_top_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, CONTROL_PROTOCOL__config_core_top_type_t config_type, CONTROL_PROTOCOL__config_core_top_params_t *params)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size) || (NULL == params)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__config_core_top_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_CONFIG_CORE_TOP, 2);

    /* config_type */
    request->parameters.config_core_top_request.config_type_length = BYTE_ORDER__htonl(
            sizeof(request->parameters.config_core_top_request.config_type));
    request->parameters.config_core_top_request.config_type = BYTE_ORDER__htonl(config_type);

    /* params */
    request->parameters.config_core_top_request.config_params_length = BYTE_ORDER__htonl(
            sizeof(request->parameters.config_core_top_request.config_params));
    (void)memcpy(&request->parameters.config_core_top_request.config_params,
            params,
            sizeof(request->parameters.config_core_top_request.config_params));

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_phy_operation_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, CONTROL_PROTOCOL__phy_operation_t operation_type)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__phy_operation_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_PHY_OPERATION, 1);

    /* operation_type */
    request->parameters.phy_operation_request.operation_type_length = BYTE_ORDER__htonl(
            sizeof(request->parameters.phy_operation_request.operation_type));
    request->parameters.phy_operation_request.operation_type = BYTE_ORDER__htonl((uint32_t)operation_type);

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_read_user_config(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint32_t address, uint32_t data_length)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__read_user_config_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_READ_USER_CONFIG, 2);

    /* Address */
    request->parameters.read_user_config_request.address_length = BYTE_ORDER__htonl(sizeof(request->parameters.read_user_config_request.address));
    request->parameters.read_user_config_request.address = BYTE_ORDER__htonl(address);

    /* Data count */
    request->parameters.read_user_config_request.data_count_length = BYTE_ORDER__htonl(sizeof(request->parameters.read_user_config_request.data_count));
    request->parameters.read_user_config_request.data_count = BYTE_ORDER__htonl(data_length);


    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_examine_user_config(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE;
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_EXAMINE_USER_CONFIG, 0);

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_write_user_config_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint32_t address, const uint8_t *data, uint32_t data_length)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size) || (NULL == data)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__write_user_config_request_t) + data_length;
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_WRITE_USER_CONFIG, 2);

    /* Address */
    request->parameters.write_user_config_request.address_length = BYTE_ORDER__htonl(sizeof(request->parameters.write_user_config_request.address));
    request->parameters.write_user_config_request.address = BYTE_ORDER__htonl(address);

    /* Data */
    request->parameters.write_user_config_request.data_length = BYTE_ORDER__htonl(data_length);
    memcpy(&(request->parameters.write_user_config_request.data), data, data_length);


    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_erase_user_config_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE;
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_ERASE_USER_CONFIG, 0);

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}


HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_start_firmware_update_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE ;
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_START_FIRMWARE_UPDATE, 0);


    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_finish_firmware_update_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE ;
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_FINISH_FIRMWARE_UPDATE, 0);

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__write_firmware_update_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint32_t offset, const uint8_t *data, uint32_t data_length)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size) || (NULL == data)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__write_firmware_update_request_t) + data_length;
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_WRITE_FIRMWARE_UPDATE, 2);

    /* offset */
    request->parameters.write_firmware_update_request.offset_length = BYTE_ORDER__htonl(sizeof(request->parameters.write_firmware_update_request.offset));
    request->parameters.write_firmware_update_request.offset = BYTE_ORDER__htonl(offset);

    /* data */
    request->parameters.write_firmware_update_request.data_length = BYTE_ORDER__htonl(data_length);
    memcpy(&(request->parameters.write_firmware_update_request.data), data, data_length);


    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__write_second_stage_to_internal_memory_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint32_t offset, uint8_t *data, uint32_t data_length)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size) || (NULL == data)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__write_second_stage_to_internal_memory_request_t) + data_length;
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_WRITE_SECOND_STAGE_TO_INTERNAL_MEMORY, 2);

    /* offset */
    request->parameters.write_second_stage_to_internal_memory_request.offset_length = BYTE_ORDER__htonl(sizeof(request->parameters.write_second_stage_to_internal_memory_request.offset));
    request->parameters.write_second_stage_to_internal_memory_request.offset = BYTE_ORDER__htonl(offset);

    /* data */
    request->parameters.write_second_stage_to_internal_memory_request.data_length = BYTE_ORDER__htonl(data_length);
    memcpy(&(request->parameters.write_second_stage_to_internal_memory_request.data), data, data_length);


    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__copy_second_stage_to_flash_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, MD5_SUM_t *expected_md5, uint32_t second_stage_size)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__copy_second_stage_to_flash_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_COPY_SECOND_STAGE_TO_FLASH, 2);

    /* expected md5 */
    request->parameters.copy_second_stage_to_flash_request.expected_md5_length = BYTE_ORDER__htonl(sizeof(request->parameters.copy_second_stage_to_flash_request.expected_md5));
    memcpy(&(request->parameters.copy_second_stage_to_flash_request.expected_md5),
            *expected_md5,
            sizeof(request->parameters.copy_second_stage_to_flash_request.expected_md5));

    /* second_stage_size */
    request->parameters.copy_second_stage_to_flash_request.second_stage_size_length = BYTE_ORDER__htonl(sizeof(request->parameters.copy_second_stage_to_flash_request.second_stage_size));
    request->parameters.copy_second_stage_to_flash_request.second_stage_size = BYTE_ORDER__htonl(second_stage_size);

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_validate_firmware_update_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, MD5_SUM_t *expected_md5, uint32_t firmware_size)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__validate_firmware_update_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_VALIDATE_FIRMWARE_UPDATE, 2);

    /* expected md5 */
    request->parameters.validate_firmware_update_request.expected_md5_length = BYTE_ORDER__htonl(sizeof(request->parameters.validate_firmware_update_request.expected_md5));
    memcpy(&(request->parameters.validate_firmware_update_request.expected_md5),
            *expected_md5,
            sizeof(request->parameters.validate_firmware_update_request.expected_md5));

    /* firmware_size */
    request->parameters.validate_firmware_update_request.firmware_size_length = BYTE_ORDER__htonl(sizeof(request->parameters.validate_firmware_update_request.firmware_size));
    request->parameters.validate_firmware_update_request.firmware_size = BYTE_ORDER__htonl(firmware_size);

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_latency_measurement_config_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint8_t latency_measurement_en, uint32_t inbound_start_buffer_number, uint32_t outbound_stop_buffer_number, uint32_t inbound_stream_index, uint32_t outbound_stream_index)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__latency_config_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_NN_CORE_LATENCY_MEASUREMENT_CONFIG, 5);

    /* latency_measurement_en */
    request->parameters.latency_config_request.latency_measurement_en_length = BYTE_ORDER__htonl(
            sizeof(request->parameters.latency_config_request.latency_measurement_en));
    request->parameters.latency_config_request.latency_measurement_en = latency_measurement_en;

    /* inbound_start_buffer_number */
    request->parameters.latency_config_request.inbound_start_buffer_number_length = BYTE_ORDER__htonl(
            sizeof(request->parameters.latency_config_request.inbound_start_buffer_number));
    request->parameters.latency_config_request.inbound_start_buffer_number = BYTE_ORDER__htonl(inbound_start_buffer_number);

    /* outbound_stop_buffer_number */
    request->parameters.latency_config_request.outbound_stop_buffer_number_length = BYTE_ORDER__htonl(
            sizeof(request->parameters.latency_config_request.outbound_stop_buffer_number));
    request->parameters.latency_config_request.outbound_stop_buffer_number = BYTE_ORDER__htonl(outbound_stop_buffer_number);

    /* inbound_stream_index */
    request->parameters.latency_config_request.inbound_stream_index_length = BYTE_ORDER__htonl(
            sizeof(request->parameters.latency_config_request.inbound_stream_index));
    request->parameters.latency_config_request.inbound_stream_index = BYTE_ORDER__htonl(inbound_stream_index);

    /* outbound_stream_index */
    request->parameters.latency_config_request.outbound_stream_index_length = BYTE_ORDER__htonl(
            sizeof(request->parameters.latency_config_request.outbound_stream_index));
    request->parameters.latency_config_request.outbound_stream_index = BYTE_ORDER__htonl(outbound_stream_index);


    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_latency_measurement_read_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE;
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_NN_CORE_LATENCY_MEASUREMENT_READ, 0);

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}


HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_sensor_store_config_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint32_t is_first, uint32_t section_index,
                                                                  uint32_t start_offset, uint32_t reset_data_size, uint32_t sensor_type, uint32_t total_data_size,
                                                                  uint8_t  *data, uint32_t data_length, uint16_t config_height, uint16_t config_width, uint16_t config_fps,
                                                                  uint32_t config_name_length, uint8_t *config_name)

{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size) || (NULL == data)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__sensor_store_config_request_t) + data_length;
    
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_SENSOR_STORE_CONFIG, 11);

    /* section index */
    request->parameters.sensor_store_config_request.section_index_length = BYTE_ORDER__htonl(sizeof(request->parameters.sensor_store_config_request.section_index));
    request->parameters.sensor_store_config_request.section_index = BYTE_ORDER__htonl(section_index);

    /* is_first */
    request->parameters.sensor_store_config_request.is_first_length = BYTE_ORDER__htonl(sizeof(request->parameters.sensor_store_config_request.is_first));
    request->parameters.sensor_store_config_request.is_first = BYTE_ORDER__htonl(is_first);

    /* start_offset */
    request->parameters.sensor_store_config_request.start_offset_length = BYTE_ORDER__htonl(sizeof(request->parameters.sensor_store_config_request.start_offset));
    request->parameters.sensor_store_config_request.start_offset = BYTE_ORDER__htonl(start_offset);

    /* reset_data_size */
    request->parameters.sensor_store_config_request.reset_data_size_length = BYTE_ORDER__htonl(sizeof(request->parameters.sensor_store_config_request.reset_data_size));
    request->parameters.sensor_store_config_request.reset_data_size = BYTE_ORDER__htonl(reset_data_size);
 
    /* sensor_type */
    request->parameters.sensor_store_config_request.sensor_type_length = BYTE_ORDER__htonl(sizeof(request->parameters.sensor_store_config_request.sensor_type));
    request->parameters.sensor_store_config_request.sensor_type = BYTE_ORDER__htonl(sensor_type);

    /* total_data_size */
    request->parameters.sensor_store_config_request.total_data_size_length = BYTE_ORDER__htonl(sizeof(request->parameters.sensor_store_config_request.total_data_size));
    request->parameters.sensor_store_config_request.total_data_size = BYTE_ORDER__htonl(total_data_size);

    /* config_width */
    request->parameters.sensor_store_config_request.config_width_length = BYTE_ORDER__htonl(sizeof(request->parameters.sensor_store_config_request.config_width));
    request->parameters.sensor_store_config_request.config_width = BYTE_ORDER__htons(config_width);
    
    /* config_height */
    request->parameters.sensor_store_config_request.config_height_length = BYTE_ORDER__htonl(sizeof(request->parameters.sensor_store_config_request.config_height));
    request->parameters.sensor_store_config_request.config_height = BYTE_ORDER__htons(config_height);
    
    /* config_fps */
    request->parameters.sensor_store_config_request.config_fps_length = BYTE_ORDER__htonl(sizeof(request->parameters.sensor_store_config_request.config_fps));
    request->parameters.sensor_store_config_request.config_fps = BYTE_ORDER__htons(config_fps);

    /* Config_name */
    if(config_name_length <= MAX_CONFIG_NAME_LEN){
        request->parameters.sensor_store_config_request.config_name_length = BYTE_ORDER__htonl(MAX_CONFIG_NAME_LEN);
        memcpy(&(request->parameters.sensor_store_config_request.config_name), config_name, config_name_length);
    }
    else{
        status = HAILO_STATUS__CONTROL_PROTOCOL__INVALID_ARGUMENT;
        goto exit;
    }
    
    /* Data */
    request->parameters.sensor_store_config_request.data_length = BYTE_ORDER__htonl(data_length);
    memcpy(&(request->parameters.sensor_store_config_request.data), data, data_length);

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_sensor_get_config_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, 
                                                                       uint32_t section_index, uint32_t offset, uint32_t data_length)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__sensor_get_config_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_SENSOR_GET_CONFIG, 3);

    /* section_index */
    request->parameters.sensor_get_config_request.section_index_length = BYTE_ORDER__htonl(sizeof(request->parameters.sensor_get_config_request.section_index));
    request->parameters.sensor_get_config_request.section_index = BYTE_ORDER__htonl(section_index);

     /* offset */
    request->parameters.sensor_get_config_request.offset_length = BYTE_ORDER__htonl(sizeof(request->parameters.sensor_get_config_request.offset));
    request->parameters.sensor_get_config_request.offset = BYTE_ORDER__htonl(offset);

    /* Data count */
    request->parameters.sensor_get_config_request.data_size_length = BYTE_ORDER__htonl(sizeof(request->parameters.sensor_get_config_request.data_size));
    request->parameters.sensor_get_config_request.data_size = BYTE_ORDER__htonl(data_length);

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

hailo_status CONTROL_PROTOCOL__pack_sensor_set_i2c_bus_index_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint32_t sensor_type, uint32_t bus_index)
{
    size_t local_request_size = 0;

    CHECK_ARG_NOT_NULL(request);
    CHECK_ARG_NOT_NULL(request_size);

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__sensor_set_i2c_bus_index_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_SENSOR_SET_I2C_BUS_INDEX, 2);

    /* section index */
    request->parameters.sensor_set_i2c_bus_index.sensor_type_length = BYTE_ORDER__htonl(sizeof(request->parameters.sensor_set_i2c_bus_index.sensor_type));
    request->parameters.sensor_set_i2c_bus_index.sensor_type = BYTE_ORDER__htonl(sensor_type);

    /* bus_index */
    request->parameters.sensor_set_i2c_bus_index.i2c_bus_index_length = BYTE_ORDER__htonl(sizeof(request->parameters.sensor_set_i2c_bus_index.i2c_bus_index));
    request->parameters.sensor_set_i2c_bus_index.i2c_bus_index = BYTE_ORDER__htonl(bus_index);

    *request_size = local_request_size;

    return HAILO_SUCCESS;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_sensor_load_and_start_config_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint32_t section_index)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size) ) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__sensor_load_config_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_SENSOR_LOAD_AND_START, 1);

    /* section index */
    request->parameters.sensor_load_config_request.section_index_length = BYTE_ORDER__htonl(sizeof(request->parameters.sensor_load_config_request.section_index));
    request->parameters.sensor_load_config_request.section_index = BYTE_ORDER__htonl(section_index);

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_sensor_reset_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint32_t section_index)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size) ) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__sensor_reset_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_SENSOR_RESET, 1);

    /* section index */
    request->parameters.sensor_reset_request.section_index_length = BYTE_ORDER__htonl(sizeof(request->parameters.sensor_reset_request.section_index));
    request->parameters.sensor_reset_request.section_index = BYTE_ORDER__htonl(section_index);

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_sensor_set_generic_i2c_slave_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint16_t slave_address,
                                                                                  uint8_t register_address_size, uint8_t bus_index, uint8_t should_hold_bus, uint8_t endianness)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size) ) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__sensor_set_generic_i2c_slave_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_SENSOR_SET_GENERIC_I2C_SLAVE, 5);

    /* slave_address */
    request->parameters.sensor_set_generic_i2c_slave_request.slave_address_length = BYTE_ORDER__htonl(sizeof(request->parameters.sensor_set_generic_i2c_slave_request.slave_address));
    request->parameters.sensor_set_generic_i2c_slave_request.slave_address = BYTE_ORDER__htons(slave_address);
    
    /* register_address_size */
    request->parameters.sensor_set_generic_i2c_slave_request.register_address_size_length = BYTE_ORDER__htonl(sizeof(request->parameters.sensor_set_generic_i2c_slave_request.register_address_size));
    request->parameters.sensor_set_generic_i2c_slave_request.register_address_size = register_address_size;

    /* bus index */
    request->parameters.sensor_set_generic_i2c_slave_request.bus_index_length = BYTE_ORDER__htonl(sizeof(request->parameters.sensor_set_generic_i2c_slave_request.bus_index));
    request->parameters.sensor_set_generic_i2c_slave_request.bus_index = bus_index;

    /* should_hold_bus */
    request->parameters.sensor_set_generic_i2c_slave_request.should_hold_bus_length = BYTE_ORDER__htonl(sizeof(request->parameters.sensor_set_generic_i2c_slave_request.should_hold_bus));
    request->parameters.sensor_set_generic_i2c_slave_request.should_hold_bus = should_hold_bus;

    /* endianness */
    request->parameters.sensor_set_generic_i2c_slave_request.endianness_length = BYTE_ORDER__htonl(sizeof(request->parameters.sensor_set_generic_i2c_slave_request.endianness));
    request->parameters.sensor_set_generic_i2c_slave_request.endianness = endianness;

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_sensor_get_sections_info_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence)
{
    return control_protocol__pack_empty_request(request, request_size, sequence, HAILO_CONTROL_OPCODE_SENSOR_GET_SECTIONS_INFO);
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_context_switch_set_network_group_header_request(
    CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence,
    const CONTROL_PROTOCOL__application_header_t *network_group_header)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size) || (NULL == network_group_header)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__context_switch_set_network_group_header_request_t);
    control_protocol__pack_request_header(request, sequence,
        HAILO_CONTROL_OPCODE_CONTEXT_SWITCH_SET_NETWORK_GROUP_HEADER, 1);

    /* application_header */
    request->parameters.context_switch_set_network_group_header_request.application_header_length = 
        BYTE_ORDER__htonl(sizeof(request->parameters.context_switch_set_network_group_header_request.application_header));
    memcpy(&(request->parameters.context_switch_set_network_group_header_request.application_header), 
            network_group_header, 
            sizeof(request->parameters.context_switch_set_network_group_header_request.application_header));

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_context_switch_set_context_info_request(
    CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, 
    const CONTROL_PROTOCOL__context_switch_context_info_chunk_t *context_info)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size) || (NULL == context_info)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + 
        sizeof(CONTROL_PROTOCOL__context_switch_set_context_info_request_t) + context_info->context_network_data_length;
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_CONTEXT_SWITCH_SET_CONTEXT_INFO, 4);

    /* is_first_chunk_per_context */
    request->parameters.context_switch_set_context_info_request.is_first_chunk_per_context_length = 
        BYTE_ORDER__htonl(sizeof(request->parameters.context_switch_set_context_info_request.is_first_chunk_per_context));
    request->parameters.context_switch_set_context_info_request.is_first_chunk_per_context = 
        context_info->is_first_chunk_per_context;

    /* is_last_chunk_per_context */
    request->parameters.context_switch_set_context_info_request.is_last_chunk_per_context_length = 
        BYTE_ORDER__htonl(sizeof(request->parameters.context_switch_set_context_info_request.is_last_chunk_per_context));
    request->parameters.context_switch_set_context_info_request.is_last_chunk_per_context = 
        context_info->is_last_chunk_per_context;

    /* context_type */
    request->parameters.context_switch_set_context_info_request.context_type_length = 
        BYTE_ORDER__htonl(sizeof(request->parameters.context_switch_set_context_info_request.context_type));
    request->parameters.context_switch_set_context_info_request.context_type = 
        context_info->context_type;

    /* Network data (edge layers + Trigger groups) */
    if (CONTROL_PROTOCOL__CONTEXT_NETWORK_DATA_SINGLE_CONTROL_MAX_SIZE < context_info->context_network_data_length) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__INVALID_BUFFER_SIZE;
        goto exit;
    }
    request->parameters.context_switch_set_context_info_request.context_network_data_length = 
        BYTE_ORDER__htonl(context_info->context_network_data_length);
    memcpy(&(request->parameters.context_switch_set_context_info_request.context_network_data), 
            &(context_info->context_network_data), context_info->context_network_data_length);

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_context_switch_signal_cache_updated_request(
    CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence)
{
    return control_protocol__pack_empty_request(request, request_size, sequence, HAILO_CONTROL_OPCODE_CONTEXT_SWITCH_SIGNAL_CACHE_UPDATED);
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_idle_time_get_measuremment_request(CONTROL_PROTOCOL__request_t *request, 
            size_t *request_size, 
            uint32_t sequence)
{    
    return control_protocol__pack_empty_request(request, request_size, sequence, HAILO_CONTROL_OPCODE_IDLE_TIME_GET_MEASUREMENT);
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_idle_time_set_measuremment_request(CONTROL_PROTOCOL__request_t *request, 
            size_t *request_size, 
            uint32_t sequence, 
            uint8_t measurement_enable)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__idle_time_set_measurement_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_IDLE_TIME_SET_MEASUREMENT, 1);

    /*measurement duration*/
    request->parameters.idle_time_set_measurement_request.measurement_enable_length = 
        BYTE_ORDER__htonl(sizeof(request->parameters.idle_time_set_measurement_request.measurement_enable));
    request->parameters.idle_time_set_measurement_request.measurement_enable = measurement_enable;
    
    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_set_pause_frames_request(CONTROL_PROTOCOL__request_t *request, 
            size_t *request_size, uint32_t sequence, uint8_t rx_pause_frames_enable)
{

    CHECK_NOT_NULL_COMMON_STATUS(request, HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED);
    CHECK_NOT_NULL_COMMON_STATUS(request_size, HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED);

    /* Header */
    size_t local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__set_pause_frames_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_SET_PAUSE_FRAMES, 1);

    /*measurement duration*/
    request->parameters.set_pause_frames_request.rx_pause_frames_enable_length = 
        BYTE_ORDER__htonl(sizeof(request->parameters.set_pause_frames_request.rx_pause_frames_enable));
    request->parameters.set_pause_frames_request.rx_pause_frames_enable = rx_pause_frames_enable;
    
    *request_size = local_request_size;

    return HAILO_COMMON_STATUS__SUCCESS;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_download_context_action_list_request(CONTROL_PROTOCOL__request_t *request, 
    size_t *request_size, uint32_t sequence, uint32_t network_group_id,
    CONTROL_PROTOCOL__context_switch_context_type_t context_type, uint16_t context_index, uint16_t action_list_offset)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__download_context_action_list_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_DOWNLOAD_CONTEXT_ACTION_LIST, 4);

    /* network_group_id */
    request->parameters.download_context_action_list_request.network_group_id_length = 
        BYTE_ORDER__htonl(sizeof(request->parameters.download_context_action_list_request.network_group_id));
    request->parameters.download_context_action_list_request.network_group_id = BYTE_ORDER__htonl(network_group_id);

    /* context_type */
    request->parameters.download_context_action_list_request.context_type_length = 
        BYTE_ORDER__htonl(sizeof(request->parameters.download_context_action_list_request.context_type));
    request->parameters.download_context_action_list_request.context_type =  static_cast<uint8_t>(context_type);

    /* context_index */
    request->parameters.download_context_action_list_request.context_index_length = 
        BYTE_ORDER__htonl(sizeof(request->parameters.download_context_action_list_request.context_index));
    request->parameters.download_context_action_list_request.context_index = context_index;

    /* action_list_offset */
    request->parameters.download_context_action_list_request.action_list_offset_length = 
        BYTE_ORDER__htonl(sizeof(request->parameters.download_context_action_list_request.action_list_offset));
    request->parameters.download_context_action_list_request.action_list_offset = BYTE_ORDER__htons(action_list_offset);

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

#define CONTEXT_SWITCH_SWITCH_STATUS_REQUEST_PARAMS (4)
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_change_context_switch_status_request(
        CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, 
        CONTROL_PROTOCOL__CONTEXT_SWITCH_STATUS_t state_machine_status, uint8_t application_index,
        uint16_t dynamic_batch_size, uint16_t batch_count)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + 
        sizeof(CONTROL_PROTOCOL__change_context_switch_status_request_t);
    control_protocol__pack_request_header(request, sequence, 
        HAILO_CONTROL_OPCODE_CHANGE_CONTEXT_SWITCH_STATUS, CONTEXT_SWITCH_SWITCH_STATUS_REQUEST_PARAMS);

    /* state_machine_status */
    request->parameters.change_context_switch_status_request.state_machine_status_length = 
        BYTE_ORDER__htonl(sizeof(request->parameters.change_context_switch_status_request.state_machine_status));
    memcpy(&(request->parameters.change_context_switch_status_request.state_machine_status), 
            &(state_machine_status), 
            sizeof(request->parameters.change_context_switch_status_request.state_machine_status));

    /* application_index */
    request->parameters.change_context_switch_status_request.application_index_length = 
        BYTE_ORDER__htonl(sizeof(request->parameters.change_context_switch_status_request.application_index));
    request->parameters.change_context_switch_status_request.application_index = application_index;

    /* dynamic_batch_size */
    request->parameters.change_context_switch_status_request.dynamic_batch_size_length = 
        BYTE_ORDER__htonl(sizeof(request->parameters.change_context_switch_status_request.dynamic_batch_size));
    request->parameters.change_context_switch_status_request.dynamic_batch_size = dynamic_batch_size;

    /* batch_count */
    request->parameters.change_context_switch_status_request.batch_count_length =
        BYTE_ORDER__htonl(sizeof(request->parameters.change_context_switch_status_request.batch_count));
    request->parameters.change_context_switch_status_request.batch_count = batch_count;

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_wd_enable(
    CONTROL_PROTOCOL__request_t *request,
    size_t *request_size,
    uint32_t sequence,
    uint8_t cpu_id,
    bool should_enable)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;
    CONTROL_PROTOCOL__OPCODE_t opcode = HAILO_CONTROL_OPCODE_COUNT;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    if (CPU_ID_CORE_CPU < cpu_id){
        status = HAILO_STATUS__CONTROL_PROTOCOL__INVALID_ARGUMENT;
        goto exit;
    }
    
    opcode = (CPU_ID_CORE_CPU == cpu_id) ? HAILO_CONTROL_OPCODE_CORE_WD_ENABLE : HAILO_CONTROL_OPCODE_APP_WD_ENABLE;

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__wd_enable_request_t);
    control_protocol__pack_request_header(request, sequence, opcode, 1);

    request->parameters.wd_enable_request.should_enable_length = 
        BYTE_ORDER__htonl(sizeof(request->parameters.wd_enable_request.should_enable));
    request->parameters.wd_enable_request.should_enable = should_enable;
    
    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;

exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_wd_config(
    CONTROL_PROTOCOL__request_t *request,
    size_t *request_size,
    uint32_t sequence,
    uint8_t cpu_id,
    uint32_t wd_cycles,
    CONTROL_PROTOCOL__WATCHDOG_MODE_t wd_mode)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;
    CONTROL_PROTOCOL__OPCODE_t opcode = HAILO_CONTROL_OPCODE_COUNT; 

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }
    if (CPU_ID_CORE_CPU < cpu_id){
        status = HAILO_STATUS__CONTROL_PROTOCOL__INVALID_ARGUMENT;
        goto exit;
    }
   
    opcode = (CPU_ID_CORE_CPU == cpu_id) ? HAILO_CONTROL_OPCODE_CORE_WD_CONFIG : HAILO_CONTROL_OPCODE_APP_WD_CONFIG;
    
    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__wd_config_request_t);
    control_protocol__pack_request_header(request, sequence, opcode, 2);

    request->parameters.wd_config_request.wd_cycles_length = 
        BYTE_ORDER__htonl(sizeof(request->parameters.wd_config_request.wd_cycles));
    request->parameters.wd_config_request.wd_cycles = BYTE_ORDER__htonl(wd_cycles);
    request->parameters.wd_config_request.wd_mode_length = 
        BYTE_ORDER__htonl(sizeof(request->parameters.wd_config_request.wd_mode));
    request->parameters.wd_config_request.wd_mode = static_cast<uint8_t>(wd_mode);
    
    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;

exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_context_switch_clear_configured_apps_request(
    CONTROL_PROTOCOL__request_t *request,
    size_t *request_size,
    uint32_t sequence)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    *request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE;
    control_protocol__pack_empty_request(request, request_size, sequence,
        HAILO_CONTROL_OPCODE_CONTEXT_SWITCH_CLEAR_CONFIGURED_APPS);
    
    status = HAILO_COMMON_STATUS__SUCCESS;

exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_previous_system_state(
    CONTROL_PROTOCOL__request_t *request,
    size_t *request_size,
    uint32_t sequence,
    uint8_t cpu_id)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;
    CONTROL_PROTOCOL__OPCODE_t opcode = HAILO_CONTROL_OPCODE_COUNT; 

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }
    if (CPU_ID_CORE_CPU < cpu_id){
        status = HAILO_STATUS__CONTROL_PROTOCOL__INVALID_ARGUMENT;
        goto exit;
    }
  
    opcode = (CPU_ID_CORE_CPU == cpu_id) ? HAILO_CONTROL_OPCODE_CORE_PREVIOUS_SYSTEM_STATE : HAILO_CONTROL_OPCODE_APP_PREVIOUS_SYSTEM_STATE;
    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE;
    *request_size = local_request_size;
    control_protocol__pack_empty_request(request, request_size, sequence, opcode);
    
    status = HAILO_COMMON_STATUS__SUCCESS;

exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_set_dataflow_interrupt_request(
        CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, 
        uint8_t interrupt_type, uint8_t interrupt_index, uint8_t interrupt_sub_index)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + 
        sizeof(CONTROL_PROTOCOL__set_dataflow_interrupt_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_SET_DATAFLOW_INTERRUPT, 3);

    /* Interrupt_type */
    request->parameters.set_dataflow_interrupt_request.interrupt_type_length = 
        BYTE_ORDER__htonl(sizeof(request->parameters.set_dataflow_interrupt_request.interrupt_type));
    memcpy(&(request->parameters.set_dataflow_interrupt_request.interrupt_type), 
            &(interrupt_type), 
            sizeof(request->parameters.set_dataflow_interrupt_request.interrupt_type));

    /* Interrupt_index */
    request->parameters.set_dataflow_interrupt_request.interrupt_index_length =
        BYTE_ORDER__htonl(sizeof(request->parameters.set_dataflow_interrupt_request.interrupt_index));
    memcpy(&(request->parameters.set_dataflow_interrupt_request.interrupt_index), 
            &(interrupt_index), 
            sizeof(request->parameters.set_dataflow_interrupt_request.interrupt_index));

    /* Interrupt_sub_index */
    request->parameters.set_dataflow_interrupt_request.interrupt_sub_index_length =
        BYTE_ORDER__htonl(sizeof(request->parameters.set_dataflow_interrupt_request.interrupt_sub_index));
    memcpy(&(request->parameters.set_dataflow_interrupt_request.interrupt_sub_index), 
            &(interrupt_sub_index), 
            sizeof(request->parameters.set_dataflow_interrupt_request.interrupt_sub_index));

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_d2h_event_manager_set_host_info_request(
        CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, 
        uint8_t connection_type, uint16_t host_port, uint32_t host_ip_address)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + 
        sizeof(CONTROL_PROTOCOL__d2h_event_manager_set_new_host_info_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_D2H_EVENT_MANAGER_SET_HOST_INFO, 3);

    /* connection_type */
    request->parameters.d2h_event_manager_set_new_host_info_request.connection_type_length = 
        BYTE_ORDER__htonl(sizeof(request->parameters.d2h_event_manager_set_new_host_info_request.connection_type));
    request->parameters.d2h_event_manager_set_new_host_info_request.connection_type = connection_type;
    

    /* remote_port */
    request->parameters.d2h_event_manager_set_new_host_info_request.host_port_length = 
        BYTE_ORDER__htonl(sizeof(request->parameters.d2h_event_manager_set_new_host_info_request.host_port));
    request->parameters.d2h_event_manager_set_new_host_info_request.host_port = BYTE_ORDER__htons(host_port);
    

    /* remote_ip_address */
    request->parameters.d2h_event_manager_set_new_host_info_request.host_ip_address_length = 
        BYTE_ORDER__htonl(sizeof(request->parameters.d2h_event_manager_set_new_host_info_request.host_ip_address));
    request->parameters.d2h_event_manager_set_new_host_info_request.host_ip_address = BYTE_ORDER__htonl(host_ip_address);
    

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_d2h_event_manager_send_host_info_event_request(
        CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, 
        uint8_t event_priority)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + 
        sizeof(CONTROL_PROTOCOL__d2h_event_manager_send_host_info_event_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_D2H_EVENT_MANAGER_SEND_EVENT_HOST_INFO, 1);

    /* event_priority */
    request->parameters.d2h_event_manager_send_host_info_event_request.priority_length =
        BYTE_ORDER__htonl(sizeof(request->parameters.d2h_event_manager_send_host_info_event_request.priority));
    request->parameters.d2h_event_manager_send_host_info_event_request.priority = event_priority;
    

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_get_chip_temperature_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence)
{
    return control_protocol__pack_empty_request(request, request_size, sequence, HAILO_CONTROL_OPCODE_GET_CHIP_TEMPERATURE);
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_read_board_config(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint32_t address, uint32_t data_length)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__read_board_config_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_READ_BOARD_CONFIG, 2);

    /* Address */
    request->parameters.read_board_config_request.address_length = BYTE_ORDER__htonl(sizeof(request->parameters.read_board_config_request.address));
    request->parameters.read_board_config_request.address = BYTE_ORDER__htonl(address);

    /* Data count */
    request->parameters.read_board_config_request.data_count_length = BYTE_ORDER__htonl(sizeof(request->parameters.read_board_config_request.data_count));
    request->parameters.read_board_config_request.data_count = BYTE_ORDER__htonl(data_length);


    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_write_board_config_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size,
                                                                         uint32_t sequence, uint32_t address, const uint8_t *data, uint32_t data_length)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size) || (NULL == data)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__write_board_config_request_t) + data_length;
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_WRITE_BOARD_CONFIG, 2);

    /* Address */
    request->parameters.write_board_config_request.address_length = BYTE_ORDER__htonl(sizeof(request->parameters.write_board_config_request.address));
    request->parameters.write_board_config_request.address = BYTE_ORDER__htonl(address);

    /* Data */
    request->parameters.write_board_config_request.data_length = BYTE_ORDER__htonl(data_length);

    memcpy(&(request->parameters.write_board_config_request.data), data, data_length);


    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_enable_debugging_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, bool is_rma)
{
    /* Header */
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_ENABLE_DEBUGGING, 1);

    /* is_rma */
    request->parameters.enable_debugging_request.is_rma_length =
        BYTE_ORDER__htonl(sizeof(request->parameters.enable_debugging_request.is_rma));
    request->parameters.enable_debugging_request.is_rma = is_rma;

    *request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__enable_debugging_request_t);

    return HAILO_COMMON_STATUS__SUCCESS;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_get_extended_device_information_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence)
{
    return control_protocol__pack_empty_request(request, request_size, sequence, HAILO_CONTROL_OPCODE_GET_DEVICE_INFORMATION);
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_get_health_information_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence)
{
    return control_protocol__pack_empty_request(request, request_size, sequence, HAILO_CONTROL_OPCODE_GET_HEALTH_INFORMATION);
}


HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_config_context_switch_breakpoint_request(
        CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence,
        uint8_t breakpoint_id,
        CONTROL_PROTOCOL__context_switch_breakpoint_control_t breakpoint_control, 
        CONTROL_PROTOCOL__context_switch_breakpoint_data_t *breakpoint_data)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size) || (NULL == breakpoint_data)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + 
        sizeof(CONTROL_PROTOCOL__config_context_switch_breakpoint_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_CONFIG_CONTEXT_SWITCH_BREAKPOINT, 3);

    /* breakpoint id */
    request->parameters.config_context_switch_breakpoint_request.breakpoint_id_length = 
        BYTE_ORDER__htonl(sizeof(request->parameters.config_context_switch_breakpoint_request.breakpoint_id));
    request->parameters.config_context_switch_breakpoint_request.breakpoint_id = breakpoint_id;

    /* breakpoint status */
    request->parameters.config_context_switch_breakpoint_request.breakpoint_control_length = 
        BYTE_ORDER__htonl(sizeof(request->parameters.config_context_switch_breakpoint_request.breakpoint_control));
    request->parameters.config_context_switch_breakpoint_request.breakpoint_control = (uint8_t)breakpoint_control;

    /* breakpoint data */
    request->parameters.config_context_switch_breakpoint_request.breakpoint_data_length = 
        BYTE_ORDER__htonl(sizeof(request->parameters.config_context_switch_breakpoint_request.breakpoint_data));
    memcpy(&(request->parameters.config_context_switch_breakpoint_request.breakpoint_data), 
            breakpoint_data, 
            sizeof(request->parameters.config_context_switch_breakpoint_request.breakpoint_data));

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_get_context_switch_breakpoint_status_request(
        CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence,
        uint8_t breakpoint_id) 
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + 
        sizeof(CONTROL_PROTOCOL__get_context_switch_breakpoint_status_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_GET_CONTEXT_SWITCH_BREAKPOINT_STATUS, 1);

    /* breakpoint id */
    request->parameters.config_context_switch_breakpoint_request.breakpoint_id_length = 
        BYTE_ORDER__htonl(sizeof(request->parameters.config_context_switch_breakpoint_request.breakpoint_id));
    request->parameters.config_context_switch_breakpoint_request.breakpoint_id = breakpoint_id;

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_get_context_switch_main_header_request(
        CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence) 
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE;
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_GET_CONTEXT_SWITCH_MAIN_HEADER, 0);

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_config_context_switch_timestamp_request(
        CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence,
        uint16_t batch_index, bool enable_user_configuration)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + 
        sizeof(CONTROL_PROTOCOL__config_context_switch_timestamp_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_CONFIG_CONTEXT_SWITCH_TIMESTAMP, 2);

    /* batch index */
    request->parameters.config_context_switch_timestamp_request.batch_index_length = 
        BYTE_ORDER__htonl(sizeof(request->parameters.config_context_switch_timestamp_request.batch_index));
    request->parameters.config_context_switch_timestamp_request.batch_index = BYTE_ORDER__htons(batch_index);

    /* enable_user_configuration */
    request->parameters.config_context_switch_timestamp_request.enable_user_configuration_length = 
       BYTE_ORDER__htonl(sizeof(request->parameters.config_context_switch_timestamp_request.enable_user_configuration));
    request->parameters.config_context_switch_timestamp_request.enable_user_configuration = enable_user_configuration;

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_run_bist_test_request(
        CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, bool is_top_test, 
        uint32_t top_bypass_bitmap, uint8_t cluster_index, uint32_t cluster_bypass_bitmap_0, uint32_t cluster_bypass_bitmap_1)
{
    size_t local_request_size = 0;

    CHECK_NOT_NULL_COMMON_STATUS(request, HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED);
    CHECK_NOT_NULL_COMMON_STATUS(request_size, HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED);

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + 
        sizeof(CONTROL_PROTOCOL__run_bist_test_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_RUN_BIST_TEST, 5);

    /* running on top */
    request->parameters.run_bist_test_request.is_top_test_length = 
        BYTE_ORDER__htonl(sizeof(request->parameters.run_bist_test_request.is_top_test));
    request->parameters.run_bist_test_request.is_top_test = is_top_test;

    /* top bypass */
    request->parameters.run_bist_test_request.top_bypass_bitmap_length = 
        BYTE_ORDER__htonl(sizeof(request->parameters.run_bist_test_request.top_bypass_bitmap));
    request->parameters.run_bist_test_request.top_bypass_bitmap = BYTE_ORDER__htonl(top_bypass_bitmap);

    /* cluster index */
    request->parameters.run_bist_test_request.cluster_index_length = 
        BYTE_ORDER__htonl(sizeof(request->parameters.run_bist_test_request.cluster_index));
    request->parameters.run_bist_test_request.cluster_index = cluster_index;

    /* cluster bypass 0 */
    request->parameters.run_bist_test_request.cluster_bypass_bitmap_0_length = 
        BYTE_ORDER__htonl(sizeof(request->parameters.run_bist_test_request.cluster_bypass_bitmap_0));
    request->parameters.run_bist_test_request.cluster_bypass_bitmap_0 = BYTE_ORDER__htonl(cluster_bypass_bitmap_0);

    /* cluster bypass 1 */
    request->parameters.run_bist_test_request.cluster_bypass_bitmap_1_length = 
        BYTE_ORDER__htonl(sizeof(request->parameters.run_bist_test_request.cluster_bypass_bitmap_1));
    request->parameters.run_bist_test_request.cluster_bypass_bitmap_1 = BYTE_ORDER__htonl(cluster_bypass_bitmap_1);

    *request_size = local_request_size;

    return HAILO_COMMON_STATUS__SUCCESS;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_set_sleep_state_request(
        CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence,
        uint8_t sleep_state)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    size_t local_request_size = 0;

    if ((NULL == request) || (NULL == request_size)) {
        status = HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE + 
        sizeof(CONTROL_PROTOCOL__set_sleep_state_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_SET_SLEEP_STATE, 1);

    /* sleep_state */
    request->parameters.set_sleep_state_request.sleep_state_length = 
        BYTE_ORDER__htonl(sizeof(request->parameters.set_sleep_state_request.sleep_state));
    request->parameters.set_sleep_state_request.sleep_state = sleep_state;

    *request_size = local_request_size;
    status = HAILO_COMMON_STATUS__SUCCESS;
exit:
    return status;
}

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_change_hw_infer_status_request(
    CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence,
    uint8_t hw_infer_state, uint8_t network_group_index, uint16_t dynamic_batch_size,
    uint16_t batch_count, CONTROL_PROTOCOL__hw_infer_channels_info_t *channels_info,
    CONTROL_PROTOCOL__boundary_channel_mode_t boundary_channel_mode)
{
    size_t local_request_size = 0;

    CHECK_COMMON_STATUS((NULL != request) && (NULL != request_size), HAILO_STATUS__CONTROL_PROTOCOL__NULL_ARGUMENT_PASSED);

    /* Header */
    local_request_size = CONTROL_PROTOCOL__REQUEST_BASE_SIZE +
        sizeof(CONTROL_PROTOCOL__change_hw_infer_status_request_t);
    control_protocol__pack_request_header(request, sequence, HAILO_CONTROL_OPCODE_CHANGE_HW_INFER_STATUS,
        CHANGE_HW_INFER_REQUEST_PARAMETER_COUNT);

    /* hw_infer_state */
    request->parameters.change_hw_infer_status_request.hw_infer_state_length =
        BYTE_ORDER__htonl(sizeof(request->parameters.change_hw_infer_status_request.hw_infer_state));
    request->parameters.change_hw_infer_status_request.hw_infer_state = hw_infer_state;

    /* network_group_index */
    request->parameters.change_hw_infer_status_request.application_index_length =
        BYTE_ORDER__htonl(sizeof(request->parameters.change_hw_infer_status_request.application_index));
    request->parameters.change_hw_infer_status_request.application_index = network_group_index;

    /* dynamic_batch_size */
    request->parameters.change_hw_infer_status_request.dynamic_batch_size_length =
        BYTE_ORDER__htonl(sizeof(request->parameters.change_hw_infer_status_request.dynamic_batch_size));
    request->parameters.change_hw_infer_status_request.dynamic_batch_size = dynamic_batch_size;

    /* batch_count */
    request->parameters.change_hw_infer_status_request.batch_count_length =
        BYTE_ORDER__htonl(sizeof(request->parameters.change_hw_infer_status_request.batch_count));
    request->parameters.change_hw_infer_status_request.batch_count = batch_count;

    /* channels_info */
    request->parameters.change_hw_infer_status_request.channels_info_length =
        BYTE_ORDER__htonl(sizeof(request->parameters.change_hw_infer_status_request.channels_info));
    memcpy(&(request->parameters.change_hw_infer_status_request.channels_info),
        channels_info, sizeof(request->parameters.change_hw_infer_status_request.channels_info));

    /* boundary channels mode */
    request->parameters.change_hw_infer_status_request.boundary_channel_mode_length =
        BYTE_ORDER__htonl(sizeof(request->parameters.change_hw_infer_status_request.boundary_channel_mode));
    request->parameters.change_hw_infer_status_request.boundary_channel_mode =
        static_cast<uint8_t>(boundary_channel_mode);

    *request_size = local_request_size;
    return HAILO_COMMON_STATUS__SUCCESS;
}

#endif /* FIRMWARE_ARCH */
