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
#include "logger_level.h"

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

static const char *g_fw_control__textual_format[HAILO_CONTROL_OPCODE_COUNT] = {
#define CONTROL_PROTOCOL__OPCODE_X(name, is_critical, cpu_id) #name,
    CONTROL_PROTOCOL__OPCODES_VARIABLES
#undef CONTROL_PROTOCOL__OPCODE_X
};


typedef std::array<std::array<float64_t, CONTROL_PROTOCOL__POWER_MEASUREMENT_TYPES__COUNT>, CONTROL_PROTOCOL__DVM_OPTIONS_COUNT> power_conversion_multiplier_t;

static const char *get_textual_opcode(CONTROL_PROTOCOL__OPCODE_t opcode)
{
    return g_fw_control__textual_format[opcode];
}

static hailo_status log_and_return_fw_error(const Device &device, CONTROL_PROTOCOL__status_t fw_status, CONTROL_PROTOCOL__OPCODE_t opcode)
{
    if (fw_status.major_status == 0) {
        return HAILO_SUCCESS;
    }

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
            get_textual_opcode(opcode), dev_arch_str);
    }

    if ((CONTROL_PROTOCOL_STATUS_UNSUPPORTED_DEVICE == fw_status.minor_status) ||
        (CONTROL_PROTOCOL_STATUS_UNSUPPORTED_DEVICE == fw_status.major_status)) {
        LOGGER__ERROR("Opcode {} is not supported on the current board.", get_textual_opcode(opcode));
        return HAILO_UNSUPPORTED_OPCODE;
    }

    if ((HAILO_CONTROL_STATUS_UNSUPPORTED_OPCODE == fw_status.minor_status) ||
        (HAILO_CONTROL_STATUS_UNSUPPORTED_OPCODE == fw_status.major_status)) {
        LOGGER__ERROR("Opcode {} is not supported", get_textual_opcode(opcode));
        return HAILO_UNSUPPORTED_OPCODE;
    }

    return HAILO_FW_CONTROL_FAILURE;
}

hailo_status Control::fw_interact(Device &device, CONTROL_PROTOCOL__request_t *req, size_t req_size, CONTROL_PROTOCOL__response_t *resp)
{
    hailo_status status = device.fw_interact((uint8_t*)req, req_size, (uint8_t*)resp);
    CHECK_SUCCESS(status);

    CONTROL_PROTOCOL__OPCODE_t opcode = static_cast<CONTROL_PROTOCOL__OPCODE_t>(BYTE_ORDER__dtohl(req->opcode));

    return log_and_return_fw_error(device, resp->status, opcode);
}

Expected<hailo_device_identity_t> Control::identify(Device &device)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_identify_request(&request);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    const CONTROL_PROTOCOL_identify_response_t *identify_response =
        CONTROL_PROTOCOL__unpack_identify_response(&response.parameters.identity_response);

    hailo_device_identity_t board_info = {};
    board_info.protocol_version = identify_response->protocol_version;
    board_info.logger_version = identify_response->logger_version;
    memcpy(&board_info.fw_version, &identify_response->fw_version, sizeof(board_info.fw_version));
    board_info.serial_number_length = static_cast<uint8_t>(identify_response->serial_number_length);
    memcpy(board_info.serial_number, identify_response->serial_number, identify_response->serial_number_length);
    board_info.part_number_length = static_cast<uint8_t>(identify_response->part_number_length);
    memcpy(board_info.part_number, identify_response->part_number, identify_response->part_number_length);
    board_info.product_name_length = static_cast<uint8_t>(identify_response->product_name_length);
    memcpy(board_info.product_name, identify_response->product_name, identify_response->product_name_length);

    board_info.is_release = !IS_REVISION_DEV(board_info.fw_version.revision);
    board_info.extended_context_switch_buffer =
        IS_REVISION_EXTENDED_CONTEXT_SWITCH_BUFFER(board_info.fw_version.revision);

    CHECK(0 == (board_info.fw_version.revision & REVISION_APP_CORE_FLAG_BIT_MASK), HAILO_INVALID_FIRMWARE,
        "Got invalid app FW type, which means the FW was not marked correctly. unmaked FW revision {}",
        board_info.fw_version.revision);

    board_info.fw_version.revision = GET_REVISION_NUMBER_VALUE(board_info.fw_version.revision);
    board_info.device_architecture = static_cast<hailo_device_architecture_t>(identify_response->device_architecture);

    // FW always returns HAILO_ARCH_HAILO15H for either HAILO15H/HAILO15M; the SCU fuse file gives us the actual type.
    if (HAILO_ARCH_HAILO15H == board_info.device_architecture) {
        TRY(const hailo_device_architecture_t dev_arch,
            PartialClusterReader::get_actual_dev_arch_from_fuse(board_info.device_architecture));
        board_info.device_architecture = dev_arch;
    }

#ifdef __linux__
    if (Device::Type::INTEGRATED == device.get_type()) {
        char hostname[HOST_NAME_MAX + 1];
        CHECK(0 == gethostname(hostname, HOST_NAME_MAX + 1), HAILO_INTERNAL_FAILURE, "Failed to get hostname");
        if (std::string(hostname).find("hailo10") != std::string::npos) {
            board_info.device_architecture = HAILO_ARCH_HAILO10H;
        }
    }
