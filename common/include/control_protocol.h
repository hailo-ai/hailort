// SPDX-License-Identifier: GPL-2.0
/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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

#include "byte_order.h"
#include "firmware_version.h"
#include "md5.h"
#include "sensor_config_exports.h"
#include "type_utils.h"

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

#define CONTROL_PROTOCOL__REQUEST_BASE_SIZE (sizeof(uint32_t))
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

#pragma pack(push, 1) /* START OF FW-CONTROL PROTOCOL STRUCTURES */

#if defined(_MSC_VER)
// TODO: warning C4200
#pragma warning(push)
#pragma warning(disable: 4200)
#endif

typedef struct {
    uint32_t protocol_version;
    firmware_version_t fw_version;
    uint32_t logger_version;
    uint32_t board_name_length;
    uint8_t board_name[CONTROL_PROTOCOL__MAX_BOARD_NAME_LENGTH];
    uint32_t device_architecture;
    uint32_t serial_number_length;
    uint8_t serial_number[CONTROL_PROTOCOL__MAX_SERIAL_NUMBER_LENGTH];
    uint32_t part_number_length;
    uint8_t part_number[CONTROL_PROTOCOL__MAX_PART_NUMBER_LENGTH];
    uint32_t product_name_length;
    uint8_t product_name[CONTROL_PROTOCOL__MAX_PRODUCT_NAME_LENGTH];
} CONTROL_PROTOCOL_identify_response_t;

typedef struct {
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
    uint32_t address;
    uint32_t data_count;
} CONTROL_PROTOCOL__read_memory_request_t;

typedef struct {
    uint32_t data_length;
    uint8_t data[CONTROL_PROTOCOL__MAX_READ_MEMORY_DATA_SIZE];
} CONTROL_PROTOCOL__read_memory_response_t;

typedef struct {
    uint32_t address;
    uint32_t data_length;
    uint8_t  data[0];
} CONTROL_PROTOCOL__write_memory_request_t;

// Tightly coupled with hailo_fw_logger_interface_t
typedef enum {
    CONTROL_PROTOCOL__INTERFACE_PCIE = 1 << 0,
    CONTROL_PROTOCOL__INTERFACE_UART = 1 << 1
} CONTROL_PROTOCOL_interface_t;

#define CONTROL_PROTOCOL__FW_MAX_LOGGER_LEVEL (FW_LOGGER_LEVEL_FATAL)

#define CONTROL_PROTOCOL__FW_MAX_LOGGER_INTERFACE (CONTROL_PROTOCOL__INTERFACE_PCIE | CONTROL_PROTOCOL__INTERFACE_UART)

typedef struct {
    uint8_t level;  // CONTROL_PROTOCOL_interface_t
    uint8_t logger_interface_bit_mask;
} CONTROL_PROTOCOL__set_fw_logger_request_t;

typedef struct {
    bool should_activate;
} CONTROL_PROTOCOL__set_throttling_state_request_t;

typedef struct {
    bool is_active;
} CONTROL_PROTOCOL__get_throttling_state_response_t;

typedef struct {
    bool should_activate;
} CONTROL_PROTOCOL__set_overcurrent_state_request_t;

typedef struct {
    uint8_t sleep_state; /* of type CONTROL_PROTOCOL__sleep_state_t */
} CONTROL_PROTOCOL__set_sleep_state_request_t;

typedef struct {
    bool is_required;
} CONTROL_PROTOCOL__get_overcurrent_state_response_t;

typedef struct {
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
    uint8_t stream_index;
    uint8_t is_input;
    uint32_t communication_type;
    uint8_t skip_nn_stream_config;
    uint8_t power_mode; // CONTROL_PROTOCOL__power_mode_t
    CONTROL_PROTOCOL__nn_stream_config_t nn_stream_config;
    CONTROL_PROTOCOL__communication_config_prams_t communication_params;
} CONTROL_PROTOCOL__config_stream_request_t;

typedef struct {
    uint8_t dataflow_manager_id;
    uint8_t is_input;
} CONTROL_PROTOCOL__open_stream_request_t;

typedef struct {
    uint8_t dataflow_manager_id;
    uint8_t is_input;
} CONTROL_PROTOCOL__close_stream_request_t;

typedef struct {
    uint32_t operation_type;
} CONTROL_PROTOCOL__phy_operation_request_t;

typedef struct {
    uint8_t rx_pause_frames_enable;
} CONTROL_PROTOCOL__set_pause_frames_t;

typedef struct {
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
    uint32_t config_type;
    CONTROL_PROTOCOL__config_core_top_params_t config_params;
} CONTROL_PROTOCOL__config_core_top_request_t;

typedef struct {
    uint32_t dvm;
    uint32_t measurement_type;
} CONTROL_PROTOCOL__power_measurement_request_t;

typedef struct {
    float32_t power_measurement;
    uint32_t dvm;
    uint32_t measurement_type;
} CONTROL_PROTOCOL__power_measurement_response_t;

typedef struct {
    uint32_t index;
    uint32_t dvm;
    uint32_t measurement_type;
} CONTROL_PROTOCOL__set_power_measurement_request_t;

typedef struct {
    uint32_t dvm;
    uint32_t measurement_type;
} CONTROL_PROTOCOL__set_power_measurement_response_t;

typedef struct {
    uint32_t index;
    uint8_t should_clear;
} CONTROL_PROTOCOL__get_power_measurement_request_t;

typedef struct {
    uint32_t total_number_of_samples;
    float32_t min_value;
    float32_t max_value;
    float32_t average_value;
    float32_t average_time_value_milliseconds;
} CONTROL_PROTOCOL__get_power_measurement_response_t;

typedef struct {
    uint32_t delay_milliseconds;
    uint16_t averaging_factor;
    uint16_t sampling_period;
} CONTROL_PROTOCOL__start_power_measurement_request_t;

typedef struct {
    uint8_t endianness;
    uint16_t slave_address;
    uint8_t register_address_size;
    uint8_t bus_index;
    uint8_t should_hold_bus;
} CONTROL_PROTOCOL__i2c_slave_config_t;

typedef struct {
    CONTROL_PROTOCOL__i2c_slave_config_t slave_config;
    uint32_t register_address_size;
    uint32_t register_address;
    uint32_t data_length;
    uint8_t data[0];
} CONTROL_PROTOCOL__i2c_write_request_t;

typedef struct {
    CONTROL_PROTOCOL__i2c_slave_config_t slave_config;
    uint32_t register_address_size;
    uint32_t register_address;
    uint32_t data_length;
} CONTROL_PROTOCOL__i2c_read_request_t;

typedef struct {
    uint32_t data_length;
    uint8_t data[CONTROL_PROTOCOL__MAX_I2C_REGISTER_SIZE];
} CONTROL_PROTOCOL__i2c_read_response_t;

typedef struct {
    uint32_t offset;
    uint32_t data_length;
    uint8_t data[0];
} CONTROL_PROTOCOL__write_firmware_update_request_t;

typedef struct {
    MD5_SUM_t expected_md5;
    uint32_t firmware_size;
} CONTROL_PROTOCOL__validate_firmware_update_request_t;

typedef CONTROL_PROTOCOL__write_firmware_update_request_t CONTROL_PROTOCOL__write_second_stage_to_internal_memory_request_t;
typedef struct {
    MD5_SUM_t expected_md5;
    uint32_t second_stage_size;
} CONTROL_PROTOCOL__copy_second_stage_to_flash_request_t; 

typedef struct {
    uint32_t version;
    uint32_t entry_count;
    uint32_t total_size;
} CONTROL_PROTOCOL__examine_user_config_response_t;

typedef struct {
    uint8_t latency_measurement_en;
    uint32_t inbound_start_buffer_number;
    uint32_t outbound_stop_buffer_number;
    uint32_t inbound_stream_index;
    uint32_t outbound_stream_index;
} CONTROL_PROTOCOL__latency_config_request_t;

typedef struct {
    uint32_t section_index;
    uint32_t is_first;
    uint32_t start_offset;
    uint32_t reset_data_size;
    uint32_t sensor_type;
    uint32_t total_data_size;
    uint16_t config_height;
    uint16_t config_width;
    uint16_t config_fps;
    uint32_t config_name_length;
    uint8_t  config_name[MAX_CONFIG_NAME_LEN];
    uint32_t data_length;
    uint8_t data[0];
} CONTROL_PROTOCOL__sensor_store_config_request_t;

typedef struct {
    uint32_t section_index;
    uint32_t offset;
    uint32_t data_size;
} CONTROL_PROTOCOL__sensor_get_config_request_t;

typedef struct {
    uint32_t sensor_type;
    uint32_t i2c_bus_index;
} CONTROL_PROTOCOL__sensor_set_i2c_bus_index_t;

typedef struct {
    uint32_t section_index;
} CONTROL_PROTOCOL__sensor_load_config_request_t;

typedef struct {
    uint16_t slave_address;
    uint8_t register_address_size;
    uint8_t  bus_index;
    uint8_t should_hold_bus;
    uint8_t endianness;
}CONTROL_PROTOCOL__sensor_set_generic_i2c_slave_request_t;

