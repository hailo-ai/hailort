/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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

DeviceWrapperPtr DeviceWrapper::create_eth(const std::string &device_address, uint16_t port,
    uint32_t timeout_milliseconds, uint8_t max_number_of_attempts)
{
    auto device = Device::create_eth(device_address, port, timeout_milliseconds, max_number_of_attempts);
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

hailo_fw_user_config_information_t DeviceWrapper::examine_user_config()
{
    auto user_config_info = device().examine_user_config();
    VALIDATE_EXPECTED(user_config_info);

    return user_config_info.release();
}

py::bytes DeviceWrapper::read_user_config()
{
    auto config_buffer = device().read_user_config();
    VALIDATE_EXPECTED(config_buffer);

    std::unique_ptr<std::string> response = std::make_unique<std::string>(
        const_cast<char*>(reinterpret_cast<const char*>(config_buffer->data())), config_buffer->size());
    VALIDATE_NOT_NULL(response, HAILO_OUT_OF_HOST_MEMORY);

    return *response;
}

void DeviceWrapper::write_user_config(py::bytes data)
{
    std::string data_str(data); 
    MemoryView data_view = MemoryView::create_const(data_str.c_str(), data_str.size());
    auto status = device().write_user_config(data_view);
    VALIDATE_STATUS(status);
}

void DeviceWrapper::erase_user_config()
{
    auto status = device().erase_user_config();
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

void DeviceWrapper::sensor_store_config(uint32_t section_index, uint32_t reset_data_size, uint32_t sensor_type, const std::string &config_file_path,
    uint16_t config_height, uint16_t config_width, uint16_t config_fps, const std::string &config_name)
{
    auto status = device().store_sensor_config(section_index, static_cast<hailo_sensor_types_t>(sensor_type), reset_data_size,
        config_height, config_width, config_fps, config_file_path, config_name);
    VALIDATE_STATUS(status);
}

void DeviceWrapper::store_isp_config(uint32_t reset_config_size, uint16_t config_height, uint16_t config_width, uint16_t config_fps,
    const std::string &isp_static_config_file_path, const std::string &isp_runtime_config_file_path, const std::string &config_name)
{
    auto status = device().store_isp_config(reset_config_size, config_height, config_width, config_fps,
        isp_static_config_file_path, isp_runtime_config_file_path, config_name);
    VALIDATE_STATUS(status);
}

py::bytes DeviceWrapper::sensor_get_sections_info()
{
    auto buffer = device().sensor_get_sections_info();
    VALIDATE_EXPECTED(buffer);
    
    std::unique_ptr<std::string> response = std::make_unique<std::string>(
        const_cast<char*>(reinterpret_cast<const char*>(buffer->data())), buffer->size());
    VALIDATE_NOT_NULL(response, HAILO_OUT_OF_HOST_MEMORY);

    return *response;
}

void DeviceWrapper::sensor_set_i2c_bus_index(uint32_t sensor_type, uint32_t bus_index)
{
    hailo_status status = device().sensor_set_i2c_bus_index(static_cast<hailo_sensor_types_t>(sensor_type), bus_index);
    VALIDATE_STATUS(status);
}

void DeviceWrapper::sensor_load_and_start_config(uint32_t section_index)
{
    auto status = device().sensor_load_and_start_config(section_index);
    VALIDATE_STATUS(status);
}

void DeviceWrapper::sensor_reset(uint32_t section_index)
{
    auto status = device().sensor_reset(section_index);
    VALIDATE_STATUS(status);
}

void DeviceWrapper::sensor_set_generic_i2c_slave(uint16_t slave_address,
    uint8_t register_address_size, uint8_t bus_index, uint8_t should_hold_bus, uint8_t endianness)
{
    auto status = device().sensor_set_generic_i2c_slave(slave_address, register_address_size,
        bus_index, should_hold_bus, endianness);
    VALIDATE_STATUS(status);
}

void DeviceWrapper::firmware_update(py::bytes fw_bin, uint32_t fw_bin_length, bool should_reset)
{
    auto status = device().firmware_update(MemoryView::create_const(std::string(fw_bin).c_str(), fw_bin_length),
        should_reset);
    VALIDATE_STATUS(status);
}

void DeviceWrapper::second_stage_update(py::bytes second_stage_bin, uint32_t second_stage_bin_length)
{
    auto status = device().second_stage_update((uint8_t *)std::string(second_stage_bin).c_str(), 
        second_stage_bin_length);
    VALIDATE_STATUS(status);
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

py::bytes DeviceWrapper::read_log(size_t byte_count, hailo_cpu_id_t cpu_id)
{
    std::string response;

    response.reserve(byte_count);
    response.resize(byte_count);

    MemoryView response_view ((&response[0]), byte_count);
    auto response_size_expected = device().read_log(response_view, cpu_id);
    VALIDATE_EXPECTED(response_size_expected);

    response.resize(response_size_expected.release());
    return py::bytes(response);
}

void DeviceWrapper::direct_write_memory(uint32_t address, py::bytes buffer)
{
    const auto buffer_str = static_cast<std::string>(buffer);
    hailo_status status = device().direct_write_memory(address, buffer_str.c_str(),
        (uint32_t)(buffer_str.length()));
    VALIDATE_STATUS(status);
}

py::bytes DeviceWrapper::direct_read_memory(uint32_t address, uint32_t size)
{
    std::string buffer_str;

    buffer_str.reserve(size);
    buffer_str.resize(size);

    hailo_status status = device().direct_read_memory(address, (char*)buffer_str.c_str(), size);
    VALIDATE_STATUS(status);

    buffer_str.resize(size);
    return py::bytes(buffer_str);
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
    .def("create_eth", &DeviceWrapper::create_eth)
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
    .def("firmware_update", &DeviceWrapper::firmware_update)
    .def("second_stage_update", &DeviceWrapper::second_stage_update)
    .def("examine_user_config", &DeviceWrapper::examine_user_config)
    .def("read_user_config", &DeviceWrapper::read_user_config)
    .def("write_user_config", &DeviceWrapper::write_user_config)
    .def("erase_user_config", &DeviceWrapper::erase_user_config)
    .def("read_board_config", &DeviceWrapper::read_board_config)
    .def("write_board_config", &DeviceWrapper::write_board_config)
    .def("i2c_write", &DeviceWrapper::i2c_write)
    .def("i2c_read", &DeviceWrapper::i2c_read)
    .def("sensor_store_config", &DeviceWrapper::sensor_store_config)
    .def("store_isp_config", &DeviceWrapper::store_isp_config)
    .def("sensor_set_i2c_bus_index", &DeviceWrapper::sensor_set_i2c_bus_index)
    .def("sensor_load_and_start_config", &DeviceWrapper::sensor_load_and_start_config)
    .def("sensor_reset", &DeviceWrapper::sensor_reset)
    .def("sensor_set_generic_i2c_slave", &DeviceWrapper::sensor_set_generic_i2c_slave)
    .def("sensor_get_sections_info", &DeviceWrapper::sensor_get_sections_info)
    .def("reset", &DeviceWrapper::reset)
    .def("wd_enable", &DeviceWrapper::wd_enable)
    .def("wd_disable", &DeviceWrapper::wd_disable)
    .def("wd_config", &DeviceWrapper::wd_config)
    .def("previous_system_state", &DeviceWrapper::previous_system_state)
    .def("get_chip_temperature", &DeviceWrapper::get_chip_temperature)
    .def("get_extended_device_information", &DeviceWrapper::get_extended_device_information)
    .def("set_pause_frames", &DeviceWrapper::set_pause_frames)
    .def("test_chip_memories", &DeviceWrapper::test_chip_memories)
    .def("_get_health_information", &DeviceWrapper::get_health_information)
    .def("set_throttling_state", &DeviceWrapper::set_throttling_state)
    .def("get_throttling_state", &DeviceWrapper::get_throttling_state)
    .def("_set_overcurrent_state", &DeviceWrapper::set_overcurrent_state)
    .def("_get_overcurrent_state", &DeviceWrapper::get_overcurrent_state)
    .def("direct_write_memory", &DeviceWrapper::direct_write_memory)
    .def("direct_read_memory", &DeviceWrapper::direct_read_memory)
    .def_property_readonly("device_id", &DeviceWrapper::get_dev_id)
    .def("read_log", &DeviceWrapper::read_log, py::return_value_policy::move)

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
