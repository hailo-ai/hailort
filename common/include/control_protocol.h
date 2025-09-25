/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file control_protocol.h
 * @brief Defines control protocol.
**/

#ifndef __CONTROL_PROTOCOL_H__
#define __CONTROL_PROTOCOL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

#include "stdfloat.h"
#include "firmware_version.h"
#include "sensor_config_exports.h"
#include "md5.h"
#include "status.h"
#include "utils.h"
#include "user_config_common.h"


#define CONTROL_PROTOCOL__MAX_REQUEST_PAYLOAD_SIZE (1024)
#define CONTROL_PROTOCOL__MAX_READ_MEMORY_DATA_SIZE (1024)
#define CONTROL_PROTOCOL__MAX_I2C_REGISTER_SIZE (4)
#define CONTROL_PROTOCOL__MAX_BOARD_NAME_LENGTH (32)
#define CONTROL_PROTOCOL__MAX_SERIAL_NUMBER_LENGTH (16)
#define CONTROL_PROTOCOL__MAX_PART_NUMBER_LENGTH (16)
#define CONTROL_PROTOCOL__MAX_PRODUCT_NAME_LENGTH (42)
#define CONTROL_PROTOCOL__MAX_CONTEXT_SWITCH_APPLICATIONS (32)
#define CONTROL_PROTOCOL__MAX_NUMBER_OF_CLUSTERS (8)
#define CONTROL_PROTOCOL__MAX_CONTROL_LENGTH (1500)
#define CONTROL_PROTOCOL__SOC_ID_LENGTH (32)
#define CONTROL_PROTOCOL__MAX_CFG_CHANNELS (4)
#define CONTROL_PROTOCOL__MAX_NETWORKS_PER_NETWORK_GROUP (8)
#define CONTROL_PROTOCOL__MAX_VDMA_CHANNELS_PER_ENGINE (32)
#define CONTROL_PROTOCOL__MAX_VDMA_ENGINES_COUNT (3)
#define CONTROL_PROTOCOL__MAX_TOTAL_CHANNEL_COUNT \
    (CONTROL_PROTOCOL__MAX_VDMA_CHANNELS_PER_ENGINE * CONTROL_PROTOCOL__MAX_VDMA_ENGINES_COUNT)
/* Tightly coupled with the sizeof PROCESS_MONITOR__detection_results_t 
    and HAILO_SOC_PM_VALUES_BYTES_LENGTH */
#define PM_RESULTS_LENGTH (24)
/* Tightly coupled to ETHERNET_SERVICE_MAC_ADDRESS_LENGTH */
#define MAC_ADDR_BYTES_LEN (6)
#define LOT_ID_BYTES_LEN (8)

/* Tightly coupled to HAILO_MAX_TEMPERATURE_THROTTLING_LEVELS_NUMBER */
#define MAX_TEMPERATURE_THROTTLING_LEVELS_NUMBER (4)

#define MAX_OVERCURRENT_THROTTLING_LEVELS_NUMBER (8)

#define CONTROL_PROTOCOL__MAX_NUMBER_OF_POWER_MEASUREMETS (4)
#define CONTROL_PROTOCOL__DEFAULT_INIT_SAMPLING_PERIOD_US (CONTROL_PROTOCOL__PERIOD_1100US)
#define CONTROL_PROTOCOL__DEFAULT_INIT_AVERAGING_FACTOR (CONTROL_PROTOCOL__AVERAGE_FACTOR_1)

#define CONTROL_PROTOCOL__REQUEST_BASE_SIZE (sizeof(CONTROL_PROTOCOL__request_header_t) + sizeof(uint32_t))
#define CONTROL_PROTOCOL__OPCODE_INVALID  0xFFFFFFFF

/* If a control accepts a dynamic_batch_size and this value is passed, the 
 * dynamic_batch_size will be ignored. The pre-configured batch_size will be used.
 */
#define CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE (0)

// Tightly coupled with BOARD_CONFIG_supported_features_t struct
#define CONTROL_PROTOCOL__SUPPORTED_FEATURES_ETHERNET_BIT_OFFSET (0)
#define CONTROL_PROTOCOL__SUPPORTED_FEATURES_MIPI_BIT_OFFSET (1)
#define CONTROL_PROTOCOL__SUPPORTED_FEATURES_PCIE_BIT_OFFSET (2)
#define CONTROL_PROTOCOL__SUPPORTED_FEATURES_CURRENT_MONITORING_BIT_OFFSET (3)
#define CONTROL_PROTOCOL__SUPPORTED_FEATURES_MDIO_BIT_OFFSET (4)

#define CONTROL_PROTOCOL_NUM_BIST_CLUSTER_STEPS (8)

/* Value to represent an operation should be performed on all streams. */
#define CONTROL_PROTOCOL__ALL_DATAFLOW_MANAGERS (0xFF)

#define CONTROL_PROTOCOL__MAX_CONTEXT_SIZE (4096)

#define CONTROL_PROTOCOL__OPCODES_VARIABLES \
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_IDENTIFY,                                  true,  CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_WRITE_MEMORY,                              false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_READ_MEMORY,                               false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_CONFIG_STREAM,                             false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_OPEN_STREAM,                               false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_CLOSE_STREAM,                              false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_PHY_OPERATION,                             false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_RESET,                                     true, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_CONFIG_CORE_TOP,                           false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_POWER_MEASUEMENT,                          false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_SET_POWER_MEASUEMENT,                      false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_GET_POWER_MEASUEMENT,                      false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_START_POWER_MEASUEMENT,                    false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_STOP_POWER_MEASUEMENT,                     false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_START_FIRMWARE_UPDATE,                     true, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_WRITE_FIRMWARE_UPDATE,                     true, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_VALIDATE_FIRMWARE_UPDATE,                  true, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_FINISH_FIRMWARE_UPDATE,                    true, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_EXAMINE_USER_CONFIG,                       true, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_READ_USER_CONFIG,                          true, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_ERASE_USER_CONFIG,                         true, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_WRITE_USER_CONFIG,                         true, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_I2C_WRITE,                                 false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_I2C_READ,                                  false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_NN_CORE_LATENCY_MEASUREMENT_CONFIG,        false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_NN_CORE_LATENCY_MEASUREMENT_READ,          false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_SENSOR_STORE_CONFIG,                       false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_SENSOR_GET_CONFIG,                         false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_SENSOR_SET_GENERIC_I2C_SLAVE,              false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_SENSOR_LOAD_AND_START,                     false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_SENSOR_RESET,                              false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_SENSOR_GET_SECTIONS_INFO,                  false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_CONTEXT_SWITCH_SET_NETWORK_GROUP_HEADER,   false, CPU_ID_CORE_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_CONTEXT_SWITCH_SET_CONTEXT_INFO,           false, CPU_ID_CORE_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_IDLE_TIME_SET_MEASUREMENT,                 false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_IDLE_TIME_GET_MEASUREMENT,                 false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_DOWNLOAD_CONTEXT_ACTION_LIST,              false, CPU_ID_CORE_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_CHANGE_CONTEXT_SWITCH_STATUS,              false, CPU_ID_CORE_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_APP_WD_ENABLE,                             false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_APP_WD_CONFIG,                             false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_APP_PREVIOUS_SYSTEM_STATE,                 false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_SET_DATAFLOW_INTERRUPT,                    false, CPU_ID_CORE_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_CORE_IDENTIFY,                             true, CPU_ID_CORE_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_D2H_EVENT_MANAGER_SET_HOST_INFO,           false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_D2H_EVENT_MANAGER_SEND_EVENT_HOST_INFO,    false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_SWITCH_APPLICATION /* obsolete */,         false, CPU_ID_CORE_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_GET_CHIP_TEMPERATURE,                      false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_READ_BOARD_CONFIG,                         true, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_WRITE_BOARD_CONFIG,                        true, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_GET_SOC_ID /* obsolete */,                 false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_ENABLE_DEBUGGING,                          false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_GET_DEVICE_INFORMATION,                    false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_CONFIG_CONTEXT_SWITCH_BREAKPOINT,          false, CPU_ID_CORE_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_GET_CONTEXT_SWITCH_BREAKPOINT_STATUS,      false, CPU_ID_CORE_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_GET_CONTEXT_SWITCH_MAIN_HEADER,            false, CPU_ID_CORE_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_SET_FW_LOGGER,                             false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_WRITE_SECOND_STAGE_TO_INTERNAL_MEMORY,     true, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_COPY_SECOND_STAGE_TO_FLASH,                true, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_SET_PAUSE_FRAMES,                          false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_CONFIG_CONTEXT_SWITCH_TIMESTAMP,           false, CPU_ID_CORE_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_RUN_BIST_TEST,                             false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_SET_CLOCK_FREQ,                            false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_GET_HEALTH_INFORMATION,                    false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_SET_THROTTLING_STATE,                      false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_GET_THROTTLING_STATE,                      false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_SENSOR_SET_I2C_BUS_INDEX,                  false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_SET_OVERCURRENT_STATE,                     false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_GET_OVERCURRENT_STATE,                     false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_CORE_PREVIOUS_SYSTEM_STATE,                false, CPU_ID_CORE_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_CORE_WD_ENABLE,                            false, CPU_ID_CORE_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_CORE_WD_CONFIG,                            false, CPU_ID_CORE_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_CONTEXT_SWITCH_CLEAR_CONFIGURED_APPS,      false, CPU_ID_CORE_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_GET_HW_CONSTS,                             false, CPU_ID_CORE_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_SET_SLEEP_STATE,                           false, CPU_ID_APP_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_CHANGE_HW_INFER_STATUS,                    false, CPU_ID_CORE_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_SIGNAL_DRIVER_DOWN,                        false, CPU_ID_CORE_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_CONTEXT_SWITCH_INIT_CACHE_INFO /* obsolete */,            false, CPU_ID_CORE_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_CONTEXT_SWITCH_GET_CACHE_INFO /* obsolete */,             false, CPU_ID_CORE_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_CONTEXT_SWITCH_UPDATE_CACHE_READ_OFFSET /* obsolete */,   false, CPU_ID_CORE_CPU)\
    CONTROL_PROTOCOL__OPCODE_X(HAILO_CONTROL_OPCODE_CONTEXT_SWITCH_SIGNAL_CACHE_UPDATED,       false, CPU_ID_CORE_CPU)\

