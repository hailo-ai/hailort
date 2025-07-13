/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file control.hpp
 * @brief Contains Defines and declarations related to controlling hailo8
 **/

#ifndef __CONTROL_HPP__
#define __CONTROL_HPP__

#include "hailo/hailort.h"
#include "hailo/device.hpp"

#include "device_common/control_protocol.hpp"

#include "control_protocol.h"
#include <stdbool.h>


namespace hailort
{

#define CONTROL__MAX_SEQUENCE (0xFFFFFFFF)
#define CONTROL__MAX_WRITE_MEMORY_CHUNK_SIZE (1024)

#define FW_MAGIC (0x1DD89DE0)
#define FW_SUPPORTED_HEADER_VERSION (0)
#define BOARD_CONFIG_SIZE (500)

/* TODO: Is this the correct size? */
#define RESPONSE_MAX_BUFFER_SIZE (2048)
#define WRITE_CHUNK_SIZE (1024)
#define WORD_SIZE (4)


class Control final
{
public:
    Control() = delete;

    static hailo_status parse_and_validate_response(uint8_t *message, uint32_t message_size, 
        CONTROL_PROTOCOL__response_header_t **header, CONTROL_PROTOCOL__payload_t **payload, 
        CONTROL_PROTOCOL__request_t *request, Device &device);

    /**
     * Receive information about the device.
     * 
     * @param[in]     device - The Hailo device.
     * @return The information about the board.
     */
    static Expected<hailo_device_identity_t> identify(Device &device);


    /**
     * Receive extended information about the device.
     * 
     * @param[in]     device - The Hailo device.
     * @return The extended information about the board.
     */
    static Expected<hailo_extended_device_information_t> get_extended_device_information(Device &device);

    /**
     * Receive information about the core cpu.
     * 
     * @param[in]     device - The Hailo device.
     * @param[out]    core_info - The information about the core cpu.
     * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a static hailo_status error.
     */
    static hailo_status core_identify(Device &device, hailo_core_information_t *core_info);

    /**
     * Configure a UDP input dataflow stream at a Hailo device.
     * 
     * @param[in]     device - The Hailo device.
     * @param[in]     params - The stream params that would be configured.
     * @param[out]    dataflow_manager_id - Unique id of the dataflow manager.
     * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a static hailo_status error.
     */
    static hailo_status config_stream_udp_input(Device &device, CONTROL_PROTOCOL__config_stream_params_t *params, uint8_t &dataflow_manager_id);

    /**
     * Configure a UDP output dataflow stream at a Hailo device.
     * 
     * @param[in]     device - The Hailo device.
     * @param[in]     params - The stream params that would be configured.
     * @param[out]    dataflow_manager_id - Unique id of the dataflow manager.
     * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a static hailo_status error.
     */
    static hailo_status config_stream_udp_output(Device &device, CONTROL_PROTOCOL__config_stream_params_t *params, uint8_t &dataflow_manager_id);

    /**
     * Configure a MIPI input dataflow stream at a Hailo device.
     * 
     * @param[in]     device - The Hailo device.
     * @param[in]     params - The stream params that would be configured.
     * @param[out]    dataflow_manager_id - Unique id of the dataflow manager.
     * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a static hailo_status error.
     */
    static hailo_status config_stream_mipi_input(Device &device, CONTROL_PROTOCOL__config_stream_params_t *params, uint8_t &dataflow_manager_id);

    /**
     * Configure a MIPI output dataflow stream at a Hailo device.
     * 
     * @param[in]     device - The Hailo device.
     * @param[in]     params - The stream params that would be configured.
     * @param[out]    dataflow_manager_id - Unique id of the dataflow manager.
     * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a static hailo_status error.
     */
    static hailo_status config_stream_mipi_output(Device &device, CONTROL_PROTOCOL__config_stream_params_t *params, uint8_t &dataflow_manager_id);

    /**
     * Configure a PCIe input dataflow stream at a Hailo device.
     * 
     * @param[in]     device - The Hailo device.
     * @param[in]     params - The stream params that would be configured.
     * @param[out]    dataflow_manager_id - Unique id of the dataflow manager.
     * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a static hailo_status error.
     */
    static hailo_status config_stream_pcie_input(Device &device, CONTROL_PROTOCOL__config_stream_params_t *params, uint8_t &dataflow_manager_id);

