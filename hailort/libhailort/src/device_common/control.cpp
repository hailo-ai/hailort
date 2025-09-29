/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file control.cpp
 * @brief Implements module which allows controling Hailo chip.
 **/

#include "common/utils.hpp"
#include "common/logger_macros.hpp"
#include "common/internal_env_vars.hpp"
#include "common/process.hpp"

#include "hailo/hailort_common.hpp"
#include "hef/core_op_metadata.hpp"
#include "device_common/control.hpp"
#include "utils/soc_utils/partial_cluster_reader.hpp"

#include "control_protocol.h"
#include "byte_order.h"
#include "firmware_status.h"
#include "firmware_header_utils.h"
#include "d2h_events.h"
#include <array>


namespace hailort
{

#ifndef MIN
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#endif

#define POWER_MEASUREMENT_DELAY_MS(__sample_period, __average_factor) \
    (static_cast<uint32_t>((__sample_period) / 1000.0 * (__average_factor) * 2 * 1.2))

#define OVERCURRENT_PROTECTION_WARNING ( \
        "Using the overcurrent protection dvm for power measurement will disable the overcurrent protection.\n" \
        "If only taking one measurement, the protection will resume automatically.\n" \
        "If doing continuous measurement, to enable overcurrent protection again you have to stop the power measurement on this dvm." \
    )

typedef std::array<std::array<float64_t, CONTROL_PROTOCOL__POWER_MEASUREMENT_TYPES__COUNT>, CONTROL_PROTOCOL__DVM_OPTIONS_COUNT> power_conversion_multiplier_t;


Expected<hailo_device_identity_t> control__parse_identify_results(CONTROL_PROTOCOL_identify_response_t *identify_response)
{
    hailo_device_identity_t board_info;

    CHECK_AS_EXPECTED(nullptr != identify_response, HAILO_INVALID_ARGUMENT);

    // Store identify response inside control
    board_info.protocol_version = BYTE_ORDER__ntohl(identify_response->protocol_version);
    board_info.logger_version = BYTE_ORDER__ntohl(identify_response->logger_version);
    (void)memcpy(&(board_info.fw_version),
            &(identify_response->fw_version),
            sizeof(board_info.fw_version));
    board_info.board_name_length = (uint8_t)BYTE_ORDER__ntohl(identify_response->board_name_length);
    (void)memcpy(&(board_info.board_name),
            &(identify_response->board_name),
            BYTE_ORDER__ntohl(identify_response->board_name_length));
    board_info.serial_number_length = (uint8_t)BYTE_ORDER__ntohl(identify_response->serial_number_length);
    (void)memcpy(&(board_info.serial_number),
            &(identify_response->serial_number),
            BYTE_ORDER__ntohl(identify_response->serial_number_length));
    board_info.part_number_length = (uint8_t)BYTE_ORDER__ntohl(identify_response->part_number_length);
    (void)memcpy(&(board_info.part_number),
        &(identify_response->part_number),
        BYTE_ORDER__ntohl(identify_response->part_number_length));
    board_info.product_name_length = (uint8_t)BYTE_ORDER__ntohl(identify_response->product_name_length);
    (void)memcpy(&(board_info.product_name),
        &(identify_response->product_name),
        BYTE_ORDER__ntohl(identify_response->product_name_length));

    // Check if the firmware is debug or release
    board_info.is_release = (!IS_REVISION_DEV(board_info.fw_version.revision));

    // Check if the firmware was compiled with EXTENDED_CONTEXT_SWITCH_BUFFER
    board_info.extended_context_switch_buffer = IS_REVISION_EXTENDED_CONTEXT_SWITCH_BUFFER(board_info.fw_version.revision);

    // Check if the firmware was compiled with EXTENDED_FW_CHECKS
    board_info.extended_fw_check = IS_REVISION_EXTENDED_FW_CHECK(board_info.fw_version.revision);

    // Make sure response was from app CPU
    CHECK_AS_EXPECTED((0 == (board_info.fw_version.revision & REVISION_APP_CORE_FLAG_BIT_MASK)), HAILO_INVALID_FIRMWARE,
     "Got invalid app FW type, which means the FW was not marked correctly. unmaked FW revision {}", board_info.fw_version.revision);

    // Keep the revision number only
    board_info.fw_version.revision = GET_REVISION_NUMBER_VALUE(board_info.fw_version.revision);

    board_info.device_architecture = static_cast<hailo_device_architecture_t>(BYTE_ORDER__ntohl(identify_response->device_architecture));

    // Device architecture can be HAILO_ARCH_HAILO15H or HAILO_ARCH_HAILO15M - but the FW will always return HAILO_ARCH_HAILO15H
    // Based on a file the SCU gives us we can deduce the actual type
    if (HAILO_ARCH_HAILO15H == board_info.device_architecture) {
        TRY(const auto dev_arch, PartialClusterReader::get_actual_dev_arch_from_fuse(board_info.device_architecture));
        board_info.device_architecture = dev_arch;
    }

    /* Write identify results to log */
    LOGGER__INFO("firmware_version is: {}.{}.{}",
            board_info.fw_version.major,
            board_info.fw_version.minor,
            board_info.fw_version.revision
            );
    LOGGER__DEBUG("Protocol version: {}", board_info.protocol_version);
    LOGGER__DEBUG("Logger version: {}", board_info.logger_version);
    LOGGER__DEBUG("Device architecture code: {}", static_cast<int>(board_info.device_architecture));

    return board_info;
}

Expected<hailo_extended_device_information_t> control__parse_get_extended_device_information_results(
    const CONTROL_PROTOCOL__get_extended_device_information_response_t &get_extended_device_information_response)
{
    uint8_t local_supported_features;
    hailo_extended_device_information_t device_info;

    local_supported_features = (uint8_t)BYTE_ORDER__ntohl(get_extended_device_information_response.supported_features);

    device_info.supported_features.ethernet = (local_supported_features &
                                            (1 << CONTROL_PROTOCOL__SUPPORTED_FEATURES_ETHERNET_BIT_OFFSET)) != 0;
    device_info.supported_features.pcie = (local_supported_features &
                                        (1 << CONTROL_PROTOCOL__SUPPORTED_FEATURES_PCIE_BIT_OFFSET)) != 0;
    device_info.supported_features.mipi = (local_supported_features &
                                        (1 << CONTROL_PROTOCOL__SUPPORTED_FEATURES_MIPI_BIT_OFFSET)) != 0;
    device_info.supported_features.current_monitoring = (local_supported_features &
                                                        (1 << CONTROL_PROTOCOL__SUPPORTED_FEATURES_CURRENT_MONITORING_BIT_OFFSET)) != 0;
    device_info.supported_features.mdio = (local_supported_features &
                                        (1 << CONTROL_PROTOCOL__SUPPORTED_FEATURES_MDIO_BIT_OFFSET)) != 0;
    device_info.neural_network_core_clock_rate = BYTE_ORDER__ntohl(get_extended_device_information_response.neural_network_core_clock_rate);

    LOGGER__DEBUG("Max Neural Network Core Clock Rate: {}", device_info.neural_network_core_clock_rate);

    device_info.boot_source = static_cast<hailo_device_boot_source_t>(
        BYTE_ORDER__ntohl(get_extended_device_information_response.boot_source));

    (void)memcpy(device_info.soc_id,
                get_extended_device_information_response.soc_id,
                BYTE_ORDER__ntohl(get_extended_device_information_response.soc_id_length));

    device_info.lcs = get_extended_device_information_response.lcs;

    memcpy(&device_info.unit_level_tracking_id[0], &get_extended_device_information_response.fuse_info, sizeof(device_info.unit_level_tracking_id));
    memcpy(&device_info.eth_mac_address[0], &get_extended_device_information_response.eth_mac_address[0], BYTE_ORDER__ntohl(get_extended_device_information_response.eth_mac_length));
    memcpy(&device_info.soc_pm_values, &get_extended_device_information_response.pd_info, sizeof(device_info.soc_pm_values));

    return device_info;
}

Expected<hailo_health_info_t> control__parse_get_health_information_results
        (CONTROL_PROTOCOL__get_health_information_response_t *get_health_information_response)
{
    hailo_health_info_t health_info;

    CHECK_AS_EXPECTED(nullptr != get_health_information_response, HAILO_INVALID_ARGUMENT);

    health_info.overcurrent_protection_active = get_health_information_response->overcurrent_protection_active;
    health_info.current_overcurrent_zone = get_health_information_response->current_overcurrent_zone;
    // Re-convertion to floats after
    health_info.red_overcurrent_threshold = float32_t(BYTE_ORDER__ntohl(get_health_information_response->red_overcurrent_threshold));
    health_info.overcurrent_throttling_active = get_health_information_response->overcurrent_throttling_active;
    health_info.temperature_throttling_active = get_health_information_response->temperature_throttling_active;
    health_info.current_temperature_zone = get_health_information_response->current_temperature_zone;
    health_info.current_temperature_throttling_level = get_health_information_response->current_temperature_throttling_level;
    memcpy(&health_info.temperature_throttling_levels[0], &get_health_information_response->temperature_throttling_levels[0],
            BYTE_ORDER__ntohl(get_health_information_response->temperature_throttling_levels_length));
    health_info.orange_temperature_threshold = BYTE_ORDER__ntohl(get_health_information_response->orange_temperature_threshold);
    health_info.orange_hysteresis_temperature_threshold = BYTE_ORDER__ntohl(get_health_information_response->orange_hysteresis_temperature_threshold);
    health_info.red_temperature_threshold = BYTE_ORDER__ntohl(get_health_information_response->red_temperature_threshold);
    health_info.red_hysteresis_temperature_threshold = BYTE_ORDER__ntohl(get_health_information_response->red_hysteresis_temperature_threshold);
    health_info.requested_overcurrent_clock_freq = BYTE_ORDER__ntohl(get_health_information_response->requested_overcurrent_clock_freq);
    health_info.requested_temperature_clock_freq = BYTE_ORDER__ntohl(get_health_information_response->requested_temperature_clock_freq);
    return health_info;
}