#endif

    LOGGER__INFO("firmware_version is: {}.{}.{}",
        board_info.fw_version.major, board_info.fw_version.minor, board_info.fw_version.revision);

    return board_info;
}

hailo_status Control::core_identify(Device &device, hailo_core_information_t *core_info)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_core_identify_request(&request);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    const CONTROL_PROTOCOL__core_identify_response_t *core_identify_response =
        CONTROL_PROTOCOL__unpack_core_identify_response(&response.parameters.core_identity_response);

    memcpy(&core_info->fw_version, &core_identify_response->fw_version, sizeof(core_info->fw_version));
    core_info->is_release = !IS_REVISION_DEV(core_info->fw_version.revision);
    core_info->extended_context_switch_buffer =
        IS_REVISION_EXTENDED_CONTEXT_SWITCH_BUFFER(core_info->fw_version.revision);

    CHECK(REVISION_APP_CORE_FLAG_BIT_MASK == (core_info->fw_version.revision & REVISION_APP_CORE_FLAG_BIT_MASK),
        HAILO_INVALID_FIRMWARE,
        "Got invalid core FW type, which means the FW was not marked correctly. unmaked FW revision {}",
        core_info->fw_version.revision);

    core_info->fw_version.revision = GET_REVISION_NUMBER_VALUE(core_info->fw_version.revision);

    LOGGER__INFO("core firmware_version is: {}.{}.{}",
        core_info->fw_version.major, core_info->fw_version.minor, core_info->fw_version.revision);

    return HAILO_SUCCESS;
}

static_assert((uint32_t)FW_LOGGER_LEVEL_TRACE == (uint32_t)HAILO_FW_LOGGER_LEVEL_TRACE,
    "mismatch in FW_LOGGER_LEVEL_TRACE and HAILO_FW_LOGGER_LEVEL_TRACE");
static_assert((uint32_t)FW_LOGGER_LEVEL_DEBUG == (uint32_t)HAILO_FW_LOGGER_LEVEL_DEBUG,
    "mismatch in FW_LOGGER_LEVEL_DEBUG and HAILO_FW_LOGGER_LEVEL_DEBUG");
static_assert((uint32_t)FW_LOGGER_LEVEL_INFO == (uint32_t)HAILO_FW_LOGGER_LEVEL_INFO,
    "mismatch in FW_LOGGER_LEVEL_INFO and HAILO_FW_LOGGER_LEVEL_INFO");
static_assert((uint32_t)FW_LOGGER_LEVEL_WARN == (uint32_t)HAILO_FW_LOGGER_LEVEL_WARN,
    "mismatch in FW_LOGGER_LEVEL_WARN and HAILO_FW_LOGGER_LEVEL_WARN");
static_assert((uint32_t)FW_LOGGER_LEVEL_ERROR == (uint32_t)HAILO_FW_LOGGER_LEVEL_ERROR,
    "mismatch in FW_LOGGER_LEVEL_ERROR and HAILO_FW_LOGGER_LEVEL_ERROR");
static_assert((uint32_t)FW_LOGGER_LEVEL_FATAL == (uint32_t)HAILO_FW_LOGGER_LEVEL_FATAL,
    "mismatch in FW_LOGGER_LEVEL_FATAL and HAILO_FW_LOGGER_LEVEL_FATAL");
static_assert((uint32_t)CONTROL_PROTOCOL__INTERFACE_PCIE == (uint32_t)HAILO_FW_LOGGER_INTERFACE_PCIE,
    "mismatch in CONTROL_PROTOCOL__INTERFACE_PCIE and HAILO_FW_LOGGER_INTERFACE_PCIE");
static_assert((uint32_t)CONTROL_PROTOCOL__INTERFACE_UART == (uint32_t)HAILO_FW_LOGGER_INTERFACE_UART,
    "mismatch in CONTROL_PROTOCOL__INTERFACE_UART and HAILO_FW_LOGGER_INTERFACE_UART");

hailo_status Control::set_fw_logger(Device &device, hailo_fw_logger_level_t level, uint32_t interface_mask)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_set_fw_logger_request(&request,
        static_cast<uint8_t>(level), static_cast<uint8_t>(interface_mask));

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::set_clock_freq(Device &device, uint32_t clock_freq)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_set_clock_freq_request(&request, clock_freq);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::set_throttling_state(Device &device, bool should_activate)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_set_throttling_state_request(&request, should_activate);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

Expected<bool> Control::get_throttling_state(Device &device)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_get_throttling_state_request(&request);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    const CONTROL_PROTOCOL__get_throttling_state_response_t *throttling_response =
        CONTROL_PROTOCOL__unpack_get_throttling_state_response(&response.parameters.get_throttling_state_response);

    return Expected<bool>(throttling_response->is_active);
}

hailo_status Control::set_overcurrent_state(Device &device, bool should_activate)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_set_overcurrent_state_request(&request, should_activate);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