    /**
     * Configure a PCIe output dataflow stream at a Hailo device.
     * 
     * @param[in]     device - The Hailo device.
     * @param[in]     params - The stream params that would be configured.
     * @param[out]    dataflow_manager_id - Unique id of the dataflow manager.
     * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a static hailo_status error.
     */
    static hailo_status config_stream_pcie_output(Device &device, CONTROL_PROTOCOL__config_stream_params_t *params, uint8_t &dataflow_manager_id);

    /**
     * Open a stream at a Hailo device.
     * 
     * @param[in]     device - The Hailo device.
     * @param[in]     dataflow_manager_id - Unique id of the dataflow manager.
     * @param[in]     is_input - Indicates whether the stream is an input or an output.
     * @note The stream must be configured prior its opening;
     * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a static hailo_status error.
     */
    static hailo_status open_stream(Device &device, uint8_t dataflow_manager_id, bool is_input);

    /**
     * Close a stream at a Hailo device.
     * 
     * @param[in]     device - The Hailo device.
     * @param[in]     dataflow_manager_id - Unique id of the dataflow manager.
     * @param[in]     is_input - Indicates whether the stream is an input or an output.
     * @note 
     *      1.  A stream must be opened before closing.
     *      2.  A stream cannot be closed twice.
     *      3.  In order to close all the streams, call \ref close_all_streams.
     * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a static hailo_status error.
     */
    static hailo_status close_stream(Device &device, uint8_t dataflow_manager_id, bool is_input);
    static hailo_status close_all_streams(Device &device);

    /**
     * Get idle time accumulated measurement.
     * 
     * @param[in]     device - The Hailo device.
     * @param[out]    measurement - pointer to store the measurement
     * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a static hailo_status error.
     */
    static hailo_status idle_time_get_measurement(Device &device, uint64_t *measurement);

    /**
     * start/stop idle time measurement
     * 
     * @param[in]     device - The Hailo device.
     * @param[in]     measurement_enable - start/stop the measurement
     * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a static hailo_status error.
     */
    static hailo_status idle_time_set_measurement(Device &device, uint8_t measurement_enable);

    /**
     *  Start firmware update of a Hailo device.
     * 
     * @param[in]     device - The Hailo device.
     * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a static hailo_status error.
     */
    static hailo_status start_firmware_update(Device &device);
    static hailo_status write_firmware_update(Device &device, uint32_t offset, const uint8_t *data, uint32_t data_length);
    static hailo_status validate_firmware_update(Device &device, MD5_SUM_t *expected_md5, uint32_t firmware_size);
    static hailo_status finish_firmware_update(Device &device);
    static hailo_status write_second_stage_to_internal_memory(Device &device, uint32_t offset, uint8_t *data, uint32_t data_length);
    static hailo_status copy_second_stage_to_flash(Device &device, MD5_SUM_t *expected_md5, uint32_t second_stage_size);

    static hailo_status examine_user_config(Device &device, hailo_fw_user_config_information_t *info);

    static hailo_status read_user_config(Device &device, uint8_t *buffer, uint32_t buffer_length);

    static hailo_status write_user_config(Device &device, const uint8_t *data, uint32_t data_length);

    static hailo_status erase_user_config(Device &device);

    static hailo_status read_board_config(Device &device, uint8_t *buffer, uint32_t buffer_length);

    static hailo_status write_board_config(Device &device, const uint8_t *data, uint32_t data_length);

    static hailo_status phy_operation(Device &device, CONTROL_PROTOCOL__phy_operation_t operation_type);
    
    static hailo_status config_core_top(Device &device, CONTROL_PROTOCOL__config_core_top_type_t config_type,
        CONTROL_PROTOCOL__config_core_top_params_t *params);

