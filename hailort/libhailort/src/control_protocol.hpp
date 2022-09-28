/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file control_protocol.hpp
 * @brief Contains Defines and declarations related to control protocl
 **/

#ifndef _CONTROL_PROTOCOL_HPP_
#define _CONTROL_PROTOCOL_HPP_

#include "control_protocol.h"
#include "firmware_status.h"
#include "hailo/hailort.h"
#include <stdint.h>

typedef enum {
    HAILO8_CLOCK_RATE = 400 * 1000 * 1000,
    HAILO8R_CLOCK_RATE = 200 * 1000 * 1000
} CONTROL_PROTOCOL__HAILO8_CLOCK_RATE_t;

typedef struct {
    uint8_t stream_index;
    uint8_t is_input;
    uint32_t communication_type;
    uint8_t skip_nn_stream_config;
    uint8_t power_mode; // CONTROL_PROTOCOL__power_mode_t
    CONTROL_PROTOCOL__nn_stream_config_t nn_stream_config;
    CONTROL_PROTOCOL__communication_config_prams_t communication_params;
} CONTROL_PROTOCOL__config_stream_params_t;

/* Context switch user structs */
#define CONTROL_PROTCOL__CONTEXT_SLICING_OFFSETS \
    ((CONTROL_PROTOCOL__CONTEXT_NETWORK_DATA_MAX_SIZE / CONTROL_PROTOCOL__CONTEXT_NETWORK_DATA_SINGLE_CONTROL_MAX_SIZE) + 1)

typedef struct {
    /* Tempurary building info, used when creating meta data*/
    CONTROL_PROTOCOL__trigger_group_t *current_building_trigger;
    uint8_t current_building_slice_index;
    uint16_t current_buillding_offset_inside_slice;
    /* Control slicing information */
    uint16_t control_slicing_offsets[CONTROL_PROTCOL__CONTEXT_SLICING_OFFSETS];
    uint8_t slice_edge_layers[CONTROL_PROTCOL__CONTEXT_SLICING_OFFSETS];
    uint8_t slice_triggers[CONTROL_PROTCOL__CONTEXT_SLICING_OFFSETS];
} CONTEXT_SWITCH__context_control_slicing_data_t;

typedef struct {
    uint8_t cfg_channels_count;
    CONTROL_PROTOCOL__config_channel_info_t config_channel_infos[CONTROL_PROTOCOL__MAX_CFG_CHANNELS];
    uint32_t context_network_data_length;
    CONTROL_PROTOCOL__stream_remap_data_t context_stream_remap_data;
    uint8_t context_network_data[CONTROL_PROTOCOL__CONTEXT_NETWORK_DATA_MAX_SIZE];
    /* control_slicing_data is used in internal hef_metadata.cpp functions only */
    CONTEXT_SWITCH__context_control_slicing_data_t control_slicing_data;
} CONTROL_PROTOCOL__context_switch_context_info_t;

typedef struct {
    CONTROL_PROTOCOL__context_switch_main_header_t context_switch_main_header;
    CONTROL_PROTOCOL__context_switch_context_info_t context[CONTROL_PROTOCOL__MAX_TOTAL_CONTEXTS];
} CONTROL_PROTOCOL__context_switch_info_t;
/* End of context switch structs */

const char *CONTROL_PROTOCOL__get_textual_opcode(CONTROL_PROTOCOL__OPCODE_t opcode);

HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__parse_response(uint8_t *message,
        uint32_t message_size,
        CONTROL_PROTOCOL__response_header_t **header,
        CONTROL_PROTOCOL__payload_t **payload,
        CONTROL_PROTOCOL__status_t *fw_status);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__get_sequence_from_response_buffer(uint8_t *response_buffer,
        size_t response_buffer_size, uint32_t *sequence);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_identify_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_core_identify_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_read_memory_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint32_t address, uint32_t data_length);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_write_memory_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint32_t address, const uint8_t *data, uint32_t data_length);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_set_fw_logger_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, hailo_fw_logger_level_t level, uint8_t interface_mask);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_open_stream_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint8_t dataflow_manager_id, uint8_t is_input);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_config_stream_udp_input_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, CONTROL_PROTOCOL__config_stream_params_t *params);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_config_stream_udp_output_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, CONTROL_PROTOCOL__config_stream_params_t *params);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_config_stream_mipi_input_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, CONTROL_PROTOCOL__config_stream_params_t *params);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_config_stream_mipi_output_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, CONTROL_PROTOCOL__config_stream_params_t *params);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_config_stream_pcie_input_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, CONTROL_PROTOCOL__config_stream_params_t *params);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_config_stream_pcie_output_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, CONTROL_PROTOCOL__config_stream_params_t *params);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_close_stream_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint8_t dataflow_manager_id, uint8_t is_input);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_reset_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, CONTROL_PROTOCOL__reset_type_t reset_type);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_power_measurement_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, CONTROL_PROTOCOL__dvm_options_t dvm, CONTROL_PROTOCOL__power_measurement_types_t measurement_type);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_set_power_measurement_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint32_t index, CONTROL_PROTOCOL__dvm_options_t dvm, CONTROL_PROTOCOL__power_measurement_types_t measurement_type);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_get_power_measurement_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint32_t index, bool should_clear);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_start_power_measurement_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint32_t delay_milliseconds, CONTROL_PROTOCOL__averaging_factor_t averaging_factor , CONTROL_PROTOCOL__sampling_period_t sampling_period);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_stop_power_measurement_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_start_firmware_update_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_finish_firmware_update_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__write_firmware_update_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint32_t offset, const uint8_t *data, uint32_t data_length);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_validate_firmware_update_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, MD5_SUM_t *expected_md5, uint32_t firmware_size);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_examine_user_config(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_read_user_config(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint32_t address, uint32_t data_length);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_write_user_config_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint32_t address, const uint8_t *data, uint32_t data_length);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_erase_user_config_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_phy_operation_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, CONTROL_PROTOCOL__phy_operation_t operation_type);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_config_core_top_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, CONTROL_PROTOCOL__config_core_top_type_t config_type, CONTROL_PROTOCOL__config_core_top_params_t *params);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_i2c_write_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size,
        uint32_t sequence, uint32_t offset, uint8_t endianness,
        uint16_t slave_address, uint8_t register_address_size, uint8_t bus_index, const uint8_t *data, uint32_t data_length);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_i2c_read_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size,
        uint32_t sequence, uint32_t offset, uint8_t endianness,
        uint16_t slave_address, uint8_t register_address_size, uint8_t bus_index, uint32_t data_length, bool should_hold_bus);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_latency_measurement_read_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_latency_measurement_config_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint8_t latency_measurement_en, uint32_t inbound_start_buffer_number, uint32_t outbound_stop_buffer_number, uint32_t inbound_stream_index, uint32_t outbound_stream_index);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_sensor_store_config_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint32_t is_first, uint32_t section_index,
                                                                   uint32_t start_offset, uint32_t reset_data_size, uint32_t sensor_type, uint32_t total_data_size, uint8_t *data, uint32_t data_length,
                                                                   uint16_t config_height, uint16_t config_width, uint16_t config_fps, uint32_t config_name_length, uint8_t *config_name);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_sensor_get_config_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, 
                                                                       uint32_t section_index, uint32_t offset, uint32_t data_length);