Expected<bool> Control::get_overcurrent_state(Device &device)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_get_overcurrent_state_request(&request);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    const CONTROL_PROTOCOL__get_overcurrent_state_response_t *overcurrent_response =
        CONTROL_PROTOCOL__unpack_get_overcurrent_state_response(&response.parameters.get_overcurrent_state_response);

    return Expected<bool>(overcurrent_response->is_required);
}

Expected<CONTROL_PROTOCOL__hw_consts_t> Control::get_hw_consts(Device &device)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_get_hw_consts_request(&request);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    const CONTROL_PROTOCOL__get_hw_consts_response_t *hw_consts_response =
        CONTROL_PROTOCOL__unpack_get_hw_consts_response(&response.parameters.get_hw_consts_response);

    return Expected<CONTROL_PROTOCOL__hw_consts_t>(hw_consts_response->hw_consts);
}

hailo_status Control::write_memory_chunk(Device &device, uint32_t address, const uint8_t *data, uint32_t chunk_size)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    ASSERT(NULL != data);
    ASSERT(CONTROL__MAX_WRITE_MEMORY_CHUNK_SIZE >= chunk_size);
    ASSERT(0 != chunk_size);

    const size_t request_size = CONTROL_PROTOCOL__pack_write_memory_request(&request, address, data, chunk_size);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
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
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    ASSERT(NULL != data);
    ASSERT(CONTROL__MAX_WRITE_MEMORY_CHUNK_SIZE >= chunk_size);
    ASSERT(0 != chunk_size);

    const size_t request_size = CONTROL_PROTOCOL__pack_read_memory_request(&request, address, chunk_size);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    const CONTROL_PROTOCOL__read_memory_response_t *read_memory_response =
        CONTROL_PROTOCOL__unpack_read_memory_response(&response.parameters.read_memory_response);
    CHECK(chunk_size == read_memory_response->data_length, HAILO_INVALID_CONTROL_RESPONSE,
        "Did not read all data from control response");
    memcpy(data, &read_memory_response->data[0], read_memory_response->data_length);

    return HAILO_SUCCESS;
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
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_open_stream_request(&request, dataflow_manager_id, is_input);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::close_stream(Device &device, uint8_t dataflow_manager_id, bool is_input)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_close_stream_request(&request, dataflow_manager_id, is_input);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
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

hailo_status Control::config_stream_mipi_input(Device &device, CONTROL_PROTOCOL__config_stream_request_t *params, uint8_t &dataflow_manager_id)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_config_stream_mipi_input_request(&request, params);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    const CONTROL_PROTOCOL__config_stream_response_t *config_stream_response =
        CONTROL_PROTOCOL__unpack_config_stream_response(&response.parameters.config_stream_response);
    dataflow_manager_id = config_stream_response->dataflow_manager_id;

    return HAILO_SUCCESS;
}

hailo_status Control::config_stream_mipi_output(Device &device, CONTROL_PROTOCOL__config_stream_request_t *params, uint8_t &dataflow_manager_id)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_config_stream_mipi_output_request(&request, params);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    const CONTROL_PROTOCOL__config_stream_response_t *config_stream_response =
        CONTROL_PROTOCOL__unpack_config_stream_response(&response.parameters.config_stream_response);
    dataflow_manager_id = config_stream_response->dataflow_manager_id;

    return HAILO_SUCCESS;
}

hailo_status Control::config_stream_pcie_input(Device &device, CONTROL_PROTOCOL__config_stream_request_t *params, uint8_t &dataflow_manager_id)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_config_stream_pcie_input_request(&request, params);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    const CONTROL_PROTOCOL__config_stream_response_t *config_stream_response =
        CONTROL_PROTOCOL__unpack_config_stream_response(&response.parameters.config_stream_response);
    dataflow_manager_id = config_stream_response->dataflow_manager_id;

    return HAILO_SUCCESS;
}

hailo_status Control::config_stream_pcie_output(Device &device, CONTROL_PROTOCOL__config_stream_request_t *params, uint8_t &dataflow_manager_id)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_config_stream_pcie_output_request(&request, params);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    const CONTROL_PROTOCOL__config_stream_response_t *config_stream_response =
        CONTROL_PROTOCOL__unpack_config_stream_response(&response.parameters.config_stream_response);
    dataflow_manager_id = config_stream_response->dataflow_manager_id;

    return HAILO_SUCCESS;
}

// TODO: needed?
hailo_status Control::power_measurement(Device &device, CONTROL_PROTOCOL__dvm_options_t dvm,
    CONTROL_PROTOCOL__power_measurement_types_t measurement_type, float32_t *measurement)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_power_measurement_request(&request, dvm, measurement_type);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    const CONTROL_PROTOCOL__power_measurement_response_t *measure_power_response =
        CONTROL_PROTOCOL__unpack_power_measurement_response(&response.parameters.measure_power_response);
    LOGGER__INFO("The chosen dvm type is: {}, and measurement type: {}", measure_power_response->dvm,
        measure_power_response->measurement_type);
    if (CONTROL_PROTOCOL__DVM_OPTIONS_OVERCURRENT_PROTECTION == measure_power_response->dvm) {
        LOGGER__WARN(OVERCURRENT_PROTECTION_WARNING);
    }
    *measurement = measure_power_response->power_measurement;

    return HAILO_SUCCESS;
}