typedef struct {
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
    uint8_t should_enable;
} CONTROL_PROTOCOL__wd_enable_request_t;

typedef struct {
    uint32_t wd_cycles;
    uint8_t wd_mode;
} CONTROL_PROTOCOL__wd_config_request_t;

/* TODO: Define bit struct (SDK-14509). */
typedef uint32_t CONTROL_PROTOCOL__system_state_t;
typedef struct {
    CONTROL_PROTOCOL__system_state_t system_state;
} CONTROL_PROTOCOL__previous_system_state_response_t;

typedef struct {
    float32_t ts0_temperature;
    float32_t ts1_temperature;
    uint16_t sample_count;
} CONTROL_PROTOCOL__temperature_info_t;

typedef struct {
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
    uint64_t dma_addr_handle; // Translated from a handle to a dma-address in the firmware.
    uint16_t desc_page_size;
    uint32_t total_desc_count; // TODO HRT-9913: Some descs may be uninitialized. Possibly add extra field here.
    uint32_t bytes_in_pattern;
} CONTROL_PROTOCOL__host_buffer_info_t;

typedef struct {
    uint8_t handle_count;
    uint64_t handles[CONTROL_PROTOCOL__MAX_VDMA_CHANNELS_PER_ENGINE];
    uint64_t dma_addrs[CONTROL_PROTOCOL__MAX_VDMA_CHANNELS_PER_ENGINE];
} CONTROL_PROTOCOL__context_switch_dma_addr_translation_table_t;

typedef struct {
    CONTROL_PROTOCOL__context_switch_dma_addr_translation_table_t translation_table;
    uint8_t is_first_chunk_per_context;
    uint8_t is_last_chunk_per_context;
    uint8_t context_type; // CONTROL_PROTOCOL__context_switch_context_type_t
    uint32_t context_network_data_length;
    uint8_t context_network_data[0];
} CONTROL_PROTOCOL__context_switch_set_context_info_request_t;

typedef CONTROL_PROTOCOL__read_memory_request_t CONTROL_PROTOCOL__read_user_config_request_t;
typedef CONTROL_PROTOCOL__read_memory_response_t CONTROL_PROTOCOL__read_user_config_response_t;
typedef CONTROL_PROTOCOL__write_memory_request_t CONTROL_PROTOCOL__write_user_config_request_t;

typedef struct {
    uint8_t measurement_enable;
} CONTROL_PROTOCOL__idle_time_set_measurement_request_t;

typedef struct {
    uint64_t idle_time_ns;
} CONTROL_PROTOCOL__idle_time_get_measurement_response_t;

typedef struct {
    uint32_t network_group_id;
    uint8_t context_type; // CONTROL_PROTOCOL__context_switch_context_type_t
    uint16_t context_index;
    uint16_t action_list_offset;
} CONTROL_PROTOCOL__download_context_action_list_request_t;

typedef struct {
    uint32_t base_address;
    uint8_t is_action_list_end;
    uint32_t batch_counter;
    uint32_t idle_time;
    uint32_t action_list_length;
    uint8_t action_list[0];
} CONTROL_PROTOCOL__download_context_action_list_response_t;

typedef enum {
    CONTROL_PROTOCOL__CONTEXT_SWITCH_STATUS_RESET = 0,
    CONTROL_PROTOCOL__CONTEXT_SWITCH_STATUS_ENABLED,
    CONTROL_PROTOCOL__CONTEXT_SWITCH_STATUS_PAUSED,

    /* must be last*/
    CONTROL_PROTOCOL__CONTEXT_SWITCH_STATUS_COUNT,
} CONTROL_PROTOCOL__CONTEXT_SWITCH_STATUS_t;

#define CONTROL_PROTOCOL__INIFINITE_BATCH_COUNT (0)
typedef struct {
    uint8_t state_machine_status;
    uint8_t application_index;
    uint16_t dynamic_batch_size;
    uint16_t batch_count;
} CONTROL_PROTOCOL__change_context_switch_status_request_t;

typedef struct {
    uint8_t interrupt_type;
    uint8_t interrupt_index;
    uint8_t interrupt_sub_index;
} CONTROL_PROTOCOL__set_dataflow_interrupt_request_t;

typedef struct {
    uint8_t  connection_type;
    uint32_t host_ip_address;
    uint16_t host_port;
}CONTROL_PROTOCOL__d2h_event_manager_set_new_host_info_request_t;

typedef struct {
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
    uint32_t neural_network_core_clock_rate;
    CONTROL_PROTOCOL__supported_features_t supported_features;
    uint32_t boot_source; /*CONTROL_PROTOCOL__boot_source_t*/
    uint8_t lcs;
    uint32_t soc_id_length;
    uint8_t soc_id[CONTROL_PROTOCOL__SOC_ID_LENGTH];
    uint32_t eth_mac_length;
    uint8_t eth_mac_address[MAC_ADDR_BYTES_LEN];
    CONTROL_PROTOCOL_fuse_info_t fuse_info;
    uint32_t pd_info_length;
    uint8_t pd_info[PM_RESULTS_LENGTH];
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
    bool overcurrent_protection_active;
    uint8_t current_overcurrent_zone;
    float32_t red_overcurrent_threshold;
    bool overcurrent_throttling_active;
    bool temperature_throttling_active;
    uint8_t current_temperature_zone;
    int8_t current_temperature_throttling_level;
    uint32_t temperature_throttling_levels_length;
    CONTROL_PROTOCOL__throttling_level_t temperature_throttling_levels[MAX_TEMPERATURE_THROTTLING_LEVELS_NUMBER];
    int32_t orange_temperature_threshold;
    int32_t orange_hysteresis_temperature_threshold;
    int32_t red_temperature_threshold;
    int32_t red_hysteresis_temperature_threshold;
    uint32_t requested_overcurrent_clock_freq;
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
    uint32_t breakpoint_id;
    uint8_t breakpoint_control;
    CONTROL_PROTOCOL__context_switch_breakpoint_data_t breakpoint_data;
} CONTROL_PROTOCOL__config_context_switch_breakpoint_request_t;

typedef struct {
    uint32_t breakpoint_id;
} CONTROL_PROTOCOL__get_context_switch_breakpoint_status_request_t;

typedef struct {
    uint8_t breakpoint_status;
} CONTROL_PROTOCOL__get_context_switch_breakpoint_status_response_t;

typedef struct {
    uint8_t is_rma;
} CONTROL_PROTOCOL__enable_debugging_request_t;

typedef struct {
    CONTROL_PROTOCOL__context_switch_main_header_t main_header;
} CONTROL_PROTOCOL__get_context_switch_main_header_response_t;

typedef struct {
    uint16_t batch_index;
    uint8_t enable_user_configuration;
} CONTROL_PROTOCOL__config_context_switch_timestamp_request_t;

typedef struct {
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
    bool is_top_test;
    uint32_t top_bypass_bitmap;
    uint8_t cluster_index;
    uint32_t cluster_bypass_bitmap_0;
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
    CONTROL_PROTOCOL__hw_consts_t hw_consts;
} CONTROL_PROTOCOL__get_hw_consts_response_t;

/* TODO HRT-9545 - Return and hw only parse results */
typedef struct {
    bool infer_done;
    uint32_t infer_cycles;
} CONTROL_PROTOCOL__hw_only_infer_results_t;

typedef struct {
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

typedef struct {
    uint8_t hw_infer_state;
    uint8_t application_index;
    uint16_t dynamic_batch_size;
    uint16_t batch_count;
    CONTROL_PROTOCOL__hw_infer_channels_info_t channels_info;
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
    uint32_t opcode;
    CONTROL_PROTOCOL__request_parameters_t parameters;
} CONTROL_PROTOCOL__request_t;

/* NOTE: This is a temporary hack to avoid FW changes. Will be fixed soon. */
typedef CONTROL_PROTOCOL__request_t CONTROL_PROTOCOL__payload_t;

typedef struct {
    uint32_t major_status;
    uint32_t minor_status;
} CONTROL_PROTOCOL__status_t;

typedef struct {
    CONTROL_PROTOCOL__status_t status;
    CONTROL_PROTOCOL__response_parameters_t parameters;
} CONTROL_PROTOCOL__response_t;


#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#pragma pack(pop) /* END OF FW-CONTROL PROTOCOL STRUCTURES */

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
    CONTROL_PROTOCOL__context_switch_dma_addr_translation_table_t translation_table;
    bool is_first_chunk_per_context;
    bool is_last_chunk_per_context;
    uint8_t context_type; // CONTROL_PROTOCOL__context_switch_context_type_t
    uint32_t context_network_data_length;
    uint8_t context_network_data[CONTROL_PROTOCOL__MAX_CONTEXT_SIZE];
} CONTROL_PROTOCOL__context_switch_context_info_chunk_t;