hailo_status control__parse_core_identify_results(CONTROL_PROTOCOL__core_identify_response_t *identify_response,
        hailo_core_information_t *core_info)
{
    CHECK_ARG_NOT_NULL(core_info);
    CHECK_ARG_NOT_NULL(identify_response);

    // Store identify response inside control
    (void)memcpy(&(core_info->fw_version),
            &(identify_response->fw_version),
            sizeof(core_info->fw_version));

    // Check if firmware is at debug/release
    core_info->is_release = !(IS_REVISION_DEV(core_info->fw_version.revision));

    // Check if the firmware was compiled with EXTENDED_CONTEXT_SWITCH_BUFFER
    core_info->extended_context_switch_buffer = IS_REVISION_EXTENDED_CONTEXT_SWITCH_BUFFER(core_info->fw_version.revision);

    // Check if the firmware was compiled with EXTENDED_FW_CHECKS
    core_info->extended_fw_check = IS_REVISION_EXTENDED_FW_CHECK(core_info->fw_version.revision);

    // Make sure response was from core CPU
    CHECK((REVISION_APP_CORE_FLAG_BIT_MASK == (core_info->fw_version.revision & REVISION_APP_CORE_FLAG_BIT_MASK)), HAILO_INVALID_FIRMWARE,
        "Got invalid core FW type, which means the FW was not marked correctly. unmaked FW revision {}", core_info->fw_version.revision);

    // Keep the revision number only
    core_info->fw_version.revision = GET_REVISION_NUMBER_VALUE(core_info->fw_version.revision);

    // Write identify results to log
    LOGGER__INFO("core firmware_version is: {}.{}.{}",
            core_info->fw_version.major,
            core_info->fw_version.minor,
            core_info->fw_version.revision
            );

    return HAILO_SUCCESS;
}

hailo_status log_detailed_fw_error(const Device &device, const CONTROL_PROTOCOL__status_t &fw_status, const CONTROL_PROTOCOL__OPCODE_t opcode)
{
    const char *firmware_status_text = NULL;
    // Special care for user_config_examine - warning log will be printed if not loaded, since it can happen on happy-flow (e.g. no EEPROM)
    if ((fw_status.major_status == CONTROL_PROTOCOL_STATUS_USER_CONFIG_EXAMINE_FAILED) &&
        (fw_status.minor_status == FIRMWARE_CONFIGS_STATUS_USER_CONFIG_NOT_LOADED)) {
            LOGGER__WARNING("Failed to examine user config, as it is not loaded or is not supported by the device.");
    }

    LOGGER__ERROR("Firmware control has failed. Major status: {:#x}, Minor status: {:#x}",
            fw_status.major_status,
            fw_status.minor_status);
    auto common_status = FIRMWARE_STATUS__get_textual((FIRMWARE_STATUS_t)fw_status.major_status, &firmware_status_text);
    if (HAILO_COMMON_STATUS__SUCCESS == common_status) {
        LOGGER__ERROR("Firmware major status: {}", firmware_status_text);
    } else {
        LOGGER__ERROR("Cannot find textual address for firmware status {:#x}, common_status = {}",
            static_cast<int>((FIRMWARE_STATUS_t)fw_status.major_status), static_cast<int>(common_status));
    }
    common_status = FIRMWARE_STATUS__get_textual((FIRMWARE_STATUS_t)fw_status.minor_status, &firmware_status_text);
    if (HAILO_COMMON_STATUS__SUCCESS == common_status) {
        LOGGER__ERROR("Firmware minor status: {}", firmware_status_text);
    } else {
        LOGGER__ERROR("Cannot find textual address for firmware status {:#x}, common_status = {}",
            static_cast<int>((FIRMWARE_STATUS_t)fw_status.minor_status), static_cast<int>(common_status));
    }

    if ((CONTROL_PROTOCOL_STATUS_CONTROL_UNSUPPORTED == fw_status.minor_status) ||
        (CONTROL_PROTOCOL_STATUS_CONTROL_UNSUPPORTED == fw_status.major_status)) {
        auto device_arch = device.get_architecture();
        auto dev_arch_str = (device_arch) ? HailoRTCommon::get_device_arch_str(*device_arch) : "Unable to parse arch";
        LOGGER__ERROR("Opcode {} is not supported on the device." \
            " This error usually occurs when the control is not supported for the device arch - ({}), or not compiled to the FW",
            CONTROL_PROTOCOL__get_textual_opcode(opcode), dev_arch_str);
    }

    if ((CONTROL_PROTOCOL_STATUS_UNSUPPORTED_DEVICE == fw_status.minor_status) ||
        (CONTROL_PROTOCOL_STATUS_UNSUPPORTED_DEVICE == fw_status.major_status)) {
        LOGGER__ERROR("Opcode {} is not supported on the current board.", CONTROL_PROTOCOL__get_textual_opcode(opcode));
        return HAILO_UNSUPPORTED_OPCODE;
    }

    if ((HAILO_CONTROL_STATUS_UNSUPPORTED_OPCODE == fw_status.minor_status) ||
        (HAILO_CONTROL_STATUS_UNSUPPORTED_OPCODE == fw_status.major_status)) {
        LOGGER__ERROR("Opcode {} is not supported", CONTROL_PROTOCOL__get_textual_opcode(opcode));
        return HAILO_UNSUPPORTED_OPCODE;
    }

    return HAILO_FW_CONTROL_FAILURE;
}

hailo_status Control::parse_and_validate_response(uint8_t *message, uint32_t message_size,
    CONTROL_PROTOCOL__response_header_t **header, CONTROL_PROTOCOL__payload_t **payload,
    CONTROL_PROTOCOL__request_t *request, Device &device)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__status_t fw_status = {};

    /* Parse the response */
    common_status = CONTROL_PROTOCOL__parse_response(message, message_size, header, payload, &fw_status);
    if (HAILO_STATUS__CONTROL_PROTOCOL__INVALID_VERSION == common_status) {
        status = HAILO_UNSUPPORTED_CONTROL_PROTOCOL_VERSION;
    }
    else {
        status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    }
    if (HAILO_SUCCESS != status) {
        goto exit;
    }
    /* Validate response was successful - both major and minor should be error free */
    if (0 != fw_status.major_status) {
        status = log_detailed_fw_error(device, fw_status,
            static_cast<CONTROL_PROTOCOL__OPCODE_t>(BYTE_ORDER__ntohl(request->header.common_header.opcode)));
        goto exit;

    }

    /* Validate response opcode is same as request */
    if (request->header.common_header.opcode != (*header)->common_header.opcode) {
        status = HAILO_INVALID_CONTROL_RESPONSE;
        LOGGER__ERROR("Invalid opcode received from FW");
        goto exit;
    }

    /* Validate response version is same as request */
    if (request->header.common_header.version != (*header)->common_header.version) {
        status = HAILO_INVALID_CONTROL_RESPONSE;
        LOGGER__ERROR("Invalid protocol version received from FW");
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}

Expected<hailo_device_identity_t> Control::identify(Device &device)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    CONTROL_PROTOCOL_identify_response_t *identify_response = NULL;

    /* Validate arguments */
    common_status = CONTROL_PROTOCOL__pack_identify_request(&request, &request_size, device.get_control_sequence());
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    CHECK_SUCCESS_AS_EXPECTED(status);

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    CHECK_SUCCESS_AS_EXPECTED(status);

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    CHECK_SUCCESS_AS_EXPECTED(status);
    identify_response = (CONTROL_PROTOCOL_identify_response_t *)(payload->parameters);

    return control__parse_identify_results(identify_response);
}

hailo_status Control::core_identify(Device &device, hailo_core_information_t *core_info)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    CONTROL_PROTOCOL__core_identify_response_t *identify_response = NULL;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(core_info);

    common_status = CONTROL_PROTOCOL__pack_core_identify_request(&request, &request_size, device.get_control_sequence());
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }
    identify_response = (CONTROL_PROTOCOL__core_identify_response_t *)(payload->parameters);

    /* Store results inside contol object */
    status = control__parse_core_identify_results(identify_response, core_info);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}