hailo_status Control::set_power_measurement(Device &device, hailo_measurement_buffer_index_t buffer_index, CONTROL_PROTOCOL__dvm_options_t dvm,
    CONTROL_PROTOCOL__power_measurement_types_t measurement_type)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    CHECK(CONTROL_PROTOCOL__MAX_NUMBER_OF_POWER_MEASUREMETS > buffer_index,
        HAILO_INVALID_ARGUMENT, "Invalid power measurement index {}", static_cast<int>(buffer_index));

    const size_t request_size = CONTROL_PROTOCOL__pack_set_power_measurement_request(&request, buffer_index,
        dvm, measurement_type);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    const CONTROL_PROTOCOL__set_power_measurement_response_t *set_measure_power_response =
        CONTROL_PROTOCOL__unpack_set_power_measurement_response(&response.parameters.set_measure_power_response);
    LOGGER__INFO("The chosen dvm type is: {}, and measurement type: {}", set_measure_power_response->dvm,
        set_measure_power_response->measurement_type);
    if (CONTROL_PROTOCOL__DVM_OPTIONS_OVERCURRENT_PROTECTION == set_measure_power_response->dvm) {
        LOGGER__WARN(OVERCURRENT_PROTECTION_WARNING);
    }

    return HAILO_SUCCESS;
}

hailo_status Control::get_power_measurement(Device &device, hailo_measurement_buffer_index_t buffer_index, bool should_clear,
    hailo_power_measurement_data_t *measurement_data)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    CHECK(CONTROL_PROTOCOL__MAX_NUMBER_OF_POWER_MEASUREMETS > buffer_index,
        HAILO_INVALID_ARGUMENT, "Invalid power measurement index {}", static_cast<int>(buffer_index));

    const size_t request_size = CONTROL_PROTOCOL__pack_get_power_measurement_request(&request, buffer_index,
        should_clear);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    const CONTROL_PROTOCOL__get_power_measurement_response_t *get_measure_power_response =
        CONTROL_PROTOCOL__unpack_get_power_measurement_response(&response.parameters.get_measure_power_response);
    measurement_data->average_time_value_milliseconds = get_measure_power_response->average_time_value_milliseconds;
    measurement_data->average_value = get_measure_power_response->average_value;
    measurement_data->min_value = get_measure_power_response->min_value;
    measurement_data->max_value = get_measure_power_response->max_value;
    measurement_data->total_number_of_samples = get_measure_power_response->total_number_of_samples;

    return HAILO_SUCCESS;
}

hailo_status Control::start_power_measurement(Device &device,
    CONTROL_PROTOCOL__averaging_factor_t averaging_factor, CONTROL_PROTOCOL__sampling_period_t sampling_period)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    uint32_t delay_milliseconds = POWER_MEASUREMENT_DELAY_MS(sampling_period, averaging_factor);
    // There is no logical way that measurement delay can be 0 - because sampling_period and averaging_factor cant be 0
    // Hence if it is 0 - it means it was 0.xx and we want to round up to 1 in that case
    if (0 == delay_milliseconds) {
        delay_milliseconds = 1;
    }

    const size_t request_size = CONTROL_PROTOCOL__pack_start_power_measurement_request(&request,
        delay_milliseconds, averaging_factor, sampling_period);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::stop_power_measurement(Device &device)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_stop_power_measurement_request(&request);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::i2c_write(Device &device, const hailo_i2c_slave_config_t *slave_config, uint32_t register_address,
        const uint8_t *data, uint32_t length)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_i2c_write_request(&request, register_address,
        static_cast<uint8_t>(slave_config->endianness), slave_config->slave_address,
        slave_config->register_address_size, slave_config->bus_index, data, length);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::i2c_read(Device &device, const hailo_i2c_slave_config_t *slave_config, uint32_t register_address,
        uint8_t *data, uint32_t length)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_i2c_read_request(&request, register_address,
        static_cast<uint8_t>(slave_config->endianness), slave_config->slave_address,
        slave_config->register_address_size, slave_config->bus_index, length, slave_config->should_hold_bus);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    const CONTROL_PROTOCOL__i2c_read_response_t *i2c_read_response =
        CONTROL_PROTOCOL__unpack_i2c_read_response(&response.parameters.i2c_read_response);
    CHECK(i2c_read_response->data_length == length, HAILO_INVALID_CONTROL_RESPONSE,
        "Read data size from I2C does not match register size. ({} != {})", i2c_read_response->data_length, length);
    memcpy(data, i2c_read_response->data, i2c_read_response->data_length);

    return HAILO_SUCCESS;
}