typedef enum {
    CONTROL_PROTOCOL__COMMUNICATION_TYPE_UDP = 0, /* Deprecated */
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

/*****************************************************************************
 * Pack / unpack functions
 *
 * Pack returns the total request size and writes the opcode + parameter
 * fields (in device byte-order) into *request. Unpack performs an in-place
 * byte-order swap of the response struct's multi-byte fields and returns the
 * same pointer. Higher-level parsing (validation, reshape into public types)
 * lives in the libhailort C++ layer.
 ****************************************************************************/

static inline size_t CONTROL_PROTOCOL__pack_identify_request(CONTROL_PROTOCOL__request_t *request)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_IDENTIFY);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE;
}

static inline CONTROL_PROTOCOL_identify_response_t *CONTROL_PROTOCOL__unpack_identify_response(
    CONTROL_PROTOCOL_identify_response_t *response)
{
    response->protocol_version = BYTE_ORDER__dtohl(response->protocol_version);
    response->fw_version.firmware_major = BYTE_ORDER__dtohl(response->fw_version.firmware_major);
    response->fw_version.firmware_minor = BYTE_ORDER__dtohl(response->fw_version.firmware_minor);
    response->fw_version.firmware_revision = BYTE_ORDER__dtohl(response->fw_version.firmware_revision);
    response->logger_version = BYTE_ORDER__dtohl(response->logger_version);
    response->board_name_length = BYTE_ORDER__dtohl(response->board_name_length);
    response->device_architecture = BYTE_ORDER__dtohl(response->device_architecture);
    response->serial_number_length = BYTE_ORDER__dtohl(response->serial_number_length);
    response->part_number_length = BYTE_ORDER__dtohl(response->part_number_length);
    response->product_name_length = BYTE_ORDER__dtohl(response->product_name_length);
    return response;
}

static inline size_t CONTROL_PROTOCOL__pack_core_identify_request(CONTROL_PROTOCOL__request_t *request)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_CORE_IDENTIFY);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE;
}

static inline CONTROL_PROTOCOL__core_identify_response_t *CONTROL_PROTOCOL__unpack_core_identify_response(
    CONTROL_PROTOCOL__core_identify_response_t *response)
{
    response->fw_version.firmware_major = BYTE_ORDER__dtohl(response->fw_version.firmware_major);
    response->fw_version.firmware_minor = BYTE_ORDER__dtohl(response->fw_version.firmware_minor);
    response->fw_version.firmware_revision = BYTE_ORDER__dtohl(response->fw_version.firmware_revision);
    return response;
}

static inline size_t CONTROL_PROTOCOL__pack_set_fw_logger_request(CONTROL_PROTOCOL__request_t *request,
    uint8_t level, uint8_t interface_mask)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_SET_FW_LOGGER);
    request->parameters.set_fw_logger_request.level = level;
    request->parameters.set_fw_logger_request.logger_interface_bit_mask = interface_mask;
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__set_fw_logger_request_t);
}

static inline size_t CONTROL_PROTOCOL__pack_set_clock_freq_request(CONTROL_PROTOCOL__request_t *request, uint32_t clock_freq)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_SET_CLOCK_FREQ);
    request->parameters.set_clock_freq_request.clock_freq = BYTE_ORDER__htodl(clock_freq);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__set_clock_freq_request_t);
}

static inline size_t CONTROL_PROTOCOL__pack_set_throttling_state_request(CONTROL_PROTOCOL__request_t *request, bool should_activate)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_SET_THROTTLING_STATE);
    request->parameters.set_throttling_state_request.should_activate = should_activate;
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__set_throttling_state_request_t);
}

static inline size_t CONTROL_PROTOCOL__pack_get_throttling_state_request(CONTROL_PROTOCOL__request_t *request)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_GET_THROTTLING_STATE);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE;
}

static inline CONTROL_PROTOCOL__get_throttling_state_response_t *CONTROL_PROTOCOL__unpack_get_throttling_state_response(
    CONTROL_PROTOCOL__get_throttling_state_response_t *response)
{
    return response;
}

static inline size_t CONTROL_PROTOCOL__pack_set_overcurrent_state_request(CONTROL_PROTOCOL__request_t *request, bool should_activate)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_SET_OVERCURRENT_STATE);
    request->parameters.set_overcurrent_state_request.should_activate = should_activate;
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__set_overcurrent_state_request_t);
}

static inline size_t CONTROL_PROTOCOL__pack_get_overcurrent_state_request(CONTROL_PROTOCOL__request_t *request)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_GET_OVERCURRENT_STATE);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE;
}

static inline CONTROL_PROTOCOL__get_overcurrent_state_response_t *CONTROL_PROTOCOL__unpack_get_overcurrent_state_response(
    CONTROL_PROTOCOL__get_overcurrent_state_response_t *response)
{
    return response;
}

static inline size_t CONTROL_PROTOCOL__pack_get_hw_consts_request(CONTROL_PROTOCOL__request_t *request)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_GET_HW_CONSTS);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE;
}

static inline CONTROL_PROTOCOL__get_hw_consts_response_t *CONTROL_PROTOCOL__unpack_get_hw_consts_response(
    CONTROL_PROTOCOL__get_hw_consts_response_t *response)
{
    response->hw_consts.fifo_word_granularity_bytes = BYTE_ORDER__dtohl(response->hw_consts.fifo_word_granularity_bytes);
    response->hw_consts.max_periph_buffers_per_frame = BYTE_ORDER__dtohs(response->hw_consts.max_periph_buffers_per_frame);
    response->hw_consts.max_periph_bytes_per_buffer = BYTE_ORDER__dtohs(response->hw_consts.max_periph_bytes_per_buffer);
    response->hw_consts.max_acceptable_bytes_per_buffer = BYTE_ORDER__dtohs(response->hw_consts.max_acceptable_bytes_per_buffer);
    response->hw_consts.outbound_data_stream_size = BYTE_ORDER__dtohl(response->hw_consts.outbound_data_stream_size);
    response->hw_consts.default_initial_credit_size = BYTE_ORDER__dtohl(response->hw_consts.default_initial_credit_size);
    return response;
}

static inline size_t CONTROL_PROTOCOL__pack_write_memory_request(CONTROL_PROTOCOL__request_t *request,
    uint32_t address, const uint8_t *data, uint32_t data_length)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_WRITE_MEMORY);
    request->parameters.write_memory_request.address = BYTE_ORDER__htodl(address);
    request->parameters.write_memory_request.data_length = BYTE_ORDER__htodl(data_length);
    memcpy(&request->parameters.write_memory_request.data, data, data_length);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__write_memory_request_t) + data_length;
}

static inline size_t CONTROL_PROTOCOL__pack_read_memory_request(CONTROL_PROTOCOL__request_t *request,
    uint32_t address, uint32_t data_length)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_READ_MEMORY);
    request->parameters.read_memory_request.address = BYTE_ORDER__htodl(address);
    request->parameters.read_memory_request.data_count = BYTE_ORDER__htodl(data_length);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__read_memory_request_t);
}

static inline CONTROL_PROTOCOL__read_memory_response_t *CONTROL_PROTOCOL__unpack_read_memory_response(
    CONTROL_PROTOCOL__read_memory_response_t *response)
{
    response->data_length = BYTE_ORDER__dtohl(response->data_length);
    return response;
}

static inline size_t CONTROL_PROTOCOL__pack_open_stream_request(CONTROL_PROTOCOL__request_t *request,
    uint8_t dataflow_manager_id, uint8_t is_input)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_OPEN_STREAM);
    request->parameters.open_stream_request.dataflow_manager_id = dataflow_manager_id;
    request->parameters.open_stream_request.is_input = is_input;
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__open_stream_request_t);
}

static inline size_t CONTROL_PROTOCOL__pack_close_stream_request(CONTROL_PROTOCOL__request_t *request,
    uint8_t dataflow_manager_id, uint8_t is_input)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_CLOSE_STREAM);
    request->parameters.close_stream_request.dataflow_manager_id = dataflow_manager_id;
    request->parameters.close_stream_request.is_input = is_input;
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__close_stream_request_t);
}

static inline void control_protocol__swap_config_stream_common(
    CONTROL_PROTOCOL__config_stream_request_t *cs)
{
    cs->communication_type = BYTE_ORDER__htodl(cs->communication_type);
    cs->nn_stream_config.core_bytes_per_buffer    = BYTE_ORDER__htods(cs->nn_stream_config.core_bytes_per_buffer);
    cs->nn_stream_config.core_buffers_per_frame   = BYTE_ORDER__htods(cs->nn_stream_config.core_buffers_per_frame);
    cs->nn_stream_config.periph_bytes_per_buffer  = BYTE_ORDER__htods(cs->nn_stream_config.periph_bytes_per_buffer);
    cs->nn_stream_config.periph_buffers_per_frame = BYTE_ORDER__htods(cs->nn_stream_config.periph_buffers_per_frame);
    cs->nn_stream_config.feature_padding_payload  = BYTE_ORDER__htods(cs->nn_stream_config.feature_padding_payload);
    cs->nn_stream_config.buffer_padding_payload   = BYTE_ORDER__htodl(cs->nn_stream_config.buffer_padding_payload);
    cs->nn_stream_config.buffer_padding           = BYTE_ORDER__htods(cs->nn_stream_config.buffer_padding);
}