typedef enum {
#define CONTROL_PROTOCOL__OPCODE_X(name, is_critical, cpu_id) name,
    CONTROL_PROTOCOL__OPCODES_VARIABLES
#undef CONTROL_PROTOCOL__OPCODE_X

    /* Must be last!! */
    HAILO_CONTROL_OPCODE_COUNT
} CONTROL_PROTOCOL__OPCODE_t;

extern bool g_CONTROL_PROTOCOL__is_critical[HAILO_CONTROL_OPCODE_COUNT];

typedef enum {
    CONTROL_PROTOCOL__PROTOCOL_VERSION_INITIAL = 0,
    CONTROL_PROTOCOL__PROTOCOL_VERSION_1 = 1,
    CONTROL_PROTOCOL__PROTOCOL_VERSION_2 = 2
} CONTROL_PROTOCOL__protocol_version_t;

#define CONTROL_PROTOCOL__PROTOCOL_VERSION (CONTROL_PROTOCOL__PROTOCOL_VERSION_2)
/*Note: Must be the same as hailo_cpu_id_t in hailort.h */
typedef enum {
    CPU_ID_APP_CPU,
    CPU_ID_CORE_CPU,
    CPU_ID_UNKNOWN
} CPU_ID_t;

extern CPU_ID_t g_CONTROL_PROTOCOL__cpu_id[HAILO_CONTROL_OPCODE_COUNT];

typedef enum {
    CONTROL_PROTOCOL__ACK_UNSET = 0,
    CONTROL_PROTOCOL__ACK_SET = 1
} CONTROL_PROTOCOL__ACK_VALUES_t;

/* Note: Must be the same as hailo_dvm_options_t in hailort.h */
typedef enum DVM_options_e {
    CONTROL_PROTOCOL__DVM_OPTIONS_VDD_CORE = 0,
    CONTROL_PROTOCOL__DVM_OPTIONS_VDD_IO,
    CONTROL_PROTOCOL__DVM_OPTIONS_MIPI_AVDD,
    CONTROL_PROTOCOL__DVM_OPTIONS_MIPI_AVDD_H,
    CONTROL_PROTOCOL__DVM_OPTIONS_USB_AVDD_IO,
    CONTROL_PROTOCOL__DVM_OPTIONS_VDD_TOP,
    CONTROL_PROTOCOL__DVM_OPTIONS_USB_AVDD_IO_HV,
    CONTROL_PROTOCOL__DVM_OPTIONS_AVDD_H,
    CONTROL_PROTOCOL__DVM_OPTIONS_SDIO_VDD_IO,
    CONTROL_PROTOCOL__DVM_OPTIONS_OVERCURRENT_PROTECTION,

    /* Must be right after the physical DVMS list */
    CONTROL_PROTOCOL__DVM_OPTIONS_COUNT,
    CONTROL_PROTOCOL__DVM_OPTIONS_EVB_TOTAL_POWER = INT_MAX - 1,
    CONTROL_PROTOCOL__DVM_OPTIONS_AUTO = INT_MAX,
} CONTROL_PROTOCOL__dvm_options_t;

/* Note: Must be the same as hailo_power_measurement_types_t in hailort.h */
typedef enum POWER__measurement_types_e {
    CONTROL_PROTOCOL__POWER_MEASUREMENT_TYPES__SHUNT_VOLTAGE = 0,
    CONTROL_PROTOCOL__POWER_MEASUREMENT_TYPES__BUS_VOLTAGE,
    CONTROL_PROTOCOL__POWER_MEASUREMENT_TYPES__POWER,
    CONTROL_PROTOCOL__POWER_MEASUREMENT_TYPES__CURRENT,

    /* Must be Last! */
    CONTROL_PROTOCOL__POWER_MEASUREMENT_TYPES__COUNT,
    CONTROL_PROTOCOL__POWER_MEASUREMENT_TYPES__AUTO = INT_MAX,
} CONTROL_PROTOCOL__power_measurement_types_t;

/* Note: Must be the same as hailo_sampling_period_t in hailort.h */
typedef enum POWER__sampling_period_e {
    CONTROL_PROTOCOL__PERIOD_140US = 0,
    CONTROL_PROTOCOL__PERIOD_204US,
    CONTROL_PROTOCOL__PERIOD_332US,
    CONTROL_PROTOCOL__PERIOD_588US,
    CONTROL_PROTOCOL__PERIOD_1100US,
    CONTROL_PROTOCOL__PERIOD_2116US,
    CONTROL_PROTOCOL__PERIOD_4156US,
    CONTROL_PROTOCOL__PERIOD_8244US,
} CONTROL_PROTOCOL__sampling_period_t;

/* Note: Must be the same as hailo_averaging_factor_t in hailort.h */
typedef enum POWER__averaging_factor_e {
    CONTROL_PROTOCOL__AVERAGE_FACTOR_1 = 0,
    CONTROL_PROTOCOL__AVERAGE_FACTOR_4,
    CONTROL_PROTOCOL__AVERAGE_FACTOR_16,
    CONTROL_PROTOCOL__AVERAGE_FACTOR_64,
    CONTROL_PROTOCOL__AVERAGE_FACTOR_128,
    CONTROL_PROTOCOL__AVERAGE_FACTOR_256,
    CONTROL_PROTOCOL__AVERAGE_FACTOR_512,
    CONTROL_PROTOCOL__AVERAGE_FACTOR_1024,
} CONTROL_PROTOCOL__averaging_factor_t;

typedef enum {
    CONTROL_PROTOCOL__PHY_OPERATION_RESET = 0,

    /* Must be last! */
    CONTROL_PROTOCOL__PHY_OPERATION_COUNT
} CONTROL_PROTOCOL__phy_operation_t;

/* TODO: add compile time assertion that the protocol is 32-bit aligned */
/* START OF NETWORK STRUCTURES */
#pragma pack(push, 1)
typedef struct {
    uint32_t ack : 1;
    uint32_t reserved : 31;
} CONTROL_PROTOCOL__flags_struct_t;

/* Union for easy arithmetic manipulations */
typedef union {
    CONTROL_PROTOCOL__flags_struct_t bitstruct;
    uint32_t integer;
} CONTROL_PROTOCOL_flags_t;

typedef struct {
    uint32_t version;
    CONTROL_PROTOCOL_flags_t flags;
    uint32_t sequence;
    uint32_t opcode;
} CONTROL_PROTOCOL__common_header_t;

typedef struct {
    /* Must be first in order to support parsing */
    CONTROL_PROTOCOL__common_header_t common_header;
} CONTROL_PROTOCOL__request_header_t;

typedef struct {
    uint32_t major_status;
    uint32_t minor_status;
} CONTROL_PROTOCOL__status_t;

typedef struct {
    /* Must be first in order to support parsing */
    CONTROL_PROTOCOL__common_header_t common_header;
    CONTROL_PROTOCOL__status_t status;
} CONTROL_PROTOCOL__response_header_t;

#if defined(_MSC_VER)
// TODO: warning C4200
#pragma warning(push)
#pragma warning(disable: 4200)
#endif
typedef struct {
    uint32_t length;
    uint8_t data[0];
} CONTROL_PROTOCOL__parameter_t;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#if defined(_MSC_VER)
// TODO: warning C4200
#pragma warning(push)
#pragma warning(disable: 4200)
#endif
typedef struct {
    uint32_t parameter_count;
    uint8_t parameters[0];
} CONTROL_PROTOCOL__payload_t;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

typedef struct {
    uint32_t protocol_version_length;
    uint32_t protocol_version;
    uint32_t fw_version_length;
    firmware_version_t fw_version;
    uint32_t logger_version_length;
    uint32_t logger_version;
    uint32_t board_name_length;
    uint8_t board_name[CONTROL_PROTOCOL__MAX_BOARD_NAME_LENGTH];
    uint32_t device_architecture_length;
    uint32_t device_architecture;
    uint32_t serial_number_length;
    uint8_t serial_number[CONTROL_PROTOCOL__MAX_SERIAL_NUMBER_LENGTH];
    uint32_t part_number_length;
    uint8_t part_number[CONTROL_PROTOCOL__MAX_PART_NUMBER_LENGTH];
    uint32_t product_name_length;
    uint8_t product_name[CONTROL_PROTOCOL__MAX_PRODUCT_NAME_LENGTH];
} CONTROL_PROTOCOL_identify_response_t;

typedef struct {
    uint32_t fw_version_length;
    firmware_version_t fw_version;
} CONTROL_PROTOCOL__core_identify_response_t;