hailo_status Control::config_core_top(Device &device, CONTROL_PROTOCOL__config_core_top_type_t config_type,
    CONTROL_PROTOCOL__config_core_top_params_t *params)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_config_core_top_request(&request, config_type, params);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::phy_operation(Device &device, CONTROL_PROTOCOL__phy_operation_t operation_type)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_phy_operation_request(&request, operation_type);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::examine_user_config(Device &device, hailo_fw_user_config_information_t *info)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_examine_user_config_request(&request);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    const CONTROL_PROTOCOL__examine_user_config_response_t *examine_user_config_response =
        CONTROL_PROTOCOL__unpack_examine_user_config_response(&response.parameters.examine_user_config_response);
    info->version = examine_user_config_response->version;
    info->entry_count = examine_user_config_response->entry_count;
    info->total_size = examine_user_config_response->total_size;

    return HAILO_SUCCESS;
}

hailo_status Control::read_user_config_chunk(Device &device, uint32_t read_offset, uint32_t read_length,
    uint8_t *buffer, uint32_t *actual_read_data_length)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_read_user_config_request(&request, read_offset, read_length);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    const CONTROL_PROTOCOL__read_user_config_response_t *read_user_config_response =
        CONTROL_PROTOCOL__unpack_read_user_config_response(&response.parameters.read_user_config_response);
    *actual_read_data_length = read_user_config_response->data_length;
    memcpy(buffer, read_user_config_response->data, *actual_read_data_length);

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
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_write_user_config_request(&request, offset,
        data + offset, chunk_size);

    hailo_status status = fw_interact(device, &request, request_size, &response);
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
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_erase_user_config_request(&request);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::read_board_config(Device &device, uint8_t *buffer, uint32_t buffer_length)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};
    const uint32_t read_offset = 0;

    CHECK(buffer_length >= BOARD_CONFIG_SIZE, HAILO_INSUFFICIENT_BUFFER,
        "read buffer is too small. provided buffer size: {} bytes, board config size: {} bytes", buffer_length,
        BOARD_CONFIG_SIZE);

    LOGGER__INFO("Preparing to read board configuration");
    const size_t request_size = CONTROL_PROTOCOL__pack_read_board_config_request(&request, read_offset, BOARD_CONFIG_SIZE);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    const CONTROL_PROTOCOL__read_board_config_response_t *read_board_config_response =
        CONTROL_PROTOCOL__unpack_read_board_config_response(&response.parameters.read_board_config_response);
    memcpy(buffer, read_board_config_response->data, read_board_config_response->data_length);

    return HAILO_SUCCESS;
}

hailo_status Control::write_board_config(Device &device, const uint8_t *data, uint32_t data_length)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};
    const uint32_t write_offset = 0;

    CHECK(BOARD_CONFIG_SIZE >= data_length, HAILO_INVALID_OPERATION,
        "Invalid size of board config. data_length={}, max_size={}", data_length, BOARD_CONFIG_SIZE);

    const size_t request_size = CONTROL_PROTOCOL__pack_write_board_config_request(&request, write_offset,
        data + write_offset, data_length);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::write_second_stage_to_internal_memory(Device &device, uint32_t offset, uint8_t *data, uint32_t data_length)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_write_second_stage_to_internal_memory_request(&request,
        offset, data, data_length);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::copy_second_stage_to_flash(Device &device, MD5_SUM_t *expected_md5, uint32_t second_stage_size)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_copy_second_stage_to_flash_request(&request, expected_md5,
        second_stage_size);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::start_firmware_update(Device &device)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_start_firmware_update_request(&request);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::finish_firmware_update(Device &device)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_finish_firmware_update_request(&request);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::write_firmware_update(Device &device, uint32_t offset, const uint8_t *data, uint32_t data_length)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_write_firmware_update_request(&request, offset,
        data, data_length);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::validate_firmware_update(Device &device, MD5_SUM_t *expected_md5, uint32_t firmware_size)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_validate_firmware_update_request(&request, expected_md5,
        firmware_size);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::latency_measurement_read(Device &device, uint32_t *inbound_to_outbound_latency_nsec)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_latency_measurement_read_request(&request);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    const CONTROL_PROTOCOL__latency_read_response_t *latency_read_response =
        CONTROL_PROTOCOL__unpack_latency_read_response(&response.parameters.latency_read_response);
    *inbound_to_outbound_latency_nsec = latency_read_response->inbound_to_outbound_latency_nsec;

    return HAILO_SUCCESS;
}