    /**
     *  Write data to an I2C slave over a hailo device.
     * 
     * @param[in]     device - The Hailo device.
     * @param[in]     slave_config - The configuration of the slave.
     * @param[in]     register_address - The address of the register to which the data will be written
     * @param[in]     data - A pointer to a buffer that contains the data to be written to the slave.
     * @param[in]     length - The size of @a data in bytes.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    static hailo_status i2c_write(Device &device, const hailo_i2c_slave_config_t *slave_config, uint32_t register_address, 
        const uint8_t *data, uint32_t length);

    /**
     *  Read data from an I2C slave over a hailo device.
     * 
     * @param[in]     device - The Hailo device.
     * @param[in]     slave_config - The configuration of the slave.
     * @param[in]     register_address - The address of the register from which the data will be read.
     * @param[in]     data - Pointer to a buffer that would store the read data.
     * @param[in]     length - The number of bytes to read into the buffer pointed by @a data.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    static hailo_status i2c_read(Device &device, const hailo_i2c_slave_config_t *slave_config, uint32_t register_address,
        uint8_t *data, uint32_t length);

    /**
     *  Measure the latency of a single image at the nn core of a hailo device.
     * 
     * @param[in]     device - The Hailo device.
     * @param[in]     latency_measurement_en - Boolean if the latency should be enabled or not.
     * @param[in]     inbound_start_buffer_number - The inbound buffer from which the system start the latency measurement.
     * @param[in]     outbound_start_buffer_number - The outbound buffer from which the system ends the latency measurement.
     * @param[in]     inbound_stream_index - Which input stream to measure latency from.
     * @param[in]     outbound_stream_index - Which output stream to measure latency from.
     *
     * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a static hailo_status error.
     */
    static hailo_status latency_measurement_config(Device &device, uint8_t latency_measurement_en,

            uint32_t inbound_start_buffer_number, uint32_t outbound_stop_buffer_number, uint32_t inbound_stream_index,
            uint32_t outbound_stream_index);
    /**
     *  Read the measurement of the latency of a single image at the nn core of a hailo device.
     * 
     * @param[in]     device - The Hailo device.
     * @param[out]    inbound_to_outbound_latency_nsec - The latency in nanoseconds.
     *
     * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a static hailo_status error.
     */
    static hailo_status latency_measurement_read(Device &device, uint32_t *inbound_to_outbound_latency_nsec);
    static hailo_status sensor_store_config(Device &device, uint32_t is_first, uint32_t section_index, uint32_t start_offset, uint32_t reset_data_size, uint32_t sensor_type, uint32_t total_data_size,
                                        uint8_t  *data, uint32_t data_length, uint16_t config_height, uint16_t config_width, uint16_t config_fps, uint32_t config_name_length, uint8_t *config_name);
    static hailo_status sensor_get_config(Device &device, uint32_t section_index, uint32_t offset, uint32_t data_length, uint8_t *data);
    static hailo_status sensor_set_i2c_bus_index(Device &device, uint32_t sensor_type, uint32_t bus_index);
    static hailo_status sensor_load_and_start_config(Device &device, uint32_t section_index);
    static hailo_status sensor_reset(Device &device, uint32_t section_index);
    static hailo_status sensor_set_generic_i2c_slave(Device &device, uint16_t slave_address, uint8_t register_address_size, uint8_t bus_index, uint8_t should_hold_bus, uint8_t endianness);
    static hailo_status sensor_get_sections_info(Device &device, uint8_t *data);

    /**
     *  Download generated context switch action list per single context
     * 
     * @param[in]     device - The Hailo device.
     * @param[in]     network_group_id - Unique identifier for the network group.
     * @param[in]     context_type - type of context
     * @param[in]     context_index - context index of the context the user wishes to download the action list. Should
     *                be 0 for non-dynamic contexts.
     * @param[out]    base address - base address of the context action list in the FW memory
     * @param[out]    action list - buffer of the action list
     * @param[out]    action_list_length - size of the action list buffer
     *
     * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a static hailo_status error.
     */
    // TODO: fix
    static hailo_status download_context_action_list(Device &device, uint32_t network_group_id,
        CONTROL_PROTOCOL__context_switch_context_type_t context_type, uint16_t context_index,
        size_t action_list_max_size, uint32_t *base_address, uint8_t *action_list, uint16_t *action_list_length,
        uint32_t *batch_counter, uint32_t *idle_time_local);
            