static inline size_t CONTROL_PROTOCOL__pack_config_stream_mipi_input_request(CONTROL_PROTOCOL__request_t *request,
    const CONTROL_PROTOCOL__config_stream_request_t *params)
{
    CONTROL_PROTOCOL__config_stream_request_t *cs = &request->parameters.config_stream_request;
    CONTROL_PROTOCOL__mipi_common_config_params_t *common = &cs->communication_params.mipi_input.common_params;
    CONTROL_PROTOCOL__isp_config_params_t *isp = &cs->communication_params.mipi_input.isp_params;

    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_CONFIG_STREAM);
    request->parameters.config_stream_request = *params;
    control_protocol__swap_config_stream_common(cs);

    common->img_width_pixels  = BYTE_ORDER__htods(common->img_width_pixels);
    common->img_height_pixels = BYTE_ORDER__htods(common->img_height_pixels);
    common->data_rate         = BYTE_ORDER__htodl(common->data_rate);
    isp->isp_crop_output_width_pixels               = BYTE_ORDER__htods(isp->isp_crop_output_width_pixels);
    isp->isp_crop_output_height_pixels              = BYTE_ORDER__htods(isp->isp_crop_output_height_pixels);
    isp->isp_crop_output_width_start_offset_pixels  = BYTE_ORDER__htods(isp->isp_crop_output_width_start_offset_pixels);
    isp->isp_crop_output_height_start_offset_pixels = BYTE_ORDER__htods(isp->isp_crop_output_height_start_offset_pixels);
    isp->isp_run_time_calculations_interval_ms      = BYTE_ORDER__htods(isp->isp_run_time_calculations_interval_ms);

    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__config_stream_request_t)
        - sizeof(CONTROL_PROTOCOL__communication_config_prams_t)
        + sizeof(CONTROL_PROTOCOL__mipi_input_config_params_t);
}

static inline size_t CONTROL_PROTOCOL__pack_config_stream_mipi_output_request(CONTROL_PROTOCOL__request_t *request,
    const CONTROL_PROTOCOL__config_stream_request_t *params)
{
    CONTROL_PROTOCOL__config_stream_request_t *cs = &request->parameters.config_stream_request;
    CONTROL_PROTOCOL__mipi_common_config_params_t *common = &cs->communication_params.mipi_output.common_params;

    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_CONFIG_STREAM);
    request->parameters.config_stream_request = *params;
    control_protocol__swap_config_stream_common(cs);

    common->img_width_pixels  = BYTE_ORDER__htods(common->img_width_pixels);
    common->img_height_pixels = BYTE_ORDER__htods(common->img_height_pixels);
    common->data_rate         = BYTE_ORDER__htodl(common->data_rate);

    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__config_stream_request_t)
        - sizeof(CONTROL_PROTOCOL__communication_config_prams_t)
        + sizeof(CONTROL_PROTOCOL__mipi_output_config_params_t);
}

static inline size_t CONTROL_PROTOCOL__pack_config_stream_pcie_input_request(CONTROL_PROTOCOL__request_t *request,
    const CONTROL_PROTOCOL__config_stream_request_t *params)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_CONFIG_STREAM);
    request->parameters.config_stream_request = *params;

    control_protocol__swap_config_stream_common(&request->parameters.config_stream_request);

    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__config_stream_request_t)
        - sizeof(CONTROL_PROTOCOL__communication_config_prams_t)
        + sizeof(CONTROL_PROTOCOL__pcie_input_config_params_t);
}

static inline size_t CONTROL_PROTOCOL__pack_config_stream_pcie_output_request(CONTROL_PROTOCOL__request_t *request,
    const CONTROL_PROTOCOL__config_stream_request_t *params)
{
    CONTROL_PROTOCOL__config_stream_request_t *cs = &request->parameters.config_stream_request;

    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_CONFIG_STREAM);
    request->parameters.config_stream_request = *params;
    control_protocol__swap_config_stream_common(cs);

    cs->communication_params.pcie_output.desc_page_size =
        BYTE_ORDER__htods(cs->communication_params.pcie_output.desc_page_size);

    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__config_stream_request_t)
        - sizeof(CONTROL_PROTOCOL__communication_config_prams_t)
        + sizeof(CONTROL_PROTOCOL__pcie_output_config_params_t);
}

static inline CONTROL_PROTOCOL__config_stream_response_t *CONTROL_PROTOCOL__unpack_config_stream_response(
    CONTROL_PROTOCOL__config_stream_response_t *response)
{
    return response;
}

static inline size_t CONTROL_PROTOCOL__pack_reset_request(CONTROL_PROTOCOL__request_t *request,
    CONTROL_PROTOCOL__reset_type_t reset_type)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_RESET);
    request->parameters.reset_resquest.reset_type = BYTE_ORDER__htodl(reset_type);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__reset_request_t);
}

static inline size_t CONTROL_PROTOCOL__pack_power_measurement_request(CONTROL_PROTOCOL__request_t *request,
    CONTROL_PROTOCOL__dvm_options_t dvm, CONTROL_PROTOCOL__power_measurement_types_t measurement_type)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_POWER_MEASUEMENT);
    request->parameters.measure_power_request.dvm = BYTE_ORDER__htodl(dvm);
    request->parameters.measure_power_request.measurement_type = BYTE_ORDER__htodl(measurement_type);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__power_measurement_request_t);
}

static inline CONTROL_PROTOCOL__power_measurement_response_t *CONTROL_PROTOCOL__unpack_power_measurement_response(
    CONTROL_PROTOCOL__power_measurement_response_t *response)
{
    response->dvm = BYTE_ORDER__dtohl(response->dvm);
    response->measurement_type = BYTE_ORDER__dtohl(response->measurement_type);
    return response;
}

static inline size_t CONTROL_PROTOCOL__pack_set_power_measurement_request(CONTROL_PROTOCOL__request_t *request,
    uint32_t index, CONTROL_PROTOCOL__dvm_options_t dvm, CONTROL_PROTOCOL__power_measurement_types_t measurement_type)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_SET_POWER_MEASUEMENT);
    request->parameters.set_measure_power_request.index = BYTE_ORDER__htodl(index);
    request->parameters.set_measure_power_request.dvm = BYTE_ORDER__htodl(dvm);
    request->parameters.set_measure_power_request.measurement_type = BYTE_ORDER__htodl(measurement_type);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__set_power_measurement_request_t);
}

static inline CONTROL_PROTOCOL__set_power_measurement_response_t *CONTROL_PROTOCOL__unpack_set_power_measurement_response(
    CONTROL_PROTOCOL__set_power_measurement_response_t *response)
{
    response->dvm = BYTE_ORDER__dtohl(response->dvm);
    response->measurement_type = BYTE_ORDER__dtohl(response->measurement_type);
    return response;
}

static inline size_t CONTROL_PROTOCOL__pack_get_power_measurement_request(CONTROL_PROTOCOL__request_t *request,
    uint32_t index, bool should_clear)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_GET_POWER_MEASUEMENT);
    request->parameters.get_measure_power_request.index = BYTE_ORDER__htodl(index);
    request->parameters.get_measure_power_request.should_clear = (uint8_t)should_clear;
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__get_power_measurement_request_t);
}

static inline CONTROL_PROTOCOL__get_power_measurement_response_t *CONTROL_PROTOCOL__unpack_get_power_measurement_response(
    CONTROL_PROTOCOL__get_power_measurement_response_t *response)
{
    response->total_number_of_samples = BYTE_ORDER__dtohl(response->total_number_of_samples);
    return response;
}

static inline size_t CONTROL_PROTOCOL__pack_start_power_measurement_request(CONTROL_PROTOCOL__request_t *request,
    uint32_t delay_milliseconds, CONTROL_PROTOCOL__averaging_factor_t averaging_factor,
    CONTROL_PROTOCOL__sampling_period_t sampling_period)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_START_POWER_MEASUEMENT);
    request->parameters.start_measure_power_request.delay_milliseconds = BYTE_ORDER__htodl(delay_milliseconds);
    request->parameters.start_measure_power_request.averaging_factor = BYTE_ORDER__htods(averaging_factor);
    request->parameters.start_measure_power_request.sampling_period = BYTE_ORDER__htods(sampling_period);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__start_power_measurement_request_t);
}

static inline size_t CONTROL_PROTOCOL__pack_stop_power_measurement_request(CONTROL_PROTOCOL__request_t *request)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_STOP_POWER_MEASUEMENT);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE;
}

static inline size_t CONTROL_PROTOCOL__pack_i2c_write_request(CONTROL_PROTOCOL__request_t *request,
    uint32_t register_address, uint8_t endianness, uint16_t slave_address,
    uint8_t register_address_size, uint8_t bus_index, const uint8_t *data, uint32_t length)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_I2C_WRITE);
    request->parameters.i2c_write_request.register_address_size =
        BYTE_ORDER__htodl(sizeof(request->parameters.i2c_write_request.register_address));
    request->parameters.i2c_write_request.register_address = BYTE_ORDER__htodl(register_address);
    request->parameters.i2c_write_request.slave_config.endianness = endianness;
    request->parameters.i2c_write_request.slave_config.slave_address = BYTE_ORDER__htods(slave_address);
    request->parameters.i2c_write_request.slave_config.register_address_size = register_address_size;
    request->parameters.i2c_write_request.slave_config.bus_index = bus_index;
    request->parameters.i2c_write_request.slave_config.should_hold_bus = false;
    request->parameters.i2c_write_request.data_length = BYTE_ORDER__htodl(length);
    memcpy(&request->parameters.i2c_write_request.data, data, length);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__i2c_write_request_t) + length;
}

