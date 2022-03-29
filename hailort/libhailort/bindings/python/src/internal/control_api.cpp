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


void ControlWrapper::set_clock_freq(uintptr_t device, uint32_t clock_freq)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device*>(device));
    auto status = Control::set_clock_freq(*reinterpret_cast<Device*>(device), clock_freq);
    VALIDATE_STATUS(status);
}

void ControlWrapper::close_all_streams(uintptr_t device)
{
    hailo_status status = HAILO_UNINITIALIZED;

    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));
    status = Control::close_all_streams(*reinterpret_cast<Device *>(device));
    VALIDATE_STATUS(status);
}

void ControlWrapper::config_ahb_to_axi(uintptr_t device, bool use_64bit_data_only)
{
    hailo_status status = HAILO_UNINITIALIZED;
    CONTROL_PROTOCOL__config_core_top_type_t config_type = CONTROL_PROTOCOL__CONFIG_CORE_TOP_TYPE_AHB_TO_AXI;
    CONTROL_PROTOCOL__config_core_top_params_t params = {0};
    params.ahb_to_axi.enable_use_64bit_data_only = use_64bit_data_only;

    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));
    status = Control::config_core_top(*reinterpret_cast<Device *>(device), config_type, &params);
    VALIDATE_STATUS(status);

    return;
}

void ControlWrapper::phy_operation(uintptr_t device, CONTROL_PROTOCOL__phy_operation_t operation_type)
{
    hailo_status status = HAILO_UNINITIALIZED;

    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));
    status = Control::phy_operation(*reinterpret_cast<Device *>(device), operation_type);
    VALIDATE_STATUS(status);

    return;
}

uint32_t ControlWrapper::latency_measurement_read(uintptr_t device)
{
    hailo_status status = HAILO_UNINITIALIZED;
    uint32_t inbound_to_outbound_latency_nsec = 0;

    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));
    status = Control::latency_measurement_read(*reinterpret_cast<Device *>(device), &inbound_to_outbound_latency_nsec);
    VALIDATE_STATUS(status);

    return inbound_to_outbound_latency_nsec;
}

void ControlWrapper::latency_measurement_config(uintptr_t device, uint8_t latency_measurement_en,
    uint32_t inbound_start_buffer_number, uint32_t outbound_stop_buffer_number, uint32_t inbound_stream_index,
    uint32_t outbound_stream_index)
{
    hailo_status status = HAILO_UNINITIALIZED;

    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));
    status = Control::latency_measurement_config(*reinterpret_cast<Device *>(device), latency_measurement_en, inbound_start_buffer_number,
            outbound_stop_buffer_number, inbound_stream_index, outbound_stream_index);
    VALIDATE_STATUS(status);

    return;
}

void ControlWrapper::start_firmware_update(uintptr_t device)
{
    hailo_status status = HAILO_UNINITIALIZED;

    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));
    status = Control::start_firmware_update(*reinterpret_cast<Device *>(device));
    VALIDATE_STATUS(status);

    return;
}

void ControlWrapper::finish_firmware_update(uintptr_t device)
{
    hailo_status status = HAILO_UNINITIALIZED;

    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));
    status = Control::finish_firmware_update(*reinterpret_cast<Device *>(device));
    VALIDATE_STATUS(status);

    return;
}

void ControlWrapper::write_firmware_update(uintptr_t device, uint32_t offset, py::bytes data, uint32_t length)
{
    hailo_status status = HAILO_UNINITIALIZED;

    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));
    status = Control::write_firmware_update(*reinterpret_cast<Device *>(device), offset, (uint8_t*)std::string(data).c_str(), length);
    VALIDATE_STATUS(status);

    return;
}

void ControlWrapper::validate_firmware_update(uintptr_t device, py::bytes md5_raw_data, uint32_t firmware_size)
{
    hailo_status status = HAILO_UNINITIALIZED;
    MD5_SUM_t expected_md5 = {0};

    memcpy(&expected_md5, (uint8_t*)std::string(md5_raw_data).c_str(), sizeof(expected_md5));

    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));
    status = Control::validate_firmware_update(*reinterpret_cast<Device *>(device), &expected_md5, firmware_size);
    VALIDATE_STATUS(status);

    return;
}

py::bytes ControlWrapper::sensor_get_config(uintptr_t device, uint32_t section_index, uint32_t offset, uint32_t data_length)
{
    hailo_status status = HAILO_UNINITIALIZED;
    std::unique_ptr<std::string> response = make_unique_nothrow<std::string>(data_length, '\x00');
    VALIDATE_NOT_NULL(response);
    
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));
    status = Control::sensor_get_config(*reinterpret_cast<Device *>(device), section_index, offset, data_length,
        (uint8_t*)(response->data()));
   
    VALIDATE_STATUS(status);

     return *response;
}