typedef enum {
    CONTROL_PROTOCOL__HAILO8_A0 = 0,
    CONTROL_PROTOCOL__HAILO8,
    CONTROL_PROTOCOL__HAILO8L,
    CONTROL_PROTOCOL__HAILO15H,
    CONTROL_PROTOCOL__PLUTO,
    CONTROL_PROTOCOL__MARS,
    /* Must be last!! */
    CONTROL_PROTOCOL__DEVICE_ARCHITECTURE_COUNT
} CONTROL_PROTOCOL__device_architecture_t;

typedef enum {
    CONTROL_PROTOCOL__MIPI_DESKEW__FORCE_DISABLE = 0,
    CONTROL_PROTOCOL__MIPI_DESKEW__FORCE_ENABLE,
    CONTROL_PROTOCOL__MIPI_DESKEW__DEFAULT
} CONTROL_PROTOCOL__mipi_deskew_enable_t;

typedef struct {
    uint32_t address_length;
    uint32_t address;
    uint32_t data_count_length;
    uint32_t data_count;
} CONTROL_PROTOCOL__read_memory_request_t;

typedef struct {
    uint32_t data_length;
    uint8_t data[CONTROL_PROTOCOL__MAX_READ_MEMORY_DATA_SIZE];
} CONTROL_PROTOCOL__read_memory_response_t;

#if defined(_MSC_VER)
// TODO: warning C4200
#pragma warning(push)
#pragma warning(disable: 4200)
#endif
typedef struct {
    uint32_t address_length;
    uint32_t address;
    uint32_t data_length;
    uint8_t  data[0];
} CONTROL_PROTOCOL__write_memory_request_t;


#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// Tightly coupled with hailo_fw_logger_interface_t
typedef enum {
    CONTROL_PROTOCOL__INTERFACE_PCIE = 1 << 0,
    CONTROL_PROTOCOL__INTERFACE_UART = 1 << 1
} CONTROL_PROTOCOL_interface_t;

#define CONTROL_PROTOCOL__FW_MAX_LOGGER_LEVEL (FW_LOGGER_LEVEL_FATAL)

#define CONTROL_PROTOCOL__FW_MAX_LOGGER_INTERFACE (CONTROL_PROTOCOL__INTERFACE_PCIE | CONTROL_PROTOCOL__INTERFACE_UART)

typedef struct {
    uint32_t level_length;
    uint8_t level;  // CONTROL_PROTOCOL_interface_t
    uint32_t logger_interface_bit_mask_length;
    uint8_t logger_interface_bit_mask;
} CONTROL_PROTOCOL__set_fw_logger_request_t;

typedef struct {
    uint32_t should_activate_length;
    bool should_activate;
} CONTROL_PROTOCOL__set_throttling_state_request_t;

typedef struct {
    uint32_t is_active_length;
    bool is_active;
} CONTROL_PROTOCOL__get_throttling_state_response_t;

typedef struct {
    uint32_t should_activate_length;
    bool should_activate;
} CONTROL_PROTOCOL__set_overcurrent_state_request_t;

typedef struct {
    uint32_t sleep_state_length;
    uint8_t sleep_state; /* of type CONTROL_PROTOCOL__sleep_state_t */
} CONTROL_PROTOCOL__set_sleep_state_request_t;

typedef struct {
    uint32_t is_required_length;
    bool is_required;
} CONTROL_PROTOCOL__get_overcurrent_state_response_t;

typedef struct {
    uint32_t clock_freq_length;
    uint32_t clock_freq;
} CONTROL_PROTOCOL__set_clock_freq_request_t;

typedef struct {
    uint16_t core_bytes_per_buffer;
    uint16_t core_buffers_per_frame;
    uint16_t periph_bytes_per_buffer;
    uint16_t periph_buffers_per_frame;
    uint16_t feature_padding_payload;
    uint32_t buffer_padding_payload;
    uint16_t buffer_padding;
    bool is_core_hw_padding_config_in_dfc;
} CONTROL_PROTOCOL__nn_stream_config_t;

typedef struct {
    uint16_t host_udp_port;
    uint16_t chip_udp_port;
    uint16_t max_udp_payload_size;
    bool should_send_sync_packets;
    uint32_t buffers_threshold;
    bool use_rtp;
} CONTROL_PROTOCOL__udp_output_config_params_t;

typedef struct {
    uint8_t should_sync;
    uint32_t frames_per_sync;
    uint32_t packets_per_frame;
    uint16_t sync_size;
} CONTROL_PROTOCOL__udp_input_config_sync_t;

typedef struct {
    uint16_t listening_port;
    CONTROL_PROTOCOL__udp_input_config_sync_t sync;
    uint32_t buffers_threshold;
    bool use_rtp;
} CONTROL_PROTOCOL__udp_input_config_params_t;

typedef struct {
    uint8_t data_type;
    uint16_t img_width_pixels; // sensor_out == mipi_in == ISP_in
    uint16_t img_height_pixels; // sensor_out == mipi_in == ISP_in
    uint8_t pixels_per_clock;
    uint8_t number_of_lanes;
    uint8_t clock_selection;
    uint8_t virtual_channel_index;
    uint32_t data_rate;
} CONTROL_PROTOCOL__mipi_common_config_params_t;

typedef struct {
    bool isp_enable;
    uint8_t isp_img_in_order;
    uint8_t isp_img_out_data_type;
    bool isp_crop_enable;
    uint16_t isp_crop_output_width_pixels; // mipi_out == ISP_out == shmifo_in
    uint16_t isp_crop_output_height_pixels; // mipi_out == ISP_out == shmifo_in
    uint16_t isp_crop_output_width_start_offset_pixels;
    uint16_t isp_crop_output_height_start_offset_pixels;
    bool isp_test_pattern_enable;
    bool isp_configuration_bypass;
    bool isp_run_time_ae_enable;
    bool isp_run_time_awb_enable;
    bool isp_run_time_adt_enable;
    bool isp_run_time_af_enable;
    uint16_t isp_run_time_calculations_interval_ms;
    uint8_t isp_light_frequency;
} CONTROL_PROTOCOL__isp_config_params_t;

typedef struct {
    CONTROL_PROTOCOL__mipi_common_config_params_t common_params;
    uint8_t mipi_rx_id;
    CONTROL_PROTOCOL__isp_config_params_t isp_params;
} CONTROL_PROTOCOL__mipi_input_config_params_t;

typedef struct {
    CONTROL_PROTOCOL__mipi_common_config_params_t common_params;
    uint8_t mipi_tx_id;
    uint8_t fifo_threshold_percent;
    uint8_t deskew_enable;
} CONTROL_PROTOCOL__mipi_output_config_params_t;

typedef enum {
    CONTROL_PROTOCOL__PCIE_DATAFLOW_TYPE_CONTINUOUS = 0,
    /* Type 1 (which is CFG flow channel) is not a valid option to be set by the user */
    CONTROL_PROTOCOL__PCIE_DATAFLOW_TYPE_BURST = 2,

    /* Must be last */
    CONTROL_PROTOCOL__PCIE_DATAFLOW_TYPE_COUNT,
} CONTROL_PROTOCOL__pcie_dataflow_type_t;

typedef struct {
    uint8_t pcie_channel_index;
    uint16_t desc_page_size;
} CONTROL_PROTOCOL__pcie_output_config_params_t;

typedef struct {
    uint8_t pcie_channel_index;
    uint8_t pcie_dataflow_type;
} CONTROL_PROTOCOL__pcie_input_config_params_t;

typedef union {
    CONTROL_PROTOCOL__udp_output_config_params_t udp_output;
    CONTROL_PROTOCOL__udp_input_config_params_t udp_input;
    CONTROL_PROTOCOL__mipi_input_config_params_t mipi_input;
    CONTROL_PROTOCOL__mipi_output_config_params_t mipi_output;
    CONTROL_PROTOCOL__pcie_input_config_params_t pcie_input;
    CONTROL_PROTOCOL__pcie_output_config_params_t pcie_output;
} CONTROL_PROTOCOL__communication_config_prams_t;

// Tightly coupled with hailo_power_mode_t
typedef enum {
    CONTROL_PROTOCOL__MODE_PERFORMANCE       = 0,
    CONTROL_PROTOCOL__MODE_ULTRA_PERFORMANCE = 1,
    
    /* Must be last */
    CONTROL_PROTOCOL__POWER_MODE_COUNT
} CONTROL_PROTOCOL__power_mode_t;

typedef struct {
    uint32_t stream_index_length;
    uint8_t stream_index;
    uint32_t is_input_length;
    uint8_t is_input;
    uint32_t communication_type_length;
    uint32_t communication_type;
    uint32_t skip_nn_stream_config_length;
    uint8_t skip_nn_stream_config;
    uint32_t power_mode_length;
    uint8_t power_mode; // CONTROL_PROTOCOL__power_mode_t
    uint32_t nn_stream_config_length;
    CONTROL_PROTOCOL__nn_stream_config_t nn_stream_config;
    // Should be last for size calculations
    uint32_t communication_params_length;
    CONTROL_PROTOCOL__communication_config_prams_t communication_params;
} CONTROL_PROTOCOL__config_stream_request_t;

typedef struct {
    uint32_t dataflow_manager_id_length;
    uint8_t dataflow_manager_id;
    uint32_t is_input_length;
    uint8_t is_input;
} CONTROL_PROTOCOL__open_stream_request_t;

typedef struct {
    uint32_t dataflow_manager_id_length;
    uint8_t dataflow_manager_id;
    uint32_t is_input_length;
    uint8_t is_input;
} CONTROL_PROTOCOL__close_stream_request_t;