hailo_status Control::set_fw_logger(Device &device, hailo_fw_logger_level_t level, uint32_t interface_mask)
{
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;

    auto common_status = CONTROL_PROTOCOL__pack_set_fw_logger_request(&request, &request_size, device.get_control_sequence(), level,
        static_cast<uint8_t>(interface_mask));

    auto status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    CHECK_SUCCESS(status);

    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = sizeof(response_buffer);
    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    CHECK_SUCCESS(status);

    /* Parse response */
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::set_clock_freq(Device &device, uint32_t clock_freq)
{
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;

    auto common_status = CONTROL_PROTOCOL__pack_set_clock_freq_request(&request, &request_size, device.get_control_sequence(), clock_freq);

    auto status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    CHECK_SUCCESS(status);

    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = sizeof(response_buffer);
    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    CHECK_SUCCESS(status);

    /* Parse response */
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::set_throttling_state(Device &device, bool should_activate)
{
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;

    auto common_status = CONTROL_PROTOCOL__pack_set_throttling_state_request(&request, &request_size, device.get_control_sequence(), should_activate);

    auto status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    CHECK_SUCCESS(status);

    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = sizeof(response_buffer);
    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    CHECK_SUCCESS(status);

    /* Parse response */
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

Expected<bool> Control::get_throttling_state(Device &device)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    CONTROL_PROTOCOL__get_throttling_state_response_t *get_throttling_state_response = NULL;

    common_status = CONTROL_PROTOCOL__pack_get_throttling_state_request(&request, &request_size, device.get_control_sequence());

    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    CHECK_SUCCESS_AS_EXPECTED(status);

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    CHECK_SUCCESS_AS_EXPECTED(status);

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload, &request, device);
    CHECK_SUCCESS_AS_EXPECTED(status);

    get_throttling_state_response = (CONTROL_PROTOCOL__get_throttling_state_response_t *)(payload->parameters);
    return std::move(get_throttling_state_response->is_active);
}

hailo_status Control::set_overcurrent_state(Device &device, bool should_activate)
{
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;

    auto common_status = CONTROL_PROTOCOL__pack_set_overcurrent_state_request(&request, &request_size, device.get_control_sequence(), should_activate);

    auto status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    CHECK_SUCCESS(status);

    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = sizeof(response_buffer);
    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    CHECK_SUCCESS(status);

    /* Parse response */
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload, &request, device);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

Expected<bool> Control::get_overcurrent_state(Device &device)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    CONTROL_PROTOCOL__get_overcurrent_state_response_t *get_overcurrent_state_response = NULL;

    common_status = CONTROL_PROTOCOL__pack_get_overcurrent_state_request(&request, &request_size, device.get_control_sequence());

    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    CHECK_SUCCESS_AS_EXPECTED(status);

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    CHECK_SUCCESS_AS_EXPECTED(status);

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload, &request, device);
    CHECK_SUCCESS_AS_EXPECTED(status);

    get_overcurrent_state_response = (CONTROL_PROTOCOL__get_overcurrent_state_response_t *)(payload->parameters);
    return std::move(get_overcurrent_state_response->is_required);
}

Expected<CONTROL_PROTOCOL__hw_consts_t> Control::get_hw_consts(Device &device)
{
    size_t request_size = 0;
    CONTROL_PROTOCOL__request_t request = {};

    auto common_status = CONTROL_PROTOCOL__pack_get_hw_consts_request(&request, &request_size, device.get_control_sequence());
    auto status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    CHECK_SUCCESS_AS_EXPECTED(status);

    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    CHECK_SUCCESS_AS_EXPECTED(status);

    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload, &request,
        device);
    CHECK_SUCCESS_AS_EXPECTED(status);

    const auto &response = *reinterpret_cast<CONTROL_PROTOCOL__get_hw_consts_response_t*>(payload->parameters);
    return Expected<CONTROL_PROTOCOL__hw_consts_t>(response.hw_consts);
}