static inline size_t CONTROL_PROTOCOL__pack_i2c_read_request(CONTROL_PROTOCOL__request_t *request,
    uint32_t register_address, uint8_t endianness, uint16_t slave_address,
    uint8_t register_address_size, uint8_t bus_index, uint32_t length, bool should_hold_bus)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_I2C_READ);
    request->parameters.i2c_read_request.register_address_size =
        BYTE_ORDER__htodl(sizeof(request->parameters.i2c_read_request.register_address));
    request->parameters.i2c_read_request.register_address = BYTE_ORDER__htodl(register_address);
    request->parameters.i2c_read_request.slave_config.endianness = endianness;
    request->parameters.i2c_read_request.slave_config.slave_address = BYTE_ORDER__htods(slave_address);
    request->parameters.i2c_read_request.slave_config.register_address_size = register_address_size;
    request->parameters.i2c_read_request.slave_config.bus_index = bus_index;
    request->parameters.i2c_read_request.slave_config.should_hold_bus = should_hold_bus;
    request->parameters.i2c_read_request.data_length = BYTE_ORDER__htodl(length);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__i2c_read_request_t);
}

static inline CONTROL_PROTOCOL__i2c_read_response_t *CONTROL_PROTOCOL__unpack_i2c_read_response(
    CONTROL_PROTOCOL__i2c_read_response_t *response)
{
    response->data_length = BYTE_ORDER__dtohl(response->data_length);
    return response;
}

static inline size_t CONTROL_PROTOCOL__pack_phy_operation_request(CONTROL_PROTOCOL__request_t *request,
    CONTROL_PROTOCOL__phy_operation_t operation_type)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_PHY_OPERATION);
    request->parameters.phy_operation_request.operation_type = BYTE_ORDER__htodl(operation_type);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__phy_operation_request_t);
}

static inline size_t CONTROL_PROTOCOL__pack_config_core_top_request(CONTROL_PROTOCOL__request_t *request,
    CONTROL_PROTOCOL__config_core_top_type_t config_type,
    const CONTROL_PROTOCOL__config_core_top_params_t *params)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_CONFIG_CORE_TOP);
    request->parameters.config_core_top_request.config_type = BYTE_ORDER__htodl(config_type);
    request->parameters.config_core_top_request.config_params = *params;
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__config_core_top_request_t);
}

static inline size_t CONTROL_PROTOCOL__pack_set_pause_frames_request(CONTROL_PROTOCOL__request_t *request,
    uint8_t rx_pause_frames_enable)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_SET_PAUSE_FRAMES);
    request->parameters.set_pause_frames_request.rx_pause_frames_enable = rx_pause_frames_enable;
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__set_pause_frames_t);
}

static inline size_t CONTROL_PROTOCOL__pack_get_chip_temperature_request(CONTROL_PROTOCOL__request_t *request)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_GET_CHIP_TEMPERATURE);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE;
}

static inline CONTROL_PROTOCOL__get_chip_temperature_response_t *CONTROL_PROTOCOL__unpack_get_chip_temperature_response(
    CONTROL_PROTOCOL__get_chip_temperature_response_t *response)
{
    response->info.sample_count = BYTE_ORDER__dtohs(response->info.sample_count);
    return response;
}

static inline size_t CONTROL_PROTOCOL__pack_examine_user_config_request(CONTROL_PROTOCOL__request_t *request)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_EXAMINE_USER_CONFIG);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE;
}

static inline CONTROL_PROTOCOL__examine_user_config_response_t *CONTROL_PROTOCOL__unpack_examine_user_config_response(
    CONTROL_PROTOCOL__examine_user_config_response_t *response)
{
    response->version = BYTE_ORDER__dtohl(response->version);
    response->entry_count = BYTE_ORDER__dtohl(response->entry_count);
    response->total_size = BYTE_ORDER__dtohl(response->total_size);
    return response;
}

static inline size_t CONTROL_PROTOCOL__pack_read_user_config_request(CONTROL_PROTOCOL__request_t *request,
    uint32_t address, uint32_t data_length)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_READ_USER_CONFIG);
    request->parameters.read_user_config_request.address = BYTE_ORDER__htodl(address);
    request->parameters.read_user_config_request.data_count = BYTE_ORDER__htodl(data_length);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__read_user_config_request_t);
}

static inline CONTROL_PROTOCOL__read_user_config_response_t *CONTROL_PROTOCOL__unpack_read_user_config_response(
    CONTROL_PROTOCOL__read_user_config_response_t *response)
{
    return CONTROL_PROTOCOL__unpack_read_memory_response(response);
}

static inline size_t CONTROL_PROTOCOL__pack_write_user_config_request(CONTROL_PROTOCOL__request_t *request,
    uint32_t address, const uint8_t *data, uint32_t data_length)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_WRITE_USER_CONFIG);
    request->parameters.write_user_config_request.address = BYTE_ORDER__htodl(address);
    request->parameters.write_user_config_request.data_length = BYTE_ORDER__htodl(data_length);
    memcpy(&request->parameters.write_user_config_request.data, data, data_length);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__write_user_config_request_t) + data_length;
}

static inline size_t CONTROL_PROTOCOL__pack_erase_user_config_request(CONTROL_PROTOCOL__request_t *request)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_ERASE_USER_CONFIG);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE;
}

static inline size_t CONTROL_PROTOCOL__pack_read_board_config_request(CONTROL_PROTOCOL__request_t *request,
    uint32_t address, uint32_t data_length)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_READ_BOARD_CONFIG);
    request->parameters.read_board_config_request.address = BYTE_ORDER__htodl(address);
    request->parameters.read_board_config_request.data_count = BYTE_ORDER__htodl(data_length);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__read_board_config_request_t);
}

static inline CONTROL_PROTOCOL__read_board_config_response_t *CONTROL_PROTOCOL__unpack_read_board_config_response(
    CONTROL_PROTOCOL__read_board_config_response_t *response)
{
    return CONTROL_PROTOCOL__unpack_read_memory_response(response);
}

static inline size_t CONTROL_PROTOCOL__pack_write_board_config_request(CONTROL_PROTOCOL__request_t *request,
    uint32_t address, const uint8_t *data, uint32_t data_length)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_WRITE_BOARD_CONFIG);
    request->parameters.write_board_config_request.address = BYTE_ORDER__htodl(address);
    request->parameters.write_board_config_request.data_length = BYTE_ORDER__htodl(data_length);
    memcpy(&request->parameters.write_board_config_request.data, data, data_length);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__write_board_config_request_t) + data_length;
}

static inline size_t CONTROL_PROTOCOL__pack_start_firmware_update_request(CONTROL_PROTOCOL__request_t *request)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_START_FIRMWARE_UPDATE);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE;
}

static inline size_t CONTROL_PROTOCOL__pack_finish_firmware_update_request(CONTROL_PROTOCOL__request_t *request)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_FINISH_FIRMWARE_UPDATE);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE;
}

static inline size_t CONTROL_PROTOCOL__pack_write_firmware_update_request(CONTROL_PROTOCOL__request_t *request,
    uint32_t offset, const uint8_t *data, uint32_t data_length)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_WRITE_FIRMWARE_UPDATE);
    request->parameters.write_firmware_update_request.offset = BYTE_ORDER__htodl(offset);
    request->parameters.write_firmware_update_request.data_length = BYTE_ORDER__htodl(data_length);
    memcpy(&request->parameters.write_firmware_update_request.data, data, data_length);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__write_firmware_update_request_t) + data_length;
}

static inline size_t CONTROL_PROTOCOL__pack_validate_firmware_update_request(CONTROL_PROTOCOL__request_t *request,
    MD5_SUM_t *expected_md5, uint32_t firmware_size)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_VALIDATE_FIRMWARE_UPDATE);
    memcpy(&request->parameters.validate_firmware_update_request.expected_md5, *expected_md5,
        sizeof(request->parameters.validate_firmware_update_request.expected_md5));
    request->parameters.validate_firmware_update_request.firmware_size = BYTE_ORDER__htodl(firmware_size);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__validate_firmware_update_request_t);
}

static inline size_t CONTROL_PROTOCOL__pack_write_second_stage_to_internal_memory_request(CONTROL_PROTOCOL__request_t *request,
    uint32_t offset, const uint8_t *data, uint32_t data_length)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_WRITE_SECOND_STAGE_TO_INTERNAL_MEMORY);
    request->parameters.write_second_stage_to_internal_memory_request.offset = BYTE_ORDER__htodl(offset);
    request->parameters.write_second_stage_to_internal_memory_request.data_length = BYTE_ORDER__htodl(data_length);
    memcpy(&request->parameters.write_second_stage_to_internal_memory_request.data, data, data_length);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE
        + sizeof(CONTROL_PROTOCOL__write_second_stage_to_internal_memory_request_t) + data_length;
}