    /**
     *  Enable core-op
     * 
     * @param[in]     device - The Hailo device.
     * @param[in]     core_op_index - core_op index
     * @param[in]     dynamic_batch_size - actual batch size
     * @param[in]     batch_count - number of batches user wish to run on hailo chip
     *
     * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a static hailo_status error.
     */
    static hailo_status enable_core_op(Device &device, uint8_t core_op_index, uint16_t dynamic_batch_size,
        uint16_t batch_count);
    /**
     *  reset context switch state machine
     * 
     * @param[in]     device - The Hailo device.
     *
     * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a static hailo_status error.
     */
    static hailo_status reset_context_switch_state_machine(Device &device);
    /**
     *  set dataflow interrupt by control
     * 
     * @param[in]     device - The Hailo device.
     * @param[in]     interrupt_type - casted from enum into unit8_t - type of the interrupt
     * @param[in]     interrupt_index  - interrupt index (PCIe channel or Cluster index)
     * @param[in]     interrupt_sub_index  - interrupt index (LCU index in cluster)
     *
     * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a static hailo_status error.
     */
    static hailo_status set_dataflow_interrupt(Device &device, uint8_t interrupt_type, uint8_t interrupt_index,
            uint8_t interrupt_sub_index);

    /**
     *  set d2h manager a new host configuration by control
     * 
     * @param[in]     device - The Hailo device.
     * @param[in]     host_port  - host port in case connection_type is Ethernet, otherwise neglected.
     * @param[in]     host_ip_address  - host ip in case connection_type is Ethernet, otherwise neglected,
     *                0 means auto detect IP address from control.
     *
     * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a static hailo_status error.
     */
    static hailo_status d2h_notification_manager_set_host_info(Device &device, uint16_t host_port, uint32_t host_ip_address);
    static hailo_status d2h_notification_manager_send_host_info_notification(Device &device, uint8_t notification_priority);

    /**
     *  Enable/disable halt transmition following Rx pause frame
     * 
     * @param[in]     device - The Hailo device.
     * @param[in]     rx_pause_frames_enable - Bool indicating whether to enable or disable rx pause frames
     * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a static hailo_status error.
     */
    static hailo_status set_pause_frames(Device &device, uint8_t rx_pause_frames_enable);

    static hailo_status set_fw_logger(Device &device, hailo_fw_logger_level_t level, uint32_t interface_mask);
    static hailo_status write_memory(Device &device, uint32_t address, const uint8_t *data, uint32_t data_length);
    static hailo_status read_memory(Device &device, uint32_t address, uint8_t *data, uint32_t data_length);
    static hailo_status context_switch_set_context_info(Device &device,
        const std::vector<CONTROL_PROTOCOL__context_switch_context_info_chunk_t> &context_infos);
    static hailo_status context_switch_set_network_group_header(Device &device,
        const CONTROL_PROTOCOL__application_header_t &network_group_header);
    static hailo_status context_switch_update_cache_read_offset(Device &device, int32_t read_offset_delta);
    static hailo_status context_switch_signal_cache_updated(Device &device);
    static hailo_status wd_enable(Device &device, uint8_t cpu_id, bool should_enable);
    static hailo_status wd_config(Device &device, uint8_t cpu_id, uint32_t wd_cycles, CONTROL_PROTOCOL__WATCHDOG_MODE_t wd_mode);
    static hailo_status previous_system_state(Device &device, uint8_t cpu_id, CONTROL_PROTOCOL__system_state_t *system_state);
    static hailo_status clear_configured_apps(Device &device);
    static hailo_status get_chip_temperature(Device &device, hailo_chip_temperature_info_t *temp_info);
    static hailo_status enable_debugging(Device &device, bool is_rma);