hailo_status Control::write_memory_chunk(Device &device, uint32_t address, const uint8_t *data, uint32_t chunk_size)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    /* Validate arguments */
    ASSERT(NULL != data);

    /* Validate chunk size is valid */
    ASSERT(CONTROL__MAX_WRITE_MEMORY_CHUNK_SIZE >= chunk_size);
    ASSERT(0 != chunk_size);

    common_status = CONTROL_PROTOCOL__pack_write_memory_request(&request, &request_size, device.get_control_sequence(), address, data, chunk_size);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::write_memory(Device &device, uint32_t address, const uint8_t *data, uint32_t data_length)
{
    hailo_status status = HAILO_UNINITIALIZED;

    uint32_t current_write_address = address;
    const uint8_t* current_data_address = data;
    uint32_t chunk_size = CONTROL__MAX_WRITE_MEMORY_CHUNK_SIZE;
    uint32_t number_of_chunks = data_length / chunk_size;
    uint32_t data_chunk_leftover = data_length % chunk_size;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(data);

    if (data_length >= chunk_size) {
        for (size_t i = 0; i < number_of_chunks; i++ ) {
            /* Write current memory chunk */
            status = write_memory_chunk(device, current_write_address, current_data_address, chunk_size);
            CHECK_SUCCESS(status);

            current_write_address += chunk_size;
            current_data_address += chunk_size;
        }
    }

    if (data_chunk_leftover > 0) {
        /* Write leftover */
        status = write_memory_chunk(device, current_write_address, current_data_address, data_chunk_leftover);
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status Control::read_memory_chunk(Device &device, uint32_t address, uint8_t *data, uint32_t chunk_size)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    uint32_t actual_read_data_length = 0;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    CONTROL_PROTOCOL__read_memory_response_t *read_memory_response = NULL;

    /* Validate arguments */
    ASSERT(NULL != data);

    /* Validate chunk size is valid */
    ASSERT(CONTROL__MAX_WRITE_MEMORY_CHUNK_SIZE >= chunk_size);
    ASSERT(0 != chunk_size);

    common_status = CONTROL_PROTOCOL__pack_read_memory_request(&request, &request_size, device.get_control_sequence(), address, chunk_size);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    read_memory_response = (CONTROL_PROTOCOL__read_memory_response_t *)(payload->parameters);
    actual_read_data_length = BYTE_ORDER__ntohl(read_memory_response->data_length);
    if (chunk_size != actual_read_data_length) {
        status = HAILO_INVALID_CONTROL_RESPONSE;
        LOGGER__ERROR("Did not read all data from control response");
        goto exit;
    }
    (void)memcpy(data, &read_memory_response->data[0], actual_read_data_length);

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::read_memory(Device &device, uint32_t address, uint8_t *data, uint32_t data_length)
{
    hailo_status status = HAILO_UNINITIALIZED;

    uint32_t current_read_address = address;
    uint8_t* current_data_address = data;
    uint32_t chunk_size = CONTROL__MAX_WRITE_MEMORY_CHUNK_SIZE;
    uint32_t number_of_chunks = data_length / chunk_size;
    uint32_t data_chunk_leftover = data_length % chunk_size;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(data);

    if (data_length >= chunk_size) {
        for (size_t i = 0; i < number_of_chunks; i++ ) {
            /* Read current memory chunk */
            status = read_memory_chunk(device, current_read_address, current_data_address, chunk_size);
            CHECK_SUCCESS(status);

            current_read_address += chunk_size;
            current_data_address += chunk_size;
        }
    }

    if (data_chunk_leftover > 0) {
        /* Read leftover */
        status = read_memory_chunk(device, current_read_address, current_data_address, data_chunk_leftover);
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status Control::open_stream(Device &device, uint8_t dataflow_manager_id, bool is_input)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    common_status = CONTROL_PROTOCOL__pack_open_stream_request(&request, &request_size, device.get_control_sequence(),
        dataflow_manager_id, is_input);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::close_stream(Device &device, uint8_t dataflow_manager_id, bool is_input)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    common_status = CONTROL_PROTOCOL__pack_close_stream_request(&request, &request_size, device.get_control_sequence(),
        dataflow_manager_id, is_input);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::close_all_streams(Device &device)
{
    hailo_status status = HAILO_UNINITIALIZED;

    /* Close all input streams */
    status = close_stream(device, CONTROL_PROTOCOL__ALL_DATAFLOW_MANAGERS, true);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Close all output streams */
    status = close_stream(device, CONTROL_PROTOCOL__ALL_DATAFLOW_MANAGERS, false);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::config_stream_udp_input(Device &device, CONTROL_PROTOCOL__config_stream_params_t *params, uint8_t &dataflow_manager_id)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    CONTROL_PROTOCOL__config_stream_response_t *response = NULL;
    uint32_t dataflow_manager_id_length = 0;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(params);

    common_status = CONTROL_PROTOCOL__pack_config_stream_udp_input_request(&request, &request_size,
        device.get_control_sequence(), params);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    response = (CONTROL_PROTOCOL__config_stream_response_t *)(payload->parameters);
    dataflow_manager_id_length = BYTE_ORDER__ntohl(response->dataflow_manager_id_length);

    /* Validate read data is data size */
    if (dataflow_manager_id_length != sizeof(response->dataflow_manager_id)) {
        status = HAILO_INVALID_CONTROL_RESPONSE;
        goto exit;
    }

    dataflow_manager_id = response->dataflow_manager_id;

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::config_stream_udp_output(Device &device, CONTROL_PROTOCOL__config_stream_params_t *params, uint8_t &dataflow_manager_id)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    CONTROL_PROTOCOL__config_stream_response_t *response = NULL;
    uint32_t dataflow_manager_id_length = 0;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(params);

    common_status = CONTROL_PROTOCOL__pack_config_stream_udp_output_request(&request, &request_size,
        device.get_control_sequence(), params);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    response = (CONTROL_PROTOCOL__config_stream_response_t *)(payload->parameters);
    dataflow_manager_id_length = BYTE_ORDER__ntohl(response->dataflow_manager_id_length);

    /* Validate read data is data size */
    if (dataflow_manager_id_length != sizeof(response->dataflow_manager_id)) {
        status = HAILO_INVALID_CONTROL_RESPONSE;
        goto exit;
    }

    dataflow_manager_id = response->dataflow_manager_id;

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::config_stream_mipi_input(Device &device, CONTROL_PROTOCOL__config_stream_params_t *params, uint8_t &dataflow_manager_id)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    CONTROL_PROTOCOL__config_stream_response_t *response = NULL;
    uint32_t dataflow_manager_id_length = 0;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(params);

    common_status = CONTROL_PROTOCOL__pack_config_stream_mipi_input_request(&request, &request_size,
        device.get_control_sequence(), params);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    response = (CONTROL_PROTOCOL__config_stream_response_t *)(payload->parameters);
    dataflow_manager_id_length = BYTE_ORDER__ntohl(response->dataflow_manager_id_length);

    /* Validate read data is data size */
    if (dataflow_manager_id_length != sizeof(response->dataflow_manager_id)) {
        status = HAILO_INVALID_CONTROL_RESPONSE;
        goto exit;
    }

    dataflow_manager_id = response->dataflow_manager_id;

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::config_stream_mipi_output(Device &device, CONTROL_PROTOCOL__config_stream_params_t *params, uint8_t &dataflow_manager_id)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    CONTROL_PROTOCOL__config_stream_response_t *response = NULL;
    uint32_t dataflow_manager_id_length = 0;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(params);

    common_status = CONTROL_PROTOCOL__pack_config_stream_mipi_output_request(&request, &request_size,
        device.get_control_sequence(), params);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    response = (CONTROL_PROTOCOL__config_stream_response_t *)(payload->parameters);
    dataflow_manager_id_length = BYTE_ORDER__ntohl(response->dataflow_manager_id_length);

    /* Validate read data is data size */
    if (dataflow_manager_id_length != sizeof(response->dataflow_manager_id)) {
        status = HAILO_INVALID_CONTROL_RESPONSE;
        goto exit;
    }

    dataflow_manager_id = response->dataflow_manager_id;

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::config_stream_pcie_input(Device &device, CONTROL_PROTOCOL__config_stream_params_t *params, uint8_t &dataflow_manager_id)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    CONTROL_PROTOCOL__config_stream_response_t *response = NULL;
    uint32_t dataflow_manager_id_length = 0;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(params);

    common_status = CONTROL_PROTOCOL__pack_config_stream_pcie_input_request(&request, &request_size,
        device.get_control_sequence(), params);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    response = (CONTROL_PROTOCOL__config_stream_response_t *)(payload->parameters);
    dataflow_manager_id_length = BYTE_ORDER__ntohl(response->dataflow_manager_id_length);

    /* Validate read data is data size */
    if (dataflow_manager_id_length != sizeof(response->dataflow_manager_id)) {
        status = HAILO_INVALID_CONTROL_RESPONSE;
        goto exit;
    }

    dataflow_manager_id = response->dataflow_manager_id;

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::config_stream_pcie_output(Device &device, CONTROL_PROTOCOL__config_stream_params_t *params, uint8_t &dataflow_manager_id)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    CONTROL_PROTOCOL__config_stream_response_t *response = NULL;
    uint32_t dataflow_manager_id_length = 0;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(params);

    common_status = CONTROL_PROTOCOL__pack_config_stream_pcie_output_request(&request, &request_size,
        device.get_control_sequence(), params);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    response = (CONTROL_PROTOCOL__config_stream_response_t *)(payload->parameters);
    dataflow_manager_id_length = BYTE_ORDER__ntohl(response->dataflow_manager_id_length);

    /* Validate read data is data size */
    if (dataflow_manager_id_length != sizeof(response->dataflow_manager_id)) {
        status = HAILO_INVALID_CONTROL_RESPONSE;
        goto exit;
    }

    dataflow_manager_id = response->dataflow_manager_id;


    status = HAILO_SUCCESS;
exit:
    return status;
}

// TODO: needed?
hailo_status Control::power_measurement(Device &device, CONTROL_PROTOCOL__dvm_options_t dvm,
    CONTROL_PROTOCOL__power_measurement_types_t measurement_type, float32_t *measurement)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    CONTROL_PROTOCOL__power_measurement_response_t *response = NULL;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(measurement);

    common_status = CONTROL_PROTOCOL__pack_power_measurement_request(&request, &request_size, device.get_control_sequence(),
            dvm, measurement_type);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }
    response = (CONTROL_PROTOCOL__power_measurement_response_t*)(payload->parameters);

    LOGGER__INFO("The chosen dvm type is: {}, and measurement type: {}", response->dvm,
        response->measurement_type);
    if (CONTROL_PROTOCOL__DVM_OPTIONS_OVERCURRENT_PROTECTION == response->dvm) {
        LOGGER__WARN(OVERCURRENT_PROTECTION_WARNING);
    }

    *measurement = response->power_measurement;

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::set_power_measurement(Device &device, hailo_measurement_buffer_index_t buffer_index, CONTROL_PROTOCOL__dvm_options_t dvm,
    CONTROL_PROTOCOL__power_measurement_types_t measurement_type)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    CONTROL_PROTOCOL__set_power_measurement_response_t *response = NULL;

    CHECK(CONTROL_PROTOCOL__MAX_NUMBER_OF_POWER_MEASUREMETS > buffer_index,
        HAILO_INVALID_ARGUMENT, "Invalid power measurement index {}", static_cast<int>(buffer_index));

    common_status = CONTROL_PROTOCOL__pack_set_power_measurement_request(&request, &request_size, device.get_control_sequence(),
            buffer_index, dvm, measurement_type);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }
    response = (CONTROL_PROTOCOL__set_power_measurement_response_t*)(payload->parameters);

    LOGGER__INFO("The chosen dvm type is: {}, and measurement type: {}", response->dvm,
        response->measurement_type);
    if (CONTROL_PROTOCOL__DVM_OPTIONS_OVERCURRENT_PROTECTION == response->dvm) {
        LOGGER__WARN(OVERCURRENT_PROTECTION_WARNING);
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::get_power_measurement(Device &device, hailo_measurement_buffer_index_t buffer_index, bool should_clear,
    hailo_power_measurement_data_t *measurement_data)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    CONTROL_PROTOCOL__get_power_measurement_response_t *get_power_response = NULL;

    /* Validate arguments */
    CHECK(CONTROL_PROTOCOL__MAX_NUMBER_OF_POWER_MEASUREMETS > buffer_index,
        HAILO_INVALID_ARGUMENT, "Invalid power measurement index {}", static_cast<int>(buffer_index));
    CHECK_ARG_NOT_NULL(measurement_data);
    common_status = CONTROL_PROTOCOL__pack_get_power_measurement_request(&request, &request_size, device.get_control_sequence(),
            buffer_index, should_clear);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }
    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }
    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }
    get_power_response = (CONTROL_PROTOCOL__get_power_measurement_response_t *)(payload->parameters);

    /* Copy measurement data from response to the exported measurement data */
    measurement_data->average_time_value_milliseconds = get_power_response->average_time_value_milliseconds;
    measurement_data->average_value = get_power_response->average_value;
    measurement_data->min_value = get_power_response->min_value;
    measurement_data->max_value = get_power_response->max_value;
    measurement_data->total_number_of_samples = BYTE_ORDER__ntohl(get_power_response->total_number_of_samples);
    LOGGER__DEBUG("avg: {:f}, min: {:f}, max: {:f}",
            measurement_data->average_value,
            measurement_data->min_value,
            measurement_data->max_value);
    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::start_power_measurement(Device &device,
    CONTROL_PROTOCOL__averaging_factor_t averaging_factor , CONTROL_PROTOCOL__sampling_period_t sampling_period)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    uint32_t delay_milliseconds = 0;

    delay_milliseconds = POWER_MEASUREMENT_DELAY_MS(sampling_period, averaging_factor);
    // There is no logical way that measurement delay can be 0 - because sampling_period and averaging_factor cant be 0
    // Hence if it is 0 - it means it was 0.xx and we want to round up to 1 in that case
    if (0 == delay_milliseconds) {
        delay_milliseconds = 1;
    }

    common_status = CONTROL_PROTOCOL__pack_start_power_measurement_request(&request, &request_size, device.get_control_sequence(),
            delay_milliseconds, averaging_factor, sampling_period);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::stop_power_measurement(Device &device)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    common_status = CONTROL_PROTOCOL__pack_stop_power_measurement_request(&request, &request_size, device.get_control_sequence());
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::i2c_write(Device &device, const hailo_i2c_slave_config_t *slave_config, uint32_t register_address,
        const uint8_t *data, uint32_t length)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(slave_config);
    CHECK_ARG_NOT_NULL(data);

    /* Pack request */
    common_status = CONTROL_PROTOCOL__pack_i2c_write_request(&request, &request_size, device.get_control_sequence(),
            register_address, static_cast<uint8_t>(slave_config->endianness),
            slave_config->slave_address, slave_config->register_address_size, slave_config->bus_index, data, length);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer,
            &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::i2c_read(Device &device, const hailo_i2c_slave_config_t *slave_config, uint32_t register_address,
        uint8_t *data, uint32_t length)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    CONTROL_PROTOCOL__i2c_read_response_t *response = NULL;
    uint32_t local_data_length = 0;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(slave_config);
    CHECK_ARG_NOT_NULL(data);

    /* Pack request */
    common_status = CONTROL_PROTOCOL__pack_i2c_read_request(&request, &request_size, device.get_control_sequence(),
            register_address, static_cast<uint8_t>(slave_config->endianness),
            slave_config->slave_address, slave_config->register_address_size, slave_config->bus_index, length,
            slave_config->should_hold_bus);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer,
            &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    response = (CONTROL_PROTOCOL__i2c_read_response_t *)(payload->parameters);
    local_data_length = BYTE_ORDER__ntohl(response->data_length);

    /* Validate read data is data size */
    if (local_data_length != length) {
        status = HAILO_INVALID_CONTROL_RESPONSE;
        LOGGER__ERROR("Read data size from I2C does not match register size. ({} != {})",
                local_data_length, length);
        goto exit;
    }

    /* Copy the returned results back to the user */
    (void)memcpy(data, response->data, local_data_length);

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::config_core_top(Device &device, CONTROL_PROTOCOL__config_core_top_type_t config_type,
    CONTROL_PROTOCOL__config_core_top_params_t *params)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(params);

    common_status = CONTROL_PROTOCOL__pack_config_core_top_request(&request, &request_size, device.get_control_sequence(), config_type, params);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::phy_operation(Device &device, CONTROL_PROTOCOL__phy_operation_t operation_type)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    common_status = CONTROL_PROTOCOL__pack_phy_operation_request(&request, &request_size, device.get_control_sequence(), operation_type);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::examine_user_config(Device &device, hailo_fw_user_config_information_t *info)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    CONTROL_PROTOCOL__examine_user_config_response_t *response = NULL;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(info);

    common_status = CONTROL_PROTOCOL__pack_examine_user_config(&request, &request_size, device.get_control_sequence());
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Save response information into exported struct */
    response = ((CONTROL_PROTOCOL__examine_user_config_response_t *)(payload->parameters));
    info->version = BYTE_ORDER__ntohl(response->version);
    info->entry_count = BYTE_ORDER__ntohl(response->entry_count);
    info->total_size = BYTE_ORDER__ntohl(response->total_size);

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::read_user_config_chunk(Device &device, uint32_t read_offset, uint32_t read_length,
    uint8_t *buffer, uint32_t *actual_read_data_length)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    CONTROL_PROTOCOL__read_user_config_response_t *response = NULL;

    common_status = CONTROL_PROTOCOL__pack_read_user_config(&request, &request_size, device.get_control_sequence(),
        read_offset, read_length);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    CHECK_SUCCESS(status);

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer,
        &response_size);
    CHECK_SUCCESS(status);

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    CHECK_SUCCESS(status);

    response = (CONTROL_PROTOCOL__read_user_config_response_t *)(payload->parameters);
    *actual_read_data_length = BYTE_ORDER__ntohl(response->data_length);
    (void) memcpy(buffer, response->data, *actual_read_data_length);

    return HAILO_SUCCESS;
}