static inline size_t CONTROL_PROTOCOL__pack_copy_second_stage_to_flash_request(CONTROL_PROTOCOL__request_t *request,
    MD5_SUM_t *expected_md5, uint32_t second_stage_size)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_COPY_SECOND_STAGE_TO_FLASH);
    memcpy(&request->parameters.copy_second_stage_to_flash_request.expected_md5, *expected_md5,
        sizeof(request->parameters.copy_second_stage_to_flash_request.expected_md5));
    request->parameters.copy_second_stage_to_flash_request.second_stage_size = BYTE_ORDER__htodl(second_stage_size);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__copy_second_stage_to_flash_request_t);
}

static inline size_t CONTROL_PROTOCOL__pack_latency_measurement_config_request(CONTROL_PROTOCOL__request_t *request,
    uint8_t latency_measurement_en, uint32_t inbound_start_buffer_number, uint32_t outbound_stop_buffer_number,
    uint32_t inbound_stream_index, uint32_t outbound_stream_index)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_NN_CORE_LATENCY_MEASUREMENT_CONFIG);
    request->parameters.latency_config_request.latency_measurement_en = latency_measurement_en;
    request->parameters.latency_config_request.inbound_start_buffer_number =
        BYTE_ORDER__htodl(inbound_start_buffer_number);
    request->parameters.latency_config_request.outbound_stop_buffer_number =
        BYTE_ORDER__htodl(outbound_stop_buffer_number);
    request->parameters.latency_config_request.inbound_stream_index = BYTE_ORDER__htodl(inbound_stream_index);
    request->parameters.latency_config_request.outbound_stream_index = BYTE_ORDER__htodl(outbound_stream_index);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__latency_config_request_t);
}

static inline size_t CONTROL_PROTOCOL__pack_latency_measurement_read_request(CONTROL_PROTOCOL__request_t *request)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_NN_CORE_LATENCY_MEASUREMENT_READ);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE;
}

static inline CONTROL_PROTOCOL__latency_read_response_t *CONTROL_PROTOCOL__unpack_latency_read_response(
    CONTROL_PROTOCOL__latency_read_response_t *response)
{
    response->inbound_to_outbound_latency_nsec = BYTE_ORDER__dtohl(response->inbound_to_outbound_latency_nsec);
    return response;
}

static inline size_t CONTROL_PROTOCOL__pack_sensor_store_config_request(CONTROL_PROTOCOL__request_t *request,
    uint32_t is_first, uint32_t section_index, uint32_t start_offset, uint32_t reset_data_size, uint32_t sensor_type,
    uint32_t total_data_size, const uint8_t *data, uint32_t data_length, uint16_t config_height,
    uint16_t config_width, uint16_t config_fps, uint32_t config_name_length, const uint8_t *config_name)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_SENSOR_STORE_CONFIG);
    request->parameters.sensor_store_config_request.section_index = BYTE_ORDER__htodl(section_index);
    request->parameters.sensor_store_config_request.is_first = BYTE_ORDER__htodl(is_first);
    request->parameters.sensor_store_config_request.start_offset = BYTE_ORDER__htodl(start_offset);
    request->parameters.sensor_store_config_request.reset_data_size = BYTE_ORDER__htodl(reset_data_size);
    request->parameters.sensor_store_config_request.sensor_type = BYTE_ORDER__htodl(sensor_type);
    request->parameters.sensor_store_config_request.total_data_size = BYTE_ORDER__htodl(total_data_size);
    request->parameters.sensor_store_config_request.config_width = BYTE_ORDER__htods(config_width);
    request->parameters.sensor_store_config_request.config_height = BYTE_ORDER__htods(config_height);
    request->parameters.sensor_store_config_request.config_fps = BYTE_ORDER__htods(config_fps);
    request->parameters.sensor_store_config_request.config_name_length = BYTE_ORDER__htodl(MAX_CONFIG_NAME_LEN);
    memcpy(&request->parameters.sensor_store_config_request.config_name, config_name, config_name_length);
    request->parameters.sensor_store_config_request.data_length = BYTE_ORDER__htodl(data_length);
    memcpy(&request->parameters.sensor_store_config_request.data, data, data_length);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__sensor_store_config_request_t) + data_length;
}

static inline size_t CONTROL_PROTOCOL__pack_sensor_get_config_request(CONTROL_PROTOCOL__request_t *request,
    uint32_t section_index, uint32_t offset, uint32_t data_length)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_SENSOR_GET_CONFIG);
    request->parameters.sensor_get_config_request.section_index = BYTE_ORDER__htodl(section_index);
    request->parameters.sensor_get_config_request.offset = BYTE_ORDER__htodl(offset);
    request->parameters.sensor_get_config_request.data_size = BYTE_ORDER__htodl(data_length);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__sensor_get_config_request_t);
}

static inline CONTROL_PROTOCOL__sensor_get_config_response_t *CONTROL_PROTOCOL__unpack_sensor_get_config_response(
    CONTROL_PROTOCOL__sensor_get_config_response_t *response)
{
    response->data_length = BYTE_ORDER__dtohl(response->data_length);
    return response;
}

static inline size_t CONTROL_PROTOCOL__pack_sensor_set_i2c_bus_index_request(CONTROL_PROTOCOL__request_t *request,
    uint32_t sensor_type, uint32_t bus_index)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_SENSOR_SET_I2C_BUS_INDEX);
    request->parameters.sensor_set_i2c_bus_index.sensor_type = BYTE_ORDER__htodl(sensor_type);
    request->parameters.sensor_set_i2c_bus_index.i2c_bus_index = BYTE_ORDER__htodl(bus_index);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__sensor_set_i2c_bus_index_t);
}

static inline size_t CONTROL_PROTOCOL__pack_sensor_load_and_start_config_request(CONTROL_PROTOCOL__request_t *request,
    uint32_t section_index)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_SENSOR_LOAD_AND_START);
    request->parameters.sensor_load_config_request.section_index = BYTE_ORDER__htodl(section_index);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__sensor_load_config_request_t);
}

static inline size_t CONTROL_PROTOCOL__pack_sensor_reset_request(CONTROL_PROTOCOL__request_t *request,
    uint32_t section_index)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_SENSOR_RESET);
    request->parameters.sensor_reset_request.section_index = BYTE_ORDER__htodl(section_index);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__sensor_reset_request_t);
}

static inline size_t CONTROL_PROTOCOL__pack_sensor_set_generic_i2c_slave_request(CONTROL_PROTOCOL__request_t *request,
    uint16_t slave_address, uint8_t register_address_size, uint8_t bus_index, uint8_t should_hold_bus,
    uint8_t endianness)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_SENSOR_SET_GENERIC_I2C_SLAVE);
    request->parameters.sensor_set_generic_i2c_slave_request.slave_address = BYTE_ORDER__htods(slave_address);
    request->parameters.sensor_set_generic_i2c_slave_request.register_address_size = register_address_size;
    request->parameters.sensor_set_generic_i2c_slave_request.bus_index = bus_index;
    request->parameters.sensor_set_generic_i2c_slave_request.should_hold_bus = should_hold_bus;
    request->parameters.sensor_set_generic_i2c_slave_request.endianness = endianness;
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__sensor_set_generic_i2c_slave_request_t);
}

static inline size_t CONTROL_PROTOCOL__pack_sensor_get_sections_info_request(CONTROL_PROTOCOL__request_t *request)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_SENSOR_GET_SECTIONS_INFO);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE;
}

static inline CONTROL_PROTOCOL__sensor_get_sections_info_response_t *CONTROL_PROTOCOL__unpack_sensor_get_sections_info_response(
    CONTROL_PROTOCOL__sensor_get_sections_info_response_t *response)
{
    response->data_length = BYTE_ORDER__dtohl(response->data_length);
    return response;
}

static inline size_t CONTROL_PROTOCOL__pack_idle_time_get_measuremment_request(CONTROL_PROTOCOL__request_t *request)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_IDLE_TIME_GET_MEASUREMENT);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE;
}

static inline CONTROL_PROTOCOL__idle_time_get_measurement_response_t *CONTROL_PROTOCOL__unpack_idle_time_get_measurement_response(
    CONTROL_PROTOCOL__idle_time_get_measurement_response_t *response)
{
    response->idle_time_ns = BYTE_ORDER__dtohll(response->idle_time_ns);
    return response;
}

static inline size_t CONTROL_PROTOCOL__pack_idle_time_set_measuremment_request(CONTROL_PROTOCOL__request_t *request,
    uint8_t measurement_enable)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_IDLE_TIME_SET_MEASUREMENT);
    request->parameters.idle_time_set_measurement_request.measurement_enable = measurement_enable;
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__idle_time_set_measurement_request_t);
}