hailo_status Control::latency_measurement_config(Device &device, uint8_t latency_measurement_en,
    uint32_t inbound_start_buffer_number, uint32_t outbound_stop_buffer_number, uint32_t inbound_stream_index,
    uint32_t outbound_stream_index)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_latency_measurement_config_request(&request,
        latency_measurement_en, inbound_start_buffer_number, outbound_stop_buffer_number,
        inbound_stream_index, outbound_stream_index);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::sensor_store_config(Device &device, uint32_t is_first, uint32_t section_index,
    uint32_t start_offset, uint32_t reset_data_size, uint32_t sensor_type, uint32_t total_data_size, uint8_t *data,
    uint32_t data_length, uint16_t config_height, uint16_t config_width, uint16_t config_fps,
    uint32_t config_name_length, uint8_t *config_name)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_sensor_store_config_request(&request, is_first, section_index,
        start_offset, reset_data_size, sensor_type, total_data_size, data, data_length, config_height,
        config_width, config_fps, config_name_length, config_name);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::sensor_set_i2c_bus_index(Device &device, uint32_t sensor_type, uint32_t bus_index)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_sensor_set_i2c_bus_index_request(&request, sensor_type,
        bus_index);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::sensor_load_and_start_config(Device &device, uint32_t section_index)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_sensor_load_and_start_config_request(&request, section_index);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::sensor_reset(Device &device, uint32_t section_index)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_sensor_reset_request(&request, section_index);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::sensor_set_generic_i2c_slave(Device &device, uint16_t slave_address,
    uint8_t register_address_size, uint8_t bus_index, uint8_t should_hold_bus, uint8_t endianness)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_sensor_set_generic_i2c_slave_request(&request, slave_address,
        register_address_size, bus_index, should_hold_bus, endianness);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::sensor_get_config(Device &device, uint32_t section_index, uint32_t offset, uint32_t data_length,
    uint8_t *data)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_sensor_get_config_request(&request, section_index, offset,
        data_length);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    const CONTROL_PROTOCOL__sensor_get_config_response_t *sensor_get_config_response =
        CONTROL_PROTOCOL__unpack_sensor_get_config_response(&response.parameters.sensor_get_config_response);
    CHECK(data_length == sensor_get_config_response->data_length, HAILO_INVALID_CONTROL_RESPONSE,
        "Did not read all data from control response");
    memcpy(data, &sensor_get_config_response->data[0], sensor_get_config_response->data_length);

    return HAILO_SUCCESS;
}

hailo_status Control::sensor_get_sections_info(Device &device, uint8_t *data)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_sensor_get_sections_info_request(&request);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    const CONTROL_PROTOCOL__sensor_get_sections_info_response_t *get_sections_info_response =
        CONTROL_PROTOCOL__unpack_sensor_get_sections_info_response(&response.parameters.sensor_get_sections_info_response);
    CHECK(0 != get_sections_info_response->data_length, HAILO_INVALID_CONTROL_RESPONSE,
        "Did not read all data from control response");
    memcpy(data, &get_sections_info_response->data[0], get_sections_info_response->data_length);

    return HAILO_SUCCESS;
}

hailo_status Control::context_switch_set_network_group_header(Device &device,
    const CONTROL_PROTOCOL__application_header_t &network_group_header)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_context_switch_set_network_group_header_request(&request,
        &network_group_header);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::context_switch_set_context_info_chunk(Device &device,
    const CONTROL_PROTOCOL__context_switch_context_info_chunk_t &context_info)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_context_switch_set_context_info_request(&request,
        &context_info);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    if (HAILO_SUCCESS != status) {
        /* In case of max memory error, add LOGGER ERROR, and set indicative error to the user */
        CHECK(CONTEXT_SWITCH_STATUS_SRAM_MEMORY_FULL != BYTE_ORDER__dtohl(response.status.major_status),
            HAILO_OUT_OF_FW_MEMORY, "Configured network groups reached maximum device internal memory (SRAM Full).");
        return status;
    }

    return HAILO_SUCCESS;
}

hailo_status Control::context_switch_signal_cache_updated(Device &device)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_context_switch_signal_cache_updated_request(&request);

    hailo_status status = fw_interact(device, &request, request_size, &response);
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
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_idle_time_get_measuremment_request(&request);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    const CONTROL_PROTOCOL__idle_time_get_measurement_response_t *idle_response =
        CONTROL_PROTOCOL__unpack_idle_time_get_measurement_response(
            &response.parameters.idle_time_get_measurement_response);
    *measurement = idle_response->idle_time_ns;

    return HAILO_SUCCESS;
}

