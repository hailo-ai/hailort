/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file device_api.cpp
 * @brief implementations of binding to hailo device
 *
 **/

#include "device_api.hpp"

#include <memory>


namespace hailort
{

std::vector<std::string> DeviceWrapper::scan()
{
    auto device_ids = Device::scan();
    VALIDATE_EXPECTED(device_ids);
    return device_ids.release();
}

DeviceWrapperPtr DeviceWrapper::create(const std::string &device_id)
{
    auto device = Device::create(device_id);
    VALIDATE_EXPECTED(device);
    return std::make_shared<DeviceWrapper>(device.release());
}

DeviceWrapperPtr DeviceWrapper::create_pcie(hailo_pcie_device_info_t &device_info)
{
    auto device = Device::create_pcie(device_info);
    VALIDATE_EXPECTED(device);

    return std::make_shared<DeviceWrapper>(device.release());
}

void DeviceWrapper::release()
{
    m_device.reset();
}

/* Controls */
hailo_device_identity_t DeviceWrapper::identify()
{
    auto board_info = device().identify();
    VALIDATE_EXPECTED(board_info);

    return board_info.release();
}

hailo_core_information_t DeviceWrapper::core_identify()
{
    auto core_info = device().core_identify();
    VALIDATE_EXPECTED(core_info);

    return core_info.release();
}

void DeviceWrapper::set_fw_logger(hailo_fw_logger_level_t level, uint32_t interface_mask)
{
    auto status = device().set_fw_logger(level, interface_mask);
    VALIDATE_STATUS(status);
}

void DeviceWrapper::set_throttling_state(bool should_activate)
{
    auto status = device().set_throttling_state(should_activate);
    VALIDATE_STATUS(status);
}

bool DeviceWrapper::get_throttling_state()
{

    auto is_active_expected = device().get_throttling_state();
    VALIDATE_EXPECTED(is_active_expected);

    return is_active_expected.release();
}

void DeviceWrapper::set_overcurrent_state(bool should_activate)
{
    auto status = device().set_overcurrent_state(should_activate);
    VALIDATE_STATUS(status);
}

bool DeviceWrapper::get_overcurrent_state()
{
    auto is_required_expected = device().get_overcurrent_state();
    VALIDATE_EXPECTED(is_required_expected);

    return is_required_expected.release();
}

py::bytes DeviceWrapper::read_memory(uint32_t address, uint32_t length)
{
    std::unique_ptr<std::string> response = std::make_unique<std::string>(length, '\x00');
    VALIDATE_NOT_NULL(response, HAILO_OUT_OF_HOST_MEMORY);

    MemoryView data_view(const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(response->data())), length);
    auto status = device().read_memory(address, data_view);
    VALIDATE_STATUS(status);

    return *response;
}

void DeviceWrapper::write_memory(uint32_t address, py::bytes data, uint32_t length)
{
    auto status = device().write_memory(address, MemoryView(const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(std::string(data).c_str())), length));
    VALIDATE_STATUS(status);
}

void DeviceWrapper::test_chip_memories()
{
    hailo_status status = device().test_chip_memories();
    VALIDATE_STATUS(status);
}

void DeviceWrapper::i2c_write(hailo_i2c_slave_config_t *slave_config, uint32_t register_address, py::bytes data,
    uint32_t length)
{
    VALIDATE_NOT_NULL(slave_config, HAILO_INVALID_ARGUMENT);

    std::string data_str(data);
    MemoryView data_view = MemoryView::create_const(data_str.c_str(), length);
    auto status = device().i2c_write(*slave_config, register_address, data_view);
    VALIDATE_STATUS(status);
}

py::bytes DeviceWrapper::i2c_read(hailo_i2c_slave_config_t *slave_config, uint32_t register_address, uint32_t length)
{
    VALIDATE_NOT_NULL(slave_config, HAILO_INVALID_ARGUMENT);

    std::unique_ptr<std::string> response = std::make_unique<std::string>(length, '\x00');
    VALIDATE_NOT_NULL(response, HAILO_OUT_OF_HOST_MEMORY);

    MemoryView data_view(const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(response->data())), length);
    auto status = device().i2c_read(*slave_config, register_address, data_view);
    VALIDATE_STATUS(status);

    return *response;
}

float32_t DeviceWrapper::power_measurement(hailo_dvm_options_t dvm,
    hailo_power_measurement_types_t measurement_type)
{
    auto measurement = device().power_measurement(dvm, measurement_type);
    VALIDATE_EXPECTED(measurement);
    
    return measurement.release();
}

void DeviceWrapper::start_power_measurement(hailo_averaging_factor_t averaging_factor, hailo_sampling_period_t sampling_period)
{
    auto status = device().start_power_measurement(averaging_factor, sampling_period);
    VALIDATE_STATUS(status);
}

