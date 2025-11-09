/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/detail/common.h>
#include <pybind11/stl.h>
#include <pybind11/complex.h>
#include <pybind11/functional.h>
#include <vector>
#include <exception>
using namespace std;

#include "hailo/hailort.h"
#include "hailo/hailort_defaults.hpp"

#include "infer_model_api.hpp"
#include "hef_api.hpp"
#include "vstream_api.hpp"
#include "vdevice_api.hpp"
#include "network_group_api.hpp"
#include "device_api.hpp"
#include "quantization_api.hpp"
#include "session_api.hpp"
#include "llm_api.hpp"
#include "vlm_api.hpp"
#include "speech2text_api.hpp"

#include "utils.hpp"

#include "bindings_common.hpp"

// should be same as socket.hpp
#define PADDING_BYTES_SIZE (6)
#define PADDING_ALIGN_BYTES (8 - PADDING_BYTES_SIZE)

namespace hailort
{

#define MAX_HAILO_PACKET_SIZE (4*1024)

bool temperature_info_equals(hailo_chip_temperature_info_t &first, hailo_chip_temperature_info_t &second){
    return ((first.ts0_temperature == second.ts0_temperature) &&
        (first.ts1_temperature == second.ts1_temperature) &&
        (first.sample_count == second.sample_count));
}

bool hailo_format_equals(hailo_format_t &first, hailo_format_t &second){
    return ((first.type == second.type) &&
        (first.order == second.order) &&
        (first.flags == second.flags));
}

class PcieScan {
public:
    PcieScan() = default;
    std::vector<hailo_pcie_device_info_t> scan_devices(void);
};

std::vector<hailo_pcie_device_info_t> PcieScan::scan_devices(void)
{
    auto scan_result = Device::scan_pcie();
    VALIDATE_EXPECTED(scan_result);

    return scan_result.release();
}

std::string get_status_message(uint32_t status_in)
{
    auto status_str = hailo_get_status_message((hailo_status)status_in);
    if (status_str == nullptr) {
        // Invalid status
        return "";
    }
    else {
        return status_str;
    }
}

std::vector<hailo_detection_t> convert_nms_by_score_buffer_to_detections(py::array src_buffer)
{
    std::vector<hailo_detection_t> detections;
    uint8_t *src_ptr = static_cast<uint8_t*>(src_buffer.mutable_data());
    uint16_t detections_count = *(uint16_t*)src_ptr;
    detections.reserve(detections_count);

    size_t buffer_offset = sizeof(uint16_t);
    for (size_t i = 0; i < detections_count; i++) {
        hailo_detection_t detection = *(hailo_detection_t*)(src_ptr + buffer_offset);
        buffer_offset += sizeof(hailo_detection_t);
        detections.emplace_back(std::move(detection));
    }
    return detections;
}

std::vector<hailo_detection_with_byte_mask_t> convert_nms_with_byte_mask_buffer_to_detections(py::array src_buffer)
{
    std::vector<hailo_detection_with_byte_mask_t> detections;
    uint8_t *src_ptr = static_cast<uint8_t*>(src_buffer.mutable_data());
    uint16_t detections_count = *(uint16_t*)src_ptr;
    detections.reserve(detections_count);

    size_t buffer_offset = sizeof(uint16_t);
    for (size_t i = 0; i < detections_count; i++) {
        detections.emplace_back(*(hailo_detection_with_byte_mask_t*)(src_ptr + buffer_offset));
        buffer_offset += sizeof(hailo_detection_with_byte_mask_t) + detections.back().mask_size;
    }
    return detections;
}

static void validate_versions_match()
{
    hailo_version_t libhailort_version = {};
    auto status = hailo_get_library_version(&libhailort_version);
    if (HAILO_SUCCESS != status) {
        throw std::logic_error("Failed to get libhailort version");
    }

    bool versions_match = ((HAILORT_MAJOR_VERSION == libhailort_version.major) &&
        (HAILORT_MINOR_VERSION == libhailort_version.minor) &&
        (HAILORT_REVISION_VERSION == libhailort_version.revision));
    if (!versions_match) {
        std::stringstream message;
        message << "libhailort version (" <<
            libhailort_version.major << "." << libhailort_version.minor << "." << libhailort_version.revision <<
            ") does not match pyhailort version (" <<
            HAILORT_MAJOR_VERSION << "." << HAILORT_MINOR_VERSION << "." << HAILORT_REVISION_VERSION << ")";
        throw std::logic_error(message.str());
    }
}

PYBIND11_MODULE(_pyhailort, m) {
    validate_versions_match();

    m.def("get_status_message", &get_status_message);
    m.def("convert_nms_by_score_buffer_to_detections", &convert_nms_by_score_buffer_to_detections);
    m.def("convert_nms_with_byte_mask_buffer_to_detections", &convert_nms_with_byte_mask_buffer_to_detections);
    m.def("dequantize_output_buffer_in_place", &QuantizationBindings::dequantize_output_buffer_in_place);
    m.def("dequantize_output_buffer", &QuantizationBindings::dequantize_output_buffer);
    m.def("quantize_input_buffer", &QuantizationBindings::quantize_input_buffer);
    m.def("is_qp_valid", &QuantizationBindings::is_qp_valid);

    m.def("get_format_data_bytes", &HailoRTCommon::get_format_data_bytes);
    m.def("get_dtype", &HailoRTBindingsCommon::get_dtype);

    py::class_<hailo_pcie_device_info_t>(m, "PcieDeviceInfo")
        .def(py::init<>())
        .def_static("_parse", [](const std::string &device_info_str) {
            auto device_info = Device::parse_pcie_device_info(device_info_str);
            VALIDATE_EXPECTED(device_info);
            return device_info.release();
        })
        .def_readwrite("domain", &hailo_pcie_device_info_t::domain)
        .def_readwrite("bus", &hailo_pcie_device_info_t::bus)
        .def_readwrite("device", &hailo_pcie_device_info_t::device)
        .def_readwrite("func", &hailo_pcie_device_info_t::func)
        .def("__str__", [](const hailo_pcie_device_info_t &self) {
            auto device_info_str = Device::pcie_device_info_to_string(self);
            VALIDATE_EXPECTED(device_info_str);
            return device_info_str.release();
        })
        ;

    py::class_<PcieScan>(m, "PcieScan")
        .def(py::init<>())
        .def("scan_devices", &PcieScan::scan_devices)
        ;

    py::class_<PowerMeasurementData>(m, "PowerMeasurementData")
        .def_readonly("average_value", &PowerMeasurementData::m_average_value, "float, The average value of the samples that were sampled")
        .def_readonly("average_time_value_milliseconds", &PowerMeasurementData::m_average_time_value_milliseconds, "float, Average time in milliseconds between sampels")
        .def_readonly("min_value", &PowerMeasurementData::m_min_value, "float, The minimum value of the samples that were sampled")
        .def_readonly("max_value", &PowerMeasurementData::m_max_value, "float, The maximun value of the samples that were sampled")
        .def_readonly("total_number_of_samples", &PowerMeasurementData::m_total_number_of_samples, "uint, The number of samples that were sampled")
        .def("equals", &PowerMeasurementData::equals)
        .def(py::pickle(&PowerMeasurementData::get_state, &PowerMeasurementData::set_state))
        ;

    py::class_<hailo_rectangle_t>(m, "HailoRectangle")
        .def_readonly("y_min", &hailo_rectangle_t::y_min)
        .def_readonly("x_min", &hailo_rectangle_t::x_min)
        .def_readonly("y_max", &hailo_rectangle_t::y_max)
        .def_readonly("x_max", &hailo_rectangle_t::x_max)
        ;

    py::class_<hailo_detection_with_byte_mask_t>(m, "DetectionWithByteMask")
        .def_readonly("box", &hailo_detection_with_byte_mask_t::box)
        .def_readonly("mask_size", &hailo_detection_with_byte_mask_t::mask_size)
        .def_readonly("score", &hailo_detection_with_byte_mask_t::score)
        .def_readonly("class_id", &hailo_detection_with_byte_mask_t::class_id)
        .def("mask", [](const hailo_detection_with_byte_mask_t &detection) -> py::array {
            auto shape = *py::array::ShapeContainer({detection.mask_size});
            return py::array(py::dtype("uint8"), shape, detection.mask);
        })
        ;

    py::class_<hailo_detection_t>(m, "Detection")
        .def_readonly("y_min", &hailo_detection_t::y_min)
        .def_readonly("x_min", &hailo_detection_t::x_min)
        .def_readonly("y_max", &hailo_detection_t::y_max)
        .def_readonly("x_max", &hailo_detection_t::x_max)
        .def_readonly("score", &hailo_detection_t::score)
        .def_readonly("class_id", &hailo_detection_t::class_id)
        ;

    py::enum_<hailo_device_architecture_t>(m, "DeviceArchitecture")
        .value("HAILO8_A0", HAILO_ARCH_HAILO8_A0)
        .value("HAILO8", HAILO_ARCH_HAILO8)
        .value("HAILO8L", HAILO_ARCH_HAILO8L)
        .value("HAILO15H", HAILO_ARCH_HAILO15H)
        .value("HAILO15L", HAILO_ARCH_HAILO15L)
        .value("HAILO15M", HAILO_ARCH_HAILO15M)
        .value("HAILO10H", HAILO_ARCH_HAILO10H)
    ;

    /* TODO: SDK-15648 */
    py::enum_<hailo_dvm_options_t>(m, "DvmTypes", "Enum-like class representing the different DVMs that can be measured.\nThis determines the device that would be measured.")
        .value("AUTO", HAILO_DVM_OPTIONS_AUTO, "Choose the default value according to the supported features.")
        .value("VDD_CORE", HAILO_DVM_OPTIONS_VDD_CORE, "Perform measurements over the core. Exists only in Hailo-8 EVB.")
        .value("VDD_IO", HAILO_DVM_OPTIONS_VDD_IO, "Perform measurements over the IO. Exists only in Hailo-8 EVB.")
        .value("MIPI_AVDD", HAILO_DVM_OPTIONS_MIPI_AVDD, "Perform measurements over the MIPI avdd. Exists only in Hailo-8 EVB.")
        .value("MIPI_AVDD_H", HAILO_DVM_OPTIONS_MIPI_AVDD_H, "Perform measurements over the MIPI avdd_h. Exists only in Hailo-8 EVB.")
        .value("USB_AVDD_IO", HAILO_DVM_OPTIONS_USB_AVDD_IO, "Perform measurements over the IO. Exists only in Hailo-8 EVB.")
        .value("VDD_TOP", HAILO_DVM_OPTIONS_VDD_TOP, "Perform measurements over the top. Exists only in Hailo-8 EVB.")
        .value("USB_AVDD_IO_HV", HAILO_DVM_OPTIONS_USB_AVDD_IO_HV, "Perform measurements over the USB_AVDD_IO_HV. Exists only in Hailo-8 EVB.")
        .value("AVDD_H", HAILO_DVM_OPTIONS_AVDD_H, "Perform measurements over the AVDD_H. Exists only in Hailo-8 EVB.")
        .value("SDIO_VDD_IO", HAILO_DVM_OPTIONS_SDIO_VDD_IO, "Perform measurements over the SDIO_VDDIO. Exists only in Hailo-8 EVB.")
        .value("OVERCURRENT_PROTECTION", HAILO_DVM_OPTIONS_OVERCURRENT_PROTECTION, "Perform measurements over the OVERCURRENT_PROTECTION dvm. Exists only for Hailo-8 platforms supporting current monitoring (such as M.2 and mPCIe).")
        ;

    py::enum_<hailo_power_measurement_types_t>(m, "PowerMeasurementTypes", "Enum-like class representing the different power measurement types. This determines what\nwould be measured on the device.")
        .value("AUTO", HAILO_POWER_MEASUREMENT_TYPES__AUTO, "Choose the default value according to the supported features.")
        .value("SHUNT_VOLTAGE", HAILO_POWER_MEASUREMENT_TYPES__SHUNT_VOLTAGE, "Measure the shunt voltage. Unit is mV")
        .value("BUS_VOLTAGE", HAILO_POWER_MEASUREMENT_TYPES__BUS_VOLTAGE, "Measure the bus voltage. Unit is mV")
        .value("POWER", HAILO_POWER_MEASUREMENT_TYPES__POWER, "Measure the power. Unit is W")
        .value("CURRENT", HAILO_POWER_MEASUREMENT_TYPES__CURRENT, "Measure the current. Unit is mA")
        ;

    py::enum_<hailo_sampling_period_t>(m, "SamplingPeriod", "Enum-like class representing all bit options and related conversion times for each bit\nsetting for Bus Voltage and Shunt Voltage.")
        .value("PERIOD_140us", HAILO_SAMPLING_PERIOD_140US, "The sensor provides a new sampling every 140us.")
        .value("PERIOD_204us", HAILO_SAMPLING_PERIOD_204US, "The sensor provides a new sampling every 204us.")
        .value("PERIOD_332us", HAILO_SAMPLING_PERIOD_332US, "The sensor provides a new sampling every 332us.")
        .value("PERIOD_588us", HAILO_SAMPLING_PERIOD_588US, "The sensor provides a new sampling every 588us.")
        .value("PERIOD_1100us", HAILO_SAMPLING_PERIOD_1100US, "The sensor provides a new sampling every 1100us.")
        .value("PERIOD_2116us", HAILO_SAMPLING_PERIOD_2116US, "The sensor provides a new sampling every 2116us.")
        .value("PERIOD_4156us", HAILO_SAMPLING_PERIOD_4156US, "The sensor provides a new sampling every 4156us.")
        .value("PERIOD_8244us", HAILO_SAMPLING_PERIOD_8244US, "The sensor provides a new sampling every 8244us.")
        ;

    py::enum_<hailo_averaging_factor_t>(m, "AveragingFactor", "Enum-like class representing all the AVG bit settings and related number of averages\nfor each bit setting.")
        .value("AVERAGE_1", HAILO_AVERAGE_FACTOR_1, "Each sample reflects a value of 1 sub-samples.")
        .value("AVERAGE_4", HAILO_AVERAGE_FACTOR_4, "Each sample reflects a value of 4 sub-samples.")
        .value("AVERAGE_16", HAILO_AVERAGE_FACTOR_16, "Each sample reflects a value of 16 sub-samples.")
        .value("AVERAGE_64", HAILO_AVERAGE_FACTOR_64, "Each sample reflects a value of 64 sub-samples.")
        .value("AVERAGE_128", HAILO_AVERAGE_FACTOR_128, "Each sample reflects a value of 128 sub-samples.")
        .value("AVERAGE_256", HAILO_AVERAGE_FACTOR_256, "Each sample reflects a value of 256 sub-samples.")
        .value("AVERAGE_512", HAILO_AVERAGE_FACTOR_512, "Each sample reflects a value of 512 sub-samples.")
        .value("AVERAGE_1024", HAILO_AVERAGE_FACTOR_1024, "Each sample reflects a value of 1024 sub-samples.")
        ;

    py::enum_<hailo_measurement_buffer_index_t>(m, "MeasurementBufferIndex", "Enum-like class representing all FW buffers for power measurements storing.")
        .value("MEASUREMENT_BUFFER_INDEX_0", HAILO_MEASUREMENT_BUFFER_INDEX_0)
        .value("MEASUREMENT_BUFFER_INDEX_1", HAILO_MEASUREMENT_BUFFER_INDEX_1)
        .value("MEASUREMENT_BUFFER_INDEX_2", HAILO_MEASUREMENT_BUFFER_INDEX_2)
        .value("MEASUREMENT_BUFFER_INDEX_3", HAILO_MEASUREMENT_BUFFER_INDEX_3)
        ;

    py::class_<hailo_notification_t>(m, "Notification")
        .def_readonly("notification_id", &hailo_notification_t::id)
        .def_readonly("sequence", &hailo_notification_t::sequence)
        .def_readonly("body", &hailo_notification_t::body)
        ;

    py::class_<hailo_notification_message_parameters_t>(m, "NotificationMessageParameters")
        UNION_PROPERTY(hailo_notification_message_parameters_t, hailo_rx_error_notification_message_t, rx_error_notification)
        UNION_PROPERTY(hailo_notification_message_parameters_t, hailo_debug_notification_message_t, debug_notification)
        UNION_PROPERTY(hailo_notification_message_parameters_t, hailo_health_monitor_dataflow_shutdown_notification_message_t, health_monitor_dataflow_shutdown_notification)
        UNION_PROPERTY(hailo_notification_message_parameters_t, hailo_health_monitor_temperature_alarm_notification_message_t, health_monitor_temperature_alarm_notification)
        UNION_PROPERTY(hailo_notification_message_parameters_t, hailo_health_monitor_overcurrent_alert_notification_message_t, health_monitor_overcurrent_alert_notification)
        UNION_PROPERTY(hailo_notification_message_parameters_t, hailo_health_monitor_lcu_ecc_error_notification_message_t, health_monitor_lcu_ecc_error_notification)
        UNION_PROPERTY(hailo_notification_message_parameters_t, hailo_health_monitor_cpu_ecc_notification_message_t, health_monitor_cpu_ecc_notification)
        UNION_PROPERTY(hailo_notification_message_parameters_t, hailo_health_monitor_clock_changed_notification_message_t, health_monitor_clock_changed_notification)
        ;

    py::enum_<hailo_overcurrent_protection_overcurrent_zone_t>(m, "OvercurrentAlertState")
        .value("OVERCURRENT_ZONE_GREEN", HAILO_OVERCURRENT_PROTECTION_OVERCURRENT_ZONE__GREEN)
        .value("OVERCURRENT_ZONE_RED", HAILO_OVERCURRENT_PROTECTION_OVERCURRENT_ZONE__RED)
        ;
    
    py::enum_<hailo_temperature_protection_temperature_zone_t>(m, "TemperatureZone")
        .value("TEMPERATURE_ZONE_GREEN", HAILO_TEMPERATURE_PROTECTION_TEMPERATURE_ZONE__GREEN)
        .value("TEMPERATURE_ZONE_ORANGE", HAILO_TEMPERATURE_PROTECTION_TEMPERATURE_ZONE__ORANGE)
        .value("TEMPERATURE_ZONE_RED", HAILO_TEMPERATURE_PROTECTION_TEMPERATURE_ZONE__RED)
        ;
    
    py::class_<hailo_rx_error_notification_message_t>(m, "RxErrorNotificationMessage")
        .def_readonly("error", &hailo_rx_error_notification_message_t::error)
        .def_readonly("queue_number", &hailo_rx_error_notification_message_t::queue_number)
        .def_readonly("rx_errors_count", &hailo_rx_error_notification_message_t::rx_errors_count)
        ;

    py::class_<hailo_debug_notification_message_t>(m, "DebugNotificationMessage")
        .def_readonly("connection_status", &hailo_debug_notification_message_t::connection_status)
        .def_readonly("connection_type", &hailo_debug_notification_message_t::connection_type)
        .def_readonly("vdma_is_active", &hailo_debug_notification_message_t::vdma_is_active)
        .def_readonly("host_port", &hailo_debug_notification_message_t::host_port)
        .def_readonly("host_ip_addr", &hailo_debug_notification_message_t::host_ip_addr)
        ;

    py::class_<hailo_health_monitor_dataflow_shutdown_notification_message_t>(m, "HealthMonitorDataflowShutdownNotificationMessage")
        .def_readonly("ts0_temperature", &hailo_health_monitor_dataflow_shutdown_notification_message_t::ts0_temperature)
        .def_readonly("ts1_temperature", &hailo_health_monitor_dataflow_shutdown_notification_message_t::ts1_temperature)
        ;

    py::class_<hailo_health_monitor_temperature_alarm_notification_message_t>(m, "HealthMonitorTemperatureAlarmNotificationMessage")
        .def_readonly("temperature_zone", &hailo_health_monitor_temperature_alarm_notification_message_t::temperature_zone)
        .def_readonly("alarm_ts_id", &hailo_health_monitor_temperature_alarm_notification_message_t::alarm_ts_id)
        .def_readonly("ts0_temperature", &hailo_health_monitor_temperature_alarm_notification_message_t::ts0_temperature)
        .def_readonly("ts1_temperature", &hailo_health_monitor_temperature_alarm_notification_message_t::ts1_temperature)
        ;

    py::class_<hailo_health_monitor_overcurrent_alert_notification_message_t>(m, "HealthMonitorOvercurrentAlertNotificationMessage")
        .def_readonly("overcurrent_zone", &hailo_health_monitor_overcurrent_alert_notification_message_t::overcurrent_zone)
        .def_readonly("exceeded_alert_threshold", &hailo_health_monitor_overcurrent_alert_notification_message_t::exceeded_alert_threshold)
        .def_readonly("is_last_overcurrent_violation_reached", &hailo_health_monitor_overcurrent_alert_notification_message_t::is_last_overcurrent_violation_reached)
        ;

    py::class_<hailo_health_monitor_lcu_ecc_error_notification_message_t>(m, "HealthMonitorLcuEccErrorNotificationMessage")
        .def_readonly("cluster_error", &hailo_health_monitor_lcu_ecc_error_notification_message_t::cluster_error)
        ;

    py::class_<hailo_health_monitor_cpu_ecc_notification_message_t>(m, "HealthMonitorCpuEccNotificationMessage")
        .def_readonly("memory_bitmap", &hailo_health_monitor_cpu_ecc_notification_message_t::memory_bitmap)
        ;

    py::class_<hailo_health_monitor_clock_changed_notification_message_t>(m, "HealthMonitoClockChangedNotificationMessage")
        .def_readonly("previous_clock", &hailo_health_monitor_clock_changed_notification_message_t::previous_clock)
        .def_readonly("current_clock", &hailo_health_monitor_clock_changed_notification_message_t::current_clock)
        ;

    py::enum_<hailo_notification_id_t>(m, "NotificationId")
        .value("ETHERNET_RX_ERROR", HAILO_NOTIFICATION_ID_ETHERNET_RX_ERROR)
        .value("HEALTH_MONITOR_TEMPERATURE_ALARM", HAILO_NOTIFICATION_ID_HEALTH_MONITOR_TEMPERATURE_ALARM)
        .value("HEALTH_MONITOR_DATAFLOW_SHUTDOWN", HAILO_NOTIFICATION_ID_HEALTH_MONITOR_DATAFLOW_SHUTDOWN)
        .value("HEALTH_MONITOR_OVERCURRENT_ALARM", HAILO_NOTIFICATION_ID_HEALTH_MONITOR_OVERCURRENT_ALARM)
        .value("HEALTH_MONITOR_LCU_ECC_CORRECTABLE_ERROR", HAILO_NOTIFICATION_ID_LCU_ECC_CORRECTABLE_ERROR)
        .value("HEALTH_MONITOR_LCU_ECC_UNCORRECTABLE_ERROR",  HAILO_NOTIFICATION_ID_LCU_ECC_UNCORRECTABLE_ERROR)
        .value("HEALTH_MONITOR_CPU_ECC_ERROR",  HAILO_NOTIFICATION_ID_CPU_ECC_ERROR)
        .value("HEALTH_MONITOR_CPU_ECC_FATAL",  HAILO_NOTIFICATION_ID_CPU_ECC_FATAL)
        .value("DEBUG", HAILO_NOTIFICATION_ID_DEBUG)
        .value("CONTEXT_SWITCH_BREAKPOINT_REACHED", HAILO_NOTIFICATION_ID_CONTEXT_SWITCH_BREAKPOINT_REACHED)
        .value("HEALTH_MONITOR_CLOCK_CHANGED_EVENT", HAILO_NOTIFICATION_ID_HEALTH_MONITOR_CLOCK_CHANGED_EVENT)
        ;

    py::enum_<hailo_watchdog_mode_t>(m, "WatchdogMode")
        .value("WATCHDOG_MODE_HW_SW", HAILO_WATCHDOG_MODE_HW_SW)
        .value("WATCHDOG_MODE_HW_ONLY", HAILO_WATCHDOG_MODE_HW_ONLY)
        ;

    py::class_<hailo_firmware_version_t>(m, "FirmwareVersion")
        .def_readonly("major", &hailo_firmware_version_t::major)
        .def_readonly("minor", &hailo_firmware_version_t::minor)
        .def_readonly("revision", &hailo_firmware_version_t::revision)
        ;

    py::class_<hailo_device_identity_t>(m, "BoardInformation")
        .def_readonly("protocol_version", &hailo_device_identity_t::protocol_version)
        .def_readonly("fw_version", &hailo_device_identity_t::fw_version)
        .def_readonly("logger_version", &hailo_device_identity_t::logger_version)
        .def_readonly("board_name_length", &hailo_device_identity_t::board_name_length)
        .def_readonly("is_release", &hailo_device_identity_t::is_release)
        .def_readonly("extended_context_switch_buffer", &hailo_device_identity_t::extended_context_switch_buffer)
        .def_readonly("device_architecture", &hailo_device_identity_t::device_architecture)
        .def_property_readonly("board_name", [](const hailo_device_identity_t& board_info) -> py::str {
            return py::str(board_info.board_name, board_info.board_name_length);
        })
        .def_readonly("serial_number_length", &hailo_device_identity_t::serial_number_length)
        .def_property_readonly("serial_number", [](const hailo_device_identity_t& board_info) -> py::str {
            return py::str(board_info.serial_number, board_info.serial_number_length);
        })
        .def_readonly("part_number_length", &hailo_device_identity_t::part_number_length)
        .def_property_readonly("part_number", [](const hailo_device_identity_t& board_info) -> py::str {
            return py::str(board_info.part_number, board_info.part_number_length);
        })
        .def_readonly("product_name_length", &hailo_device_identity_t::product_name_length)
        .def_property_readonly("product_name", [](const hailo_device_identity_t& board_info) -> py::str {
            return py::str(board_info.product_name, board_info.product_name_length);
        })
        ;

    py::class_<hailo_core_information_t>(m, "CoreInformation")
        .def_readonly("is_release", &hailo_core_information_t::is_release)
        .def_readonly("extended_context_switch_buffer", &hailo_core_information_t::extended_context_switch_buffer)
        .def_readonly("fw_version", &hailo_core_information_t::fw_version)
        ;

    py::class_<hailo_fw_user_config_information_t>(m, "FirmwareUserConfigInformation")
        .def_readonly("version", &hailo_fw_user_config_information_t::version)
        .def_readonly("entry_count", &hailo_fw_user_config_information_t::entry_count)
        .def_readonly("total_size", &hailo_fw_user_config_information_t::total_size)
        ;

    py::enum_<hailo_endianness_t>(m, "Endianness")
        .value("BIG_ENDIAN", HAILO_BIG_ENDIAN)
        .value("LITTLE_ENDIAN", HAILO_LITTLE_ENDIAN)
        ;

    py::enum_<hailo_sensor_types_t>(m, "SensorConfigTypes")
        .value("SENSOR_GENERIC", HAILO_SENSOR_TYPES_GENERIC)
        .value("ONSEMI_AR0220AT", HAILO_SENSOR_TYPES_ONSEMI_AR0220AT)
        .value("SENSOR_RASPICAM", HAILO_SENSOR_TYPES_RASPICAM)
        .value("ONSEMI_AS0149AT", HAILO_SENSOR_TYPES_ONSEMI_AS0149AT)
        .value("HAILO8_ISP", HAILO_SENSOR_TYPES_HAILO8_ISP)
        ;

    py::class_<hailo_i2c_slave_config_t>(m, "I2CSlaveConfig")
        .def(py::init<>())
        .def_readwrite("endianness", &hailo_i2c_slave_config_t::endianness)
        .def_readwrite("slave_address", &hailo_i2c_slave_config_t::slave_address)
        .def_readwrite("register_address_size", &hailo_i2c_slave_config_t::register_address_size)
        .def_readwrite("bus_index", &hailo_i2c_slave_config_t::bus_index)
        .def_readwrite("should_hold_bus", &hailo_i2c_slave_config_t::should_hold_bus)
        ;

    py::enum_<hailo_reset_device_mode_t>(m, "ResetDeviceMode")
        .value("CHIP", HAILO_RESET_DEVICE_MODE_CHIP)
        .value("NN_CORE", HAILO_RESET_DEVICE_MODE_NN_CORE)
        .value("SOFT", HAILO_RESET_DEVICE_MODE_SOFT)
        .value("FORCED_SOFT", HAILO_RESET_DEVICE_MODE_FORCED_SOFT)
        ;

    py::enum_<hailo_stream_direction_t>(m, "StreamDirection")
        .value("H2D", HAILO_H2D_STREAM)
        .value("D2H", HAILO_D2H_STREAM)
        ;

    py::class_<hailo_3d_image_shape_t>(m, "ImageShape")
        .def(py::init<>())
        .def(py::init<const uint32_t, const uint32_t, const uint32_t>())
        .def_readwrite("height", &hailo_3d_image_shape_t::height)
        .def_readwrite("width", &hailo_3d_image_shape_t::width)
        .def_readwrite("features", &hailo_3d_image_shape_t::features)
        .def(py::pickle(
            [](const hailo_3d_image_shape_t &shape) { // __getstate__
                return py::make_tuple(
                    shape.height,
                    shape.width,
                    shape.features);
            },
            [](py::tuple t) { // __setstate__
                hailo_3d_image_shape_t shape;
                shape.height = t[0].cast<uint32_t>();
                shape.width = t[1].cast<uint32_t>();
                shape.features = t[2].cast<uint32_t>();
                return shape;
            }
        ))
        ;

    py::class_<hailo_nms_shape_t>(m, "NmsShape")
        .def(py::init<>())
        .def_readonly("number_of_classes", &hailo_nms_shape_t::number_of_classes)
        .def_readonly("max_bboxes_per_class", &hailo_nms_shape_t::max_bboxes_per_class)
        .def_readonly("max_accumulated_mask_size", &hailo_nms_shape_t::max_accumulated_mask_size)
        .def(py::pickle(
            [](const hailo_nms_shape_t &nms_shape) { // __getstate__
                return py::make_tuple(
                    nms_shape.number_of_classes,
                    nms_shape.max_bboxes_per_class,
                    nms_shape.max_accumulated_mask_size);
            },
            [](py::tuple t) { // __setstate__
                hailo_nms_shape_t nms_shape;
                nms_shape.number_of_classes = t[0].cast<uint32_t>();
                nms_shape.max_bboxes_per_class = t[1].cast<uint32_t>();
                nms_shape.max_accumulated_mask_size = t[2].cast<uint32_t>();
                return nms_shape;
            }
        ))
        ;

    py::class_<hailo_nms_info_t>(m, "NmsInfo")
        .def(py::init<>())
        .def_readwrite("number_of_classes", &hailo_nms_info_t::number_of_classes)
        .def_readwrite("max_bboxes_per_class", &hailo_nms_info_t::max_bboxes_per_class)
        .def_readwrite("bbox_size", &hailo_nms_info_t::bbox_size)
        .def_readwrite("chunks_per_frame", &hailo_nms_info_t::chunks_per_frame)
        ;

    py::enum_<hailo_format_type_t>(m, "FormatType", "Data formats accepted by HailoRT.")
        .value("AUTO", HAILO_FORMAT_TYPE_AUTO, "Chosen automatically to match the format expected by the device, usually UINT8.")
        .value("UINT8", HAILO_FORMAT_TYPE_UINT8)
        .value("UINT16", HAILO_FORMAT_TYPE_UINT16)
        .value("FLOAT32", HAILO_FORMAT_TYPE_FLOAT32)
        ;

    py::enum_<hailo_format_order_t>(m, "FormatOrder")
        .value("AUTO", HAILO_FORMAT_ORDER_AUTO)
        .value("NHWC", HAILO_FORMAT_ORDER_NHWC)
        .value("NHCW", HAILO_FORMAT_ORDER_NHCW)
        .value("FCR", HAILO_FORMAT_ORDER_FCR)
        .value("F8CR", HAILO_FORMAT_ORDER_F8CR)
        .value("NHW", HAILO_FORMAT_ORDER_NHW)
        .value("NC", HAILO_FORMAT_ORDER_NC)
        .value("BAYER_RGB", HAILO_FORMAT_ORDER_BAYER_RGB)
        .value("12_BIT_BAYER_RGB", HAILO_FORMAT_ORDER_12_BIT_BAYER_RGB)
        .value("RGB888", HAILO_FORMAT_ORDER_RGB888)
        .value("NCHW", HAILO_FORMAT_ORDER_NCHW)
        .value("YUY2", HAILO_FORMAT_ORDER_YUY2)
        .value("NV12", HAILO_FORMAT_ORDER_NV12)
        .value("YYUV", HAILO_FORMAT_ORDER_HAILO_YYUV)
        .value("NV21", HAILO_FORMAT_ORDER_NV21)
        .value("YYVU", HAILO_FORMAT_ORDER_HAILO_YYVU)
        .value("RGB4", HAILO_FORMAT_ORDER_RGB4)
        .value("I420", HAILO_FORMAT_ORDER_I420)
        .value("YYYYUV", HAILO_FORMAT_ORDER_HAILO_YYYYUV)
        .value("HAILO_NMS_WITH_BYTE_MASK", HAILO_FORMAT_ORDER_HAILO_NMS_WITH_BYTE_MASK)
        .value("HAILO_NMS_ON_CHIP", HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP)
        .value("HAILO_NMS_BY_CLASS", HAILO_FORMAT_ORDER_HAILO_NMS_BY_CLASS)
        .value("HAILO_NMS_BY_SCORE", HAILO_FORMAT_ORDER_HAILO_NMS_BY_SCORE)
        ;

    py::enum_<hailo_format_flags_t>(m, "FormatFlags", py::arithmetic())
        .value("NONE", HAILO_FORMAT_FLAGS_NONE)
        .value("TRANSPOSED", HAILO_FORMAT_FLAGS_TRANSPOSED)
        ;

    py::enum_<hailo_stream_transform_mode_t>(m, "TransformMode")
        .value("NO_TRANSFORM", HAILO_STREAM_NO_TRANSFORM)
        .value("TRANSFORM_COPY", HAILO_STREAM_TRANSFORM_COPY)
        ;

    py::class_<hailo_format_t>(m, "HailoFormat")
        .def(py::init<>())
        .def_readwrite("type", &hailo_format_t::type)
        .def_readwrite("order", &hailo_format_t::order)
        .def_readwrite("flags", &hailo_format_t::flags)
        .def("equals", &hailo_format_equals)
        .def(py::pickle(
            [](const hailo_format_t &hailo_format) { // __getstate__
                return py::make_tuple(
                    hailo_format.type,
                    hailo_format.order,
                    hailo_format.flags);
            },
            [](py::tuple t) { // __setstate__
                hailo_format_t hailo_format;
                hailo_format.type = t[0].cast<hailo_format_type_t>();
                hailo_format.order = t[1].cast<hailo_format_order_t>();
                hailo_format.flags = t[2].cast<hailo_format_flags_t>();
                return hailo_format;
            }
        ))
        ;

    py::class_<hailo_quant_info_t>(m, "QuantInfo")
        .def(py::init<>())
        .def(py::init<const float32_t, const float32_t, const float32_t, const float32_t>())
        .def_readwrite("qp_zp", &hailo_quant_info_t::qp_zp)
        .def_readwrite("qp_scale", &hailo_quant_info_t::qp_scale)
        .def_readwrite("limvals_min", &hailo_quant_info_t::limvals_min)
        .def_readwrite("limvals_max", &hailo_quant_info_t::limvals_max)
        .def(py::pickle(
            [](const hailo_quant_info_t &quant_info) { // __getstate__
                return py::make_tuple(
                    quant_info.qp_zp,
                    quant_info.qp_scale,
                    quant_info.limvals_min,
                    quant_info.limvals_max);
            },
            [](py::tuple t) { // __setstate__
                hailo_quant_info_t quant_info;
                quant_info.qp_zp = t[0].cast<float32_t>();
                quant_info.qp_scale = t[1].cast<float32_t>();
                quant_info.limvals_min = t[2].cast<float32_t>();
                quant_info.limvals_max = t[3].cast<float32_t>();
                return quant_info;
            }
        ))
        ;

    py::class_<hailo_transform_params_t>(m, "TransformParams")
        .def(py::init<>())
        .def_readwrite("transform_mode", &hailo_transform_params_t::transform_mode)
        .def_readwrite("user_buffer_format", &hailo_transform_params_t::user_buffer_format)
        ;


    py::class_<hailo_pcie_output_stream_params_t>(m, "PcieOutputStreamParams")
        .def(py::init<>())
        ;

    py::class_<hailo_pcie_input_stream_params_t>(m, "PcieInputStreamParams")
        .def(py::init<>())
        ;

    py::class_<hailo_integrated_input_stream_params_t>(m, "IntegratedInputStreamParams")
        .def(py::init<>())
        ;

    py::class_<hailo_integrated_output_stream_params_t>(m, "IntegratedOutputStreamParams")
        .def(py::init<>())
        ;

    py::enum_<hailo_stream_interface_t>(m, "StreamInterface")
        .value("PCIe", HAILO_STREAM_INTERFACE_PCIE)
        .value("INTEGRATED", HAILO_STREAM_INTERFACE_INTEGRATED)
        .value("ETH", HAILO_STREAM_INTERFACE_ETH)
        .value("MIPI", HAILO_STREAM_INTERFACE_MIPI)
        ;

    py::enum_<hailo_vstream_stats_flags_t>(m, "VStreamStatsFlags")
        .value("NONE", hailo_vstream_stats_flags_t::HAILO_VSTREAM_STATS_NONE)
        .value("MEASURE_FPS", hailo_vstream_stats_flags_t::HAILO_VSTREAM_STATS_MEASURE_FPS)
        .value("MEASURE_LATENCY", hailo_vstream_stats_flags_t::HAILO_VSTREAM_STATS_MEASURE_LATENCY)
        ;

    py::enum_<hailo_pipeline_elem_stats_flags_t>(m, "PipelineElemStatsFlags")
        .value("NONE", hailo_pipeline_elem_stats_flags_t::HAILO_PIPELINE_ELEM_STATS_NONE)
        .value("MEASURE_FPS", hailo_pipeline_elem_stats_flags_t::HAILO_PIPELINE_ELEM_STATS_MEASURE_FPS)
        .value("MEASURE_LATENCY", hailo_pipeline_elem_stats_flags_t::HAILO_PIPELINE_ELEM_STATS_MEASURE_LATENCY)
        .value("MEASURE_QUEUE_SIZE", hailo_pipeline_elem_stats_flags_t::HAILO_PIPELINE_ELEM_STATS_MEASURE_QUEUE_SIZE)
        ;

    py::class_<hailo_vstream_params_t>(m, "VStreamParams")
        .def(py::init<>())
        .def_readwrite("user_buffer_format", &hailo_vstream_params_t::user_buffer_format)
        .def_readwrite("timeout_ms", &hailo_vstream_params_t::timeout_ms)
        .def_readwrite("queue_size", &hailo_vstream_params_t::queue_size)
        .def_readonly("vstream_stats_flags", &hailo_vstream_params_t::vstream_stats_flags)
        .def_readonly("pipeline_elements_stats_flags", &hailo_vstream_params_t::pipeline_elements_stats_flags)
        .def(py::pickle(
            [](const hailo_vstream_params_t &vstream_params) { // __getstate__
                return py::make_tuple(
                    vstream_params.user_buffer_format,
                    vstream_params.timeout_ms,
                    vstream_params.queue_size,
                    vstream_params.vstream_stats_flags,
                    vstream_params.pipeline_elements_stats_flags);
            },
            [](py::tuple t) { // __setstate__
                hailo_vstream_params_t vstream_params;
                vstream_params.user_buffer_format = t[0].cast<hailo_format_t>();
                vstream_params.timeout_ms = t[1].cast<uint32_t>();
                vstream_params.queue_size = t[2].cast<uint32_t>();
                vstream_params.vstream_stats_flags = t[3].cast<hailo_vstream_stats_flags_t>();
                vstream_params.pipeline_elements_stats_flags = t[4].cast<hailo_pipeline_elem_stats_flags_t>();
                return vstream_params;
            }
        ))
        ;

    py::enum_<hailo_latency_measurement_flags_t>(m, "LatencyMeasurementFlags")
        .value("NONE", HAILO_LATENCY_NONE)
        .value("CLEAR_AFTER_GET", HAILO_LATENCY_CLEAR_AFTER_GET)
        .value("MEASURE", HAILO_LATENCY_MEASURE)
        ;

    py::enum_<hailo_power_mode_t>(m, "PowerMode")
        .value("ULTRA_PERFORMANCE", HAILO_POWER_MODE_ULTRA_PERFORMANCE)
        .value("PERFORMANCE", HAILO_POWER_MODE_PERFORMANCE)
        ;

    py::class_<hailo_activate_network_group_params_t>(m, "ActivateNetworkGroupParams")
        .def(py::init<>())
        .def_static("default", []() {
            return HailoRTDefaults::get_active_network_group_params();
        });
        ;

    py::enum_<hailo_scheduling_algorithm_t>(m, "SchedulingAlgorithm")
        .value("NONE", HAILO_SCHEDULING_ALGORITHM_NONE)
        .value("ROUND_ROBIN", HAILO_SCHEDULING_ALGORITHM_ROUND_ROBIN)
    ;

    py::class_<VDeviceParamsWrapper>(m, "VDeviceParams")
        .def(py::init<>())
        .def_property("device_ids",
            [](const VDeviceParamsWrapper &params) -> py::list {
                py::list ids;
                if (params.orig_params.device_ids != nullptr) {
                    for (size_t i = 0; i < params.orig_params.device_count; i++) {
                        ids.append(std::string(params.orig_params.device_ids[i].id));
                    }
                }
                return ids;
            },
            [](VDeviceParamsWrapper &params, const py::list &device_ids) {
                uint32_t count = static_cast<uint32_t>(py::len(device_ids));
                params.ids.resize(count);
                for (size_t i = 0; i < count; i++) {
                    std::string id_str = py::cast<std::string>(device_ids[i]);
                    auto expected_device_id = HailoRTCommon::to_device_id(id_str);
                    VALIDATE_EXPECTED(expected_device_id);
                    params.ids[i] = expected_device_id.release();
                }
                params.orig_params.device_ids = params.ids.data();
                params.orig_params.device_count = count;
            }
        )
        .def_property("device_count",
            [](const VDeviceParamsWrapper& params) -> uint32_t {
                return params.orig_params.device_count;
            },
            [](VDeviceParamsWrapper& params, const uint32_t& device_count) {
                params.orig_params.device_count = device_count;
            }
        )
        .def_property("scheduling_algorithm",
            [](const VDeviceParamsWrapper& params) -> uint32_t {
                return params.orig_params.scheduling_algorithm;
            },
            [](VDeviceParamsWrapper& params, hailo_scheduling_algorithm_t scheduling_algorithm) {
                params.orig_params.scheduling_algorithm = scheduling_algorithm;
            }
        )
        .def_property("group_id",
            [](const VDeviceParamsWrapper& params) -> py::str {
                return std::string(params.orig_params.group_id);
            },
            [](VDeviceParamsWrapper& params, const std::string& group_id) {
                params.group_id_str = group_id;
                params.orig_params.group_id = params.group_id_str.c_str();
            }
        )
        .def_property("multi_process_service",
            [](const VDeviceParamsWrapper& params) -> bool {
                return params.orig_params.multi_process_service;
            },
            [](VDeviceParamsWrapper& params, bool multi_process_service) {
                params.orig_params.multi_process_service = multi_process_service;
            }
        )
        .def_static("default", []() {
            auto orig_params = HailoRTDefaults::get_vdevice_params();
            VDeviceParamsWrapper params_wrapper{orig_params, "", {}};
            return params_wrapper;
        });
        ;

    py::class_<hailo_stream_parameters_t>(m, "StreamParameters")
        .def_readwrite("stream_interface", &hailo_stream_parameters_t::stream_interface)
        .def_readonly("direction", &hailo_stream_parameters_t::direction)
        STREAM_PARAMETERS_UNION_PROPERTY(pcie_input_params, hailo_pcie_input_stream_params_t,
            HAILO_STREAM_INTERFACE_PCIE, HAILO_H2D_STREAM)
        STREAM_PARAMETERS_UNION_PROPERTY(integrated_input_params, hailo_integrated_input_stream_params_t,
            HAILO_STREAM_INTERFACE_INTEGRATED, HAILO_H2D_STREAM)
        STREAM_PARAMETERS_UNION_PROPERTY(pcie_output_params, hailo_pcie_output_stream_params_t,
            HAILO_STREAM_INTERFACE_PCIE, HAILO_D2H_STREAM)
        STREAM_PARAMETERS_UNION_PROPERTY(integrated_output_params, hailo_integrated_output_stream_params_t,
            HAILO_STREAM_INTERFACE_INTEGRATED, HAILO_D2H_STREAM)
        ;

    py::class_<hailo_network_parameters_t>(m, "NetworkParameters")
        .def(py::init<>())
        .def_readwrite("batch_size", &hailo_network_parameters_t::batch_size)
        ;


    py::class_<ConfigureNetworkParams>(m, "ConfigureParams")
        .def(py::init<>())
        .def_readwrite("batch_size", &ConfigureNetworkParams::batch_size)
        .def_readwrite("power_mode", &ConfigureNetworkParams::power_mode)
        .def_readwrite("stream_params_by_name", &ConfigureNetworkParams::stream_params_by_name)
        .def_readwrite("network_params_by_name", &ConfigureNetworkParams::network_params_by_name)
        ;

    py::class_<hailo_chip_temperature_info_t>(m, "TemperatureInfo")
        .def_readonly("ts0_temperature", &hailo_chip_temperature_info_t::ts0_temperature)
        .def_readonly("ts1_temperature", &hailo_chip_temperature_info_t::ts1_temperature)
        .def_readonly("sample_count", &hailo_chip_temperature_info_t::sample_count)
        .def("equals", &temperature_info_equals)
        .def(py::pickle(
            [](const hailo_chip_temperature_info_t &temperature_info) { // __getstate__
                return py::make_tuple(
                    temperature_info.ts0_temperature,
                    temperature_info.ts1_temperature,
                    temperature_info.sample_count);
            },
            [](py::tuple t) { // __setstate__
                hailo_chip_temperature_info_t temperature_info;
                temperature_info.ts0_temperature = t[0].cast<float32_t>();
                temperature_info.ts1_temperature = t[1].cast<float32_t>();
                temperature_info.sample_count = t[2].cast<uint16_t>();
                return temperature_info;
            }
        ))
        ;

    py::class_<hailo_throttling_level_t>(m, "ThrottlingLevel", py::module_local())
        .def_readonly("temperature_threshold", &hailo_throttling_level_t::temperature_threshold)
        .def_readonly("hysteresis_temperature_threshold", &hailo_throttling_level_t::hysteresis_temperature_threshold)
        .def_readonly("throttling_nn_clock_freq", &hailo_throttling_level_t::throttling_nn_clock_freq)
        ;

    py::class_<hailo_health_info_t>(m, "HealthInformation")
        .def_readonly("overcurrent_protection_active", &hailo_health_info_t::overcurrent_protection_active)
        .def_readonly("current_overcurrent_zone", &hailo_health_info_t::current_overcurrent_zone)
        .def_readonly("red_overcurrent_threshold", &hailo_health_info_t::red_overcurrent_threshold)
        .def_readonly("overcurrent_throttling_active", &hailo_health_info_t::overcurrent_throttling_active)
        .def_readonly("temperature_throttling_active", &hailo_health_info_t::temperature_throttling_active)
        .def_readonly("current_temperature_zone", &hailo_health_info_t::current_temperature_zone)
        .def_readonly("current_temperature_throttling_level", &hailo_health_info_t::current_temperature_throttling_level)
        .def_readonly("temperature_throttling_levels", &hailo_health_info_t::temperature_throttling_levels)
        .def_property_readonly("temperature_throttling_levels", [](const hailo_health_info_t& info) -> py::list {
            std::vector<hailo_throttling_level_t> throttling_levels;
            for (const auto &temperature_throttling_level : info.temperature_throttling_levels) {
                throttling_levels.push_back(temperature_throttling_level);
            }
            return py::cast(throttling_levels);
        })
        .def_readonly("orange_temperature_threshold", &hailo_health_info_t::orange_temperature_threshold)
        .def_readonly("orange_hysteresis_temperature_threshold", &hailo_health_info_t::orange_hysteresis_temperature_threshold)
        .def_readonly("red_temperature_threshold", &hailo_health_info_t::red_temperature_threshold)
        .def_readonly("red_hysteresis_temperature_threshold", &hailo_health_info_t::red_hysteresis_temperature_threshold)
        .def_readonly("requested_overcurrent_clock_freq", &hailo_health_info_t::requested_overcurrent_clock_freq)
        .def_readonly("requested_temperature_clock_freq", &hailo_health_info_t::requested_temperature_clock_freq)
        ;

    py::class_<hailo_extended_device_information_t>(m, "ExtendedDeviceInformation")
        .def_readonly("neural_network_core_clock_rate", &hailo_extended_device_information_t::neural_network_core_clock_rate)
        .def_readonly("supported_features", &hailo_extended_device_information_t::supported_features)
        .def_readonly("boot_source", &hailo_extended_device_information_t::boot_source)
        .def_readonly("lcs", &hailo_extended_device_information_t::lcs)
        .def_property_readonly("unit_level_tracking_id", [](const hailo_extended_device_information_t& info) -> py::bytes {
            return std::string((const char*) info.unit_level_tracking_id, sizeof(info.unit_level_tracking_id));
        })
        .def_property_readonly("eth_mac_address", [](const hailo_extended_device_information_t& info) -> py::bytes {
            return std::string((const char*) info.eth_mac_address, sizeof(info.eth_mac_address));
        })
        .def_property_readonly("soc_id", [](const hailo_extended_device_information_t& info) -> py::bytes {
            return std::string((const char*) info.soc_id, sizeof(info.soc_id));
        })
        .def_property_readonly("soc_pm_values", [](const hailo_extended_device_information_t& info) -> py::bytes {
            return std::string((const char*) info.soc_pm_values, sizeof(info.soc_pm_values));
        })
        .def_readonly("gpio_mask", &hailo_extended_device_information_t::gpio_mask)
        ;

    py::enum_<hailo_device_boot_source_t>(m, "BootSource")
        .value("INVALID", HAILO_DEVICE_BOOT_SOURCE_INVALID)
        .value("PCIE", HAILO_DEVICE_BOOT_SOURCE_PCIE)
        .value("FLASH", HAILO_DEVICE_BOOT_SOURCE_FLASH)
        ;

    py::enum_<hailo_fw_logger_interface_t>(m, "FwLoggerInterface", py::arithmetic())
        .value("PCIE", HAILO_FW_LOGGER_INTERFACE_PCIE)
        .value("UART", HAILO_FW_LOGGER_INTERFACE_UART)
        ;

    py::enum_<hailo_fw_logger_level_t>(m, "FwLoggerLevel")
        .value("TRACE", HAILO_FW_LOGGER_LEVEL_TRACE)
        .value("DEBUG", HAILO_FW_LOGGER_LEVEL_DEBUG)
        .value("INFO", HAILO_FW_LOGGER_LEVEL_INFO)
        .value("WARN", HAILO_FW_LOGGER_LEVEL_WARN)
        .value("ERROR", HAILO_FW_LOGGER_LEVEL_ERROR)
        .value("FATAL", HAILO_FW_LOGGER_LEVEL_FATAL)
        ;

    py::class_<hailo_device_supported_features_t>(m, "SupportedFeatures")
        .def_readonly("ethernet", &hailo_device_supported_features_t::ethernet)
        .def_readonly("pcie", &hailo_device_supported_features_t::pcie)
        .def_readonly("current_monitoring", &hailo_device_supported_features_t::current_monitoring)
        .def_readonly("mdio", &hailo_device_supported_features_t::mdio)
        .def_readonly("power_measurement", &hailo_device_supported_features_t::power_measurement)
        ;

    py::class_<sockaddr_in>(m, "sockaddr_in")
        .def_readwrite("sin_port", &sockaddr_in::sin_port)
        ;

    py::register_exception<HailoRTException>(m, "HailoRTException");
    py::register_exception<HailoRTCustomException>(m, "HailoRTCustomException");
    py::register_exception<HailoRTStatusException>(m, "HailoRTStatusException");

    py::enum_<hailo_cpu_id_t>(m, "CpuId")
        .value("CPU0", HAILO_CPU_ID_0)
        .value("CPU1", HAILO_CPU_ID_1)
        ;

    py::enum_<hailo_sleep_state_t>(m, "SleepState")
        .value("SLEEP_STATE_SLEEPING", HAILO_SLEEP_STATE_SLEEPING)
        .value("SLEEP_STATE_AWAKE", HAILO_SLEEP_STATE_AWAKE)
        ;

    py::class_<uint32_t>(m, "HailoRTDefaults")
        .def_static("HAILO_INFINITE", []() { return HAILO_INFINITE;} )
        .def_static("BBOX_PARAMS", []() { return HailoRTCommon::BBOX_PARAMS;} )
        .def_static("DEVICE_BASE_INPUT_STREAM_PORT", []() { return HailoRTCommon::ETH_INPUT_BASE_PORT;} )
        .def_static("DEVICE_BASE_OUTPUT_STREAM_PORT", []() { return HailoRTCommon::ETH_OUTPUT_BASE_PORT;} )
        .def_static("PCIE_ANY_DOMAIN", []() { return HAILO_PCIE_ANY_DOMAIN;} )
        .def_static("HAILO_UNIQUE_VDEVICE_GROUP_ID", []() { return std::string(HAILO_UNIQUE_VDEVICE_GROUP_ID); } )
        ;

    py::class_<hailo_network_group_info_t>(m, "NetworkGroupInfo", py::module_local())
        .def_readonly("name", &hailo_network_group_info_t::name)
        .def_readonly("is_multi_context", &hailo_network_group_info_t::is_multi_context)
        ;

    py::class_<hailo_vstream_info_t>(m, "VStreamInfo", py::module_local())
        .def_property_readonly("shape", [](const hailo_vstream_info_t &self) {
            switch (self.format.order) {
                case HAILO_FORMAT_ORDER_NC:
                    return py::make_tuple(self.shape.features);
                case HAILO_FORMAT_ORDER_NHW:
                    return py::make_tuple(self.shape.height, self.shape.width);
                case HAILO_FORMAT_ORDER_HAILO_NMS_BY_CLASS:
                    return py::make_tuple(self.nms_shape.number_of_classes, HailoRTCommon::BBOX_PARAMS, self.nms_shape.max_bboxes_per_class);
                case HAILO_FORMAT_ORDER_HAILO_NMS_BY_SCORE:
                case HAILO_FORMAT_ORDER_HAILO_NMS_WITH_BYTE_MASK:
                    throw HailoRTCustomException("Format order has no shape");
                default:
                    return py::make_tuple(self.shape.height, self.shape.width, self.shape.features);
            }
        })
        .def_property_readonly("nms_shape", [](const hailo_vstream_info_t &self) {
            if (!HailoRTCommon::is_nms(self)) {
                throw HailoRTCustomException("nms_shape is availale only on nms order vstreams");
            }
            return self.nms_shape;
        })
        .def_readonly("direction", &hailo_vstream_info_t::direction)
        .def_readonly("format", &hailo_vstream_info_t::format)
        .def_readonly("quant_info", &hailo_vstream_info_t::quant_info)
        .def_readonly("name", &hailo_vstream_info_t::name)
        .def_readonly("network_name", &hailo_vstream_info_t::network_name)
        .def("__repr__", [](const hailo_vstream_info_t &self) {
            return std::string("VStreamInfo(\"") + std::string(self.name) + std::string("\")");
        })
        .def(py::pickle(
            [](const hailo_vstream_info_t &vstream_info) { // __getstate__
                if (HailoRTCommon::is_nms(vstream_info)) {
                    return py::make_tuple(
                        vstream_info.name,
                        vstream_info.network_name,
                        vstream_info.direction,
                        vstream_info.format,
                        vstream_info.nms_shape,
                        vstream_info.quant_info);
                }
                else {
                    return py::make_tuple(
                        vstream_info.name,
                        vstream_info.network_name,
                        vstream_info.direction,
                        vstream_info.format,
                        vstream_info.shape,
                        vstream_info.quant_info);
                }
            },
            [](py::tuple t) { // __setstate__
                hailo_vstream_info_t vstream_info;
                strcpy(vstream_info.name, t[0].cast<std::string>().c_str());
                strcpy(vstream_info.network_name, t[1].cast<std::string>().c_str());
                vstream_info.direction = t[2].cast<hailo_stream_direction_t>();
                vstream_info.format = t[3].cast<hailo_format_t>();
                if (HailoRTCommon::is_nms(vstream_info)) {
                    vstream_info.nms_shape = t[4].cast<hailo_nms_shape_t>();
                }
                else {
                    vstream_info.shape = t[4].cast<hailo_3d_image_shape_t>();
                }
                vstream_info.quant_info = t[5].cast<hailo_quant_info_t>();
                return vstream_info;
            }
        ))
        ;

    py::class_<hailo_stream_info_t>(m, "StreamInfo", py::module_local())
        .def_property_readonly("shape", [](const hailo_stream_info_t &self) {
            switch (self.format.order) {
                case HAILO_FORMAT_ORDER_NC:
                    return py::make_tuple(self.hw_shape.features);
                case HAILO_FORMAT_ORDER_NHW:
                    return py::make_tuple(self.hw_shape.height, self.hw_shape.width);
                case HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP:
                    return py::make_tuple(HailoRTCommon::get_nms_hw_frame_size(self.nms_info));
                default:
                    return py::make_tuple(self.hw_shape.height, self.hw_shape.width, self.hw_shape.features);
            }
        })
        .def_property_readonly("nms_shape", [](const hailo_stream_info_t &self) {
            if (HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP != self.format.order) {
                throw HailoRTCustomException("nms_shape is availale only on nms order streams");
            }
            return py::make_tuple(HailoRTCommon::get_nms_hw_frame_size(self.nms_info));
        })
        .def_readonly("direction", &hailo_stream_info_t::direction)
        .def_readonly("format", &hailo_stream_info_t::format)
        .def_readonly("name", &hailo_stream_info_t::name)
        .def_readonly("sys_index", &hailo_stream_info_t::index)
        .def_readonly("data_bytes", &hailo_stream_info_t::hw_data_bytes)
        .def_readonly("quant_info", &hailo_stream_info_t::quant_info)
        .def("__repr__", [](const hailo_stream_info_t &self) {
            return std::string("StreamInfo(\"") + std::string(self.name) + std::string("\")");
        })
        ;

    // TODO: Consider moving this class to a separate file, and only bind here
    py::class_<genai::LLMGeneratorParams>(m, "LLMGeneratorParams", py::module_local())
        .def_property("temperature",
            [](const genai::LLMGeneratorParams& params) -> float32_t {
                return params.temperature();
            },
            [](genai::LLMGeneratorParams& params, float32_t value) {
                VALIDATE_STATUS(params.set_temperature(value));
            })
        .def_property("top_k",
            [](const genai::LLMGeneratorParams& params) -> uint32_t {
                return params.top_k();
            },
            [](genai::LLMGeneratorParams& params, uint32_t value) {
                VALIDATE_STATUS(params.set_top_k(value));
            })
        .def_property("top_p",
            [](const genai::LLMGeneratorParams& params) -> float32_t {
                return params.top_p();
            },
            [](genai::LLMGeneratorParams& params, float32_t value) {
                VALIDATE_STATUS(params.set_top_p(value));
            })
        .def_property("frequency_penalty",
            [](const genai::LLMGeneratorParams& params) -> float32_t {
                return params.frequency_penalty();
            },
            [](genai::LLMGeneratorParams& params, float32_t value) {
                VALIDATE_STATUS(params.set_frequency_penalty(value));
            })
        .def_property("max_generated_tokens",
            [](const genai::LLMGeneratorParams& params) -> uint32_t {
                return params.max_generated_tokens();
            },
            [](genai::LLMGeneratorParams& params, uint32_t value) {
                VALIDATE_STATUS(params.set_max_generated_tokens(value));
            })
        .def_property("seed",
            [](const genai::LLMGeneratorParams& params) -> uint32_t {
                return params.seed();
            },
            [](genai::LLMGeneratorParams& params, uint32_t value) {
                VALIDATE_STATUS(params.set_seed(value));
            })
        .def_property("do_sample",
            [](const genai::LLMGeneratorParams& params) -> bool {
                return params.do_sample();
            },
            [](genai::LLMGeneratorParams& params, bool value) {
                VALIDATE_STATUS(params.set_do_sample(value));
            })
        ;

    py::enum_<genai::LLMGeneratorCompletion::Status>(m, "LLMGeneratorCompletionStatus")
        .value("GENERATING", genai::LLMGeneratorCompletion::Status::GENERATING)
        .value("LOGICAL_END_OF_GENERATION", genai::LLMGeneratorCompletion::Status::LOGICAL_END_OF_GENERATION)
        .value("MAX_TOKENS_REACHED", genai::LLMGeneratorCompletion::Status::MAX_TOKENS_REACHED)
        .value("ABORTED", genai::LLMGeneratorCompletion::Status::ABORTED)
        ;

    ActivatedAppContextManagerWrapper::bind(m);
    AsyncInferJobWrapper::bind(m);
    ConfiguredInferModelBindingsInferStreamWrapper::bind(m);
    ConfiguredInferModelBindingsWrapper::bind(m);
    ConfiguredInferModelWrapper::bind(m);
    ConfiguredNetworkGroupWrapper::bind(m);
    DeviceWrapper::bind(m);
    HefWrapper::bind(m);
    InferModelInferStreamWrapper::bind(m);
    InferModelWrapper::bind(m);
    InferVStreamsWrapper::bind(m);
    InputVStreamWrapper::bind(m);
    InputVStreamsWrapper::bind(m);
    OutputVStreamWrapper::bind(m);
    OutputVStreamsWrapper::bind(m);
    VDeviceWrapper::bind(m);
    SessionWrapper::bind(m);
    SessionListenerWrapper::bind(m);

    LLMGeneratorCompletionWrapper::bind(m);
    LLMWrapper::bind(m);
    VLMWrapper::bind(m);
    Speech2TextWrapper::bind(m);

    std::stringstream version;
    version << HAILORT_MAJOR_VERSION << "." << HAILORT_MINOR_VERSION << "." << HAILORT_REVISION_VERSION;
    m.attr("__version__") = version.str();
}

} /* namespace hailort */