typedef struct {
    uint32_t operation_type_length;
    uint32_t operation_type;
} CONTROL_PROTOCOL__phy_operation_request_t;

typedef struct {
    uint32_t rx_pause_frames_enable_length;
    uint8_t rx_pause_frames_enable;
} CONTROL_PROTOCOL__set_pause_frames_t;

typedef struct {
    uint32_t reset_type_length;
    uint32_t reset_type;
} CONTROL_PROTOCOL__reset_request_t;

typedef enum {
    CONTROL_PROTOCOL__CONFIG_CORE_TOP_TYPE_AHB_TO_AXI = 0,

    /* Must be last! */
    CONTROL_PROTOCOL__CONFIG_CORE_TOP_OPCODE_COUNT
} CONTROL_PROTOCOL__config_core_top_type_t;

typedef struct {
    uint8_t enable_use_64bit_data_only;
} CONTROL_PROTOCOL__config_ahb_to_axi_params_t;

typedef union {
    CONTROL_PROTOCOL__config_ahb_to_axi_params_t ahb_to_axi;
} CONTROL_PROTOCOL__config_core_top_params_t;

typedef struct {
    uint32_t config_type_length;
    uint32_t config_type;
    uint32_t config_params_length;
    CONTROL_PROTOCOL__config_core_top_params_t config_params;
} CONTROL_PROTOCOL__config_core_top_request_t;

typedef struct {
    uint32_t dvm_length;
    uint32_t dvm;
    uint32_t measurement_type_length;
    uint32_t measurement_type;
} CONTROL_PROTOCOL__power_measurement_request_t;

typedef struct {
    uint32_t power_measurement_length;
    float32_t power_measurement;
    uint32_t dvm_length;
    uint32_t dvm;
    uint32_t measurement_type_length;
    uint32_t measurement_type;
} CONTROL_PROTOCOL__power_measurement_response_t;

typedef struct {
    uint32_t index_length;
    uint32_t index;
    uint32_t dvm_length;
    uint32_t dvm;
    uint32_t measurement_type_length;
    uint32_t measurement_type;
} CONTROL_PROTOCOL__set_power_measurement_request_t;

typedef struct {
    uint32_t dvm_length;
    uint32_t dvm;
    uint32_t measurement_type_length;
    uint32_t measurement_type;
} CONTROL_PROTOCOL__set_power_measurement_response_t;

typedef struct {
    uint32_t index_length;
    uint32_t index;
    uint32_t should_clear_length;
    uint8_t should_clear;
} CONTROL_PROTOCOL__get_power_measurement_request_t;

typedef struct {
    uint32_t total_number_of_samples_length;
    uint32_t total_number_of_samples;
    uint32_t min_value_length;
    float32_t min_value;
    uint32_t max_value_length;
    float32_t max_value;
    uint32_t average_value_length;
    float32_t average_value;
    uint32_t average_time_value_milliseconds_length;
    float32_t average_time_value_milliseconds;
} CONTROL_PROTOCOL__get_power_measurement_response_t;

typedef struct {
    uint32_t delay_milliseconds_length;
    uint32_t delay_milliseconds;
    uint32_t averaging_factor_length;
    uint16_t averaging_factor;
    uint32_t sampling_period_length;
    uint16_t sampling_period;
} CONTROL_PROTOCOL__start_power_measurement_request_t;


typedef struct {
    uint32_t endianness_length;
    uint8_t endianness;
    uint32_t slave_address_length;
    uint16_t slave_address;
    uint32_t register_address_size_length;
    uint8_t register_address_size;
    uint32_t bus_index_length;
    uint8_t bus_index;
    uint32_t should_hold_bus_length;
    uint8_t should_hold_bus;
} CONTROL_PROTOCOL__i2c_slave_config_t;


#if defined(_MSC_VER)
// TODO: warning C4200
#pragma warning(push)
#pragma warning(disable: 4200)
#endif
typedef struct {
    CONTROL_PROTOCOL__i2c_slave_config_t slave_config;
    uint32_t register_address_size;
    uint32_t register_address;
    uint32_t data_length;
    uint8_t data[0];
} CONTROL_PROTOCOL__i2c_write_request_t;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

typedef struct {
    CONTROL_PROTOCOL__i2c_slave_config_t slave_config;
    uint32_t register_address_size;
    uint32_t register_address;
    uint32_t data_length_length;
    uint32_t data_length;
} CONTROL_PROTOCOL__i2c_read_request_t;

typedef struct {
    uint32_t data_length;
    uint8_t data[CONTROL_PROTOCOL__MAX_I2C_REGISTER_SIZE];
} CONTROL_PROTOCOL__i2c_read_response_t;

#if defined(_MSC_VER)
// TODO: warning C4200
#pragma warning(push)
#pragma warning(disable: 4200)
#endif
typedef struct {
    uint32_t offset_length;
    uint32_t offset;
    uint32_t data_length;
    uint8_t data[0];
} CONTROL_PROTOCOL__write_firmware_update_request_t;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

typedef struct {
    uint32_t expected_md5_length;
    MD5_SUM_t expected_md5;
    uint32_t firmware_size_length;
    uint32_t firmware_size;
} CONTROL_PROTOCOL__validate_firmware_update_request_t;

typedef CONTROL_PROTOCOL__write_firmware_update_request_t CONTROL_PROTOCOL__write_second_stage_to_internal_memory_request_t;
typedef struct {
    uint32_t expected_md5_length;
    MD5_SUM_t expected_md5;
    uint32_t second_stage_size_length;
    uint32_t second_stage_size;
} CONTROL_PROTOCOL__copy_second_stage_to_flash_request_t; 

typedef struct {
    uint32_t version_length;
    uint32_t version;
    uint32_t entry_count_length;
    uint32_t entry_count;
    uint32_t total_size_length;
    uint32_t total_size;
} CONTROL_PROTOCOL__examine_user_config_response_t;

typedef struct {
    uint32_t latency_measurement_en_length;
    uint8_t latency_measurement_en;
    uint32_t inbound_start_buffer_number_length;
    uint32_t inbound_start_buffer_number;
    uint32_t outbound_stop_buffer_number_length;
    uint32_t outbound_stop_buffer_number;
    uint32_t inbound_stream_index_length;
    uint32_t inbound_stream_index;
    uint32_t outbound_stream_index_length;
    uint32_t outbound_stream_index;
} CONTROL_PROTOCOL__latency_config_request_t;


#if defined(_MSC_VER)
// TODO: warning C4200
#pragma warning(push)
#pragma warning(disable: 4200)
#endif
typedef struct {
    uint32_t section_index_length;
    uint32_t section_index;
    uint32_t is_first_length;
    uint32_t is_first;
    uint32_t start_offset_length;
    uint32_t start_offset;
    uint32_t reset_data_size_length;
    uint32_t reset_data_size;
    uint32_t sensor_type_length;
    uint32_t sensor_type;
    uint32_t total_data_size_length;
    uint32_t total_data_size;
    uint32_t config_height_length;
    uint16_t config_height;
    uint32_t config_width_length;
    uint16_t config_width;
    uint32_t config_fps_length;
    uint16_t config_fps;
    uint32_t config_name_length;
    uint8_t  config_name[MAX_CONFIG_NAME_LEN];
    uint32_t data_length;
    uint8_t data[0];
} CONTROL_PROTOCOL__sensor_store_config_request_t;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

typedef struct {
    uint32_t section_index_length;
    uint32_t section_index;
    uint32_t offset_length;
    uint32_t offset;
    uint32_t data_size_length;
    uint32_t data_size;
} CONTROL_PROTOCOL__sensor_get_config_request_t;

typedef struct {
    uint32_t sensor_type_length;
    uint32_t sensor_type;
    uint32_t i2c_bus_index_length;
    uint32_t i2c_bus_index;
} CONTROL_PROTOCOL__sensor_set_i2c_bus_index_t;

typedef struct {
    uint32_t section_index_length;
    uint32_t section_index;
} CONTROL_PROTOCOL__sensor_load_config_request_t;

typedef struct {
    uint32_t slave_address_length;
    uint16_t slave_address;
    uint32_t register_address_size_length;
    uint8_t register_address_size;
    uint32_t bus_index_length;
    uint8_t  bus_index;
    uint32_t should_hold_bus_length;
    uint8_t should_hold_bus;
    uint32_t endianness_length;
    uint8_t endianness;
}CONTROL_PROTOCOL__sensor_set_generic_i2c_slave_request_t;

typedef struct {
    uint32_t section_index_length;
    uint32_t section_index;
} CONTROL_PROTOCOL__sensor_reset_request_t;

typedef struct {
    uint32_t data_length;
    uint8_t data[CONTROL_PROTOCOL__MAX_READ_MEMORY_DATA_SIZE];
} CONTROL_PROTOCOL__sensor_get_config_response_t;

typedef struct {
    uint32_t data_length;
    uint8_t data[CONTROL_PROTOCOL__MAX_READ_MEMORY_DATA_SIZE];
} CONTROL_PROTOCOL__sensor_get_sections_info_response_t;

typedef struct {
    uint32_t inbound_to_outbound_latency_nsec_length;
    uint32_t inbound_to_outbound_latency_nsec;
} CONTROL_PROTOCOL__latency_read_response_t;

typedef struct {
    bool is_abbale_supported;
} CONTROL_PROTOCOL__VALIDATION_FEATURE_LIST_t;