void DeviceWrapper::set_power_measurement(hailo_measurement_buffer_index_t buffer_index, hailo_dvm_options_t dvm,
    hailo_power_measurement_types_t measurement_type)
{
    auto status = device().set_power_measurement(buffer_index,
        dvm, measurement_type);
    VALIDATE_STATUS(status);
}

PowerMeasurementData DeviceWrapper::get_power_measurement(hailo_measurement_buffer_index_t buffer_index, bool should_clear)
{
    auto measurement_data = device().get_power_measurement(buffer_index,
        should_clear);
    VALIDATE_EXPECTED(measurement_data);

    return PowerMeasurementData(measurement_data.release());
}

void DeviceWrapper::stop_power_measurement()
{
    auto status = device().stop_power_measurement();
    VALIDATE_STATUS(status);
}

void DeviceWrapper::reset(hailo_reset_device_mode_t mode)
{
    auto status = device().reset(mode);
    VALIDATE_STATUS(status);
}

py::bytes DeviceWrapper::read_board_config()
{
    auto config_buffer = device().read_board_config();
    VALIDATE_EXPECTED(config_buffer);

    std::unique_ptr<std::string> response = std::make_unique<std::string>(
        const_cast<char*>(reinterpret_cast<const char*>(config_buffer->data())), config_buffer->size());
    VALIDATE_NOT_NULL(response, HAILO_OUT_OF_HOST_MEMORY);
    
    return *response;
}

void DeviceWrapper::write_board_config(py::bytes data)
{
    std::string data_str(data); 
    MemoryView data_view = MemoryView::create_const(data_str.c_str(), data_str.size());
    auto status = device().write_board_config(data_view);
    VALIDATE_STATUS(status);
}

hailo_extended_device_information_t DeviceWrapper::get_extended_device_information()
{
    auto extended_device_info = device().get_extended_device_information();
    VALIDATE_EXPECTED(extended_device_info);
    
    return extended_device_info.release();
}

hailo_health_info_t DeviceWrapper::get_health_information()
{
    auto health_info = device().get_health_information();
    VALIDATE_EXPECTED(health_info);
    
    return health_info.release();
}

void DeviceWrapper::set_pause_frames(bool rx_pause_frames_enable)
{
    auto status = device().set_pause_frames(rx_pause_frames_enable);
    VALIDATE_STATUS(status);
}

void DeviceWrapper::wd_enable(hailo_cpu_id_t cpu_id)
{
    hailo_status status = device().wd_enable(cpu_id);
    VALIDATE_STATUS(status);
}

void DeviceWrapper::wd_disable(hailo_cpu_id_t cpu_id)
{
    hailo_status status = device().wd_disable(cpu_id);
    VALIDATE_STATUS(status);
}

void DeviceWrapper::wd_config(hailo_cpu_id_t cpu_id, uint32_t wd_cycles, hailo_watchdog_mode_t wd_mode)
{
    auto status = device().wd_config(cpu_id, wd_cycles, wd_mode);
    VALIDATE_STATUS(status);
}

uint32_t DeviceWrapper::previous_system_state(hailo_cpu_id_t cpu_id)
{
    auto system_state = device().previous_system_state(cpu_id);
    VALIDATE_EXPECTED(system_state);

    return system_state.release();
}

hailo_chip_temperature_info_t DeviceWrapper::get_chip_temperature()
{
    auto temp_info = device().get_chip_temperature();
    VALIDATE_EXPECTED(temp_info);

    return temp_info.release();
}

hailo_performance_stats_t DeviceWrapper::query_performance_stats(uint32_t sampling_period_ms)
{
    auto perf_stats = device().query_performance_stats(std::chrono::milliseconds(sampling_period_ms));
    VALIDATE_EXPECTED(perf_stats);
    return perf_stats.release();
}

uint32_t DeviceWrapper::get_current_limit()
{
    auto current_limit = device().get_current_limit();
    VALIDATE_EXPECTED(current_limit);

    return current_limit.release();
}

void DeviceWrapper::set_notification_callback(const std::function<void(uintptr_t, const hailo_notification_t&, py::object)> &callback,
    hailo_notification_id_t notification_id, py::object opaque)
{
    // capture the opaque and move it, this is because when opaque goes out of scope it will be automatically deleted,
    // so capturing it ensures that it will not be deleted
    hailo_status status = device().set_notification_callback(
        [callback, op = std::move(opaque)] (Device &device, const hailo_notification_t &notification, void* opaque) {
            (void)opaque;
            callback((uintptr_t)(&device), notification, op);
        }, notification_id, nullptr);
    VALIDATE_STATUS(status);
}

void DeviceWrapper::remove_notification_callback(hailo_notification_id_t notification_id)
{
    auto status = device().remove_notification_callback(notification_id);
    VALIDATE_STATUS(status);
}

const char *DeviceWrapper::get_dev_id() const
{
    return device().get_dev_id();
}

void DeviceWrapper::set_sleep_state(hailo_sleep_state_t sleep_state)
{
    auto status = device().set_sleep_state(sleep_state);
    VALIDATE_STATUS(status);
}