static inline size_t CONTROL_PROTOCOL__pack_context_switch_set_network_group_header_request(
    CONTROL_PROTOCOL__request_t *request, const CONTROL_PROTOCOL__application_header_t *network_group_header)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_CONTEXT_SWITCH_SET_NETWORK_GROUP_HEADER);
    memcpy(&request->parameters.context_switch_set_network_group_header_request.application_header,
        network_group_header,
        sizeof(request->parameters.context_switch_set_network_group_header_request.application_header));
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE
        + sizeof(CONTROL_PROTOCOL__context_switch_set_network_group_header_request_t);
}

static inline size_t CONTROL_PROTOCOL__pack_context_switch_set_context_info_request(CONTROL_PROTOCOL__request_t *request,
    const CONTROL_PROTOCOL__context_switch_context_info_chunk_t *context_info)
{
    size_t i;
    const uint32_t copy_length = context_info->context_network_data_length;

    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_CONTEXT_SWITCH_SET_CONTEXT_INFO);
    request->parameters.context_switch_set_context_info_request.translation_table.handle_count =
        context_info->translation_table.handle_count;
    for (i = 0; i < context_info->translation_table.handle_count; i++) {
        request->parameters.context_switch_set_context_info_request.translation_table.handles[i] =
            BYTE_ORDER__htodll(context_info->translation_table.handles[i]);
    }
    for (i = 0; i < context_info->translation_table.handle_count; i++) {
        request->parameters.context_switch_set_context_info_request.translation_table.dma_addrs[i] =
            BYTE_ORDER__htodll(context_info->translation_table.dma_addrs[i]);
    }
    request->parameters.context_switch_set_context_info_request.is_first_chunk_per_context =
        context_info->is_first_chunk_per_context;
    request->parameters.context_switch_set_context_info_request.is_last_chunk_per_context =
        context_info->is_last_chunk_per_context;
    request->parameters.context_switch_set_context_info_request.context_type = context_info->context_type;

    request->parameters.context_switch_set_context_info_request.context_network_data_length =
        BYTE_ORDER__htodl(copy_length);
    memcpy(&request->parameters.context_switch_set_context_info_request.context_network_data,
        &context_info->context_network_data, copy_length);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE
        + sizeof(CONTROL_PROTOCOL__context_switch_set_context_info_request_t) + copy_length;
}

static inline size_t CONTROL_PROTOCOL__pack_context_switch_signal_cache_updated_request(CONTROL_PROTOCOL__request_t *request)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_CONTEXT_SWITCH_SIGNAL_CACHE_UPDATED);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE;
}

static inline size_t CONTROL_PROTOCOL__pack_context_switch_clear_configured_apps_request(CONTROL_PROTOCOL__request_t *request)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_CONTEXT_SWITCH_CLEAR_CONFIGURED_APPS);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE;
}

static inline size_t CONTROL_PROTOCOL__pack_download_context_action_list_request(CONTROL_PROTOCOL__request_t *request,
    uint32_t network_group_id, CONTROL_PROTOCOL__context_switch_context_type_t context_type, uint16_t context_index,
    uint16_t action_list_offset)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_DOWNLOAD_CONTEXT_ACTION_LIST);
    request->parameters.download_context_action_list_request.network_group_id = BYTE_ORDER__htodl(network_group_id);
    request->parameters.download_context_action_list_request.context_type = (uint8_t)context_type;
    request->parameters.download_context_action_list_request.context_index = BYTE_ORDER__htods(context_index);
    request->parameters.download_context_action_list_request.action_list_offset = BYTE_ORDER__htods(action_list_offset);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__download_context_action_list_request_t);
}

static inline CONTROL_PROTOCOL__download_context_action_list_response_t *
CONTROL_PROTOCOL__unpack_download_context_action_list_response(
    CONTROL_PROTOCOL__download_context_action_list_response_t *response)
{
    response->base_address       = BYTE_ORDER__dtohl(response->base_address);
    response->batch_counter      = BYTE_ORDER__dtohl(response->batch_counter);
    response->idle_time          = BYTE_ORDER__dtohl(response->idle_time);
    response->action_list_length = BYTE_ORDER__dtohl(response->action_list_length);
    return response;
}

static inline size_t CONTROL_PROTOCOL__pack_change_context_switch_status_request(CONTROL_PROTOCOL__request_t *request,
    CONTROL_PROTOCOL__CONTEXT_SWITCH_STATUS_t state_machine_status, uint8_t application_index,
    uint16_t dynamic_batch_size, uint16_t batch_count)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_CHANGE_CONTEXT_SWITCH_STATUS);
    request->parameters.change_context_switch_status_request.state_machine_status = (uint8_t)state_machine_status;
    request->parameters.change_context_switch_status_request.application_index = application_index;
    request->parameters.change_context_switch_status_request.dynamic_batch_size = BYTE_ORDER__htods(dynamic_batch_size);
    request->parameters.change_context_switch_status_request.batch_count = BYTE_ORDER__htods(batch_count);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__change_context_switch_status_request_t);
}

static inline size_t CONTROL_PROTOCOL__pack_wd_enable(CONTROL_PROTOCOL__request_t *request,
    uint8_t cpu_id, bool should_enable)
{
    const CONTROL_PROTOCOL__OPCODE_t opcode = (CPU_ID_CORE_CPU == cpu_id) ?
        HAILO_CONTROL_OPCODE_CORE_WD_ENABLE : HAILO_CONTROL_OPCODE_APP_WD_ENABLE;
    request->opcode = BYTE_ORDER__htodl(opcode);
    request->parameters.wd_enable_request.should_enable = should_enable;
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__wd_enable_request_t);
}

static inline size_t CONTROL_PROTOCOL__pack_wd_config(CONTROL_PROTOCOL__request_t *request,
    uint8_t cpu_id, uint32_t wd_cycles, CONTROL_PROTOCOL__WATCHDOG_MODE_t wd_mode)
{
    const CONTROL_PROTOCOL__OPCODE_t opcode = (CPU_ID_CORE_CPU == cpu_id) ?
        HAILO_CONTROL_OPCODE_CORE_WD_CONFIG : HAILO_CONTROL_OPCODE_APP_WD_CONFIG;
    request->opcode = BYTE_ORDER__htodl(opcode);
    request->parameters.wd_config_request.wd_cycles = BYTE_ORDER__htodl(wd_cycles);
    request->parameters.wd_config_request.wd_mode = (uint8_t)wd_mode;
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__wd_config_request_t);
}

static inline size_t CONTROL_PROTOCOL__pack_previous_system_state_request(CONTROL_PROTOCOL__request_t *request,
    uint8_t cpu_id)
{
    const CONTROL_PROTOCOL__OPCODE_t opcode = (CPU_ID_CORE_CPU == cpu_id) ?
        HAILO_CONTROL_OPCODE_CORE_PREVIOUS_SYSTEM_STATE : HAILO_CONTROL_OPCODE_APP_PREVIOUS_SYSTEM_STATE;
    request->opcode = BYTE_ORDER__htodl(opcode);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE;
}

static inline CONTROL_PROTOCOL__previous_system_state_response_t *CONTROL_PROTOCOL__unpack_previous_system_state_response(
    CONTROL_PROTOCOL__previous_system_state_response_t *response)
{
    response->system_state = BYTE_ORDER__dtohl(response->system_state);
    return response;
}

static inline size_t CONTROL_PROTOCOL__pack_set_dataflow_interrupt_request(CONTROL_PROTOCOL__request_t *request,
    uint8_t interrupt_type, uint8_t interrupt_index, uint8_t interrupt_sub_index)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_SET_DATAFLOW_INTERRUPT);
    request->parameters.set_dataflow_interrupt_request.interrupt_type = interrupt_type;
    request->parameters.set_dataflow_interrupt_request.interrupt_index = interrupt_index;
    request->parameters.set_dataflow_interrupt_request.interrupt_sub_index = interrupt_sub_index;
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__set_dataflow_interrupt_request_t);
}

static inline size_t CONTROL_PROTOCOL__pack_config_context_switch_breakpoint_request(CONTROL_PROTOCOL__request_t *request,
    uint8_t breakpoint_id, CONTROL_PROTOCOL__context_switch_breakpoint_control_t breakpoint_control,
    CONTROL_PROTOCOL__context_switch_breakpoint_data_t *breakpoint_data)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_CONFIG_CONTEXT_SWITCH_BREAKPOINT);
    request->parameters.config_context_switch_breakpoint_request.breakpoint_id = BYTE_ORDER__htodl(breakpoint_id);
    request->parameters.config_context_switch_breakpoint_request.breakpoint_control = (uint8_t)breakpoint_control;
    memcpy(&request->parameters.config_context_switch_breakpoint_request.breakpoint_data, breakpoint_data,
        sizeof(request->parameters.config_context_switch_breakpoint_request.breakpoint_data));
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE
        + sizeof(CONTROL_PROTOCOL__config_context_switch_breakpoint_request_t);
}

static inline size_t CONTROL_PROTOCOL__pack_get_context_switch_breakpoint_status_request(
    CONTROL_PROTOCOL__request_t *request, uint8_t breakpoint_id)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_GET_CONTEXT_SWITCH_BREAKPOINT_STATUS);
    request->parameters.get_context_switch_breakpoint_status_request.breakpoint_id =
        BYTE_ORDER__htodl(breakpoint_id);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE
        + sizeof(CONTROL_PROTOCOL__get_context_switch_breakpoint_status_request_t);
}