hailo_status CONTROL_PROTOCOL__pack_sensor_set_i2c_bus_index_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint32_t sensor_type, uint32_t bus_index);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_sensor_load_and_start_config_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint32_t section_index);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_sensor_reset_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint32_t section_index);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_sensor_set_generic_i2c_slave_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint16_t slave_address,
                                                                                  uint8_t register_address_size, uint8_t bus_index, uint8_t should_hold_bus, uint8_t endianness);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_sensor_get_sections_info_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_context_switch_set_main_header_request(
        CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, 
        CONTROL_PROTOCOL__context_switch_main_header_t *context_switch_header);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_context_switch_set_context_info_request(
        CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, 
        CONTROL_PROTOCOL__context_switch_context_info_single_control_t *context_info);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_idle_time_set_measuremment_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint8_t measurement_enable);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_idle_time_get_measuremment_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_download_context_action_list_request(CONTROL_PROTOCOL__request_t *request, 
        size_t *request_size, uint32_t sequence, uint8_t context_index, uint16_t action_list_offset);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_change_context_switch_status_request(
        CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence,
        CONTROL_PROTOCOL__CONTEXT_SWITCH_STATUS_t state_machine_status, uint8_t application_index,
        uint16_t dynamic_batch_size, bool keep_nn_config_during_reset);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_wd_enable(
    CONTROL_PROTOCOL__request_t *request,
    size_t *request_size,
    uint32_t sequence,
    uint8_t cpu_id,
    bool should_enable);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_wd_config(
    CONTROL_PROTOCOL__request_t *request,
    size_t *request_size,
    uint32_t sequence,
    uint8_t cpu_id,
    uint32_t wd_cycles,
    CONTROL_PROTOCOL__WATCHDOG_MODE_t wd_mode);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_context_switch_clear_configured_apps_request(
    CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_previous_system_state(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint8_t cpu_id);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_set_dataflow_interrupt_request(
        CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, 
        uint8_t interrupt_type, uint8_t interrupt_index, uint8_t interrupt_sub_index);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_d2h_event_manager_set_host_info_request( CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, 
        uint8_t connection_type, uint16_t host_port, uint32_t host_ip_address);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_d2h_event_manager_send_host_info_event_request( CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, 
        uint8_t event_priority);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_get_chip_temperature_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_read_board_config(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint32_t address, uint32_t data_length);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_write_board_config_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, uint32_t address, const uint8_t *data, uint32_t data_length);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_enable_debugging_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, bool is_rma);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_get_extended_device_information_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_config_context_switch_breakpoint_request(
        CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence,
        uint8_t breakpoint_id, 
        CONTROL_PROTOCOL__context_switch_breakpoint_control_t breakpoint_control, 
        CONTROL_PROTOCOL__context_switch_breakpoint_data_t *breakpoint_data);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_get_context_switch_breakpoint_status_request(
        CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence,
        uint8_t breakpoint_id); 
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_get_context_switch_main_header_request(
        CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence); 
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__write_second_stage_to_internal_memory_request(
    CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence,
     uint32_t offset, uint8_t *data, uint32_t data_length);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__copy_second_stage_to_flash_request(
    CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence,
     MD5_SUM_t *expected_md5, uint32_t second_stage_size);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_set_pause_frames_request(CONTROL_PROTOCOL__request_t *request, 
            size_t *request_size, 
            uint32_t sequence, 
            uint8_t rx_pause_frames_enable);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_config_context_switch_timestamp_request(
        CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence,
        uint16_t batch_index, bool enable_user_configuration);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_run_bist_test_request(
        CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, bool is_top_test,
        uint32_t top_bypass_bitmap, uint8_t cluster_index, uint32_t cluster_bypass_bitmap_0, uint32_t cluster_bypass_bitmap_1);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_set_clock_freq_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence,
        uint32_t clock_freq);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_get_health_information_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_set_throttling_state_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, bool should_activate);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_get_throttling_state_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_set_overcurrent_state_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence, bool should_activate);
HAILO_COMMON_STATUS_t CONTROL_PROTOCOL__pack_get_overcurrent_state_request(CONTROL_PROTOCOL__request_t *request, size_t *request_size, uint32_t sequence);

#endif /* _CONTROL_PROTOCOL_HPP_ */