typedef struct {
    bool preliminary_run_asap;
    bool batch_register_config;
    bool can_fast_batch_switch;
} CONTROL_PROTOCOL__INFER_FEATURE_LIST_t;

typedef struct {
    uint8_t packed_vdma_channel_id;
} CONTROL_PROTOCOL__config_channel_info_t;

typedef struct {
    uint16_t dynamic_contexts_count;
    CONTROL_PROTOCOL__INFER_FEATURE_LIST_t infer_features;
    CONTROL_PROTOCOL__VALIDATION_FEATURE_LIST_t validation_features;
    uint8_t networks_count;
    uint16_t csm_buffer_size;
    uint16_t batch_size;
    uint32_t external_action_list_address;
    uint32_t boundary_channels_bitmap[CONTROL_PROTOCOL__MAX_VDMA_ENGINES_COUNT];
    uint8_t config_channels_count;
    CONTROL_PROTOCOL__config_channel_info_t config_channel_info[CONTROL_PROTOCOL__MAX_CFG_CHANNELS];
} CONTROL_PROTOCOL__application_header_t;

typedef struct {
    uint32_t application_header_length;
    CONTROL_PROTOCOL__application_header_t application_header;
} CONTROL_PROTOCOL__context_switch_set_network_group_header_request_t;

typedef enum {
    CONTROL_PROTOCOL__WATCHDOG_MODE_HW_SW = 0,
    CONTROL_PROTOCOL__WATCHDOG_MODE_HW_ONLY,

    /* must be last*/
    CONTROL_PROTOCOL__WATCHDOG_NUM_MODES,
} CONTROL_PROTOCOL__WATCHDOG_MODE_t;

typedef struct {
    uint8_t application_count;
    CONTROL_PROTOCOL__application_header_t application_header[CONTROL_PROTOCOL__MAX_CONTEXT_SWITCH_APPLICATIONS];
} CONTROL_PROTOCOL__context_switch_main_header_t;

typedef struct {
    uint32_t should_enable_length;
    uint8_t should_enable;
} CONTROL_PROTOCOL__wd_enable_request_t;

typedef struct {
    uint32_t wd_cycles_length;
    uint32_t wd_cycles;
    uint32_t wd_mode_length;
    uint8_t wd_mode;
} CONTROL_PROTOCOL__wd_config_request_t;

/* TODO: Define bit struct (SDK-14509). */
typedef uint32_t CONTROL_PROTOCOL__system_state_t;
typedef struct {
    uint32_t system_state_length;
    CONTROL_PROTOCOL__system_state_t system_state;
} CONTROL_PROTOCOL__previous_system_state_response_t;

typedef struct {
    float32_t ts0_temperature;
    float32_t ts1_temperature;
    uint16_t sample_count;
} CONTROL_PROTOCOL__temperature_info_t;

typedef struct {
    uint32_t info_length;
    CONTROL_PROTOCOL__temperature_info_t info;
} CONTROL_PROTOCOL__get_chip_temperature_response_t;


typedef enum {
    CONTROL_PROTOCOL__HOST_BUFFER_TYPE_EXTERNAL_DESC = 0,
    CONTROL_PROTOCOL__HOST_BUFFER_TYPE_CCB,
    CONTROL_PROTOCOL__HOST_BUFFER_TYPE_HOST_MANAGED_EXTERNAL_DESC, /* DEPRECATED */

    /* must be last */
    CONTROL_PROTOCOL__HOST_BUFFER_TYPE_COUNT
} CONTROL_PROTOCOL__HOST_BUFFER_TYPE_t;

typedef struct {
    uint8_t buffer_type;   // CONTROL_PROTOCOL__HOST_BUFFER_TYPE_t
    uint64_t dma_address;
    uint16_t desc_page_size;
    uint32_t total_desc_count; //HRT-9913 - Some descriptors may not be initialized (to save space), needs to
                               // change this param or add another one for validation.
    uint32_t bytes_in_pattern;
} CONTROL_PROTOCOL__host_buffer_info_t;


#if defined(_MSC_VER)
// TODO: warning C4200
#pragma warning(push)
#pragma warning(disable: 4200)
#endif
typedef struct {
    uint32_t is_first_chunk_per_context_length;
    uint8_t is_first_chunk_per_context;
    uint32_t is_last_chunk_per_context_length;
    uint8_t is_last_chunk_per_context;
    uint32_t context_type_length;
    uint8_t context_type; // CONTROL_PROTOCOL__context_switch_context_type_t
    uint32_t context_network_data_length;
    uint8_t context_network_data[0];
} CONTROL_PROTOCOL__context_switch_set_context_info_request_t;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

typedef CONTROL_PROTOCOL__read_memory_request_t CONTROL_PROTOCOL__read_user_config_request_t;
typedef CONTROL_PROTOCOL__read_memory_response_t CONTROL_PROTOCOL__read_user_config_response_t;
typedef CONTROL_PROTOCOL__write_memory_request_t CONTROL_PROTOCOL__write_user_config_request_t;

typedef struct {
    uint32_t measurement_enable_length;
    uint8_t measurement_enable;
} CONTROL_PROTOCOL__idle_time_set_measurement_request_t;

typedef struct {
    uint32_t idle_time_ns_length;
    uint64_t idle_time_ns;
} CONTROL_PROTOCOL__idle_time_get_measurement_response_t;

typedef struct {
    uint32_t network_group_id_length;
    uint32_t network_group_id;
    uint32_t context_type_length;
    uint8_t context_type; // CONTROL_PROTOCOL__context_switch_context_type_t
    uint32_t context_index_length;
    uint16_t context_index;
    uint32_t action_list_offset_length;
    uint16_t action_list_offset;
} CONTROL_PROTOCOL__download_context_action_list_request_t;

#if defined(_MSC_VER)
// TODO: warning C4200
#pragma warning(push)
#pragma warning(disable: 4200)
#endif
typedef struct {
    uint32_t base_address_length;
    uint32_t base_address;
    uint32_t is_action_list_end_length;
    uint8_t is_action_list_end;
    uint32_t batch_counter_length;
    uint32_t batch_counter;
    uint32_t idle_time_length;
    uint32_t idle_time;
    uint32_t action_list_length;
    uint8_t action_list[0];
} CONTROL_PROTOCOL__download_context_action_list_response_t;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

typedef enum {
    CONTROL_PROTOCOL__CONTEXT_SWITCH_STATUS_RESET = 0,
    CONTROL_PROTOCOL__CONTEXT_SWITCH_STATUS_ENABLED,
    CONTROL_PROTOCOL__CONTEXT_SWITCH_STATUS_PAUSED,

    /* must be last*/
    CONTROL_PROTOCOL__CONTEXT_SWITCH_STATUS_COUNT,
} CONTROL_PROTOCOL__CONTEXT_SWITCH_STATUS_t;

#define CONTROL_PROTOCOL__INIFINITE_BATCH_COUNT (0)
typedef struct {
    uint32_t state_machine_status_length;
    uint8_t state_machine_status;
    uint32_t application_index_length;
    uint8_t application_index;
    uint32_t dynamic_batch_size_length;
    uint16_t dynamic_batch_size;
    uint32_t batch_count_length;
    uint16_t batch_count;
} CONTROL_PROTOCOL__change_context_switch_status_request_t;

typedef struct {
    uint32_t interrupt_type_length;
    uint8_t interrupt_type;
    uint32_t interrupt_index_length;
    uint8_t interrupt_index;
    uint32_t interrupt_sub_index_length;
    uint8_t interrupt_sub_index;
} CONTROL_PROTOCOL__set_dataflow_interrupt_request_t;

typedef struct {
    uint32_t connection_type_length;
    uint8_t  connection_type;
    uint32_t host_ip_address_length;
    uint32_t host_ip_address;
    uint32_t host_port_length;
    uint16_t host_port;
}CONTROL_PROTOCOL__d2h_event_manager_set_new_host_info_request_t;

typedef struct {
    uint32_t priority_length;
    uint8_t  priority;
}CONTROL_PROTOCOL__d2h_event_manager_send_host_info_event_request_t;

typedef CONTROL_PROTOCOL__read_memory_request_t CONTROL_PROTOCOL__read_board_config_request_t;
typedef CONTROL_PROTOCOL__read_memory_response_t CONTROL_PROTOCOL__read_board_config_response_t;
typedef CONTROL_PROTOCOL__write_memory_request_t CONTROL_PROTOCOL__write_board_config_request_t;
/* Tightly coupled hailo_device_supported_features_t */
typedef uint64_t CONTROL_PROTOCOL__supported_features_t;

/* Tightly coupled hailo_device_boot_source_t */
typedef enum {
    CONTROL_PROTOCOL__BOOT_SOURCE_INVALID = 0,
    CONTROL_PROTOCOL__BOOT_SOURCE_PCIE,
    CONTROL_PROTOCOL__BOOT_SOURCE_FLASH
} CONTROL_PROTOCOL__boot_source_t;

/* CONTROL_PROTOCOL_fuse_info_t sturct will be packed to unit_level_tracking_id field in hailo_extended_device_information_t */
/* CONTROL_PROTOCOL_fuse_info_t size is tightly coupled HAILO_UNIT_LEVEL_TRACKING_BYTES_LEN */
typedef struct {
    uint8_t lot_id[LOT_ID_BYTES_LEN];
    uint32_t die_wafer_info;
} CONTROL_PROTOCOL_fuse_info_t;