hailo_status Control::read_user_config(Device &device, uint8_t *buffer, uint32_t buffer_length)
{
    hailo_status status = HAILO_UNINITIALIZED;
    uint32_t actual_read_data_length = 0;
    uint32_t read_offset = 0;
    hailo_fw_user_config_information_t user_config_info = {};

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(buffer);

    status = examine_user_config(device, &user_config_info);
    CHECK_SUCCESS(status);

    CHECK(buffer_length >= user_config_info.total_size, HAILO_INSUFFICIENT_BUFFER,
        "read buffer is too small. provided buffer size: {} bytes, user config size: {} bytes", buffer_length,
        user_config_info.total_size);

    LOGGER__INFO("Preparing to read user configuration. Version: {}, Entry Count: {}, Total Size (bytes): {}",
        user_config_info.version, user_config_info.entry_count, user_config_info.total_size);

    while (read_offset < user_config_info.total_size) {
        read_user_config_chunk(device, read_offset, user_config_info.total_size - read_offset,
            buffer + read_offset, &actual_read_data_length);
        read_offset += actual_read_data_length;
    }

    return HAILO_SUCCESS;
}

hailo_status Control::write_user_config_chunk(Device &device, uint32_t offset, const uint8_t *data, uint32_t chunk_size)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    common_status = CONTROL_PROTOCOL__pack_write_user_config_request(&request, &request_size,
        device.get_control_sequence(), offset, data + offset, chunk_size);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    CHECK_SUCCESS(status);

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer,
        &response_size);
    CHECK_SUCCESS(status);

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::write_user_config(Device &device, const uint8_t *data, uint32_t data_length)
{
    hailo_status status = HAILO_UNINITIALIZED;
    uint32_t offset = 0;
    uint32_t chunk_size = 0;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(data);

    while (offset < data_length) {
        chunk_size = MIN(WRITE_CHUNK_SIZE, (data_length - offset));
        status = write_user_config_chunk(device, offset, data, chunk_size);
        CHECK_SUCCESS(status);
        offset += chunk_size;
    }

    return HAILO_SUCCESS;
}

hailo_status Control::erase_user_config(Device &device)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    common_status = CONTROL_PROTOCOL__pack_erase_user_config_request(&request, &request_size, device.get_control_sequence());
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}


hailo_status Control::read_board_config(Device &device, uint8_t *buffer, uint32_t buffer_length)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    uint32_t actual_read_data_length = 0;
    uint32_t read_offset = 0;
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    CONTROL_PROTOCOL__read_user_config_response_t *response = NULL;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(buffer);

    CHECK(buffer_length >= BOARD_CONFIG_SIZE, HAILO_INSUFFICIENT_BUFFER,
        "read buffer is too small. provided buffer size: {} bytes, board config size: {} bytes", buffer_length,
        BOARD_CONFIG_SIZE);

    LOGGER__INFO("Preparing to read board configuration");
    common_status = CONTROL_PROTOCOL__pack_read_board_config(&request, &request_size, device.get_control_sequence(),
        read_offset, BOARD_CONFIG_SIZE);

    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    CHECK_SUCCESS(status);

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer,
        &response_size);
    CHECK_SUCCESS(status);

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    CHECK_SUCCESS(status);
    response = (CONTROL_PROTOCOL__read_board_config_response_t *)(payload->parameters);
    actual_read_data_length = BYTE_ORDER__ntohl(response->data_length);
    (void) memcpy(buffer, response->data, actual_read_data_length);

    return HAILO_SUCCESS;
}



hailo_status Control::write_board_config(Device &device, const uint8_t *data, uint32_t data_length)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    uint32_t write_offset = 0;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(data);

    CHECK(BOARD_CONFIG_SIZE >= data_length, HAILO_INVALID_OPERATION,
        "Invalid size of board config. data_length={},  max_size={}" , data_length, BOARD_CONFIG_SIZE);

    common_status = CONTROL_PROTOCOL__pack_write_board_config_request(&request, &request_size,
        device.get_control_sequence(), write_offset, data + write_offset, data_length);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    CHECK_SUCCESS(status);

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer,
        &response_size);
    CHECK_SUCCESS(status);

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::write_second_stage_to_internal_memory(Device &device, uint32_t offset, uint8_t *data, uint32_t data_length)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(data);

    common_status = CONTROL_PROTOCOL__write_second_stage_to_internal_memory_request(&request, &request_size, device.get_control_sequence(), offset,
            data, data_length);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}


hailo_status Control::copy_second_stage_to_flash(Device &device, MD5_SUM_t *expected_md5, uint32_t second_stage_size)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(expected_md5);

    common_status = CONTROL_PROTOCOL__copy_second_stage_to_flash_request(&request, &request_size, device.get_control_sequence(), expected_md5, second_stage_size);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::start_firmware_update(Device &device)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    common_status = CONTROL_PROTOCOL__pack_start_firmware_update_request(&request, &request_size, device.get_control_sequence());
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::finish_firmware_update(Device &device)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    common_status = CONTROL_PROTOCOL__pack_finish_firmware_update_request(&request, &request_size, device.get_control_sequence());
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::write_firmware_update(Device &device, uint32_t offset, const uint8_t *data, uint32_t data_length)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(data);

    common_status = CONTROL_PROTOCOL__write_firmware_update_request(&request, &request_size, device.get_control_sequence(), offset,
            data, data_length);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::validate_firmware_update(Device &device, MD5_SUM_t *expected_md5, uint32_t firmware_size)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(expected_md5);

    common_status = CONTROL_PROTOCOL__pack_validate_firmware_update_request(&request, &request_size, device.get_control_sequence(),
            expected_md5, firmware_size);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::latency_measurement_read(Device &device, uint32_t *inbound_to_outbound_latency_nsec)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    CONTROL_PROTOCOL__latency_read_response_t *response = NULL;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(inbound_to_outbound_latency_nsec);

    common_status = CONTROL_PROTOCOL__pack_latency_measurement_read_request(&request, &request_size, device.get_control_sequence());
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    response = (CONTROL_PROTOCOL__latency_read_response_t*)(payload->parameters);
    *inbound_to_outbound_latency_nsec = BYTE_ORDER__ntohl(response->inbound_to_outbound_latency_nsec);

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::latency_measurement_config(Device &device, uint8_t latency_measurement_en,
    uint32_t inbound_start_buffer_number, uint32_t outbound_stop_buffer_number, uint32_t inbound_stream_index,
    uint32_t outbound_stream_index)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    common_status = CONTROL_PROTOCOL__pack_latency_measurement_config_request(&request, &request_size, device.get_control_sequence(),
            latency_measurement_en, inbound_start_buffer_number, outbound_stop_buffer_number,
            inbound_stream_index, outbound_stream_index);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}


