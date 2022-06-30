/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file control_api.hpp
 * @brief Defines binding to control functions
 *
 **/

#ifndef _CONTROL_API_HPP_
#define _CONTROL_API_HPP_

#include "control.hpp"
#include "utils.hpp"

#include "device_api.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/detail/common.h>
#include <pybind11/stl.h>
#include <pybind11/complex.h>
#include <pybind11/functional.h>

namespace hailort
{

class ControlWrapper {
public:
    static void add_to_python_module(py::module &m);

    static void set_clock_freq(DeviceWrapper &device, uint32_t clock_freq);
    static void close_all_streams(DeviceWrapper &device);
    static void config_ahb_to_axi(DeviceWrapper &device, bool use_64bit_data_only);
    static void phy_operation(DeviceWrapper &device, CONTROL_PROTOCOL__phy_operation_t operation_type);
    static uint32_t latency_measurement_read(DeviceWrapper &device);
    static void latency_measurement_config(DeviceWrapper &device, uint8_t latency_measurement_en,
        uint32_t inbound_start_buffer_number, uint32_t outbound_stop_buffer_number, uint32_t inbound_stream_index,
        uint32_t outbound_stream_index);
    static void start_firmware_update(DeviceWrapper &device);
    static void finish_firmware_update(DeviceWrapper &device);
    static void write_firmware_update(DeviceWrapper &device, uint32_t offset, py::bytes data, uint32_t length);
    static void validate_firmware_update(DeviceWrapper &device, py::bytes md5_raw_data, uint32_t firmware_size);
    static py::bytes sensor_get_config(DeviceWrapper &device, uint32_t section_index, uint32_t offset, uint32_t data_length);
    static void idle_time_set_measurement(DeviceWrapper &device, bool measurement_enable);
    static uint64_t idle_time_get_measurement(DeviceWrapper &device);
    static void d2h_notification_manager_set_host_info(DeviceWrapper &device, uint16_t host_port, uint32_t host_ip_address);
    static void d2h_notification_manager_send_host_info_notification(DeviceWrapper &device, uint8_t notification_priority);
    static void enable_debugging(DeviceWrapper &device, bool is_rma);

    /* Context switch */
    static void set_context_switch_breakpoint(DeviceWrapper &device, uint8_t breakpoint_id,
        bool break_at_any_network_group_index, uint8_t network_group_index, 
        bool break_at_any_batch_index, uint16_t batch_index, 
        bool break_at_any_context_index,uint8_t context_index, 
        bool break_at_any_action_index, uint16_t action_index);
    static void continue_context_switch_breakpoint(DeviceWrapper &device, uint8_t breakpoint_id);
    static void clear_context_switch_breakpoint(DeviceWrapper &device, uint8_t breakpoint_id);
    static uint8_t get_context_switch_breakpoint_status(DeviceWrapper &device, uint8_t breakpoint_id);
    static void config_context_switch_timestamp(DeviceWrapper &device, uint16_t batch_index);
    static void remove_context_switch_timestamp_configuration(DeviceWrapper &device);
};

} /* namespace hailort */

#endif /* _CONTROL_API_HPP_ */