typedef struct {
    uint32_t neural_network_core_clock_rate_length;
    uint32_t neural_network_core_clock_rate;
    uint32_t supported_features_length;
    CONTROL_PROTOCOL__supported_features_t supported_features;
    uint32_t boot_source_length;
    uint32_t boot_source; /*CONTROL_PROTOCOL__boot_source_t*/
    uint32_t lcs_length;
    uint8_t lcs;
    uint32_t soc_id_length;
    uint8_t soc_id[CONTROL_PROTOCOL__SOC_ID_LENGTH];
    uint32_t eth_mac_length;
    uint8_t eth_mac_address[MAC_ADDR_BYTES_LEN];
    uint32_t fuse_info_length;
    CONTROL_PROTOCOL_fuse_info_t fuse_info;
    uint32_t pd_info_length;
    uint8_t pd_info[PM_RESULTS_LENGTH];
    uint32_t partial_clusters_layout_bitmap_length;
    uint32_t partial_clusters_layout_bitmap;
} CONTROL_PROTOCOL__get_extended_device_information_response_t;

/* Tightly coupled to hailo_throttling_level_t */
typedef struct {
    float32_t temperature_threshold;
    float32_t hysteresis_temperature_threshold;
    uint32_t throttling_nn_clock_freq;
} CONTROL_PROTOCOL__throttling_level_t;

/* Tightly coupled to hailo_health_info_t */
typedef struct {
    uint32_t overcurrent_protection_active_length;
    bool overcurrent_protection_active;
    uint32_t current_overcurrent_zone_length;
    uint8_t current_overcurrent_zone;
    uint32_t red_overcurrent_threshold_length;
    float32_t red_overcurrent_threshold;
    uint32_t overcurrent_throttling_active_length;
    bool overcurrent_throttling_active;
    uint32_t temperature_throttling_active_length;
    bool temperature_throttling_active;
    uint32_t current_temperature_zone_length;
    uint8_t current_temperature_zone;
    uint32_t current_temperature_throttling_level_length;
    int8_t current_temperature_throttling_level;
    uint32_t temperature_throttling_levels_length;
    CONTROL_PROTOCOL__throttling_level_t temperature_throttling_levels[MAX_TEMPERATURE_THROTTLING_LEVELS_NUMBER];
    uint32_t orange_temperature_threshold_length;
    int32_t orange_temperature_threshold;
    uint32_t orange_hysteresis_temperature_threshold_length;
    int32_t orange_hysteresis_temperature_threshold;
    uint32_t red_temperature_threshold_length;
    int32_t red_temperature_threshold;
    uint32_t red_hysteresis_temperature_threshold_length;
    int32_t red_hysteresis_temperature_threshold;
    uint32_t requested_overcurrent_clock_freq_length;
    uint32_t requested_overcurrent_clock_freq;
    uint32_t requested_temperature_clock_freq_length;
    uint32_t requested_temperature_clock_freq;
} CONTROL_PROTOCOL__get_health_information_response_t;

typedef enum {
    CONTROL_PROTOCOL__CONTEXT_SWITCH_BREAKPOINT_CONTROL_SET = 0,
    CONTROL_PROTOCOL__CONTEXT_SWITCH_BREAKPOINT_CONTROL_CONTINUE,
    CONTROL_PROTOCOL__CONTEXT_SWITCH_BREAKPOINT_CONTROL_CLEAR,

    /* Must be last */
    CONTROL_PROTOCOL__CONTEXT_SWITCH_BREAKPOINT_CONTROL_COUNT
} CONTROL_PROTOCOL__context_switch_breakpoint_control_t;

typedef enum {
    CONTROL_PROTOCOL__CONTEXT_SWITCH_DEBUG_SYS_STATUS_CLEARED = 0,
    CONTROL_PROTOCOL__CONTEXT_SWITCH_DEBUG_SYS_STATUS_WAITING_FOR_BREAKPOINT,
    CONTROL_PROTOCOL__CONTEXT_SWITCH_DEBUG_SYS_STATUS_REACHED_BREAKPOINT,

    /* Must be last */
    CONTROL_PROTOCOL__CONTEXT_SWITCH_DEBUG_SYS_STATUS_COUNT,
} CONTROL_PROTOCOL__context_switch_debug_sys_status_t;

typedef struct {
    bool break_at_any_application_index;
    uint8_t application_index;
    bool break_at_any_batch_index;
    uint16_t batch_index;
    bool break_at_any_context_index;
    uint16_t context_index;
    bool break_at_any_action_index;
    uint16_t action_index;
} CONTROL_PROTOCOL__context_switch_breakpoint_data_t;

typedef struct {
    uint32_t breakpoint_id_length;
    uint32_t breakpoint_id;
    uint32_t breakpoint_control_length;
    uint8_t breakpoint_control;
    uint32_t breakpoint_data_length;
    CONTROL_PROTOCOL__context_switch_breakpoint_data_t breakpoint_data;
} CONTROL_PROTOCOL__config_context_switch_breakpoint_request_t;

typedef struct {
    uint32_t breakpoint_id_length;
    uint32_t breakpoint_id;
} CONTROL_PROTOCOL__get_context_switch_breakpoint_status_request_t;

typedef struct {
    uint32_t breakpoint_status_length;
    uint8_t breakpoint_status;
} CONTROL_PROTOCOL__get_context_switch_breakpoint_status_response_t;

typedef struct {
    uint32_t is_rma_length;
    uint8_t is_rma;
} CONTROL_PROTOCOL__enable_debugging_request_t;

typedef struct {
    uint32_t main_header_length;
    CONTROL_PROTOCOL__context_switch_main_header_t main_header;
} CONTROL_PROTOCOL__get_context_switch_main_header_response_t;

typedef struct {
    uint32_t batch_index_length;
    uint16_t batch_index;
    uint32_t enable_user_configuration_length;
    uint8_t enable_user_configuration;
} CONTROL_PROTOCOL__config_context_switch_timestamp_request_t;

typedef struct {
    uint32_t dataflow_manager_id_length;
    uint8_t dataflow_manager_id;
} CONTROL_PROTOCOL__config_stream_response_t;

typedef enum {
    CONTROL_PROTOCOL__TOP_MEM_BLOCK_CRYPTO_1 = 0,
    CONTROL_PROTOCOL__TOP_MEM_BLOCK_L4_0_2,
    CONTROL_PROTOCOL__TOP_MEM_BLOCK_L4_1_3,
    CONTROL_PROTOCOL__TOP_MEM_BLOCK_L4_2_4,
    CONTROL_PROTOCOL__TOP_MEM_BLOCK_L4_3_5,
    CONTROL_PROTOCOL__TOP_MEM_BLOCK_SAGE1_CPU_6,
    CONTROL_PROTOCOL__TOP_MEM_BLOCK_SAGE1_CPU_FAST_BUS_7,
    CONTROL_PROTOCOL__TOP_MEM_BLOCK_SAGE1_DEBUG_8,
    CONTROL_PROTOCOL__TOP_MEM_BLOCK_SAGE1_ETH_9,
    CONTROL_PROTOCOL__TOP_MEM_BLOCK_SAGE1_FLASH_10,
    CONTROL_PROTOCOL__TOP_MEM_BLOCK_SAGE1_H264_11,
    CONTROL_PROTOCOL__TOP_MEM_BLOCK_SAGE1_ISP_12,
    CONTROL_PROTOCOL__TOP_MEM_BLOCK_SAGE1_MIPI_RX_13,
    CONTROL_PROTOCOL__TOP_MEM_BLOCK_SAGE1_MIPI_TX_14,
    CONTROL_PROTOCOL__TOP_MEM_BLOCK_SAGE1_PCIE_15,
    CONTROL_PROTOCOL__TOP_MEM_BLOCK_SAGE1_SDIO_16,
    CONTROL_PROTOCOL__TOP_MEM_BLOCK_SAGE1_SOFTMAX_17,
    CONTROL_PROTOCOL__TOP_MEM_BLOCK_SAGE1_USB_18,
    CONTROL_PROTOCOL__TOP_MEM_BLOCK_SAGE1_19,
    /*the cluster ring_s*/
    CONTROL_PROTOCOL__TOP_MEM_BLOCK_SUB_SERVER0_20,
    CONTROL_PROTOCOL__TOP_MEM_BLOCK_SUB_SERVER1_21,
    CONTROL_PROTOCOL__TOP_MEM_BLOCK_SUB_SERVER2_22,
    CONTROL_PROTOCOL__TOP_MEM_BLOCK_SUB_SERVER3_23,
    CONTROL_PROTOCOL__TOP_MEM_BLOCK_SUB_SERVER4_24,
    CONTROL_PROTOCOL__TOP_MEM_BLOCK_SUB_SERVER5_25,
    CONTROL_PROTOCOL__TOP_MEM_BLOCK_SUB_SERVER6_26,
    CONTROL_PROTOCOL__TOP_MEM_BLOCK_SUB_SERVER7_27,
    CONTROL_PROTOCOL__TOP_NUM_MEM_BLOCKS
} CONTROL_PROTOCOL__bist_top_mem_block_t;

/* Must be identical to hailo_sleep_state_t, tightly coupled */
typedef enum {
    CONTROL_PROTOCOL_SLEEP_STATE_SLEEPING = 0,
    CONTROL_PROTOCOL_SLEEP_STATE_AWAKE    = 1,
    /* must be last */
    CONTROL_PROTOCOL_SLEEP_STATE_COUNT
} CONTROL_PROTOCOL__sleep_state_t;