void ControlWrapper::idle_time_set_measurement(uintptr_t device, bool measurement_enable)
{
    hailo_status status = HAILO_UNINITIALIZED;

    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));
    status = Control::idle_time_set_measurement(*reinterpret_cast<Device *>(device), measurement_enable);
    VALIDATE_STATUS(status);
}

uint64_t ControlWrapper::idle_time_get_measurement(uintptr_t device)
{
    hailo_status status = HAILO_UNINITIALIZED;
    uint64_t measurement = 0;

    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));
    status = Control::idle_time_get_measurement(*reinterpret_cast<Device *>(device), &measurement);
    VALIDATE_STATUS(status);

    return measurement;
}

void ControlWrapper::d2h_notification_manager_set_host_info(uintptr_t device, uint16_t host_port, uint32_t host_ip_address)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));
    hailo_status status = Control::d2h_notification_manager_set_host_info(*reinterpret_cast<Device *>(device), host_port, host_ip_address);
    VALIDATE_STATUS(status);
}

void ControlWrapper::d2h_notification_manager_send_host_info_notification(uintptr_t device, uint8_t notification_priority)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));
    hailo_status status = Control::d2h_notification_manager_send_host_info_notification(*reinterpret_cast<Device *>(device), notification_priority);
    VALIDATE_STATUS(status);
}

/* Context switch */
void ControlWrapper::set_context_switch_breakpoint(uintptr_t device, 
        uint8_t breakpoint_id,
        bool break_at_any_network_group_index, uint8_t network_group_index, 
        bool break_at_any_batch_index, uint16_t batch_index, 
        bool break_at_any_context_index,uint8_t context_index, 
        bool break_at_any_action_index, uint16_t action_index) 
{
    hailo_status status = HAILO_UNINITIALIZED;
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

    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));
    status = Control::config_context_switch_breakpoint(*reinterpret_cast<Device *>(device), breakpoint_id,
            breakpoint_control, &breakpoint_data);
    VALIDATE_STATUS(status);
}

void ControlWrapper::continue_context_switch_breakpoint(uintptr_t device, uint8_t breakpoint_id) 
{
    hailo_status status = HAILO_UNINITIALIZED;
    CONTROL_PROTOCOL__context_switch_breakpoint_control_t breakpoint_control = 
        CONTROL_PROTOCOL__CONTEXT_SWITCH_BREAKPOINT_CONTROL_CONTINUE;
    CONTROL_PROTOCOL__context_switch_breakpoint_data_t breakpoint_data = {false,0,false,0,false,0,false,0};

    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));
    status = Control::config_context_switch_breakpoint(*reinterpret_cast<Device *>(device), breakpoint_id, 
            breakpoint_control, &breakpoint_data);
    VALIDATE_STATUS(status);
}

void ControlWrapper::clear_context_switch_breakpoint(uintptr_t device, uint8_t breakpoint_id) 
{
    hailo_status status = HAILO_UNINITIALIZED;
    CONTROL_PROTOCOL__context_switch_breakpoint_control_t breakpoint_control = 
        CONTROL_PROTOCOL__CONTEXT_SWITCH_BREAKPOINT_CONTROL_CLEAR;
    CONTROL_PROTOCOL__context_switch_breakpoint_data_t breakpoint_data = {false,0,false,0,false,0,false,0};

    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));
    status = Control::config_context_switch_breakpoint(*reinterpret_cast<Device *>(device), breakpoint_id,
            breakpoint_control, &breakpoint_data);
    VALIDATE_STATUS(status);
}

uint8_t ControlWrapper::get_context_switch_breakpoint_status(uintptr_t device, uint8_t breakpoint_id) 
{
    hailo_status status = HAILO_UNINITIALIZED;
    CONTROL_PROTOCOL__context_switch_debug_sys_status_t breakpoint_status = 
        CONTROL_PROTOCOL__CONTEXT_SWITCH_DEBUG_SYS_STATUS_COUNT;

    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));
    status = Control::get_context_switch_breakpoint_status(*reinterpret_cast<Device *>(device), breakpoint_id,
            &breakpoint_status);
    VALIDATE_STATUS(status);

    return static_cast<uint8_t>(breakpoint_status);
}

void ControlWrapper::config_context_switch_timestamp(uintptr_t device, uint16_t batch_index) 
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));
    hailo_status status = Control::config_context_switch_timestamp(*reinterpret_cast<Device *>(device), batch_index, true);
    VALIDATE_STATUS(status);
}

void ControlWrapper::remove_context_switch_timestamp_configuration(uintptr_t device) 
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));
    hailo_status status = Control::config_context_switch_timestamp(*reinterpret_cast<Device *>(device), 0, false);
    VALIDATE_STATUS(status);
}

void ControlWrapper::enable_debugging(uintptr_t device, bool is_rma)
{
    hailo_status status = HAILO_UNINITIALIZED;

    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));
    status = Control::enable_debugging(*reinterpret_cast<Device *>(device), is_rma);
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