hailo_status Control::idle_time_set_measurement(Device &device, uint8_t measurement_enable)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_idle_time_set_measuremment_request(&request,
        measurement_enable);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::set_pause_frames(Device &device, uint8_t rx_pause_frames_enable)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_set_pause_frames_request(&request, rx_pause_frames_enable);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::download_context_action_list_chunk(Device &device, uint32_t network_group_id,
    CONTROL_PROTOCOL__context_switch_context_type_t context_type, uint16_t context_index,
    uint16_t action_list_offset, size_t action_list_max_size, uint32_t *base_address, uint8_t *action_list,
    uint16_t *action_list_length, bool *is_action_list_end, uint32_t *batch_counter, uint32_t *idle_time)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_download_context_action_list_request(&request,
        network_group_id, context_type, context_index, action_list_offset);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    const CONTROL_PROTOCOL__download_context_action_list_response_t *context_action_list_response =
        CONTROL_PROTOCOL__unpack_download_context_action_list_response(
            &response.parameters.download_context_action_list_response);

    CHECK(0 != context_action_list_response->action_list_length, HAILO_INVALID_CONTROL_RESPONSE,
        "Received empty action list");
    CHECK(0 != context_action_list_response->base_address, HAILO_INVALID_CONTROL_RESPONSE,
        "Received NULL pointer to base address");
    CHECK(action_list_max_size >= context_action_list_response->action_list_length, HAILO_INVALID_CONTROL_RESPONSE,
        "Received action list bigger than allocated user buffer");

    memcpy(action_list, context_action_list_response->action_list,
        context_action_list_response->action_list_length);

    *action_list_length = (uint16_t)context_action_list_response->action_list_length;
    *base_address = context_action_list_response->base_address;
    *is_action_list_end = context_action_list_response->is_action_list_end;
    *batch_counter = context_action_list_response->batch_counter;
    *idle_time = context_action_list_response->idle_time;

    return HAILO_SUCCESS;
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
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_change_context_switch_status_request(&request,
        state_machine_status, network_group_index, dynamic_batch_size, batch_count);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
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
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_wd_enable(&request, cpu_id, should_enable);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::wd_config(Device &device, uint8_t cpu_id, uint32_t wd_cycles, CONTROL_PROTOCOL__WATCHDOG_MODE_t wd_mode)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_wd_config(&request, cpu_id, wd_cycles, wd_mode);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::previous_system_state(Device &device, uint8_t cpu_id, CONTROL_PROTOCOL__system_state_t *system)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_previous_system_state_request(&request, cpu_id);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    const CONTROL_PROTOCOL__previous_system_state_response_t *previous_system_state_response =
        CONTROL_PROTOCOL__unpack_previous_system_state_response(
            &response.parameters.previous_system_state_response);
    *system = previous_system_state_response->system_state;

    return HAILO_SUCCESS;
}

hailo_status Control::set_dataflow_interrupt(Device &device, uint8_t interrupt_type, uint8_t interrupt_index,
        uint8_t interrupt_sub_index)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_set_dataflow_interrupt_request(&request, interrupt_type,
        interrupt_index, interrupt_sub_index);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::d2h_notification_manager_set_host_info(Device &device, uint16_t host_port, uint32_t host_ip_address)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};
    const auto connection_type = ((Device::Type::PCIE == device.get_type() ||
                                   Device::Type::INTEGRATED == device.get_type()) ?
        D2H_EVENT_COMMUNICATION_TYPE_VDMA : D2H_EVENT_COMMUNICATION_TYPE_UDP);

    const size_t request_size = CONTROL_PROTOCOL__pack_d2h_event_manager_set_host_info_request(&request,
        static_cast<uint8_t>(connection_type), host_port, host_ip_address);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::d2h_notification_manager_send_host_info_notification(Device &device, uint8_t notification_priority)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_d2h_event_manager_send_host_info_event_request(&request,
        notification_priority);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::clear_configured_apps(Device &device)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_context_switch_clear_configured_apps_request(&request);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::get_chip_temperature(Device &device, hailo_chip_temperature_info_t *temp_info)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_get_chip_temperature_request(&request);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    const CONTROL_PROTOCOL__get_chip_temperature_response_t *temps =
        CONTROL_PROTOCOL__unpack_get_chip_temperature_response(&response.parameters.get_chip_temperature_response);
    temp_info->sample_count = temps->info.sample_count;
    temp_info->ts0_temperature = temps->info.ts0_temperature;
    temp_info->ts1_temperature = temps->info.ts1_temperature;

    return HAILO_SUCCESS;
}

hailo_status Control::enable_debugging(Device &device, bool is_rma)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_enable_debugging_request(&request, is_rma);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

Expected<CONTROL_PROTOCOL__get_extended_device_information_response_t> Control::get_extended_device_info_response(Device &device)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_get_extended_device_information_request(&request);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    CONTROL_PROTOCOL__unpack_get_extended_device_information_response(
        &response.parameters.get_extended_device_information_response);

    return std::move(response.parameters.get_extended_device_information_response);
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
        return Expected<uint32_t>(extended_device_info_response.partial_clusters_layout_bitmap);
    }
}

Expected<hailo_extended_device_information_t> Control::get_extended_device_information(Device &device)
{
    TRY(const auto extended_device_info_response, get_extended_device_info_response(device));

    hailo_extended_device_information_t device_info;
    const uint8_t local_supported_features = (uint8_t)extended_device_info_response.supported_features;
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
    device_info.neural_network_core_clock_rate = extended_device_info_response.neural_network_core_clock_rate;

    device_info.boot_source = static_cast<hailo_device_boot_source_t>(extended_device_info_response.boot_source);

    memcpy(device_info.soc_id, extended_device_info_response.soc_id, extended_device_info_response.soc_id_length);
    device_info.lcs = extended_device_info_response.lcs;
    memcpy(&device_info.unit_level_tracking_id[0], &extended_device_info_response.fuse_info,
        sizeof(device_info.unit_level_tracking_id));
    memcpy(&device_info.eth_mac_address[0], &extended_device_info_response.eth_mac_address[0],
        extended_device_info_response.eth_mac_length);
    memcpy(&device_info.soc_pm_values, &extended_device_info_response.pd_info, sizeof(device_info.soc_pm_values));

    return device_info;
}