/*only allowing bist on the following memories*/
 #define CONTROL_PROTOCOL__BIST_TOP_WHITELIST ((1 << CONTROL_PROTOCOL__TOP_MEM_BLOCK_L4_0_2) | \
                                            (1 << CONTROL_PROTOCOL__TOP_MEM_BLOCK_L4_1_3) | \
                                            (1 << CONTROL_PROTOCOL__TOP_MEM_BLOCK_L4_2_4) | \
                                            (1 <<  CONTROL_PROTOCOL__TOP_MEM_BLOCK_L4_3_5))

                                            /*only allowing bist on the following memories*/
 #define CONTROL_PROTOCOL__BIST_TOP_BYPASS_ALL_MASK (0x7FFFF)

typedef struct {
    uint32_t is_top_test_length;
    bool is_top_test;
    uint32_t top_bypass_bitmap_length;
    uint32_t top_bypass_bitmap;
    uint32_t cluster_index_length;
    uint8_t cluster_index;
    uint32_t cluster_bypass_bitmap_0_length;
    uint32_t cluster_bypass_bitmap_0;
    uint32_t cluster_bypass_bitmap_1_length;
    uint32_t cluster_bypass_bitmap_1;
} CONTROL_PROTOCOL__run_bist_test_request_t;

typedef struct {
    uint32_t fifo_word_granularity_bytes;
    uint16_t max_periph_buffers_per_frame;
    uint16_t max_periph_bytes_per_buffer;
    uint16_t max_acceptable_bytes_per_buffer;
    uint32_t outbound_data_stream_size;
    uint8_t should_optimize_credits;
    uint32_t default_initial_credit_size;
} CONTROL_PROTOCOL__hw_consts_t;

typedef struct {
    uint32_t hw_consts_length;
    CONTROL_PROTOCOL__hw_consts_t hw_consts;
} CONTROL_PROTOCOL__get_hw_consts_response_t;

/* TODO HRT-9545 - Return and hw only parse results */
typedef struct {
    bool infer_done;
    uint32_t infer_cycles;
} CONTROL_PROTOCOL__hw_only_infer_results_t;

typedef struct {
    uint32_t results_length;
    CONTROL_PROTOCOL__hw_only_infer_results_t results;
} CONTROL_PROTOCOL__change_hw_infer_status_response_t;

typedef struct {
    uint8_t channel_index;
    uint8_t engine_index;
    uint16_t desc_programed;
} CONTROL_PROTOCOL__hw_infer_channel_info_t;

typedef struct {
    CONTROL_PROTOCOL__hw_infer_channel_info_t channel_info[CONTROL_PROTOCOL__MAX_TOTAL_CHANNEL_COUNT];
    uint8_t channel_count;
} CONTROL_PROTOCOL__hw_infer_channels_info_t;

typedef enum {
    CONTROL_PROTOCOL__HW_INFER_STATE_START,
    CONTROL_PROTOCOL__HW_INFER_STATE_STOP,

    /* must be last*/
    CONTROL_PROTOCOL__HW_INFER_STATE_COUNT
} CONTROL_PROTOCOL__hw_infer_state_t;

typedef enum {
    CONTROL_PROTOCOL__DESC_BOUNDARY_CHANNEL,
    CONTROL_PROTOCOL__CCB_BOUNDARY_CHANNEL,

    /* must be last*/
    CONTROL_PROTOCOL__BOUNDARY_CHANNEL_MODE_COUNT
} CONTROL_PROTOCOL__boundary_channel_mode_t;

#define CHANGE_HW_INFER_REQUEST_PARAMETER_COUNT (6)

typedef struct {
    uint32_t hw_infer_state_length;
    uint8_t hw_infer_state;
    uint32_t application_index_length;
    uint8_t application_index;
    uint32_t dynamic_batch_size_length;
    uint16_t dynamic_batch_size;
    uint32_t batch_count_length;
    uint16_t batch_count;
    uint32_t channels_info_length;
    CONTROL_PROTOCOL__hw_infer_channels_info_t channels_info;
    uint32_t boundary_channel_mode_length;
    uint8_t boundary_channel_mode;
} CONTROL_PROTOCOL__change_hw_infer_status_request_t;

typedef union {
    CONTROL_PROTOCOL_identify_response_t identity_response;
    CONTROL_PROTOCOL__core_identify_response_t core_identity_response;
    CONTROL_PROTOCOL__read_memory_response_t read_memory_response;
    CONTROL_PROTOCOL__power_measurement_response_t measure_power_response;
    CONTROL_PROTOCOL__set_power_measurement_response_t set_measure_power_response;
    CONTROL_PROTOCOL__get_power_measurement_response_t get_measure_power_response;
    CONTROL_PROTOCOL__examine_user_config_response_t examine_user_config_response;
    CONTROL_PROTOCOL__read_user_config_response_t read_user_config_response;
    CONTROL_PROTOCOL__i2c_read_response_t i2c_read_response;
    CONTROL_PROTOCOL__latency_read_response_t latency_read_response;
    CONTROL_PROTOCOL__sensor_get_config_response_t sensor_get_config_response;
    CONTROL_PROTOCOL__sensor_get_sections_info_response_t sensor_get_sections_info_response;
    CONTROL_PROTOCOL__idle_time_get_measurement_response_t idle_time_get_measurement_response;
    CONTROL_PROTOCOL__download_context_action_list_response_t download_context_action_list_response;
    CONTROL_PROTOCOL__previous_system_state_response_t previous_system_state_response;
    CONTROL_PROTOCOL__get_chip_temperature_response_t get_chip_temperature_response;
    CONTROL_PROTOCOL__read_board_config_response_t read_board_config_response;
    CONTROL_PROTOCOL__get_extended_device_information_response_t get_extended_device_information_response;
    CONTROL_PROTOCOL__get_context_switch_breakpoint_status_response_t get_context_switch_breakpoint_status_response;
    CONTROL_PROTOCOL__get_context_switch_main_header_response_t get_context_switch_main_header_response;
    CONTROL_PROTOCOL__config_stream_response_t config_stream_response;
    CONTROL_PROTOCOL__get_health_information_response_t get_health_information_response;
    CONTROL_PROTOCOL__get_throttling_state_response_t get_throttling_state_response;
    CONTROL_PROTOCOL__get_overcurrent_state_response_t get_overcurrent_state_response;
    CONTROL_PROTOCOL__get_hw_consts_response_t get_hw_consts_response;
    CONTROL_PROTOCOL__change_hw_infer_status_response_t change_hw_infer_status_response;

   // Note: This array is larger than any legal request:
   // * Functions in this module won't write more than CONTROL_PROTOCOL__MAX_CONTROL_LENGTH bytes
   //   when recieving a pointer to CONTROL_PROTOCOL__request_parameters_t.
   // * Hence, CONTROL_PROTOCOL__response_parameters_t can be stored on the stack of the calling function.
   uint8_t max_response_size[CONTROL_PROTOCOL__MAX_CONTROL_LENGTH];
} CONTROL_PROTOCOL__response_parameters_t;