    static hailo_status config_context_switch_breakpoint(Device &device, uint8_t breakpoint_id,
            CONTROL_PROTOCOL__context_switch_breakpoint_control_t breakpoint_control,
            CONTROL_PROTOCOL__context_switch_breakpoint_data_t *breakpoint_data);
    static hailo_status get_context_switch_breakpoint_status(Device &device, uint8_t breakpoint_id,
            CONTROL_PROTOCOL__context_switch_debug_sys_status_t *breakpoint_status);
    static hailo_status get_context_switch_main_header(Device &device, 
            CONTROL_PROTOCOL__context_switch_main_header_t *main_header);
    static hailo_status config_context_switch_timestamp(Device &device, uint32_t batch_index, bool enable_user_configuration);
    static hailo_status test_chip_memories(Device &device);
    static hailo_status run_bist_test(Device &device, bool is_top_test, uint32_t top_bypass_bitmap,
                     uint8_t cluster_index, uint32_t cluster_bypass_bitmap_0, uint32_t cluster_bypass_bitmap_1);
    static hailo_status set_clock_freq(Device &device, uint32_t clock_freq);
    static Expected<hailo_health_info_t> get_health_information(Device &device);
    static hailo_status set_throttling_state(Device &device, bool should_activate);
    static Expected<bool> get_throttling_state(Device &device);
    static hailo_status set_overcurrent_state(Device &device, bool should_activate);
    static Expected<bool> get_overcurrent_state(Device &device);
    static Expected<CONTROL_PROTOCOL__hw_consts_t> get_hw_consts(Device &device);
    static hailo_status set_sleep_state(Device &device, hailo_sleep_state_t sleep_state);
    static hailo_status change_hw_infer_status(Device &device, CONTROL_PROTOCOL__hw_infer_state_t state,
        uint8_t network_group_index, uint16_t dynamic_batch_size, uint16_t batch_count,
        CONTROL_PROTOCOL__hw_infer_channels_info_t *channels_info, CONTROL_PROTOCOL__hw_only_infer_results_t *results,
        CONTROL_PROTOCOL__boundary_channel_mode_t boundary_channel_mode);
    static hailo_status start_hw_only_infer(Device &device, uint8_t network_group_index, uint16_t dynamic_batch_size,
        uint16_t batch_count, CONTROL_PROTOCOL__hw_infer_channels_info_t *channels_info,
        CONTROL_PROTOCOL__boundary_channel_mode_t boundary_channel_mode);
    static hailo_status stop_hw_only_infer(Device &device, CONTROL_PROTOCOL__hw_only_infer_results_t *results);
    // TODO: needed?
    static hailo_status power_measurement(Device &device, CONTROL_PROTOCOL__dvm_options_t dvm,
        CONTROL_PROTOCOL__power_measurement_types_t measurement_type, float32_t *measurement);
    static hailo_status set_power_measurement(Device &device, hailo_measurement_buffer_index_t buffer_index, CONTROL_PROTOCOL__dvm_options_t dvm,
        CONTROL_PROTOCOL__power_measurement_types_t measurement_type);
    static hailo_status get_power_measurement(Device &device, hailo_measurement_buffer_index_t buffer_index, bool should_clear,
        hailo_power_measurement_data_t *measurement_data);
    static hailo_status start_power_measurement(Device &device,
        CONTROL_PROTOCOL__averaging_factor_t averaging_factor, CONTROL_PROTOCOL__sampling_period_t sampling_period);
    static hailo_status stop_power_measurement(Device &device);

    static Expected<uint32_t> get_partial_clusters_layout_bitmap(Device &device);

private:
    static hailo_status write_memory_chunk(Device &device, uint32_t address, const uint8_t *data, uint32_t chunk_size);
    static hailo_status read_memory_chunk(Device &device, uint32_t address, uint8_t *data, uint32_t chunk_size);
    static hailo_status read_user_config_chunk(Device &device, uint32_t read_offset, uint32_t read_length,
        uint8_t *buffer, uint32_t *actual_read_data_length);
    static hailo_status write_user_config_chunk(Device &device, uint32_t offset, const uint8_t *data, uint32_t chunk_size);
    static hailo_status download_context_action_list_chunk(Device &device, uint32_t network_group_id,
        CONTROL_PROTOCOL__context_switch_context_type_t context_type, uint16_t context_index, uint16_t action_list_offset,
        size_t action_list_max_size, uint32_t *base_address, uint8_t *action_list, uint16_t *action_list_length,
        bool *is_action_list_end, uint32_t *batch_counter, uint32_t *idle_time_local);
    static hailo_status context_switch_set_context_info_chunk(Device &device,
        const CONTROL_PROTOCOL__context_switch_context_info_chunk_t &context_info);
    static hailo_status change_context_switch_status(Device &device,
            CONTROL_PROTOCOL__CONTEXT_SWITCH_STATUS_t state_machine_status,
            uint8_t network_group_index, uint16_t dynamic_batch_size, uint16_t batch_count);
    static Expected<CONTROL_PROTOCOL__get_extended_device_information_response_t> get_extended_device_info_response(Device &device);
};

} /* namespace hailort */

#endif /* __CONTROL_HPP__ */