hailo_status Control::sensor_store_config(Device &device, uint32_t is_first, uint32_t section_index,
    uint32_t start_offset, uint32_t reset_data_size, uint32_t sensor_type, uint32_t total_data_size, uint8_t *data,
    uint32_t data_length,uint16_t config_height, uint16_t config_width, uint16_t config_fps,
    uint32_t config_name_length, uint8_t *config_name)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(data);
    CHECK_ARG_NOT_NULL(config_name);

    common_status =  CONTROL_PROTOCOL__pack_sensor_store_config_request(&request, &request_size, device.get_control_sequence(), is_first, section_index, start_offset,
                                                                        reset_data_size, sensor_type, total_data_size, data, data_length, config_height,
                                                                        config_width, config_fps, config_name_length, config_name);

    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::sensor_set_i2c_bus_index(Device &device, uint32_t sensor_type, uint32_t bus_index)
{
    hailo_status status = HAILO_UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    status = CONTROL_PROTOCOL__pack_sensor_set_i2c_bus_index_request(&request, &request_size, device.get_control_sequence(), sensor_type, bus_index);
    CHECK_SUCCESS(status);

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    CHECK_SUCCESS(status);

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload, &request, device);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::sensor_load_and_start_config(Device &device, uint32_t section_index)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    common_status = CONTROL_PROTOCOL__pack_sensor_load_and_start_config_request(&request, &request_size, device.get_control_sequence(), section_index);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::sensor_reset(Device &device, uint32_t section_index)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    common_status = CONTROL_PROTOCOL__pack_sensor_reset_request(&request, &request_size, device.get_control_sequence(), section_index);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::sensor_set_generic_i2c_slave(Device &device, uint16_t slave_address,
    uint8_t register_address_size, uint8_t bus_index, uint8_t should_hold_bus, uint8_t endianness)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    common_status = CONTROL_PROTOCOL__pack_sensor_set_generic_i2c_slave_request(&request, &request_size, device.get_control_sequence(), slave_address, register_address_size, bus_index, should_hold_bus, endianness);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}


hailo_status Control::sensor_get_config(Device &device, uint32_t section_index, uint32_t offset, uint32_t data_length,
    uint8_t *data)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    uint32_t actual_read_data_length = 0;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    CONTROL_PROTOCOL__sensor_get_config_response_t *sensor_get_config_response = NULL;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(data);

    common_status = CONTROL_PROTOCOL__pack_sensor_get_config_request(&request, &request_size, device.get_control_sequence(), section_index, offset, data_length);

    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    sensor_get_config_response = (CONTROL_PROTOCOL__sensor_get_config_response_t *)(payload->parameters);
    actual_read_data_length = BYTE_ORDER__ntohl(sensor_get_config_response->data_length);
    if (data_length != actual_read_data_length) {
        status = HAILO_INVALID_CONTROL_RESPONSE;
        LOGGER__ERROR("Did not read all data from control response");
        goto exit;
    }
    (void)memcpy(data, &sensor_get_config_response->data[0], actual_read_data_length);

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::sensor_get_sections_info(Device &device, uint8_t *data)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    uint32_t actual_read_data_length = 0;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    CONTROL_PROTOCOL__sensor_get_sections_info_response_t *get_sections_info_response = NULL;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(data);

    common_status = CONTROL_PROTOCOL__pack_sensor_get_sections_info_request(&request, &request_size, device.get_control_sequence());

    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    get_sections_info_response = (CONTROL_PROTOCOL__sensor_get_sections_info_response_t *)(payload->parameters);

    actual_read_data_length = BYTE_ORDER__ntohl(get_sections_info_response->data_length);
    if (0 == actual_read_data_length) {
        status = HAILO_INVALID_CONTROL_RESPONSE;
        LOGGER__ERROR("Did not read all data from control response");
        goto exit;
    }
    (void)memcpy(data, &get_sections_info_response->data[0], actual_read_data_length);
    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::context_switch_set_network_group_header(Device &device,
    const CONTROL_PROTOCOL__application_header_t &network_group_header)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    common_status = CONTROL_PROTOCOL__pack_context_switch_set_network_group_header_request(&request, &request_size,
        device.get_control_sequence(), &network_group_header);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::context_switch_set_context_info_chunk(Device &device,
    const CONTROL_PROTOCOL__context_switch_context_info_chunk_t &context_info)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    common_status = CONTROL_PROTOCOL__pack_context_switch_set_context_info_request(&request, &request_size, device.get_control_sequence(),
        &context_info);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        /* In case of max memory error, add LOGGER ERROR, and set indicative error to the user */
        CHECK(CONTEXT_SWITCH_STATUS_SRAM_MEMORY_FULL != BYTE_ORDER__htonl(header->status.major_status),
            HAILO_OUT_OF_FW_MEMORY, "Configured network groups reached maximum device internal memory (SRAM Full).");
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}
hailo_status Control::context_switch_signal_cache_updated(Device &device)
{
    CONTROL_PROTOCOL__request_t request{};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    const auto common_status = CONTROL_PROTOCOL__pack_context_switch_signal_cache_updated_request(&request, &request_size,
        device.get_control_sequence());
    auto status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    CHECK_SUCCESS(status);

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    CHECK_SUCCESS(status);

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::context_switch_set_context_info(Device &device,
    const std::vector<CONTROL_PROTOCOL__context_switch_context_info_chunk_t> &context_infos)
{
    for (const auto &context_info : context_infos) {
        auto status = context_switch_set_context_info_chunk(device, context_info);
        CHECK_SUCCESS(status);
    }
    return HAILO_SUCCESS;
}

hailo_status Control::idle_time_get_measurement(Device &device, uint64_t *measurement)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    CONTROL_PROTOCOL__idle_time_get_measurement_response_t *idle_time_get_measurement_response = NULL;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(measurement);

    common_status = CONTROL_PROTOCOL__pack_idle_time_get_measuremment_request(&request, &request_size, device.get_control_sequence());
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("failed CONTROL_PROTOCOL__pack_idle_time_get_measuremment_request with status {:#X}", static_cast<int>(common_status));
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("failed idle_time_get_measurement control with status {}", status);
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("failed validating idle_time_get_measurement control response with status {}", status);
        goto exit;
    }

    idle_time_get_measurement_response = (CONTROL_PROTOCOL__idle_time_get_measurement_response_t *)(payload->parameters);

    /*copy the measurement*/
    *measurement = BYTE_ORDER__ntohll(idle_time_get_measurement_response->idle_time_ns);

    LOGGER__DEBUG("Received idle measurement low: {:#X} ns",
        *((uint32_t *) measurement));
    LOGGER__DEBUG("Received idle measurement high: {:#X} ns",
        *(((uint32_t *) measurement) + 1));

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::idle_time_set_measurement(Device &device, uint8_t measurement_enable)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    common_status = CONTROL_PROTOCOL__pack_idle_time_set_measuremment_request(&request, &request_size, device.get_control_sequence(), measurement_enable);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("failed CONTROL_PROTOCOL__pack_idle_time_set_measuremment_request with status {:#X}", static_cast<int>(common_status));
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("failed idle_time_set_measurement control with status {}", status);
        goto exit;
    }
    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::set_pause_frames(Device &device, uint8_t rx_pause_frames_enable)
{
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;

    HAILO_COMMON_STATUS_t common_status = CONTROL_PROTOCOL__pack_set_pause_frames_request(&request, &request_size,
                                             device.get_control_sequence(), rx_pause_frames_enable);
    auto status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    CHECK_SUCCESS(status);

    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    CHECK_SUCCESS(status);

    /* Parse response */
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::download_context_action_list_chunk(Device &device, uint32_t network_group_id,
    CONTROL_PROTOCOL__context_switch_context_type_t context_type, uint16_t context_index,
    uint16_t action_list_offset, size_t action_list_max_size, uint32_t *base_address, uint8_t *action_list,
    uint16_t *action_list_length, bool *is_action_list_end, uint32_t *batch_counter, uint32_t *idle_time )
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = sizeof(response_buffer);
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    CONTROL_PROTOCOL__download_context_action_list_response_t *context_action_list_response = NULL;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(base_address);
    CHECK_ARG_NOT_NULL(action_list);
    CHECK_ARG_NOT_NULL(action_list_length);

    common_status = CONTROL_PROTOCOL__pack_download_context_action_list_request(&request, &request_size, device.get_control_sequence(),
        network_group_id, context_type, context_index, action_list_offset);

    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    context_action_list_response = (CONTROL_PROTOCOL__download_context_action_list_response_t *)(payload->parameters);

    if (0 == BYTE_ORDER__ntohl(context_action_list_response->action_list_length)) {
        status = HAILO_INVALID_CONTROL_RESPONSE;
        LOGGER__ERROR("Received empty action list");
        goto exit;
    }
    if (0 == BYTE_ORDER__ntohl(context_action_list_response->base_address)) {
        status = HAILO_INVALID_CONTROL_RESPONSE;
        LOGGER__ERROR("Received NULL pointer to base address");
        goto exit;
    }

    if (action_list_max_size < BYTE_ORDER__ntohl(context_action_list_response->action_list_length)) {
        status = HAILO_INVALID_CONTROL_RESPONSE;
        LOGGER__ERROR("Received action list bigger than allocated user buffer");
    }

    (void)memcpy(action_list, context_action_list_response->action_list
            ,BYTE_ORDER__ntohl(context_action_list_response->action_list_length));

    *action_list_length = (uint16_t)(BYTE_ORDER__ntohl(context_action_list_response->action_list_length));
    *base_address = BYTE_ORDER__ntohl(context_action_list_response->base_address);
    *is_action_list_end = context_action_list_response->is_action_list_end;
    *batch_counter = BYTE_ORDER__ntohl(context_action_list_response->batch_counter);
    *idle_time = BYTE_ORDER__ntohl(context_action_list_response->idle_time);

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::download_context_action_list(Device &device, uint32_t network_group_id,
    CONTROL_PROTOCOL__context_switch_context_type_t context_type, uint16_t context_index, size_t action_list_max_size,
    uint32_t *base_address, uint8_t *action_list, uint16_t *action_list_length, uint32_t *batch_counter, uint32_t *idle_time)
{
    hailo_status status = HAILO_UNINITIALIZED;
    bool is_action_list_end = false;
    uint16_t chunk_action_list_length = 0;
    uint16_t accumulated_action_list_length = 0;
    uint8_t *action_list_current_offset = 0;
    size_t remaining_action_list_max_size = 0;
    uint32_t chunk_base_address = 0;
    uint32_t batch_counter_local = 0;
    uint32_t idle_time_local = 0;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(base_address);
    CHECK_ARG_NOT_NULL(action_list);
    CHECK_ARG_NOT_NULL(action_list_length);

    action_list_current_offset = action_list;
    remaining_action_list_max_size = action_list_max_size;

    do {
        status = download_context_action_list_chunk(device, network_group_id, context_type, context_index,
            accumulated_action_list_length, remaining_action_list_max_size, &chunk_base_address,
            action_list_current_offset, &chunk_action_list_length, &is_action_list_end, &batch_counter_local, &idle_time_local);
        CHECK_SUCCESS(status);

        accumulated_action_list_length = (uint16_t)(accumulated_action_list_length + chunk_action_list_length);
        action_list_current_offset += chunk_action_list_length;
        remaining_action_list_max_size -= chunk_action_list_length;
    }
    while (!is_action_list_end);

    /* Set output variables */
    *base_address =  chunk_base_address;
    *action_list_length = accumulated_action_list_length;
    *batch_counter = batch_counter_local;
    *idle_time =  idle_time_local;

    return HAILO_SUCCESS;
}

hailo_status Control::change_context_switch_status(Device &device,
        CONTROL_PROTOCOL__CONTEXT_SWITCH_STATUS_t state_machine_status,
        uint8_t network_group_index, uint16_t dynamic_batch_size, uint16_t batch_count)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = sizeof(response_buffer);
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    common_status = CONTROL_PROTOCOL__pack_change_context_switch_status_request(&request, &request_size,
            device.get_control_sequence(), state_machine_status, network_group_index, dynamic_batch_size, batch_count);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::enable_core_op(Device &device, uint8_t network_group_index, uint16_t dynamic_batch_size,
    uint16_t batch_count)
{
    return Control::change_context_switch_status(device, CONTROL_PROTOCOL__CONTEXT_SWITCH_STATUS_ENABLED,
        network_group_index, dynamic_batch_size, batch_count);
}

hailo_status Control::reset_context_switch_state_machine(Device &device)
{
    static const auto IGNORE_NETWORK_GROUP_INDEX = 255;
    static const auto IGNORE_DYNAMIC_BATCH_SIZE = 0;
    static const auto DEFAULT_BATCH_COUNT = 0;
    return Control::change_context_switch_status(device, CONTROL_PROTOCOL__CONTEXT_SWITCH_STATUS_RESET,
        IGNORE_NETWORK_GROUP_INDEX, IGNORE_DYNAMIC_BATCH_SIZE, DEFAULT_BATCH_COUNT);
}

hailo_status Control::wd_enable(Device &device, uint8_t cpu_id, bool should_enable)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    common_status = CONTROL_PROTOCOL__pack_wd_enable(&request, &request_size, device.get_control_sequence(), cpu_id, should_enable);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("failed CONTROL_PROTOCOL__pack_wd_enable with status {:#X}", static_cast<int>(common_status));
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("failed wd_enable control with status {}", status);
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}
hailo_status Control::wd_config(Device &device, uint8_t cpu_id, uint32_t wd_cycles, CONTROL_PROTOCOL__WATCHDOG_MODE_t wd_mode)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    common_status = CONTROL_PROTOCOL__pack_wd_config(&request, &request_size, device.get_control_sequence(), cpu_id, wd_cycles, wd_mode);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("failed CONTROL_PROTOCOL__pack_wd_config with status {:#X}", static_cast<int>(common_status));
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("failed wd_config control with status {}", status);
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::previous_system_state(Device &device, uint8_t cpu_id, CONTROL_PROTOCOL__system_state_t *system)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    CONTROL_PROTOCOL__previous_system_state_response_t *previous_system_state_response = NULL;

    CHECK_ARG_NOT_NULL(system);

    common_status = CONTROL_PROTOCOL__pack_previous_system_state(&request, &request_size, device.get_control_sequence(), cpu_id);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("failed CONTROL_PROTOCOL__pack_previous_system_state with status {:#X}", static_cast<int>(common_status));
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("failed previous_system_state control with status {}", status);
        goto exit;
    }

    previous_system_state_response = (CONTROL_PROTOCOL__previous_system_state_response_t *)(payload->parameters);

    /*copy the measurement*/
    *system = BYTE_ORDER__ntohl(previous_system_state_response->system_state);

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::set_dataflow_interrupt(Device &device, uint8_t interrupt_type, uint8_t interrupt_index,
        uint8_t interrupt_sub_index)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = sizeof(response_buffer);
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    common_status = CONTROL_PROTOCOL__pack_set_dataflow_interrupt_request(&request, &request_size, device.get_control_sequence(),
            interrupt_type, interrupt_index, interrupt_sub_index);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::d2h_notification_manager_set_host_info(Device &device, uint16_t host_port, uint32_t host_ip_address)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = sizeof(response_buffer);
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    auto connection_type = ((Device::Type::PCIE == device.get_type() || Device::Type::INTEGRATED == device.get_type()) ?
        D2H_EVENT_COMMUNICATION_TYPE_VDMA : D2H_EVENT_COMMUNICATION_TYPE_UDP);

    common_status = CONTROL_PROTOCOL__pack_d2h_event_manager_set_host_info_request(&request, &request_size, device.get_control_sequence(),
            static_cast<uint8_t>(connection_type), host_port, host_ip_address);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::d2h_notification_manager_send_host_info_notification(Device &device, uint8_t notification_priority)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = sizeof(response_buffer);
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    common_status = CONTROL_PROTOCOL__pack_d2h_event_manager_send_host_info_event_request(&request, &request_size, device.get_control_sequence(), notification_priority);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}