typedef union {
   CONTROL_PROTOCOL__read_memory_request_t read_memory_request;
   CONTROL_PROTOCOL__write_memory_request_t write_memory_request;
   CONTROL_PROTOCOL__config_stream_request_t config_stream_request;
   CONTROL_PROTOCOL__open_stream_request_t open_stream_request;
   CONTROL_PROTOCOL__close_stream_request_t close_stream_request;
   CONTROL_PROTOCOL__phy_operation_request_t phy_operation_request;
   CONTROL_PROTOCOL__reset_request_t reset_resquest;
   CONTROL_PROTOCOL__config_core_top_request_t config_core_top_request;
   CONTROL_PROTOCOL__power_measurement_request_t measure_power_request;
   CONTROL_PROTOCOL__set_power_measurement_request_t set_measure_power_request;
   CONTROL_PROTOCOL__get_power_measurement_request_t get_measure_power_request;
   CONTROL_PROTOCOL__start_power_measurement_request_t start_measure_power_request;
   CONTROL_PROTOCOL__i2c_write_request_t i2c_write_request;
   CONTROL_PROTOCOL__i2c_read_request_t i2c_read_request;
   CONTROL_PROTOCOL__write_firmware_update_request_t write_firmware_update_request;
   CONTROL_PROTOCOL__validate_firmware_update_request_t validate_firmware_update_request;
   CONTROL_PROTOCOL__read_user_config_request_t read_user_config_request;
   CONTROL_PROTOCOL__write_user_config_request_t write_user_config_request;
   CONTROL_PROTOCOL__latency_config_request_t latency_config_request;
   CONTROL_PROTOCOL__sensor_store_config_request_t sensor_store_config_request;
   CONTROL_PROTOCOL__sensor_load_config_request_t sensor_load_config_request;
   CONTROL_PROTOCOL__sensor_reset_request_t sensor_reset_request;
   CONTROL_PROTOCOL__sensor_get_config_request_t sensor_get_config_request;
   CONTROL_PROTOCOL__sensor_set_generic_i2c_slave_request_t sensor_set_generic_i2c_slave_request;
   CONTROL_PROTOCOL__context_switch_set_network_group_header_request_t context_switch_set_network_group_header_request;
   CONTROL_PROTOCOL__context_switch_set_context_info_request_t context_switch_set_context_info_request;
   CONTROL_PROTOCOL__idle_time_set_measurement_request_t idle_time_set_measurement_request;
   CONTROL_PROTOCOL__download_context_action_list_request_t download_context_action_list_request;
   CONTROL_PROTOCOL__change_context_switch_status_request_t change_context_switch_status_request;
   CONTROL_PROTOCOL__wd_enable_request_t wd_enable_request;
   CONTROL_PROTOCOL__wd_config_request_t wd_config_request;
   CONTROL_PROTOCOL__set_dataflow_interrupt_request_t set_dataflow_interrupt_request;
   CONTROL_PROTOCOL__d2h_event_manager_set_new_host_info_request_t d2h_event_manager_set_new_host_info_request;
   CONTROL_PROTOCOL__d2h_event_manager_send_host_info_event_request_t d2h_event_manager_send_host_info_event_request;
   CONTROL_PROTOCOL__read_board_config_request_t read_board_config_request;
   CONTROL_PROTOCOL__write_board_config_request_t write_board_config_request;
   CONTROL_PROTOCOL__config_context_switch_breakpoint_request_t config_context_switch_breakpoint_request;
   CONTROL_PROTOCOL__get_context_switch_breakpoint_status_request_t get_context_switch_breakpoint_status_request;
   CONTROL_PROTOCOL__enable_debugging_request_t enable_debugging_request;
   CONTROL_PROTOCOL__set_fw_logger_request_t set_fw_logger_request;
   CONTROL_PROTOCOL__write_second_stage_to_internal_memory_request_t write_second_stage_to_internal_memory_request; 
   CONTROL_PROTOCOL__copy_second_stage_to_flash_request_t copy_second_stage_to_flash_request;
   CONTROL_PROTOCOL__set_pause_frames_t set_pause_frames_request;
   CONTROL_PROTOCOL__config_context_switch_timestamp_request_t config_context_switch_timestamp_request;
   CONTROL_PROTOCOL__run_bist_test_request_t run_bist_test_request;
   CONTROL_PROTOCOL__set_clock_freq_request_t set_clock_freq_request; 
   CONTROL_PROTOCOL__set_throttling_state_request_t set_throttling_state_request;
   CONTROL_PROTOCOL__sensor_set_i2c_bus_index_t sensor_set_i2c_bus_index;
   CONTROL_PROTOCOL__set_overcurrent_state_request_t set_overcurrent_state_request;
   CONTROL_PROTOCOL__set_sleep_state_request_t set_sleep_state_request;
   CONTROL_PROTOCOL__change_hw_infer_status_request_t change_hw_infer_status_request;
   // Note: This array is larger than any legal request:
   // * Functions in this module won't write more than CONTROL_PROTOCOL__MAX_CONTROL_LENGTH bytes
   //   when recieving a pointer to CONTROL_PROTOCOL__request_parameters_t.
   // * Hence, CONTROL_PROTOCOL__request_parameters_t can be stored on the stack of the calling function.
   uint8_t max_request_size[CONTROL_PROTOCOL__MAX_CONTROL_LENGTH];
} CONTROL_PROTOCOL__request_parameters_t;

typedef struct {
    CONTROL_PROTOCOL__request_header_t header;
    uint32_t parameter_count;
    /* Must be last */
    CONTROL_PROTOCOL__request_parameters_t parameters;
} CONTROL_PROTOCOL__request_t;

typedef struct {
    CONTROL_PROTOCOL__response_header_t header;
    uint32_t parameter_count;
    /* Must be last */
    CONTROL_PROTOCOL__response_parameters_t parameters;
} CONTROL_PROTOCOL__response_t;

#pragma pack(pop)
/* END OF NETWORK STRUCTURES */

#define CONTROL_PROTOCOL__MAX_REQUEST_PARAMETERS_LENGTH \
    (CONTROL_PROTOCOL__MAX_CONTROL_LENGTH - offsetof(CONTROL_PROTOCOL__request_t, parameters))
#define CONTROL_PROTOCOL__MAX_RESPONSE_PARAMETERS_LENGTH \
    (CONTROL_PROTOCOL__MAX_CONTROL_LENGTH - offsetof(CONTROL_PROTOCOL__response_t, parameters))

#define CONTROL_PROTOCOL__ACTION_LIST_RESPONSE_MAX_SIZE \
    (CONTROL_PROTOCOL__MAX_RESPONSE_PARAMETERS_LENGTH - sizeof(CONTROL_PROTOCOL__download_context_action_list_response_t))

/* Context switch structs - as it's used by the control.c file and inter cpu control */
#define CONTROL_PROTOCOL__CONTEXT_NETWORK_DATA_SINGLE_CONTROL_MAX_SIZE \
    (CONTROL_PROTOCOL__MAX_REQUEST_PARAMETERS_LENGTH - sizeof(CONTROL_PROTOCOL__context_switch_set_context_info_request_t))

typedef enum {
    CONTROL_PROTOCOL__CONTEXT_SWITCH_CONTEXT_TYPE_PRELIMINARY,
    CONTROL_PROTOCOL__CONTEXT_SWITCH_CONTEXT_TYPE_DYNAMIC,
    CONTROL_PROTOCOL__CONTEXT_SWITCH_CONTEXT_TYPE_BATCH_SWITCHING,
    CONTROL_PROTOCOL__CONTEXT_SWITCH_CONTEXT_TYPE_ACTIVATION,

    /* must be last*/
    CONTROL_PROTOCOL__CONTEXT_SWITCH_CONTEXT_TYPE_COUNT,
} CONTROL_PROTOCOL__context_switch_context_type_t;

typedef enum {
    CONTROL_PROTOCOL__CONTEXT_SWITCH_INDEX_ACTIVATION_CONTEXT = 0,
    CONTROL_PROTOCOL__CONTEXT_SWITCH_INDEX_BATCH_SWITCHING_CONTEXT,
    CONTROL_PROTOCOL__CONTEXT_SWITCH_INDEX_PRELIMINARY_CONTEXT,
    CONTROL_PROTOCOL__CONTEXT_SWITCH_NUMBER_OF_NON_DYNAMIC_CONTEXTS,
    CONTROL_PROTOCOL__CONTEXT_SWITCH_INDEX_FIRST_DYNAMIC_CONTEXT = CONTROL_PROTOCOL__CONTEXT_SWITCH_NUMBER_OF_NON_DYNAMIC_CONTEXTS,

    /* must be last*/
    CONTROL_PROTOCOL__CONTEXT_SWITCH_INDEX_COUNT,
} CONTROL_PROTOCOL__context_switch_context_index_t;

#define CONTROL_PROTOCOL__MAX_CONTEXTS_PER_NETWORK_GROUP (1024)

// This struct will be used for both ControlActionList and DDRActionlist (in order to keep flow in FW as similar as possible)
// The context_network_data array will never have more data than CONTROL_PROTOCOL__CONTEXT_NETWORK_DATA_SINGLE_CONTROL_MAX_SIZE
// In case of ControlActionList - this is verified when sending and receiving control. We make it larger here to be
// able to hold DDRActionList Contexts without needing to copy or do more processing in fw.
// In both cases this struct holds a chunk of the context - in ControlActionList - it will be as much of the context a
// Single control message is able to carry and in DDRActionlist will be the whole context
typedef struct {
    bool is_first_chunk_per_context;
    bool is_last_chunk_per_context;
    uint8_t context_type; // CONTROL_PROTOCOL__context_switch_context_type_t
    uint32_t context_network_data_length;
    uint8_t context_network_data[CONTROL_PROTOCOL__MAX_CONTEXT_SIZE];
} CONTROL_PROTOCOL__context_switch_context_info_chunk_t;

CASSERT(sizeof(CONTROL_PROTOCOL__context_switch_context_index_t)<=UINT8_MAX, control_protocol_h);
CASSERT(sizeof(CONTROL_PROTOCOL__context_switch_context_type_t)<=UINT8_MAX, control_protocol_h);

typedef enum {
    CONTROL_PROTOCOL__MESSAGE_TYPE__REQUEST = 0,
    CONTROL_PROTOCOL__MESSAGE_TYPE__RESPONSE,
} CONTROL_PROTOCOL__message_type_t;

typedef enum {
    CONTROL_PROTOCOL__COMMUNICATION_TYPE_UDP = 0,
    CONTROL_PROTOCOL__COMMUNICATION_TYPE_MIPI,
    CONTROL_PROTOCOL__COMMUNICATION_TYPE_PCIE,
    CONTROL_PROTOCOL__COMMUNICATION_TYPE_INTER_CPU,

    /* Must be last! */
    CONTROL_PROTOCOL__COMMUNICATION_TYPE_COUNT
} CONTROL_PROTOCOL__communication_type_t;

typedef enum {
    CONTROL_PROTOCOL__RESET_TYPE__CHIP = 0,
    CONTROL_PROTOCOL__RESET_TYPE__NN_CORE,
    CONTROL_PROTOCOL__RESET_TYPE__SOFT,
    CONTROL_PROTOCOL__RESET_TYPE__FORCED_SOFT,

    /* Must be last! */
    CONTROL_PROTOCOL__RESET_TYPE__COUNT
} CONTROL_PROTOCOL__reset_type_t;

typedef union {
    /* Needed in order to parse unknown header */
    CONTROL_PROTOCOL__common_header_t common;

    CONTROL_PROTOCOL__request_header_t request;
    CONTROL_PROTOCOL__response_header_t response;
} CONTROL_PROTOCOL__message_header_t;

#ifdef __cplusplus
}
#endif

#endif /* __CONTROL_PROTOCOL_H__ */