Expected<hailo_health_info_t> Control::get_health_information(Device &device)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_get_health_information_request(&request);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    const CONTROL_PROTOCOL__get_health_information_response_t *health_response =
        CONTROL_PROTOCOL__unpack_get_health_information_response(&response.parameters.get_health_information_response);

    hailo_health_info_t health_info;
    health_info.overcurrent_protection_active = health_response->overcurrent_protection_active;
    health_info.current_overcurrent_zone = health_response->current_overcurrent_zone;
    health_info.red_overcurrent_threshold = health_response->red_overcurrent_threshold;
    health_info.overcurrent_throttling_active = health_response->overcurrent_throttling_active;
    health_info.temperature_throttling_active = health_response->temperature_throttling_active;
    health_info.current_temperature_zone = health_response->current_temperature_zone;
    health_info.current_temperature_throttling_level = health_response->current_temperature_throttling_level;
    memcpy(&health_info.temperature_throttling_levels[0], &health_response->temperature_throttling_levels[0],
        health_response->temperature_throttling_levels_length);
    health_info.orange_temperature_threshold = health_response->orange_temperature_threshold;
    health_info.orange_hysteresis_temperature_threshold = health_response->orange_hysteresis_temperature_threshold;
    health_info.red_temperature_threshold = health_response->red_temperature_threshold;
    health_info.red_hysteresis_temperature_threshold = health_response->red_hysteresis_temperature_threshold;
    health_info.requested_overcurrent_clock_freq = health_response->requested_overcurrent_clock_freq;
    health_info.requested_temperature_clock_freq = health_response->requested_temperature_clock_freq;

    return health_info;
}

hailo_status Control::config_context_switch_breakpoint(Device &device, uint8_t breakpoint_id,
        CONTROL_PROTOCOL__context_switch_breakpoint_control_t breakpoint_control,
        CONTROL_PROTOCOL__context_switch_breakpoint_data_t *breakpoint_data)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_config_context_switch_breakpoint_request(&request,
        breakpoint_id, breakpoint_control, breakpoint_data);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::get_context_switch_breakpoint_status(Device &device, uint8_t breakpoint_id,
        CONTROL_PROTOCOL__context_switch_debug_sys_status_t *breakpoint_status)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_get_context_switch_breakpoint_status_request(&request,
        breakpoint_id);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    const CONTROL_PROTOCOL__get_context_switch_breakpoint_status_response_t *breakpoint_status_response =
        CONTROL_PROTOCOL__unpack_get_context_switch_breakpoint_status_response(
            &response.parameters.get_context_switch_breakpoint_status_response);
    memcpy(breakpoint_status, &breakpoint_status_response->breakpoint_status, sizeof(*breakpoint_status));

    return HAILO_SUCCESS;
}

hailo_status Control::get_context_switch_main_header(Device &device, CONTROL_PROTOCOL__context_switch_main_header_t *main_header)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_get_context_switch_main_header_request(&request);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    const CONTROL_PROTOCOL__get_context_switch_main_header_response_t *main_header_response =
        CONTROL_PROTOCOL__unpack_get_context_switch_main_header_response(
            &response.parameters.get_context_switch_main_header_response);
    memcpy(main_header, &main_header_response->main_header, sizeof(*main_header));

    return HAILO_SUCCESS;
}

hailo_status Control::config_context_switch_timestamp(Device &device, uint16_t batch_index, bool enable_user_configuration)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_config_context_switch_timestamp_request(&request,
        batch_index, enable_user_configuration);

    hailo_status status = fw_interact(device, &request, request_size, &response);
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
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_run_bist_test_request(&request, is_top_test,
        top_bypass_bitmap, cluster_index, cluster_bypass_bitmap_0, cluster_bypass_bitmap_1);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::set_sleep_state(Device &device, hailo_sleep_state_t sleep_state)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_set_sleep_state_request(&request,
        static_cast<uint8_t>(sleep_state));

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Control::change_hw_infer_status(Device &device, CONTROL_PROTOCOL__hw_infer_state_t state,
    uint8_t network_group_index, uint16_t dynamic_batch_size, uint16_t batch_count,
    CONTROL_PROTOCOL__hw_infer_channels_info_t *channels_info, CONTROL_PROTOCOL__hw_only_infer_results_t *results,
    CONTROL_PROTOCOL__boundary_channel_mode_t boundary_channel_mode)
{
    CONTROL_PROTOCOL__request_t request = {};
    CONTROL_PROTOCOL__response_t response = {};

    const size_t request_size = CONTROL_PROTOCOL__pack_change_hw_infer_status_request(&request,
        static_cast<uint8_t>(state), network_group_index, dynamic_batch_size, batch_count, channels_info,
        boundary_channel_mode);

    hailo_status status = fw_interact(device, &request, request_size, &response);
    CHECK_SUCCESS(status);

    const CONTROL_PROTOCOL__change_hw_infer_status_response_t *hw_infer_status_response =
        CONTROL_PROTOCOL__unpack_change_hw_infer_status_response(
            &response.parameters.change_hw_infer_status_response);
    memcpy(results, &hw_infer_status_response->results, sizeof(*results));

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