hailo_status Control::clear_configured_apps(Device &device)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    common_status = CONTROL_PROTOCOL__pack_context_switch_clear_configured_apps_request(&request, &request_size,
        device.get_control_sequence());
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("failed CONTROL_PROTOCOL__pack_context_switch_clear_configured_apps_request with status {:#X}",
            static_cast<int>(common_status));
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload, &request, device);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("failed clear_configured_apps control with status {}", status);
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::get_chip_temperature(Device &device, hailo_chip_temperature_info_t *temp_info)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = sizeof(response_buffer);
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    CONTROL_PROTOCOL__get_chip_temperature_response_t* temps = NULL;

    common_status = CONTROL_PROTOCOL__pack_get_chip_temperature_request(&request, &request_size, device.get_control_sequence());
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    temps = (CONTROL_PROTOCOL__get_chip_temperature_response_t *)(payload->parameters);
    temp_info->sample_count = BYTE_ORDER__ntohs(temps->info.sample_count);
    temp_info->ts0_temperature = temps->info.ts0_temperature;
    temp_info->ts1_temperature = temps->info.ts1_temperature;

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::enable_debugging(Device &device, bool is_rma)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = sizeof(response_buffer);
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    common_status = CONTROL_PROTOCOL__pack_enable_debugging_request(&request, &request_size, device.get_control_sequence(), is_rma);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}

Expected<CONTROL_PROTOCOL__get_extended_device_information_response_t> Control::get_extended_device_info_response(Device &device)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    common_status = CONTROL_PROTOCOL__pack_get_extended_device_information_request(&request, &request_size, device.get_control_sequence());
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    CHECK_SUCCESS_AS_EXPECTED(status);

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    CHECK_SUCCESS_AS_EXPECTED(status);

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload, &request, device);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return std::move(*(CONTROL_PROTOCOL__get_extended_device_information_response_t *)(payload->parameters));
}

Expected<uint32_t> Control::get_partial_clusters_layout_bitmap(Device &device)
{
    auto force_layout_env = get_env_variable(FORCE_LAYOUT_INTERNAL_ENV_VAR);
    if (force_layout_env) {
        return std::stoi(force_layout_env.value());
    }

    TRY(const auto dev_arch, device.get_architecture());
    // In Both cases of Hailo15H and Hailo15M read fuse file (If no file found will return default value of all clusters)
    if ((HAILO_ARCH_HAILO15H == dev_arch) || (HAILO_ARCH_HAILO15M == dev_arch)) {
        TRY(const auto bitmap, PartialClusterReader::get_partial_clusters_layout_bitmap(dev_arch));
        if (PARTIAL_CLUSTERS_LAYOUT_BITMAP__HAILO15_DEFAULT == bitmap) {
            return Expected<uint32_t>(PARTIAL_CLUSTERS_LAYOUT_IGNORE);
        } else {
            return Expected<uint32_t>(bitmap);
        }
    } else if (HAILO_ARCH_HAILO8L != dev_arch) {
        // Partial clusters layout is only relevant in HAILO_ARCH_HAILO8L and HAILO_ARCH_HAILO15M arch
        return Expected<uint32_t>(PARTIAL_CLUSTERS_LAYOUT_IGNORE);
    } else {
        TRY(const auto extended_device_info_response, get_extended_device_info_response(device));
        return BYTE_ORDER__ntohl(extended_device_info_response.partial_clusters_layout_bitmap);
    }
}

Expected<hailo_extended_device_information_t> Control::get_extended_device_information(Device &device)
{
    TRY(const auto extended_device_info_response, get_extended_device_info_response(device));
    return control__parse_get_extended_device_information_results(extended_device_info_response);
}