static inline CONTROL_PROTOCOL__get_context_switch_breakpoint_status_response_t *CONTROL_PROTOCOL__unpack_get_context_switch_breakpoint_status_response(
    CONTROL_PROTOCOL__get_context_switch_breakpoint_status_response_t *response)
{
    return response;
}

static inline size_t CONTROL_PROTOCOL__pack_get_context_switch_main_header_request(CONTROL_PROTOCOL__request_t *request)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_GET_CONTEXT_SWITCH_MAIN_HEADER);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE;
}

static inline CONTROL_PROTOCOL__get_context_switch_main_header_response_t *CONTROL_PROTOCOL__unpack_get_context_switch_main_header_response(
    CONTROL_PROTOCOL__get_context_switch_main_header_response_t *response)
{
    return response;
}

static inline size_t CONTROL_PROTOCOL__pack_config_context_switch_timestamp_request(CONTROL_PROTOCOL__request_t *request,
    uint16_t batch_index, bool enable_user_configuration)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_CONFIG_CONTEXT_SWITCH_TIMESTAMP);
    request->parameters.config_context_switch_timestamp_request.batch_index = BYTE_ORDER__htods(batch_index);
    request->parameters.config_context_switch_timestamp_request.enable_user_configuration = enable_user_configuration;
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE
        + sizeof(CONTROL_PROTOCOL__config_context_switch_timestamp_request_t);
}

static inline size_t CONTROL_PROTOCOL__pack_d2h_event_manager_set_host_info_request(CONTROL_PROTOCOL__request_t *request,
    uint8_t connection_type, uint16_t host_port, uint32_t host_ip_address)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_D2H_EVENT_MANAGER_SET_HOST_INFO);
    request->parameters.d2h_event_manager_set_new_host_info_request.connection_type = connection_type;
    request->parameters.d2h_event_manager_set_new_host_info_request.host_port = BYTE_ORDER__htods(host_port);
    request->parameters.d2h_event_manager_set_new_host_info_request.host_ip_address =
        BYTE_ORDER__htodl(host_ip_address);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE
        + sizeof(CONTROL_PROTOCOL__d2h_event_manager_set_new_host_info_request_t);
}

static inline size_t CONTROL_PROTOCOL__pack_d2h_event_manager_send_host_info_event_request(
    CONTROL_PROTOCOL__request_t *request, uint8_t event_priority)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_D2H_EVENT_MANAGER_SEND_EVENT_HOST_INFO);
    request->parameters.d2h_event_manager_send_host_info_event_request.priority = event_priority;
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE
        + sizeof(CONTROL_PROTOCOL__d2h_event_manager_send_host_info_event_request_t);
}

static inline size_t CONTROL_PROTOCOL__pack_enable_debugging_request(CONTROL_PROTOCOL__request_t *request, bool is_rma)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_ENABLE_DEBUGGING);
    request->parameters.enable_debugging_request.is_rma = is_rma;
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__enable_debugging_request_t);
}

static inline size_t CONTROL_PROTOCOL__pack_get_extended_device_information_request(CONTROL_PROTOCOL__request_t *request)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_GET_DEVICE_INFORMATION);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE;
}

static inline CONTROL_PROTOCOL__get_extended_device_information_response_t *CONTROL_PROTOCOL__unpack_get_extended_device_information_response(
    CONTROL_PROTOCOL__get_extended_device_information_response_t *response)
{
    response->neural_network_core_clock_rate = BYTE_ORDER__dtohl(response->neural_network_core_clock_rate);
    response->supported_features = BYTE_ORDER__dtohll(response->supported_features);
    response->boot_source = BYTE_ORDER__dtohl(response->boot_source);
    response->soc_id_length = BYTE_ORDER__dtohl(response->soc_id_length);
    response->eth_mac_length = BYTE_ORDER__dtohl(response->eth_mac_length);
    response->fuse_info.die_wafer_info = BYTE_ORDER__dtohl(response->fuse_info.die_wafer_info);
    response->pd_info_length = BYTE_ORDER__dtohl(response->pd_info_length);
    response->partial_clusters_layout_bitmap = BYTE_ORDER__dtohl(response->partial_clusters_layout_bitmap);
    return response;
}

static inline size_t CONTROL_PROTOCOL__pack_get_health_information_request(CONTROL_PROTOCOL__request_t *request)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_GET_HEALTH_INFORMATION);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE;
}

static inline CONTROL_PROTOCOL__get_health_information_response_t *CONTROL_PROTOCOL__unpack_get_health_information_response(
    CONTROL_PROTOCOL__get_health_information_response_t *response)
{
    /* float32_t fields (red_overcurrent_threshold and the throttling-level entries) are not byte-swapped here -
     * they rely on the always-little-endian wire format established by HRT-20552. */
    response->temperature_throttling_levels_length =
        BYTE_ORDER__dtohl(response->temperature_throttling_levels_length);
    response->orange_temperature_threshold = (int32_t)BYTE_ORDER__dtohl(response->orange_temperature_threshold);
    response->orange_hysteresis_temperature_threshold =
        (int32_t)BYTE_ORDER__dtohl(response->orange_hysteresis_temperature_threshold);
    response->red_temperature_threshold = (int32_t)BYTE_ORDER__dtohl(response->red_temperature_threshold);
    response->red_hysteresis_temperature_threshold =
        (int32_t)BYTE_ORDER__dtohl(response->red_hysteresis_temperature_threshold);
    response->requested_overcurrent_clock_freq = BYTE_ORDER__dtohl(response->requested_overcurrent_clock_freq);
    response->requested_temperature_clock_freq = BYTE_ORDER__dtohl(response->requested_temperature_clock_freq);
    return response;
}

static inline size_t CONTROL_PROTOCOL__pack_run_bist_test_request(CONTROL_PROTOCOL__request_t *request,
    bool is_top_test, uint32_t top_bypass_bitmap, uint8_t cluster_index, uint32_t cluster_bypass_bitmap_0,
    uint32_t cluster_bypass_bitmap_1)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_RUN_BIST_TEST);
    request->parameters.run_bist_test_request.is_top_test = is_top_test;
    request->parameters.run_bist_test_request.top_bypass_bitmap = BYTE_ORDER__htodl(top_bypass_bitmap);
    request->parameters.run_bist_test_request.cluster_index = cluster_index;
    request->parameters.run_bist_test_request.cluster_bypass_bitmap_0 = BYTE_ORDER__htodl(cluster_bypass_bitmap_0);
    request->parameters.run_bist_test_request.cluster_bypass_bitmap_1 = BYTE_ORDER__htodl(cluster_bypass_bitmap_1);
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__run_bist_test_request_t);
}

static inline size_t CONTROL_PROTOCOL__pack_set_sleep_state_request(CONTROL_PROTOCOL__request_t *request, uint8_t sleep_state)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_SET_SLEEP_STATE);
    request->parameters.set_sleep_state_request.sleep_state = sleep_state;
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__set_sleep_state_request_t);
}

static inline size_t CONTROL_PROTOCOL__pack_change_hw_infer_status_request(CONTROL_PROTOCOL__request_t *request,
    uint8_t hw_infer_state, uint8_t network_group_index, uint16_t dynamic_batch_size, uint16_t batch_count,
    CONTROL_PROTOCOL__hw_infer_channels_info_t *channels_info,
    CONTROL_PROTOCOL__boundary_channel_mode_t boundary_channel_mode)
{
    request->opcode = BYTE_ORDER__htodl(HAILO_CONTROL_OPCODE_CHANGE_HW_INFER_STATUS);
    request->parameters.change_hw_infer_status_request.hw_infer_state = hw_infer_state;
    request->parameters.change_hw_infer_status_request.application_index = network_group_index;
    request->parameters.change_hw_infer_status_request.dynamic_batch_size = BYTE_ORDER__htods(dynamic_batch_size);
    request->parameters.change_hw_infer_status_request.batch_count = BYTE_ORDER__htods(batch_count);
    memcpy(&request->parameters.change_hw_infer_status_request.channels_info, channels_info,
        sizeof(request->parameters.change_hw_infer_status_request.channels_info));
    request->parameters.change_hw_infer_status_request.boundary_channel_mode = (uint8_t)boundary_channel_mode;
    return CONTROL_PROTOCOL__REQUEST_BASE_SIZE + sizeof(CONTROL_PROTOCOL__change_hw_infer_status_request_t);
}

static inline CONTROL_PROTOCOL__change_hw_infer_status_response_t *CONTROL_PROTOCOL__unpack_change_hw_infer_status_response(
    CONTROL_PROTOCOL__change_hw_infer_status_response_t *response)
{
    response->results.infer_cycles = BYTE_ORDER__dtohl(response->results.infer_cycles);
    return response;
}

#ifdef __cplusplus
}
#endif

#endif /* __CONTROL_PROTOCOL_H__ */
