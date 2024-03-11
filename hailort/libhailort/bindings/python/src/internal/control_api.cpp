/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file control_api.cpp
 * @brief Defines binding to control functions
 *
 **/

#include "control_api.hpp"
#include "utils.hpp"
#include "hailo/device.hpp"
#include "common/utils.hpp"

namespace hailort
{

void ControlWrapper::set_clock_freq(DeviceWrapper &device, uint32_t clock_freq)
{
    auto status = Control::set_clock_freq(*device, clock_freq);
    VALIDATE_STATUS(status);
}

void ControlWrapper::close_all_streams(DeviceWrapper &device)
{
    auto status = Control::close_all_streams(*device);
    VALIDATE_STATUS(status);
}

void ControlWrapper::config_ahb_to_axi(DeviceWrapper &device, bool use_64bit_data_only)
{
    CONTROL_PROTOCOL__config_core_top_type_t config_type = CONTROL_PROTOCOL__CONFIG_CORE_TOP_TYPE_AHB_TO_AXI;
    CONTROL_PROTOCOL__config_core_top_params_t params = {0};
    params.ahb_to_axi.enable_use_64bit_data_only = use_64bit_data_only;

    auto status = Control::config_core_top(*device, config_type, &params);
    VALIDATE_STATUS(status);
}

void ControlWrapper::phy_operation(DeviceWrapper &device, CONTROL_PROTOCOL__phy_operation_t operation_type)
{
    auto status = Control::phy_operation(*device, operation_type);
    VALIDATE_STATUS(status);
}

uint32_t ControlWrapper::latency_measurement_read(DeviceWrapper &device)
{
    uint32_t inbound_to_outbound_latency_nsec = 0;

    auto status = Control::latency_measurement_read(*device, &inbound_to_outbound_latency_nsec);
    VALIDATE_STATUS(status);

    return inbound_to_outbound_latency_nsec;
}

void ControlWrapper::latency_measurement_config(DeviceWrapper &device, uint8_t latency_measurement_en,
    uint32_t inbound_start_buffer_number, uint32_t outbound_stop_buffer_number, uint32_t inbound_stream_index,
    uint32_t outbound_stream_index)
{
    auto status = Control::latency_measurement_config(*device, latency_measurement_en, inbound_start_buffer_number,
            outbound_stop_buffer_number, inbound_stream_index, outbound_stream_index);
    VALIDATE_STATUS(status);
}

void ControlWrapper::start_firmware_update(DeviceWrapper &device)
{
    auto status = Control::start_firmware_update(*device);
    VALIDATE_STATUS(status);
}

void ControlWrapper::finish_firmware_update(DeviceWrapper &device)
{
    auto status = Control::finish_firmware_update(*device);
    VALIDATE_STATUS(status);
}

void ControlWrapper::write_firmware_update(DeviceWrapper &device, uint32_t offset, py::bytes data, uint32_t length)
{
    auto status = Control::write_firmware_update(*device, offset, (uint8_t*)std::string(data).c_str(), length);
    VALIDATE_STATUS(status);
}

void ControlWrapper::validate_firmware_update(DeviceWrapper &device, py::bytes md5_raw_data, uint32_t firmware_size)
{
    MD5_SUM_t expected_md5 = {0};
    memcpy(&expected_md5, (uint8_t*)std::string(md5_raw_data).c_str(), sizeof(expected_md5));

    auto status = Control::validate_firmware_update(*device, &expected_md5, firmware_size);
    VALIDATE_STATUS(status);
}

py::bytes ControlWrapper::sensor_get_config(DeviceWrapper &device, uint32_t section_index, uint32_t offset, uint32_t data_length)
{
    std::unique_ptr<std::string> response = make_unique_nothrow<std::string>(data_length, '\x00');
    VALIDATE_NOT_NULL(response, HAILO_OUT_OF_HOST_MEMORY);
    
    auto status = Control::sensor_get_config(*device, section_index, offset, data_length, (uint8_t*)(response->data()));
    VALIDATE_STATUS(status);

     return *response;
}

void ControlWrapper::idle_time_set_measurement(DeviceWrapper &device, bool measurement_enable)
{
    auto status = Control::idle_time_set_measurement(*device, measurement_enable);
    VALIDATE_STATUS(status);
}

uint64_t ControlWrapper::idle_time_get_measurement(DeviceWrapper &device)
{
    uint64_t measurement = 0;

    auto status = Control::idle_time_get_measurement(*device, &measurement);
    VALIDATE_STATUS(status);

    return measurement;
}

void ControlWrapper::d2h_notification_manager_set_host_info(DeviceWrapper &device, uint16_t host_port, uint32_t host_ip_address)
{
    auto status = Control::d2h_notification_manager_set_host_info(*device, host_port, host_ip_address);
    VALIDATE_STATUS(status);
}

void ControlWrapper::d2h_notification_manager_send_host_info_notification(DeviceWrapper &device, uint8_t notification_priority)
{
    auto status = Control::d2h_notification_manager_send_host_info_notification(*device, notification_priority);
    VALIDATE_STATUS(status);
}

/* Context switch */
void ControlWrapper::set_context_switch_breakpoint(DeviceWrapper &device, 
        uint8_t breakpoint_id,
        bool break_at_any_network_group_index, uint8_t network_group_index, 
        bool break_at_any_batch_index, uint16_t batch_index, 
        bool break_at_any_context_index,uint16_t context_index, 
        bool break_at_any_action_index, uint16_t action_index) 
{
    CONTROL_PROTOCOL__context_switch_breakpoint_control_t breakpoint_control = 
        CONTROL_PROTOCOL__CONTEXT_SWITCH_BREAKPOINT_CONTROL_SET;
    CONTROL_PROTOCOL__context_switch_breakpoint_data_t breakpoint_data = {
        break_at_any_network_group_index,
        network_group_index,
        break_at_any_batch_index,
        batch_index,
        break_at_any_context_index,
        context_index,
        break_at_any_action_index,
        action_index};

    auto status = Control::config_context_switch_breakpoint(*device, breakpoint_id, breakpoint_control, &breakpoint_data);
    VALIDATE_STATUS(status);
}

void ControlWrapper::continue_context_switch_breakpoint(DeviceWrapper &device, uint8_t breakpoint_id) 
{
    CONTROL_PROTOCOL__context_switch_breakpoint_control_t breakpoint_control = 
        CONTROL_PROTOCOL__CONTEXT_SWITCH_BREAKPOINT_CONTROL_CONTINUE;
    CONTROL_PROTOCOL__context_switch_breakpoint_data_t breakpoint_data = {false,0,false,0,false,0,false,0};

    auto status = Control::config_context_switch_breakpoint(*device, breakpoint_id, 
            breakpoint_control, &breakpoint_data);
    VALIDATE_STATUS(status);
}

void ControlWrapper::clear_context_switch_breakpoint(DeviceWrapper &device, uint8_t breakpoint_id) 
{
    CONTROL_PROTOCOL__context_switch_breakpoint_control_t breakpoint_control = 
        CONTROL_PROTOCOL__CONTEXT_SWITCH_BREAKPOINT_CONTROL_CLEAR;
    CONTROL_PROTOCOL__context_switch_breakpoint_data_t breakpoint_data = {false,0,false,0,false,0,false,0};

    auto status = Control::config_context_switch_breakpoint(*device, breakpoint_id,
            breakpoint_control, &breakpoint_data);
    VALIDATE_STATUS(status);
}

uint8_t ControlWrapper::get_context_switch_breakpoint_status(DeviceWrapper &device, uint8_t breakpoint_id) 
{
    CONTROL_PROTOCOL__context_switch_debug_sys_status_t breakpoint_status = 
        CONTROL_PROTOCOL__CONTEXT_SWITCH_DEBUG_SYS_STATUS_COUNT;

    auto status = Control::get_context_switch_breakpoint_status(*device, breakpoint_id,
            &breakpoint_status);
    VALIDATE_STATUS(status);

    return static_cast<uint8_t>(breakpoint_status);
}

void ControlWrapper::config_context_switch_timestamp(DeviceWrapper &device, uint16_t batch_index) 
{
    auto status = Control::config_context_switch_timestamp(*device, batch_index, true);
    VALIDATE_STATUS(status);
}

void ControlWrapper::remove_context_switch_timestamp_configuration(DeviceWrapper &device) 
{
    auto status = Control::config_context_switch_timestamp(*device, 0, false);
    VALIDATE_STATUS(status);
}

void ControlWrapper::enable_debugging(DeviceWrapper &device, bool is_rma)
{
    auto status = Control::enable_debugging(*device, is_rma);
    VALIDATE_STATUS(status);
}

void ControlWrapper::add_to_python_module(py::module &m)
{
    m.def("_set_clock_freq", &ControlWrapper::set_clock_freq);
    m.def("close_all_streams", &ControlWrapper::close_all_streams);
    m.def("config_ahb_to_axi", &ControlWrapper::config_ahb_to_axi);
    m.def("phy_operation", &ControlWrapper::phy_operation);
    m.def("latency_measurement_read", &ControlWrapper::latency_measurement_read);
    m.def("latency_measurement_config", &ControlWrapper::latency_measurement_config);
    m.def("start_firmware_update", &ControlWrapper::start_firmware_update);
    m.def("finish_firmware_update", &ControlWrapper::finish_firmware_update);
    m.def("write_firmware_update", &ControlWrapper::write_firmware_update);
    m.def("validate_firmware_update", &ControlWrapper::validate_firmware_update);
    m.def("sensor_get_config", &ControlWrapper::sensor_get_config);
    m.def("idle_time_set_measurement", &ControlWrapper::idle_time_set_measurement);
    m.def("idle_time_get_measurement", &ControlWrapper::idle_time_get_measurement);
    m.def("d2h_notification_manager_set_host_info", &ControlWrapper::d2h_notification_manager_set_host_info);
    m.def("d2h_notification_manager_send_host_info_notification", &ControlWrapper::d2h_notification_manager_send_host_info_notification);
    m.def("set_context_switch_breakpoint", &set_context_switch_breakpoint);
    m.def("continue_context_switch_breakpoint", &continue_context_switch_breakpoint);
    m.def("clear_context_switch_breakpoint", &clear_context_switch_breakpoint);
    m.def("get_context_switch_breakpoint_status", &get_context_switch_breakpoint_status);
    m.def("config_context_switch_timestamp", &config_context_switch_timestamp);
    m.def("remove_context_switch_timestamp_configuration", &remove_context_switch_timestamp_configuration);
    m.def("enable_debugging", &enable_debugging);
    
    // TODO: HRT-5764 - Remove 'py::module_local()' when removing _pyhailort_internal from external
    // py::module_local() is needed because these enums are currently in both _pyhailort and _pyhailort_internal, 
    // and when trying to import one of them on the python side you will get the error:
    // ImportError: generic_type: type "enum_name" is already registered!
    // py::module_local() tells pybind11 to keep the external class/enum binding localized to the module. 
    py::enum_<CONTROL_PROTOCOL__context_switch_debug_sys_status_t>(m, "ContextSwitchBreakpointStatus", py::module_local())
        .value("CONTEXT_SWITCH_BREAKPOINT_STATUS_CLEARED",CONTROL_PROTOCOL__CONTEXT_SWITCH_DEBUG_SYS_STATUS_CLEARED)
        .value("CONTEXT_SWITCH_BREAKPOINT_STATUS_WAITING_FOR_BREAKPOINT",CONTROL_PROTOCOL__CONTEXT_SWITCH_DEBUG_SYS_STATUS_WAITING_FOR_BREAKPOINT)
        .value("CONTEXT_SWITCH_BREAKPOINT_STATUS_REACHED_BREAKPOINT",CONTROL_PROTOCOL__CONTEXT_SWITCH_DEBUG_SYS_STATUS_REACHED_BREAKPOINT)
        ;

    py::enum_<CONTROL_PROTOCOL__phy_operation_t>(m, "CONTROL_PROTOCOL__phy_operation_t", py::module_local())
        .value("PHY_OPERATION_RESET", CONTROL_PROTOCOL__PHY_OPERATION_RESET)
        ;

    py::enum_<CONTROL_PROTOCOL__mipi_deskew_enable_t>(m, "CONTROL_PROTOCOL__mipi_deskew_enable_t", py::module_local())
        .value("MIPI__DESKEW_FORCE_DISABLE", CONTROL_PROTOCOL__MIPI_DESKEW__FORCE_DISABLE)
        .value("MIPI__DESKEW_FORCE_ENABLE", CONTROL_PROTOCOL__MIPI_DESKEW__FORCE_ENABLE)
        .value("MIPI__DESKEW_DEFAULT", CONTROL_PROTOCOL__MIPI_DESKEW__DEFAULT)
        ;

}

} /* namespace hailort */