Expected<hailo_health_info_t> Control::get_health_information(Device &device)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    CONTROL_PROTOCOL__get_health_information_response_t *get_health_information_response = NULL;

    common_status = CONTROL_PROTOCOL__pack_get_health_information_request(&request, &request_size, device.get_control_sequence());
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    CHECK_SUCCESS_AS_EXPECTED(status);

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    CHECK_SUCCESS_AS_EXPECTED(status);

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload, &request,
        device);
    CHECK_SUCCESS_AS_EXPECTED(status);

    get_health_information_response = (CONTROL_PROTOCOL__get_health_information_response_t *)(payload->parameters);

    return control__parse_get_health_information_results(get_health_information_response);
}

hailo_status Control::config_context_switch_breakpoint(Device &device, uint8_t breakpoint_id,
        CONTROL_PROTOCOL__context_switch_breakpoint_control_t breakpoint_control,
        CONTROL_PROTOCOL__context_switch_breakpoint_data_t *breakpoint_data)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = sizeof(response_buffer);
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    common_status = CONTROL_PROTOCOL__pack_config_context_switch_breakpoint_request(
            &request, &request_size, device.get_control_sequence(), breakpoint_id, breakpoint_control, breakpoint_data);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    CHECK_SUCCESS(status);

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    CHECK_SUCCESS(status);

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::get_context_switch_breakpoint_status(Device &device, uint8_t breakpoint_id,
        CONTROL_PROTOCOL__context_switch_debug_sys_status_t *breakpoint_status)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = sizeof(response_buffer);
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    CONTROL_PROTOCOL__get_context_switch_breakpoint_status_response_t *get_context_switch_breakpoint_status_response = NULL;

    RETURN_IF_ARG_NULL(breakpoint_status);

    common_status = CONTROL_PROTOCOL__pack_get_context_switch_breakpoint_status_request(
            &request, &request_size, device.get_control_sequence(), breakpoint_id);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    CHECK_SUCCESS(status);

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    CHECK_SUCCESS(status);

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    get_context_switch_breakpoint_status_response =
        (CONTROL_PROTOCOL__get_context_switch_breakpoint_status_response_t *)(payload->parameters);

    memcpy(breakpoint_status,
            &(get_context_switch_breakpoint_status_response->breakpoint_status),
            sizeof((*breakpoint_status)));
    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::get_context_switch_main_header(Device &device, CONTROL_PROTOCOL__context_switch_main_header_t *main_header)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = sizeof(response_buffer);
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    CONTROL_PROTOCOL__get_context_switch_main_header_response_t *get_context_switch_main_header_response = NULL;

    RETURN_IF_ARG_NULL(main_header);

    common_status = CONTROL_PROTOCOL__pack_get_context_switch_main_header_request(
            &request, &request_size, device.get_control_sequence());
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    CHECK_SUCCESS(status);

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    CHECK_SUCCESS(status);

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
            &request, device);
    if (HAILO_SUCCESS != status) {
        goto exit;
    }

    get_context_switch_main_header_response =
        (CONTROL_PROTOCOL__get_context_switch_main_header_response_t *)(payload->parameters);

    memcpy(main_header,
            &(get_context_switch_main_header_response->main_header),
            sizeof((*main_header)));

    status = HAILO_SUCCESS;
exit:
    return status;
}

hailo_status Control::config_context_switch_timestamp(Device &device, uint32_t batch_index, bool enable_user_configuration)
{
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = sizeof(response_buffer);
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    auto common_status = CONTROL_PROTOCOL__pack_config_context_switch_timestamp_request(
        &request, &request_size, device.get_control_sequence(), batch_index, enable_user_configuration);
    auto status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    CHECK_SUCCESS(status);

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    CHECK_SUCCESS(status);

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
        &request, device);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::test_chip_memories(Device &device)
{
    uint32_t top_bypass_bitmap = 0;
    hailo_status status = HAILO_UNINITIALIZED;

    /*cluster bypass and index are irrelevant for top*/
    uint32_t cluster_bypass_bitmap_0 = 0;
    uint32_t cluster_bypass_bitmap_1 = 0;

    for (size_t mem_block = 0; mem_block <  CONTROL_PROTOCOL__TOP_NUM_MEM_BLOCKS; mem_block++) {
        /*only run test on allowed blocks */
        if (0 == (CONTROL_PROTOCOL__BIST_TOP_WHITELIST & (1 << mem_block))) {
            continue;
        }
        top_bypass_bitmap = CONTROL_PROTOCOL__BIST_TOP_BYPASS_ALL_MASK ^ (1 << mem_block);
        auto block_status = run_bist_test(device, true, top_bypass_bitmap, 0, cluster_bypass_bitmap_0, cluster_bypass_bitmap_1);
        if (HAILO_SUCCESS != block_status) {
            LOGGER__ERROR("bist test failed on memory block {}", mem_block);
            status = block_status;
        }
    }

    for (uint8_t cluster_index = 0; cluster_index < CONTROL_PROTOCOL_NUM_BIST_CLUSTER_STEPS; cluster_index++) {
        /*top bypass irrelevant for clusters*/
        top_bypass_bitmap = 0;
        /*run on all memory blocks, bypass = 0*/
        cluster_bypass_bitmap_0 = 0;
        cluster_bypass_bitmap_1 = 0;
        auto cluster_status = run_bist_test(device, false, top_bypass_bitmap, cluster_index, cluster_bypass_bitmap_0, cluster_bypass_bitmap_1);
        if (HAILO_SUCCESS != cluster_status) {
            LOGGER__ERROR("bist test failed on cluster block {}", cluster_index);
            status = cluster_status;
        }
    }

    /*No errors encountered*/
    if (HAILO_UNINITIALIZED == status){
        status = HAILO_SUCCESS;
    }

    return status;
}

hailo_status Control::run_bist_test(Device &device, bool is_top_test, uint32_t top_bypass_bitmap,
                     uint8_t cluster_index, uint32_t cluster_bypass_bitmap_0, uint32_t cluster_bypass_bitmap_1)
{
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = sizeof(response_buffer);
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    auto common_status = CONTROL_PROTOCOL__pack_run_bist_test_request(
        &request, &request_size, device.get_control_sequence(),
        is_top_test, top_bypass_bitmap, cluster_index, cluster_bypass_bitmap_0, cluster_bypass_bitmap_1);
    auto status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    CHECK_SUCCESS(status);

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    CHECK_SUCCESS(status);

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
        &request, device);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::set_sleep_state(Device &device, hailo_sleep_state_t sleep_state)
{
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = sizeof(response_buffer);
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;

    auto common_status = CONTROL_PROTOCOL__pack_set_sleep_state_request(
        &request, &request_size, device.get_control_sequence(), static_cast<uint8_t>(sleep_state));
    auto status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    CHECK_SUCCESS(status);

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    CHECK_SUCCESS(status);

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
        &request, device);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::change_hw_infer_status(Device &device, CONTROL_PROTOCOL__hw_infer_state_t state,
    uint8_t network_group_index, uint16_t dynamic_batch_size, uint16_t batch_count,
    CONTROL_PROTOCOL__hw_infer_channels_info_t *channels_info, CONTROL_PROTOCOL__hw_only_infer_results_t *results,
    CONTROL_PROTOCOL__boundary_channel_mode_t boundary_channel_mode)
{
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = sizeof(response_buffer);
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    CONTROL_PROTOCOL__change_hw_infer_status_response_t *change_hw_infer_status_response = NULL;

    RETURN_IF_ARG_NULL(results);

    auto common_status = CONTROL_PROTOCOL__pack_change_hw_infer_status_request(
        &request, &request_size, device.get_control_sequence(), static_cast<uint8_t>(state),
        network_group_index, dynamic_batch_size, batch_count, channels_info, boundary_channel_mode);
    auto status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    CHECK_SUCCESS(status);

    status = device.fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    CHECK_SUCCESS(status);

    /* Parse response */
    status = parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload,
        &request, device);
    CHECK_SUCCESS(status);

    change_hw_infer_status_response = (CONTROL_PROTOCOL__change_hw_infer_status_response_t *)(payload->parameters);

    memcpy(results, &(change_hw_infer_status_response->results), sizeof((*results)));

    return HAILO_SUCCESS;
}

hailo_status Control::start_hw_only_infer(Device &device, uint8_t network_group_index, uint16_t dynamic_batch_size,
    uint16_t batch_count, CONTROL_PROTOCOL__hw_infer_channels_info_t *channels_info,
    CONTROL_PROTOCOL__boundary_channel_mode_t boundary_channel_mode)
{
    CONTROL_PROTOCOL__hw_only_infer_results_t results = {};
    return Control::change_hw_infer_status(device, CONTROL_PROTOCOL__HW_INFER_STATE_START,
        network_group_index, dynamic_batch_size, batch_count, channels_info ,&results, boundary_channel_mode);
}

hailo_status Control::stop_hw_only_infer(Device &device, CONTROL_PROTOCOL__hw_only_infer_results_t *results)
{
    const uint8_t DEFAULT_NETWORK_GROUP = 0;
    const uint16_t DEFAULT_DYNAMIC_BATCH_SIZE = 1;
    const uint16_t DEFAULT_BATCH_COUNT = 1;
    const CONTROL_PROTOCOL__boundary_channel_mode_t DEFAULT_BOUNDARY_TYPE = CONTROL_PROTOCOL__DESC_BOUNDARY_CHANNEL;
    CONTROL_PROTOCOL__hw_infer_channels_info_t channels_info_default = {};
    return Control::change_hw_infer_status(device, CONTROL_PROTOCOL__HW_INFER_STATE_STOP,
        DEFAULT_NETWORK_GROUP, DEFAULT_DYNAMIC_BATCH_SIZE, DEFAULT_BATCH_COUNT, &channels_info_default, results,
        DEFAULT_BOUNDARY_TYPE);
}

} /* namespace hailort */