void DeviceWrapper::bind(py::module &m)
{
    py::class_<DeviceWrapper, DeviceWrapperPtr>(m, "Device")
    .def("is_valid", &DeviceWrapper::is_valid)

    // Scan
    .def("scan", &DeviceWrapper::scan)

    // C'tors
    .def("create", &DeviceWrapper::create)
    .def("create_pcie", &DeviceWrapper::create_pcie)
    .def("release", &DeviceWrapper::release)

    // Controls
    .def("identify", &DeviceWrapper::identify)
    .def("core_identify", &DeviceWrapper::core_identify)
    .def("set_fw_logger", &DeviceWrapper::set_fw_logger)
    .def("read_memory", &DeviceWrapper::read_memory)
    .def("write_memory", &DeviceWrapper::write_memory)
    .def("power_measurement", &DeviceWrapper::power_measurement)
    .def("start_power_measurement", &DeviceWrapper::start_power_measurement)
    .def("stop_power_measurement", &DeviceWrapper::stop_power_measurement)
    .def("set_power_measurement", &DeviceWrapper::set_power_measurement)
    .def("get_power_measurement", &DeviceWrapper::get_power_measurement)
    .def("read_board_config", &DeviceWrapper::read_board_config)
    .def("write_board_config", &DeviceWrapper::write_board_config)
    .def("i2c_write", &DeviceWrapper::i2c_write)
    .def("i2c_read", &DeviceWrapper::i2c_read)
    .def("reset", &DeviceWrapper::reset)
    .def("wd_enable", &DeviceWrapper::wd_enable)
    .def("wd_disable", &DeviceWrapper::wd_disable)
    .def("wd_config", &DeviceWrapper::wd_config)
    .def("previous_system_state", &DeviceWrapper::previous_system_state)
    .def("get_chip_temperature", &DeviceWrapper::get_chip_temperature)
    .def("query_performance_stats", &DeviceWrapper::query_performance_stats, py::arg("sampling_period_ms") = 100)
    .def("get_current_limit", &DeviceWrapper::get_current_limit)
    .def("get_extended_device_information", &DeviceWrapper::get_extended_device_information)
    .def("set_pause_frames", &DeviceWrapper::set_pause_frames)
    .def("test_chip_memories", &DeviceWrapper::test_chip_memories)
    .def("_get_health_information", &DeviceWrapper::get_health_information)
    .def("set_throttling_state", &DeviceWrapper::set_throttling_state)
    .def("get_throttling_state", &DeviceWrapper::get_throttling_state)
    .def("_set_overcurrent_state", &DeviceWrapper::set_overcurrent_state)
    .def("_get_overcurrent_state", &DeviceWrapper::get_overcurrent_state)
    .def_property_readonly("device_id", &DeviceWrapper::get_dev_id)

    .def("set_notification_callback", &DeviceWrapper::set_notification_callback)
    .def("remove_notification_callback", &DeviceWrapper::remove_notification_callback)
    .def("set_sleep_state", &DeviceWrapper::set_sleep_state)
    ;
}

PowerMeasurementData::PowerMeasurementData(hailo_power_measurement_data_t &&c_power_data)
{
    m_average_value = c_power_data.average_value;
    m_average_time_value_milliseconds = c_power_data.average_time_value_milliseconds;
    m_min_value = c_power_data.min_value;
    m_max_value = c_power_data.max_value;
    m_total_number_of_samples = c_power_data.total_number_of_samples;
}

/* Return a tuple that fully encodes the state of the object */
py::tuple PowerMeasurementData::get_state(const PowerMeasurementData &power_measurement_data)
{
    return py::make_tuple(
        power_measurement_data.m_average_value,
        power_measurement_data.m_average_time_value_milliseconds,
        power_measurement_data.m_min_value,
        power_measurement_data.m_max_value,
        power_measurement_data.m_total_number_of_samples);
}

PowerMeasurementData PowerMeasurementData::set_state(py::tuple t)
{
    if (PowerMeasurementData::NUM_OF_MEMBERS != t.size())
        throw std::runtime_error("Invalid power measurement data state!");

    /* Create a new C++ instance */
    hailo_power_measurement_data_t data;
    data.average_value = t[0].cast<float32_t>();
    data.average_time_value_milliseconds = t[1].cast<float32_t>();
    data.min_value = t[2].cast<float32_t>();
    data.max_value = t[3].cast<float32_t>();
    data.total_number_of_samples = t[4].cast<uint32_t>();
    return PowerMeasurementData(std::move(data));
}

bool PowerMeasurementData::equals(const PowerMeasurementData &other)
{
    return ((this->m_average_value == other.m_average_value) &&
        (this->m_average_time_value_milliseconds == other.m_average_time_value_milliseconds) &&
        (this->m_min_value == other.m_min_value) &&
        (this->m_max_value == other.m_max_value) &&
        (this->m_total_number_of_samples == other.m_total_number_of_samples));
}

} /* namespace hailort */
